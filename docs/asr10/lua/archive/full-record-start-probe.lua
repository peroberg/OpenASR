-- Full ASR-10 sampling path: Sample-Source Select -> unused Instrument 1
-- -> Level-Detect -> minimum threshold -> Enter-Yes (RECORD/start).
-- Observation only. No device state is written except through documented
-- front-panel controls.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local phase = "boot"
local iack = {}
local iack_vectors = {}
local iack_printed = {}
local sib_reads = {}
local sib_writes = {}
local descriptor_writes = { SCC1 = 0, SCC2 = 0 }
local descriptor_writes_phase = {}
local buffer_writes = { SCC1 = 0, SCC2 = 0 }
local buffer_writes_phase = {}
local sample_ram_writes = 0
local sample_ram_writes_phase = {}
local sample_ram_first = nil
local ram_witness_writes = 0
local path_hits = {}
local relevant_writes = {}
local pc_samples = {}
local display_transitions = {}
local last_display = nil

local CHANNELS = {
  SCC1 = { bd = 0x00fc6400, extra = 0x00fc6480, regs = 0x00fc6880 },
  SCC2 = { bd = 0x00fc6500, extra = 0x00fc6580, regs = 0x00fc6890 },
}

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local function count(table_, key)
  table_[key] = (table_[key] or 0) + 1
end

local function interesting_sib_write(address)
  return (address >= 0x00fc6400 and address <= 0x00fc64af)
    or (address >= 0x00fc6500 and address <= 0x00fc65af)
    or (address >= 0x00fc6814 and address <= 0x00fc6818)
    or (address >= 0x00fc6880 and address <= 0x00fc689b)
    or address == 0x00fc68b4
end

local function values_hex(values)
  local out = {}
  for _, value in ipairs(values) do out[#out + 1] = string.format("%04X", value) end
  return table.concat(out, ",")
end

local function observe_display(label)
  local text = display.read_raw()
  if text ~= last_display then
    last_display = text
    local item = {
      t = now(), phase = phase, label = label, text = text,
      values = values_hex(display.read_values()),
    }
    display_transitions[#display_transitions + 1] = item
    print(string.format(
      "RECORD_DISPLAY t=%.6f phase=%s label=%s text=\"%s\" glyphs=%s",
      item.t, item.phase, item.label, item.text, item.values))
  end
end

local function wait_track(seconds, label, sample_pc)
  local deadline = now() + seconds
  while now() < deadline do
    observe_display(label)
    if sample_pc then count(pc_samples, pc()) end
    emu.wait(emu.attotime.from_msec(10))
  end
  observe_display(label)
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  print(string.format("RECORD_BUTTON t=%.6f phase=%s code=%02X action=press", now(), phase, code))
  field:set_value(1)
  wait_track(0.080, string.format("btn_%02X_down", code), false)
  field:clear_value()
  wait_track((settle_ms or 250) / 1000, string.format("btn_%02X_up", code), false)
end

local function buffer_owner(address)
  for name, channel in pairs(CHANNELS) do
    for index = 0, 7 do
      local buffer = prog:read_u32(channel.bd + index * 8 + 4) & 0x00ffffff
      if address >= buffer and address < buffer + 0x0320 then return name end
    end
  end
  return nil
end

local baseline_buffers = {}
local function snapshot(label)
  print(string.format("RECORD_SNAPSHOT_BEGIN t=%.6f phase=%s label=%s pc=%06X", now(), phase, label, pc()))
  print(string.format(
    "RECORD_IRQ_STATE label=%s GIMR=%04X IPR=%04X IMR=%04X ISR=%04X SIMODE=%04X",
    label, prog:read_u16(0x00fc6812) & 0xffff, prog:read_u16(0x00fc6814) & 0xffff,
    prog:read_u16(0x00fc6816) & 0xffff, prog:read_u16(0x00fc6818) & 0xffff,
    prog:read_u16(0x00fc68b4) & 0xffff))

  for name, channel in pairs(CHANNELS) do
    print(string.format(
      "RECORD_SCC_STATE label=%s channel=%s SCON=%04X SCM=%04X DSR=%04X SCCE=%04X SCCM=%04X RFCR=%04X MRBLR=%04X RBPTR=%04X CURRENT=%04X RXTMP=%04X",
      label, name, prog:read_u16(channel.regs + 2) & 0xffff,
      prog:read_u16(channel.regs + 4) & 0xffff, prog:read_u16(channel.regs + 6) & 0xffff,
      prog:read_u16(channel.regs + 8) & 0xffff, prog:read_u16(channel.regs + 10) & 0xffff,
      prog:read_u16(channel.extra) & 0xffff, prog:read_u16(channel.extra + 2) & 0xffff,
      prog:read_u16(channel.extra + 4) & 0xffff, prog:read_u16(channel.extra + 6) & 0xffff,
      prog:read_u16(channel.extra + 0x2e) & 0xffff))

    local nonzero, sum, changed = 0, 0, 0
    for index = 0, 7 do
      local descriptor = channel.bd + index * 8
      local status = prog:read_u16(descriptor) & 0xffff
      local length = prog:read_u16(descriptor + 2) & 0xffff
      local buffer = prog:read_u32(descriptor + 4) & 0x00ffffff
      print(string.format(
        "RECORD_BD label=%s channel=%s index=%u status=%04X length=%04X buffer=%06X",
        label, name, index, status, length, buffer))
      for offset = 0, 0x031f do
        local address = buffer + offset
        local value = prog:read_u8(address) & 0xff
        local key = string.format("%06X", address)
        if value ~= 0 then nonzero = nonzero + 1 end
        sum = (sum + value) & 0xffffffff
        if label == "baseline" then
          baseline_buffers[key] = value
        elseif baseline_buffers[key] ~= value then
          changed = changed + 1
        end
      end
    end
    print(string.format(
      "RECORD_BUFFER_STATE label=%s channel=%s bytes=6400 nonzero=%u sum=%08X changed_from_baseline=%u",
      label, name, nonzero, sum, changed))
  end
  print(string.format("RECORD_SNAPSHOT_END t=%.6f phase=%s label=%s", now(), phase, label))
end

-- IACK taps are outside the BAR-controlled SIB window and remain valid from reset.
for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff,
    string.format("record_iack_l%d", level), function(offset, data, mask)
      if offset ~= expected then return nil end
      count(iack, level)
      count(iack, string.format("%s:L%d", phase, level))
      local key = string.format("L%d:%02X", level, data & 0xff)
      count(iack_vectors, key)
      iack_printed[key] = (iack_printed[key] or 0) + 1
      if level == 4 or iack_printed[key] <= 3 then
        print(string.format(
          "RECORD_IACK t=%.6f phase=%s level=%d vector=%02X pc=%06X ordinal=%u",
          now(), phase, level, data & 0xff, pc(), iack_printed[key]))
      end
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("full_record_start_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

phase = "baseline"
observe_display("file1")

-- Install SIB taps only after FILE 1, well after the final BAR rewrite at ~5.395 s.
taps[#taps + 1] = prog:install_read_tap(0x00fc6400, 0x00fc68b5, "record_sib_r", function(offset, data, mask)
  count(sib_reads, string.format("%06X", offset & 0x00fffffe))
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00fc6400, 0x00fc68b5, "record_sib_w", function(offset, data, mask)
  local address = offset & 0x00fffffe
  count(sib_writes, string.format("%06X", address))
  if address >= 0x00fc6400 and address <= 0x00fc643f then
    descriptor_writes.SCC1 = descriptor_writes.SCC1 + 1
    count(descriptor_writes_phase, phase .. ":SCC1")
  end
  if address >= 0x00fc6500 and address <= 0x00fc653f then
    descriptor_writes.SCC2 = descriptor_writes.SCC2 + 1
    count(descriptor_writes_phase, phase .. ":SCC2")
  end
  if interesting_sib_write(address) then
    count(relevant_writes, string.format("%s:%06X:%08X:%08X", phase, address, data, mask))
    print(string.format(
      "RECORD_SIB_WRITE t=%.6f phase=%s addr=%06X data=%08X mask=%08X pc=%06X",
      now(), phase, address, data, mask, pc()))
  end
  return nil
end)

-- Zero-result witnesses: a hot sibling RAM range for sample RAM, and all
-- IACK levels for the level-4 count. Buffer writes are classified dynamically
-- from the descriptor addresses rather than assigning semantics to the rings.
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff, "record_ram_witness_w", function(offset, data, mask)
  ram_witness_writes = ram_witness_writes + 1
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x100000, 0x1fffff, "record_sample_ram_w", function(offset, data, mask)
  sample_ram_writes = sample_ram_writes + 1
  count(sample_ram_writes_phase, phase)
  if not sample_ram_first then sample_ram_first = { t = now(), address = offset, data = data, mask = mask, pc = pc() } end
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00f74b00, 0x00f77eff, "record_scc_buffers_w", function(offset, data, mask)
  local owner = buffer_owner(offset & 0x00ffffff)
  if owner then
    buffer_writes[owner] = buffer_writes[owner] + 1
    count(buffer_writes_phase, phase .. ":" .. owner)
  end
  return nil
end)

local PATHS = {
  [0x00643c] = "scc_rx_common",
  [0x006482] = "error_005_path",
  [0x006492] = "error_006_path",
  [0x00ff643c] = "scc_rx_common_high_alias",
  [0x00ff6482] = "error_005_path_high_alias",
  [0x00ff6492] = "error_006_path_high_alias",
  [0x00f8c0e6] = "scc_disable_both",
}
for address, name in pairs(PATHS) do
  taps[#taps + 1] = opcodes:install_read_tap(address, address + 1, "record_path_" .. name, function(offset, data, mask)
    count(path_hits, name)
    count(path_hits, name .. "@" .. phase)
    print(string.format("RECORD_PATH t=%.6f phase=%s name=%s addr=%06X pc=%06X", now(), phase, name, address, pc()))
    return nil
  end)
end

snapshot("baseline")

phase = "sample_source"
press_button(0x20, 500) -- Sample-Source Select
snapshot("sample_source")

phase = "level_detect"
press_button(0x02, 1000) -- unused Instrument-Sequence Track 1
snapshot("level_detect")

phase = "threshold_min"
for _ = 1, 24 do press_button(0x0a, 40) end -- Down Arrow lowers threshold
snapshot("threshold_min")

phase = "record_start"
press_button(0x23, 200) -- Enter-Yes: actual RECORD/start command
snapshot("record_start_200ms")
wait_track(0.8, "record_start_1s", true)
snapshot("record_start_1s")
wait_track(4.0, "record_start_5s", true)
snapshot("record_start_5s")
wait_track(7.0, "record_start_12s", true)
snapshot("record_start_12s")

for level = 1, 7 do
  print(string.format("RECORD_IACK_SUMMARY level=%d count=%u", level, iack[level] or 0))
end
for key, hits in pairs(iack_vectors) do print(string.format("RECORD_IACK_VECTOR key=%s count=%u", key, hits)) end
for name, hits in pairs(path_hits) do print(string.format("RECORD_PATH_SUMMARY name=%s count=%u", name, hits)) end
for key, hits in pairs(descriptor_writes_phase) do print(string.format("RECORD_DESCRIPTOR_PHASE key=%s count=%u", key, hits)) end
for key, hits in pairs(buffer_writes_phase) do print(string.format("RECORD_BUFFER_WRITE_PHASE key=%s count=%u", key, hits)) end
for key, hits in pairs(sample_ram_writes_phase) do print(string.format("RECORD_SAMPLE_RAM_PHASE key=%s count=%u", key, hits)) end
for key, hits in pairs(relevant_writes) do print(string.format("RECORD_RELEVANT_WRITE key=%s count=%u", key, hits)) end
for key, hits in pairs(iack) do
  if type(key) == "string" then print(string.format("RECORD_IACK_PHASE key=%s count=%u", key, hits)) end
end

local ranked = {}
for address, hits in pairs(pc_samples) do ranked[#ranked + 1] = { address = address, hits = hits } end
table.sort(ranked, function(a, b) return a.hits > b.hits end)
for index = 1, math.min(20, #ranked) do
  print(string.format("RECORD_PC_SAMPLE rank=%u pc=%06X count=%u", index, ranked[index].address, ranked[index].hits))
end

print(string.format(
  "RECORD_ACTIVITY descriptor_writes_scc1=%u descriptor_writes_scc2=%u buffer_writes_scc1=%u buffer_writes_scc2=%u sample_ram_writes=%u ram_witness_writes=%u sib_read_offsets=%u sib_write_offsets=%u display_transitions=%u",
  descriptor_writes.SCC1, descriptor_writes.SCC2, buffer_writes.SCC1, buffer_writes.SCC2,
  sample_ram_writes, ram_witness_writes,
  (function() local n = 0 for _ in pairs(sib_reads) do n = n + 1 end return n end)(),
  (function() local n = 0 for _ in pairs(sib_writes) do n = n + 1 end return n end)(),
  #display_transitions))
if sample_ram_first then
  print(string.format(
    "RECORD_SAMPLE_RAM_FIRST t=%.6f addr=%06X data=%08X mask=%08X pc=%06X",
    sample_ram_first.t, sample_ram_first.address, sample_ram_first.data,
    sample_ram_first.mask, sample_ram_first.pc))
else
  print("RECORD_SAMPLE_RAM_FIRST none")
end
print(string.format(
  "RECORD_FINAL t=%.6f phase=%s pc=%06X display=\"%s\" glyphs=%s",
  now(), phase, pc(), display.read_raw(), values_hex(display.read_values())))

reg.pass("full_record_start_probe", string.format(
  "display=\"%s\" iack4=%u sample_ram=%u witness=%u",
  display.read_raw(), iack[4] or 0, sample_ram_writes, ram_witness_writes))
