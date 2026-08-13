-- ASR-10 load departure observer.
--
-- Observation/in-panel-input only. Starts an instrument load from FILE 1,
-- records the last FDC status polls, detects when $FC4001 polling has stopped
-- for more than one emulated second, then dumps the post-poll PC sequence and
-- scheduler slot table.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["CURPC"] or cpu.state["GENPC"] or cpu.state["PC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local ring = {}
local ring_limit = 50
local total_polls = 0
local last_poll_time = nil
local saw_load_poll = false
local load_started = false
local since_last_poll_samples = {}
local sample_after_poll_until = nil
local taps = {}

local function pc()
  if pc_state then
    return pc_state.value & 0x00ffffff
  end
  return 0xffffffff
end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  if (mask & 0x00ff) ~= 0 then
    return data & 0xff
  end
  return (data >> 8) & 0xff
end

local function remember_poll(value)
  total_polls = total_polls + 1
  last_poll_time = emu.time()
  if load_started then
    saw_load_poll = true
  end
  ring[#ring + 1] = { pc = pc(), value = value, time = last_poll_time }
  if #ring > ring_limit then
    table.remove(ring, 1)
  end
  since_last_poll_samples = {}
  sample_after_poll_until = last_poll_time + 0.020
end

local function install_taps()
  taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4001, "asr10_load_departure_fdc_status_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4001 then
      remember_poll(byte_value(data, mask))
    end
    return nil
  end)
end

local function read_slot_table()
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

local function bytes_to_hex(bytes)
  local parts = {}
  for _, value in ipairs(bytes) do
    parts[#parts + 1] = string.format("%02X", value)
  end
  return table.concat(parts, " ")
end

local function dump_slots(label, table_data)
  print(string.format("LOAD_DEPARTURE_SLOTS label=%s base=%04X limit=%04X count=%u display=\"%s\"",
    label, table_data.base, table_data.limit, #table_data.slots, display.read_raw()))
  for _, slot in ipairs(table_data.slots) do
    local head = slot.bytes[3]
    local tail = slot.bytes[4]
    print(string.format("LOAD_DEPARTURE_SLOT label=%s index=%u address=%04X head=%02X tail=%02X bytes=%s",
      label, slot.index, slot.address, head, tail, bytes_to_hex(slot.bytes)))
  end
end

local function dump_slot_diff(before, after)
  print("LOAD_DEPARTURE_SLOT_DIFF_BEGIN")
  local count = math.min(#before.slots, #after.slots)
  for i = 1, count do
    local b = before.slots[i]
    local a = after.slots[i]
    local diffs = {}
    for offset = 0, 0x15 do
      local bv = b.bytes[offset + 1]
      local av = a.bytes[offset + 1]
      if bv ~= av then
        diffs[#diffs + 1] = string.format("+%02X:%02X->%02X", offset, bv, av)
      end
    end
    if #diffs == 0 then
      print(string.format("LOAD_DEPARTURE_SLOT_DIFF index=%u status=unchanged", b.index))
    else
      print(string.format("LOAD_DEPARTURE_SLOT_DIFF index=%u %s", b.index, table.concat(diffs, " ")))
    end
  end
  print("LOAD_DEPARTURE_SLOT_DIFF_END")
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  if not port then
    reg.fail("load_departure", string.format("missing_ioport %s", port_name))
    return
  end
  local field = port:field(mask)
  if not field then
    reg.fail("load_departure", string.format("missing_field code=%02X mask=%08X", code, mask))
    return
  end

  print(string.format("LOAD_DEPARTURE_PRESS t=%.6f code=%02X", emu.time(), code))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local function dump_ring()
  print(string.format("LOAD_DEPARTURE_LAST_POLLS total=%u count=%u", total_polls, #ring))
  for index, entry in ipairs(ring) do
    local status = (index > (#ring - 10)) and string.format(" status=%02X", entry.value) or ""
    print(string.format("LOAD_DEPARTURE_LAST_POLL index=%02u t=%.6f pc=%06X%s",
      index, entry.time, entry.pc, status))
  end
end

local function sample_pc_after_gap()
  print("LOAD_DEPARTURE_AFTER_LAST_POLL_PC_BEGIN interval_us=50 duration_ms=20")
  for index, sample in ipairs(since_last_poll_samples) do
    print(string.format("LOAD_DEPARTURE_AFTER_LAST_POLL_PC index=%03u t=%.6f pc=%06X",
      index, sample.time, sample.pc))
  end
  print("LOAD_DEPARTURE_AFTER_LAST_POLL_PC_END")

  print("LOAD_DEPARTURE_POST_DETECT_PC_BEGIN interval_us=50 duration_ms=20")
  for index = 1, 400 do
    print(string.format("LOAD_DEPARTURE_POST_DETECT_PC index=%03u t=%.6f pc=%06X", index, emu.time(), pc()))
    emu.wait(emu.attotime.from_usec(50))
  end
  print("LOAD_DEPARTURE_POST_DETECT_PC_END")
end

install_taps()
print("LOAD_DEPARTURE start wait_for=FILE1 sequence=0A,23,02")

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("load_departure", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

emu.wait(emu.attotime.from_msec(500))
local idle_slots = read_slot_table()
dump_slots("idle", idle_slots)

ring = {}
total_polls = 0
last_poll_time = nil
saw_load_poll = false
load_started = true
press_button(0x0a)
press_button(0x23)
press_button(0x02)
print(string.format("LOAD_DEPARTURE_SEQUENCE_DONE t=%.6f display=\"%s\"", emu.time(), display.read_raw()))

local deadline = emu.time() + 120
while emu.time() < deadline do
  emu.wait(emu.attotime.from_usec(50))
  if sample_after_poll_until and emu.time() <= sample_after_poll_until then
    since_last_poll_samples[#since_last_poll_samples + 1] = { time = emu.time(), pc = pc() }
  end
  if saw_load_poll and last_poll_time and (emu.time() - last_poll_time) > 1.0 then
    print(string.format("LOAD_DEPARTURE_POLL_STOP t=%.6f last_poll=%.6f gap=%.6f display=\"%s\"",
      emu.time(), last_poll_time, emu.time() - last_poll_time, display.read_raw()))
    dump_ring()
    sample_pc_after_gap()
    local stall_slots = read_slot_table()
    dump_slots("stall", stall_slots)
    dump_slot_diff(idle_slots, stall_slots)
    manager.machine:exit()
    return
  end
end

print(string.format("FAIL load_departure no_poll_stop total_polls=%u saw_load_poll=%s display=\"%s\"",
  total_polls, tostring(saw_load_poll), display.read_raw()))
manager.machine:exit()
