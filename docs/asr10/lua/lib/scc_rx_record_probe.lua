-- Shared Phase 4B runner. The caller defines SCC_RX_RECORD_CONFIG before
-- loading this file. It feeds post-framing bytes through the model's hidden
-- SCC1RX/SCC2RX debugger states and observes firmware-owned descriptors,
-- IDMA setup, completion, and destination bytes.

local config = assert(_G.SCC_RX_RECORD_CONFIG, "missing SCC_RX_RECORD_CONFIG")
local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local rx_state = {
  [1] = cpu.state["SCC1RX"],
  [2] = cpu.state["SCC2RX"],
}
local taps = {}
local transfers = {}
local feed_records = {}
local iack = {}
local witness_writes = 0
local completion_state_events = {}
local completion_paths = {}

local objects = {
  [1] = { pointer = 0x12d8, first_buffer = 0x00f76600 },
  [2] = { pointer = 0x1320, first_buffer = 0x00f74b00 },
}

local patterns = {
  counter_trigger = function(offset)
    if offset == 0x40 then return 0x7f end
    if offset == 0x41 then return 0xff end
    return offset & 0xff
  end,
  aa55 = function(offset) return (offset & 1) == 0 and 0xaa or 0x55 end,
  signed_triplet = function(offset)
    local sequence = { 0x00, 0x00, 0x80, 0x00, 0xff, 0xff }
    return sequence[(offset % #sequence) + 1]
  end,
  aa_trigger = function(offset)
    if offset == 0x40 then return 0x7f end
    if offset == 0x41 then return 0xff end
    return 0xaa
  end,
  byte55 = function() return 0x55 end,
}

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function wait_seconds(seconds) emu.wait(emu.attotime.from_seconds(seconds)) end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local function trace_button(code, label, observe_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  local last = ""
  local function sample(phase)
    local text = display.read_raw()
    local state = prog:read_u16(0x0d04) & 0xffff
    local current = string.format("%04X:%s", state, text)
    if current ~= last then
      print(string.format("RECORD_COMPLETE_UI t=%.6f input=%s phase=%s state=%04X display=\"%s\"",
        now(), label, phase, state, text))
      last = current
    end
  end
  sample("before")
  field:set_value(1)
  for _ = 1, 4 do emu.wait(emu.attotime.from_msec(20)); sample("pressed") end
  field:clear_value()
  for _ = 1, math.max(1, math.floor(observe_ms / 20)) do
    emu.wait(emu.attotime.from_msec(20))
    sample("released")
  end
end

local function signed16(value)
  return value >= 0x8000 and value - 0x10000 or value
end

local function object_snapshot(label)
  for channel = 1, 2 do
    local object = prog:read_u32(objects[channel].pointer) & 0x00ffffff
    objects[channel].address = object
    objects[channel].initial_destination = prog:read_u32(object + 0x20) & 0x00ffffff
    objects[channel].initial_remaining = prog:read_u32(object + 0x24)
    print(string.format(
      "SCC_FORMAT_OBJECT label=%s channel=%u object=%06X descriptor_index=%u destination=%06X remaining=%08X",
      label, channel, object, prog:read_u16(object + 0x10) & 0xffff,
      objects[channel].initial_destination, objects[channel].initial_remaining))
  end
end

local function wait_for_completion(previous, timeout_seconds)
  local deadline = now() + timeout_seconds
  while now() < deadline do
    if (iack["L4:4B"] or 0) > previous then return true end
    emu.wait(emu.attotime.from_msec(10))
  end
  return false
end

local function format_bytes(address, count)
  local values = {}
  for offset = 0, count - 1 do
    values[#values + 1] = string.format("%02X", prog:read_u8(address + offset) & 0xff)
  end
  return table.concat(values, " ")
end

local function print_words(label, address, count)
  for offset = 0, count - 1, 2 do
    local high = prog:read_u8(address + offset) & 0xff
    local low = prog:read_u8(address + offset + 1) & 0xff
    local be = (high << 8) | low
    local le = (low << 8) | high
    print(string.format(
      "SCC_FORMAT_WORD label=%s offset=%04X bytes=%02X%02X be=%04X be_signed=%d le=%04X le_signed=%d",
      label, offset, high, low, be, signed16(be), le, signed16(le)))
  end
end

for channel = 1, 2 do
  if not rx_state[channel] or not rx_state[channel].writeable then
    reg.fail(config.name, string.format("missing_writeable_SCC%uRX_state", channel))
    return
  end
end

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("scc_format_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local vector = data & 0xff
      local key = string.format("L%d:%02X", level, vector)
      iack[key] = (iack[key] or 0) + 1
      if level == 4 then
        print(string.format("SCC_FORMAT_IACK t=%.6f vector=%02X pc=%06X", now(), vector, pc()))
      end
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail(config.name, string.format("boot_timeout display=\"%s\"", text))
  return
end

if config.trace_completion then
  taps[#taps + 1] = prog:install_write_tap(0x000d04, 0x000d05,
    "record_completion_state", function(_, data, mask)
      completion_state_events[#completion_state_events + 1] = string.format(
        "t=%.6f,pc=%06X,data=%08X,mask=%08X", now(), pc(),
        data & 0xffffffff, mask & 0xffffffff)
      return nil
    end)
  for address, name in pairs({
    [0x005c6c] = "record_setup",
    [0x00643c] = "scc_rx",
    [0x00665c] = "post_90e8",
    [0x00aa48] = "idma_complete",
    [0x00ffd54a] = "threshold_scan",
    [0x00f95eb2] = "destination_setup",
    [0x000174ec] = "left_layer_setup",
    [0x000174f4] = "right_layer_setup",
    [0x00017546] = "attach_left_range",
    [0x00017554] = "attach_right_range",
    [0x00f8e93c] = "sample_field_f0",
    [0x00f8e948] = "sample_field_f8",
    [0x00f8e954] = "sample_field_100",
    [0x00f8e960] = "sample_field_108",
    [0x00f8e96a] = "sample_field_flags",
  }) do
    taps[#taps + 1] = (cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog)
      :install_read_tap(address, address + 1, "record_completion_path_" .. name, function()
        completion_paths[name] = (completion_paths[name] or 0) + 1
        return nil
      end)
  end
end

print(string.format("SCC_FORMAT_FIRMWARE addr=FFD520 len=128 bytes=\"%s\"",
  format_bytes(0x00ffd520, 128):gsub(" ", "")))

taps[#taps + 1] = prog:install_write_tap(0x00fc6802, 0x00fc6803,
  "scc_format_idma_start", function(_, data)
    if (data & 0xffff) == 0x37a1 then
      transfers[#transfers + 1] = {
        source = prog:read_u32(0x00fc6804) & 0x00ffffff,
        destination = prog:read_u32(0x00fc6808) & 0x00ffffff,
        length = prog:read_u16(0x00fc680c) & 0xffff,
      }
      local transfer = transfers[#transfers]
      print(string.format(
        "SCC_FORMAT_IDMA index=%u pc=%06X source=%06X destination=%06X length=%u",
        #transfers, pc(), transfer.source, transfer.destination, transfer.length))
    end
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff,
  "scc_format_live_witness", function()
    witness_writes = witness_writes + 1
    return nil
  end)

press_button(0x20, 500)
print(string.format("SCC_FORMAT_MODE step=0 value=%u display=\"%s\"",
  prog:read_u8(0x016f), display.read_raw()))
for step = 1, config.source_steps do
  press_button(0x0a, 250)
  print(string.format("SCC_FORMAT_MODE step=%u value=%u display=\"%s\"",
    step, prog:read_u8(0x016f), display.read_raw()))
end

if prog:read_u8(0x016f) ~= config.expected_mode then
  reg.fail(config.name, string.format("source_mode expected=%u actual=%u display=\"%s\"",
    config.expected_mode, prog:read_u8(0x016f), display.read_raw()))
  return
end

press_button(0x02, 1000)
print(string.format("SCC_FORMAT_STATE stage=level_detect_entry state=%04X display=\"%s\"",
  prog:read_u16(0x0d04), display.read_raw()))
if config.expected_instrument_error then
  local error_text = display.read_raw()
  local no_scc_iack = (iack["L4:4A"] or 0) == 0 and (iack["L4:4D"] or 0) == 0
    and (iack["L4:4B"] or 0) == 0
  if error_text:find("ERR0R", 1, true) and no_scc_iack and witness_writes > 0 then
    reg.pass(config.name, string.format(
      "bounded_instrument_error display=\"%s\" iack4a=0 iack4d=0 iack4b=0 witness=%u",
      error_text, witness_writes))
  else
    reg.fail(config.name, string.format(
      "expected_instrument_error display=\"%s\" iack4a=%u iack4d=%u iack4b=%u witness=%u",
      error_text, iack["L4:4A"] or 0, iack["L4:4D"] or 0,
      iack["L4:4B"] or 0, witness_writes))
  end
  return
end
for _ = 1, 24 do press_button(0x0a, 40) end
print(string.format("SCC_FORMAT_STATE stage=threshold_endpoint state=%04X display=\"%s\"",
  prog:read_u16(0x0d04), display.read_raw()))
press_button(0x23, 100)
wait_seconds(1)
print(string.format("SCC_FORMAT_STATE stage=before_rx state=%04X display=\"%s\"",
  prog:read_u16(0x0d04), display.read_raw()))

if prog:read_u16(0x0d04) ~= (config.expected_pre_rx_state or 2) then
  reg.fail(config.name, string.format("unexpected_pre_rx_state expected=%04X actual=%04X display=\"%s\"",
    config.expected_pre_rx_state or 2, prog:read_u16(0x0d04), display.read_raw()))
  return
end

object_snapshot("before_rx")

local channel_descriptor_count = { 0, 0 }
for feed_index, feed in ipairs(config.feeds) do
  local pattern = assert(patterns[feed.pattern], "unknown pattern " .. feed.pattern)
  local descriptor_index = channel_descriptor_count[feed.channel]
  local buffer = objects[feed.channel].first_buffer + descriptor_index * 0x0320
  channel_descriptor_count[feed.channel] = descriptor_index + 1
  local completion_before = iack["L4:4B"] or 0

  print(string.format(
    "SCC_FORMAT_FEED index=%u channel=%u descriptor=%u pattern=%s buffer=%06X",
    feed_index, feed.channel, descriptor_index, feed.pattern, buffer))
  for offset = 0, 0x031f do
    rx_state[feed.channel].value = pattern(offset)
  end
  feed_records[#feed_records + 1] = {
    channel = feed.channel,
    descriptor = descriptor_index,
    pattern_name = feed.pattern,
    pattern = pattern,
    buffer = buffer,
  }

  if not wait_for_completion(completion_before, 3) then
    reg.fail(config.name, string.format(
      "first_stop feed=%u channel=%u pattern=%s state=%04X iack4a=%u iack4d=%u iack4b=%u witness=%u",
      feed_index, feed.channel, feed.pattern, prog:read_u16(0x0d04),
      iack["L4:4A"] or 0, iack["L4:4D"] or 0, iack["L4:4B"] or 0, witness_writes))
    return
  end
end

wait_seconds(1)

if config.finish_sample then
  local recorded_start = objects[1].initial_destination
  local recorded_end = (prog:read_u32(objects[1].address + 0x20) & 0x00ffffff) - 1
  local cpu_reads = 0
  local es_reads = 0
  local es_matches = 0
  local es_mismatches = 0
  local first_cpu_read = nil
  local last_cpu_read = nil
  local first_es_read = nil
  local last_es_read = nil
  local es_samples = {}
  local metadata_start = 0x02b600
  local metadata_end = 0x02d000
  local metadata_writes = {}
  local function metadata_snapshot()
    local result = {}
    for address = metadata_start, metadata_end - 1 do
      if address < recorded_start or address > recorded_end then
        result[address] = prog:read_u8(address) & 0xff
      end
    end
    return result
  end
  local function metadata_diff(label, previous)
    local changed = {}
    local current = metadata_snapshot()
    for address = metadata_start, metadata_end - 1 do
      if previous[address] ~= nil and previous[address] ~= current[address] then
        changed[#changed + 1] = string.format("%06X:%02X>%02X",
          address, previous[address], current[address])
      end
    end
    print(string.format("RECORD_COMPLETE_METADATA stage=%s changes=%u values=%s",
      label, #changed, table.concat(changed, ",")))
    return current
  end
  local metadata_previous = metadata_snapshot()
  local metadata_tap = nil
  if config.trace_completion then
    metadata_tap = prog:install_write_tap(metadata_start, metadata_end - 1,
      "record_completion_metadata", function(offset, data, mask)
        local key = string.format("%06X@%06X", offset, pc())
        local item = metadata_writes[key] or { address = offset, pc = pc(), count = 0 }
        item.count = item.count + 1
        item.data = data & 0xffffffff
        item.mask = mask & 0xffffffff
        metadata_writes[key] = item
        return nil
      end)
  end
  local es5506 = manager.machine.devices[":es5506_host"]
  local bank1 = es5506 and es5506.spaces["bank1"] or nil

  local cpu_read_tap = prog:install_read_tap(recorded_start, recorded_end,
    "scc_format_post_record_cpu_reads", function(offset)
      cpu_reads = cpu_reads + 1
      first_cpu_read = first_cpu_read or offset
      last_cpu_read = offset
      return nil
    end)
  local es_read_tap = nil
  if bank1 then
    es_read_tap = bank1:install_read_tap(recorded_start >> 1, recorded_end >> 1,
      "scc_format_post_record_es_reads", function(offset, data)
        es_reads = es_reads + 1
        first_es_read = first_es_read or offset
        last_es_read = offset
        if #es_samples < 256 then
          es_samples[#es_samples + 1] = { address = offset, data = data & 0xffff }
        end
        return nil
      end)
  end

  if config.trace_completion then trace_button(0x22, "BTN_22", 600)
  else press_button(0x22, 500) end
  print(string.format("SCC_FORMAT_FINISH stage=cancel state=%04X display=\"%s\"",
    prog:read_u16(0x0d04), display.read_raw()))
  if config.trace_completion then metadata_previous = metadata_diff("after_btn22", metadata_previous) end
  if config.trace_completion then trace_button(0x23, "BTN_23", 1000)
  else press_button(0x23, 1000) end
  print(string.format("SCC_FORMAT_FINISH stage=root_enter state=%04X display=\"%s\"",
    prog:read_u16(0x0d04), display.read_raw()))
  if config.trace_completion then metadata_previous = metadata_diff("after_btn23", metadata_previous) end

  local key_port = manager.machine.ioport.ports[":panel:keys_0"]
  local key_field = key_port and key_port:field(0x00000001) or nil
  if key_field then
    key_field:set_value(1)
    if config.trace_completion then
      for _ = 1, 15 do emu.wait(emu.attotime.from_msec(20)) end
    else
      emu.wait(emu.attotime.from_msec(300))
    end
    key_field:clear_value()
    wait_seconds(1)
  end
  if config.trace_completion then
    metadata_previous = metadata_diff("after_root_key", metadata_previous)
    local writer_lines = {}
    for _, item in pairs(metadata_writes) do
      writer_lines[#writer_lines + 1] = string.format(
        "%06X@%06Xx%u=%08X/%08X", item.address, item.pc, item.count,
        item.data, item.mask)
    end
    table.sort(writer_lines)
    print("RECORD_COMPLETE_WRITERS " .. table.concat(writer_lines, " "))
    print("RECORD_COMPLETE_STATES " .. table.concat(completion_state_events, " | "))
    local path_lines = {}
    for name, count in pairs(completion_paths) do
      path_lines[#path_lines + 1] = string.format("%s=%u", name, count)
    end
    table.sort(path_lines)
    print("RECORD_COMPLETE_PATHS " .. table.concat(path_lines, " "))
    print("RECORD_COMPLETE_CODE start=FFBF00 bytes=" .. format_bytes(0x00ffbf00, 0x500):gsub(" ", ""))
    if metadata_tap then metadata_tap:remove() end
  end

  cpu_read_tap:remove()
  if es_read_tap then es_read_tap:remove() end
  for _, sample in ipairs(es_samples) do
    if sample.data == (prog:read_u16(sample.address << 1) & 0xffff) then
      es_matches = es_matches + 1
    else
      es_mismatches = es_mismatches + 1
    end
  end
  print(string.format(
    "SCC_FORMAT_CONSUMER range=%06X-%06X cpu_reads=%u cpu_first=%06X cpu_last=%06X es_reads=%u es_first_word=%06X es_last_word=%06X es_matches=%u es_mismatches=%u display=\"%s\"",
    recorded_start, recorded_end, cpu_reads, first_cpu_read or 0, last_cpu_read or 0,
    es_reads, first_es_read or 0, last_es_read or 0, es_matches, es_mismatches,
    display.read_raw()))
end

local total_matches = 0
local total_mismatches = 0
for transfer_index, transfer in ipairs(transfers) do
  local owner = nil
  for _, feed in ipairs(feed_records) do
    if transfer.source >= feed.buffer and transfer.source < feed.buffer + 0x0320 then
      owner = feed
      break
    end
  end
  local matches = 0
  local mismatches = 0
  if owner then
    local source_offset = transfer.source - owner.buffer
    for offset = 0, transfer.length - 1 do
      local expected = owner.pattern(source_offset + offset)
      local source_byte = prog:read_u8(transfer.source + offset) & 0xff
      local destination_byte = prog:read_u8(transfer.destination + offset) & 0xff
      if source_byte == expected and destination_byte == expected then
        matches = matches + 1
      else
        mismatches = mismatches + 1
      end
    end
    print(string.format(
      "SCC_FORMAT_TRANSFER index=%u channel=%u descriptor=%u pattern=%s source=%06X destination=%06X length=%u matches=%u mismatches=%u bytes=\"%s\"",
      transfer_index, owner.channel, owner.descriptor, owner.pattern_name,
      transfer.source, transfer.destination, transfer.length, matches, mismatches,
      format_bytes(transfer.destination, math.min(12, transfer.length))))
    print_words(string.format("transfer_%u_%s", transfer_index, owner.pattern_name),
      transfer.destination, math.min(12, transfer.length))
  else
    mismatches = transfer.length
    print(string.format("SCC_FORMAT_TRANSFER index=%u owner=none source=%06X destination=%06X length=%u",
      transfer_index, transfer.source, transfer.destination, transfer.length))
  end
  total_matches = total_matches + matches
  total_mismatches = total_mismatches + mismatches
end

for channel = 1, 2 do
  local object = objects[channel].address
  print(string.format(
    "SCC_FORMAT_FINAL channel=%u destination=%06X remaining=%08X descriptor_index=%u",
    channel, prog:read_u32(object + 0x20) & 0x00ffffff,
    prog:read_u32(object + 0x24), prog:read_u16(object + 0x10) & 0xffff))
end

local expected_bytes = 0
for _, transfer in ipairs(transfers) do expected_bytes = expected_bytes + transfer.length end
local passed = #transfers == #config.feeds
  and total_matches == expected_bytes
  and total_mismatches == 0
  and (iack["L4:4B"] or 0) == #config.feeds
  and witness_writes > 0

if passed then
  reg.pass(config.name, string.format(
    "mode=%u transfers=%u bytes=%u matches=%u iack4a=%u iack4d=%u iack4b=%u witness=%u",
    config.expected_mode, #transfers, expected_bytes, total_matches,
    iack["L4:4A"] or 0, iack["L4:4D"] or 0, iack["L4:4B"] or 0, witness_writes))
else
  reg.fail(config.name, string.format(
    "transfers=%u/%u bytes=%u matches=%u mismatches=%u iack4b=%u witness=%u",
    #transfers, #config.feeds, expected_bytes, total_matches, total_mismatches,
    iack["L4:4B"] or 0, witness_writes))
end
