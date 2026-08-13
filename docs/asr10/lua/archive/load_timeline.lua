-- ASR-10 V3.50 instrument-load timeline observer.
--
-- Observation only. No C++ instrumentation, no machine-state fabrication.
-- The script waits for FILE 1, presses the documented load sequence
-- 0A, 23, 02, then logs compact event transitions from the first
-- "LOADING JM DIGI SYN" display until progress or 120 emulated seconds.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local a1_state = cpu.state["A1"]
local a7_state = cpu.state["A7"]
local d0_state = cpu.state["D0"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "

local taps = {}
local measuring = false
local stopped = false
local load_started_at = nil
local last_display = ""
local fdc_phase = nil
local fdc_phase_index = 0
local panel_phase = nil
local panel_phase_index = 0
local fdc_gap = 0.005
local panel_gap = 0.005
local last_fdc_status_bits = nil
local recent_fdc_writes = {}
local recent_fdc_limit = 32
local pc_calibration = { status = {}, data = {} }
local block = nil
local blocks = {}
local block_index = 0
local last_data_time = nil
local last_fdc_activity_time = nil
local after_last_samples = {}
local after_last_sampling_until = nil
local after_last_sequence_printed = false
local slot_tap_installed = false
local slot_table = nil
local dispatches = {}
local dispatch_seen = {}
local slot_target_snapshot = {}
local isr_counts = { reads = 0, bit0 = 0, bit1 = 0, bit3 = 0, bit5 = 0 }
local rhrb_reads = 0
local thrb_writes = 0
local lowmem_writes = {}
local os_before = nil
local os_after = nil
local sanity_button_before = nil
local sanity_button_after = nil

local function now()
  return emu.time()
end

local function pc()
  return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff
end

local function a1()
  return a1_state and (a1_state.value & 0x00ffffff) or 0xffffffff
end

local function a7()
  return a7_state and (a7_state.value & 0x00ffffff) or 0xffffffff
end

local function d0()
  return d0_state and (d0_state.value & 0xffffffff) or 0xffffffff
end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local function read_long(address)
  return prog:read_u32(address) & 0xffffffff
end

local function event(tag, fields)
  local parts = { string.format("LOAD_TIMELINE t=%.6f event=%s pc=%06X", now(), tag, pc()) }
  for _, item in ipairs(fields or {}) do
    parts[#parts + 1] = item
  end
  print(table.concat(parts, " "))
end

local function bytes_to_hex(bytes)
  local parts = {}
  for _, value in ipairs(bytes) do
    parts[#parts + 1] = string.format("%02X", value)
  end
  return table.concat(parts, " ")
end

local function remember_calibration(kind, source_pc)
  local table_for_kind = pc_calibration[kind]
  table_for_kind[source_pc] = (table_for_kind[source_pc] or 0) + 1
end

local function remember_recent_fdc_write(address, value, source_pc)
  recent_fdc_writes[#recent_fdc_writes + 1] = {
    time = now(),
    pc = source_pc,
    address = address,
    value = value,
  }
  while #recent_fdc_writes > recent_fdc_limit do
    table.remove(recent_fdc_writes, 1)
  end
end

local function recent_fdc_write_string()
  local parts = {}
  for _, item in ipairs(recent_fdc_writes) do
    parts[#parts + 1] = string.format("%06X:%06X=%02X", item.pc, item.address, item.value)
  end
  return table.concat(parts, ",")
end

local function start_fdc_phase(address, direction)
  fdc_phase_index = fdc_phase_index + 1
  fdc_phase = {
    index = fdc_phase_index,
    first = now(),
    last = now(),
    first_pc = pc(),
    last_pc = pc(),
    reads = 0,
    writes = 0,
  }
  event("fdc_phase_start", {
    string.format("phase=%u", fdc_phase.index),
    string.format("direction=%s", direction),
    string.format("address=%06X", address),
  })
end

local function touch_fdc(address, direction)
  if not measuring then
    return
  end
  last_fdc_activity_time = now()
  after_last_samples = {}
  after_last_sampling_until = now() + 0.020
  after_last_sequence_printed = false
  if not fdc_phase or (now() - fdc_phase.last) > fdc_gap then
    if fdc_phase then
      event("fdc_phase_end", {
        string.format("phase=%u", fdc_phase.index),
        string.format("first=%.6f", fdc_phase.first),
        string.format("last=%.6f", fdc_phase.last),
        string.format("reads=%u", fdc_phase.reads),
        string.format("writes=%u", fdc_phase.writes),
        string.format("last_pc=%06X", fdc_phase.last_pc),
      })
    end
    start_fdc_phase(address, direction)
  end
  fdc_phase.last = now()
  fdc_phase.last_pc = pc()
  if direction == "r" then
    fdc_phase.reads = fdc_phase.reads + 1
  else
    fdc_phase.writes = fdc_phase.writes + 1
  end
end

local function touch_panel(address, direction, value)
  if not measuring then
    return
  end
  if direction == "tx" then
    thrb_writes = thrb_writes + 1
  else
    rhrb_reads = rhrb_reads + 1
  end
  if not panel_phase or (now() - panel_phase.last) > panel_gap then
    if panel_phase then
      event("panel_phase_end", {
        string.format("phase=%u", panel_phase.index),
        string.format("first=%.6f", panel_phase.first),
        string.format("last=%.6f", panel_phase.last),
        string.format("first_%s=%02X", panel_phase.first_direction, panel_phase.first_value),
        string.format("last_%s=%02X", panel_phase.last_direction, panel_phase.last_value),
      })
    end
    panel_phase_index = panel_phase_index + 1
    panel_phase = {
      index = panel_phase_index,
      first = now(),
      last = now(),
      first_direction = direction,
      last_direction = direction,
      first_value = value,
      last_value = value,
    }
    event("panel_phase_start", {
      string.format("phase=%u", panel_phase.index),
      string.format("direction=%s", direction),
      string.format("address=%06X", address),
      string.format("value=%02X", value),
    })
  else
    panel_phase.last = now()
    panel_phase.last_direction = direction
    panel_phase.last_value = value
  end
end

local function close_block(reason)
  if not block then
    return
  end
  block.end_time = block.last_time
  block.reason = reason
  blocks[#blocks + 1] = block
  event("fdc_transfer_block_end", {
    string.format("block=%u", block.index),
    string.format("reason=%s", reason),
    string.format("caller_return=%06X", block.return_pc),
    string.format("start_a1=%06X", block.start_a1),
    string.format("end_a1=%06X", block.end_a1),
    string.format("bytes=%u", block.count),
  })
  block = nil
end

local function start_or_continue_block()
  local source_pc = pc()
  local post_a1 = a1()
  local byte_destination = (post_a1 - 1) & 0x00ffffff

  if block and block.return_pc ~= (read_long(a7()) & 0x00ffffff) then
    close_block("return_pc_changed")
  end

  if not block then
    block_index = block_index + 1
    block = {
      index = block_index,
      first_time = now(),
      last_time = now(),
      caller_pc = source_pc,
      return_pc = read_long(a7()) & 0x00ffffff,
      sp = a7(),
      start_a1 = byte_destination,
      end_a1 = byte_destination,
      count = 0,
      d0_start = d0(),
      command_before = recent_fdc_write_string(),
      result = {},
    }
    if not slot_table or not slot_table.active then
      slot_table = slot_table or {}
      slot_table.active = read_slot_table and read_slot_table() or nil
    end
    event("fdc_transfer_block_start", {
      string.format("block=%u", block.index),
      string.format("tap_pc=%06X", source_pc),
      string.format("return_pc=%06X", block.return_pc),
      string.format("start_a1=%06X", block.start_a1),
      string.format("d0_start=%08X", block.d0_start),
      string.format("fdc_writes=\"%s\"", block.command_before),
    })
  end

  block.last_time = now()
  block.end_a1 = byte_destination
  block.count = block.count + 1
  last_data_time = now()
  after_last_samples = {}
  after_last_sampling_until = now() + 0.020
  after_last_sequence_printed = false
end

function read_slot_table()
  local base = prog:read_u16(0x00c6) & 0xffff
  local limit = prog:read_u16(0x00c8) & 0xffff
  local slots = {}
  local address = base
  local index = 0
  while address < limit and index < 16 do
    local bytes = {}
    for offset = 0, 0x15 do
      bytes[#bytes + 1] = prog:read_u8(address + offset) & 0xff
    end
    slots[#slots + 1] = { index = index, address = address, bytes = bytes }
    address = address + 0x16
    index = index + 1
  end
  return { base = base, limit = limit, slots = slots }
end

local function dump_slots(label, data)
  print(string.format("LOAD_TIMELINE_SLOTS label=%s t=%.6f base=%04X limit=%04X count=%u display=\"%s\"",
    label, now(), data.base, data.limit, #data.slots, display.read_raw()))
  for _, slot in ipairs(data.slots) do
    local target = ((slot.bytes[7] << 24) | (slot.bytes[8] << 16) | (slot.bytes[9] << 8) | slot.bytes[10]) & 0x00ffffff
    print(string.format("LOAD_TIMELINE_SLOT label=%s index=%u address=%04X head=%02X tail=%02X pc=%06X bytes=%s",
      label, slot.index, slot.address, slot.bytes[3], slot.bytes[4],
      target, bytes_to_hex(slot.bytes)))
  end
end

local function dump_slot_diff(label, before, after)
  print(string.format("LOAD_TIMELINE_SLOT_DIFF_BEGIN label=%s", label))
  for i = 1, math.min(#before.slots, #after.slots) do
    local diffs = {}
    for offset = 0, 0x15 do
      local bv = before.slots[i].bytes[offset + 1]
      local av = after.slots[i].bytes[offset + 1]
      if bv ~= av then
        diffs[#diffs + 1] = string.format("+%02X:%02X->%02X", offset, bv, av)
      end
    end
    print(string.format("LOAD_TIMELINE_SLOT_DIFF label=%s index=%u %s",
      label, before.slots[i].index, (#diffs == 0) and "unchanged" or table.concat(diffs, " ")))
  end
  print(string.format("LOAD_TIMELINE_SLOT_DIFF_END label=%s", label))
end

local function install_slot_tap()
  if slot_tap_installed then
    return
  end
  local table_data = read_slot_table()
  slot_table = slot_table or {}
  slot_table.idle = table_data
  slot_target_snapshot = {}
  for _, slot in ipairs(table_data.slots) do
    slot_target_snapshot[slot.index] = ((slot.bytes[7] << 24) | (slot.bytes[8] << 16) | (slot.bytes[9] << 8) | slot.bytes[10]) & 0x00ffffff
  end
  dump_slots("idle", table_data)
  taps[#taps + 1] = prog:install_read_tap(table_data.base, table_data.limit + 1, "asr10_load_timeline_slots_r", function(offset, data, mask)
    if not measuring then
      return nil
    end
    local address = byte_address(offset, mask)
    for _, slot in ipairs(table_data.slots) do
      if address >= slot.address + 6 and address <= slot.address + 9 then
        local target = slot_target_snapshot[slot.index] or 0xffffffff
        local key = string.format("%u:%06X:%06X", slot.index, target, pc())
        if not dispatch_seen[key] then
          dispatch_seen[key] = true
          dispatches[#dispatches + 1] = { time = now(), slot = slot.index, target = target, pc = pc() }
          event("scheduler_saved_pc_read", {
            string.format("slot=%u", slot.index),
            string.format("slot_address=%04X", slot.address),
            string.format("target_pc=%06X", target),
          })
        end
      end
    end
    return nil
  end)
  slot_tap_installed = true
end

local function compare_region(before, base)
  local changed = {}
  for index = 0, #before - 1 do
    local value = prog:read_u8(base + index) & 0xff
    if value ~= before[index + 1] then
      changed[#changed + 1] = { address = base + index, before = before[index + 1], after = value }
    end
  end
  return changed
end

local function read_region(base, size)
  local bytes = {}
  for index = 0, size - 1 do
    bytes[#bytes + 1] = prog:read_u8(base + index) & 0xff
  end
  return bytes
end

local function print_pc_calibration()
  local function print_kind(kind)
    local list = {}
    for source_pc, count in pairs(pc_calibration[kind]) do
      list[#list + 1] = { pc = source_pc, count = count }
    end
    table.sort(list, function(a, b)
      if a.count == b.count then return a.pc < b.pc end
      return a.count > b.count
    end)
    for _, entry in ipairs(list) do
      print(string.format("LOAD_TIMELINE_PC_CAL kind=%s pc=%06X count=%u", kind, entry.pc, entry.count))
    end
  end
  print_kind("status")
  print_kind("data")
  print("LOAD_TIMELINE_PC_CAL_RESULT data_tap_pc=FB8ABC means=previous_instruction_FB8AB6_if_status_tap_pc_FB8AA8")
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  if not port then
    reg.fail("load_timeline", string.format("missing_ioport %s", port_name))
    return
  end
  local field = port:field(mask)
  if not field then
    reg.fail("load_timeline", string.format("missing_field code=%02X mask=%08X", code, mask))
    return
  end
  event("button_press", { string.format("code=%02X", code) })
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local function handle_fdc_read(offset, data, mask)
  local address = byte_address(offset, mask)
  local value = byte_value(data, mask)
  local source_pc = pc()
  if address == 0x00fc4001 then
    remember_calibration("status", source_pc)
    local bits = value & 0xe0
    if measuring and bits ~= last_fdc_status_bits then
      event("fdc_status_bits", {
        string.format("value=%02X", value),
        string.format("RQM=%u", (value & 0x80) ~= 0 and 1 or 0),
        string.format("DIO=%u", (value & 0x40) ~= 0 and 1 or 0),
        string.format("EXM=%u", (value & 0x20) ~= 0 and 1 or 0),
      })
      last_fdc_status_bits = bits
    end
  elseif address == 0x00fc4003 then
    remember_calibration("data", source_pc)
    if measuring and source_pc == 0x00fb8abc then
      start_or_continue_block()
    elseif measuring and source_pc == 0x00fb8db8 and #blocks > 0 then
      local last = blocks[#blocks]
      last.result[#last.result + 1] = value
    end
  end
  touch_fdc(address, "r")
  return nil
end

local function handle_fdc_write(offset, data, mask)
  local address = byte_address(offset, mask)
  local value = byte_value(data, mask)
  remember_recent_fdc_write(address, value, pc())
  if measuring then
    event("fdc_write", { string.format("address=%06X", address), string.format("value=%02X", value) })
  end
  touch_fdc(address, "w")
  return nil
end

local function install_taps()
  taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "asr10_load_timeline_fdc_r", handle_fdc_read)
  taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "asr10_load_timeline_fdc_w", handle_fdc_write)

  taps[#taps + 1] = prog:install_write_tap(0x0000049c, 0x000004ef, "asr10_load_timeline_error_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    if address == 0x049d or address == 0x04ae or address == 0x04ee then
      local value = byte_value(data, mask)
      lowmem_writes[#lowmem_writes + 1] = { time = now(), pc = pc(), address = address, value = value }
      if measuring then
        event("lowmem_error_write", { string.format("address=%04X", address), string.format("value=%02X", value) })
      end
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc480a, 0x00fc480b, "asr10_load_timeline_isr_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc480b then
      local value = byte_value(data, mask)
      isr_counts.reads = isr_counts.reads + 1
      if (value & 0x01) ~= 0 then isr_counts.bit0 = isr_counts.bit0 + 1 end
      if (value & 0x02) ~= 0 then isr_counts.bit1 = isr_counts.bit1 + 1 end
      if (value & 0x08) ~= 0 then isr_counts.bit3 = isr_counts.bit3 + 1 end
      if (value & 0x20) ~= 0 then isr_counts.bit5 = isr_counts.bit5 + 1 end
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "asr10_load_timeline_panel_rx_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4817 then
      touch_panel(0x00fc4817, "rx", byte_value(data, mask))
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc4816, 0x00fc4817, "asr10_load_timeline_panel_tx_w", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4817 then
      touch_panel(0x00fc4817, "tx", byte_value(data, mask))
    end
    return nil
  end)
end

install_taps()
print("LOAD_TIMELINE start wait_for=FILE1 sequence=0A,23,02")

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("load_timeline", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

install_slot_tap()
last_display = text
event("display", { string.format("text=\"%s\"", text) })
emu.wait(emu.attotime.from_msec(500))

press_button(0x0a)
press_button(0x23)
press_button(0x02)

local load_deadline = now() + 10
while now() < load_deadline do
  local current = display.read_raw()
  if current ~= last_display then
    last_display = current
    event("display", { string.format("text=\"%s\"", current) })
  end
  if current == load_text then
    measuring = true
    load_started_at = now()
    os_before = read_region(0x00ffb0b0, 0x41)
    slot_table.load_start = read_slot_table()
    dump_slots("load_start", slot_table.load_start)
    event("load_measurement_start", { string.format("display=\"%s\"", current) })
    break
  end
  emu.wait(emu.attotime.from_msec(5))
end

if not measuring then
  reg.fail("load_timeline", string.format("load_display_timeout final_display=\"%s\"", display.read_raw()))
  manager.machine:exit()
  return
end

local deadline = now() + 120
local ten_second_dump_done = false
local sanity_done = false
local next_display_check = now()
while now() < deadline do
  emu.wait(emu.attotime.from_usec(50))

  if now() >= next_display_check then
    local current = display.read_raw()
    if current ~= last_display then
      last_display = current
      event("display", { string.format("text=\"%s\"", current) })
      if current ~= load_text then
        event("progress_or_stable_new_display", { string.format("text=\"%s\"", current) })
        break
      end
    end
    next_display_check = now() + 0.010
  end

  if after_last_sampling_until and now() <= after_last_sampling_until then
    after_last_samples[#after_last_samples + 1] = { time = now(), pc = pc() }
  end

  if block and last_data_time and (now() - last_data_time) > 0.001 then
    close_block("data_gap")
    slot_table.after_last_transfer = read_slot_table()
    dump_slots("after_last_transfer", slot_table.after_last_transfer)
  end

  local progress_anchor = last_data_time or last_fdc_activity_time
  if progress_anchor and not after_last_sequence_printed and (now() - progress_anchor) > 1.0 then
    print("LOAD_TIMELINE_AFTER_LAST_PC_BEGIN")
    for index, sample in ipairs(after_last_samples) do
      print(string.format("LOAD_TIMELINE_AFTER_LAST_PC index=%03u t=%.6f pc=%06X", index, sample.time, sample.pc))
    end
    print("LOAD_TIMELINE_AFTER_LAST_PC_END")
    after_last_sequence_printed = true
    if not slot_table.after_last_transfer then
      slot_table.after_last_transfer = read_slot_table()
      dump_slots("after_last_fdc_activity", slot_table.after_last_transfer)
    end
  end

  if fdc_phase and (now() - fdc_phase.last) > fdc_gap then
    event("fdc_phase_end", {
      string.format("phase=%u", fdc_phase.index),
      string.format("first=%.6f", fdc_phase.first),
      string.format("last=%.6f", fdc_phase.last),
      string.format("reads=%u", fdc_phase.reads),
      string.format("writes=%u", fdc_phase.writes),
      string.format("last_pc=%06X", fdc_phase.last_pc),
    })
    fdc_phase = nil
  end

  if panel_phase and (now() - panel_phase.last) > panel_gap then
    event("panel_phase_end", {
      string.format("phase=%u", panel_phase.index),
      string.format("first=%.6f", panel_phase.first),
      string.format("last=%.6f", panel_phase.last),
      string.format("first_%s=%02X", panel_phase.first_direction, panel_phase.first_value),
      string.format("last_%s=%02X", panel_phase.last_direction, panel_phase.last_value),
    })
    panel_phase = nil
  end

  if progress_anchor and not ten_second_dump_done and (now() - progress_anchor) > 10.0 then
    slot_table.ten_seconds_later = read_slot_table()
    dump_slots("ten_seconds_later", slot_table.ten_seconds_later)
    dump_slot_diff("idle_to_after_last", slot_table.idle, slot_table.after_last_transfer or slot_table.ten_seconds_later)
    dump_slot_diff("after_last_to_10s", slot_table.after_last_transfer or slot_table.ten_seconds_later, slot_table.ten_seconds_later)
    ten_second_dump_done = true
  end

  if ten_second_dump_done and not sanity_done then
    sanity_button_before = rhrb_reads
    press_button(0x0a)
    sanity_button_after = rhrb_reads
    event("sanity_panel_rx", {
      string.format("before=%u", sanity_button_before),
      string.format("after=%u", sanity_button_after),
      string.format("delta=%u", sanity_button_after - sanity_button_before),
    })
    sanity_done = true
  end
end

measuring = false
if block then
  close_block("script_end")
end
if fdc_phase then
  event("fdc_phase_end", {
    string.format("phase=%u", fdc_phase.index),
    string.format("first=%.6f", fdc_phase.first),
    string.format("last=%.6f", fdc_phase.last),
    string.format("reads=%u", fdc_phase.reads),
    string.format("writes=%u", fdc_phase.writes),
    string.format("last_pc=%06X", fdc_phase.last_pc),
  })
end
if panel_phase then
  event("panel_phase_end", {
    string.format("phase=%u", panel_phase.index),
    string.format("first=%.6f", panel_phase.first),
    string.format("last=%.6f", panel_phase.last),
    string.format("first_%s=%02X", panel_phase.first_direction, panel_phase.first_value),
    string.format("last_%s=%02X", panel_phase.last_direction, panel_phase.last_value),
  })
end

os_after = read_region(0x00ffb0b0, 0x41)
local os_changes = os_before and compare_region(os_before, 0x00ffb0b0) or {}
print_pc_calibration()
print(string.format("LOAD_TIMELINE_BLOCK_SUMMARY count=%u", #blocks))
for _, item in ipairs(blocks) do
  print(string.format("LOAD_TIMELINE_BLOCK index=%u return_pc=%06X start_a1=%06X end_a1=%06X bytes=%u d0_start=%08X result=\"%s\" fdc_writes=\"%s\"",
    item.index, item.return_pc, item.start_a1, item.end_a1, item.count,
    item.d0_start, bytes_to_hex(item.result), item.command_before))
end
print(string.format("LOAD_TIMELINE_DISPATCH_SUMMARY count=%u", #dispatches))
for _, item in ipairs(dispatches) do
  print(string.format("LOAD_TIMELINE_DISPATCH t=%.6f slot=%u target_pc=%06X tap_pc=%06X",
    item.time, item.slot, item.target, item.pc))
end
print(string.format("LOAD_TIMELINE_SANITY os_region_changes=%u isr_reads=%u bit3_counter=%u bit5_rxB=%u bit1_rxA=%u bit0_txA=%u thrb_writes=%u rhrb_reads=%u panel_rx_sanity_delta=%s final_display=\"%s\"",
  #os_changes, isr_counts.reads, isr_counts.bit3, isr_counts.bit5, isr_counts.bit1,
  isr_counts.bit0, thrb_writes, rhrb_reads,
  sanity_button_before and tostring(sanity_button_after - sanity_button_before) or "not_run",
  display.read_raw()))
for _, change in ipairs(os_changes) do
  print(string.format("LOAD_TIMELINE_OS_CHANGE address=%06X before=%02X after=%02X", change.address, change.before, change.after))
end

manager.machine:exit()
