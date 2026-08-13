-- ASR-10 FDC load destination observer.
--
-- Observation/in-panel-input only. Starts an instrument load from FILE 1,
-- samples A1 only when the $FC4003 FIFO read is the destination-copy
-- instruction at $FB8AB6, reports other FIFO readers separately, and exits
-- when FIFO reads have stopped for more than one emulated second.
--
-- [RETRACTED] Destinationsgrupperna fran forsta A1-matningen. A1 ar
--             destinationspekare endast vid $FB8AB6; ovriga observationer
--             var A1:s ovidkommande innehall vid andra FDC-lasare.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local a1_state = cpu.state["A1"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local destination_pc = 0xfb8ab6
local total_reads = 0
local destination_reads = 0
local other_reads = 0
local other_readers = {}
local first_a1 = nil
local last_a1 = nil
local min_a1 = nil
local max_a1 = nil
local last_read_time = nil
local saw_load_read = false
local load_started = false
local previous_total_reads = 0
local previous_destination_reads = 0
local previous_other_reads = 0
local seconds = 0
local ranges = {}
local current_range = nil
local taps = {}
local writes_enabled = false
local os_writes = {}
local binding_writes = {}

local function pc()
  if pc_state then
    return pc_state.value & 0x00ffffff
  end
  return 0xffffffff
end

local function a1()
  if a1_state then
    return a1_state.value & 0x00ffffff
  end
  return 0xffffffff
end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0x00ff) or ((data >> 8) & 0x00ff)
end

local function remember_range(value)
  if current_range and value == ((current_range.last + 1) & 0x00ffffff) then
    current_range.last = value
    current_range.count = current_range.count + 1
    return
  end

  if current_range then
    ranges[#ranges + 1] = current_range
  end
  current_range = { first = value, last = value, count = 1 }
end

local function remember_destination_read(value)
  destination_reads = destination_reads + 1
  first_a1 = first_a1 or value
  last_a1 = value
  min_a1 = min_a1 and math.min(min_a1, value) or value
  max_a1 = max_a1 and math.max(max_a1, value) or value
  remember_range(value)
end

local function remember_other_read(source_pc)
  other_reads = other_reads + 1
  other_readers[source_pc] = (other_readers[source_pc] or 0) + 1
end

local function remember_fifo_read()
  total_reads = total_reads + 1
  last_read_time = emu.time()
  if load_started then
    saw_load_read = true
  end

  local source_pc = pc()
  if source_pc == destination_pc then
    remember_destination_read(a1())
  else
    remember_other_read(source_pc)
  end
end

local function remember_write(events, label, offset, data, mask)
  if not writes_enabled then
    return
  end

  local address = byte_address(offset, mask)
  local old_value = prog:read_u8(address)
  local new_value = byte_value(data, mask)
  local event = {
    time = emu.time(),
    pc = pc(),
    address = address,
    old_value = old_value,
    new_value = new_value
  }
  events[#events + 1] = event
  print(string.format(
    "LOAD_DEST_WRITE region=%s t=%.6f pc=%06X address=%06X old=%02X new=%02X",
    label, event.time, event.pc, event.address, event.old_value, event.new_value))
end

local function install_taps()
  local function fifo_read(offset, data, mask)
    -- MAME's 16-bit 68000 taps report the ASR-10 FIFO byte stream through the
    -- containing word at $FC4002. Count every read in this two-byte window.
    remember_fifo_read()
    return nil
  end

  taps[#taps + 1] = prog:install_read_tap(0x00fc4002, 0x00fc4003, "asr10_load_destination_fifo_r", fifo_read)
  taps[#taps + 1] = prog:install_write_tap(0x00ffb0b0, 0x00ffb0f1, "asr10_load_destination_os_region_w", function(offset, data, mask)
    remember_write(os_writes, "os_ffb0b0_ffb0f0", offset, data, mask)
  end)
  taps[#taps + 1] = prog:install_write_tap(0x00ff8000, 0x00ff8101, "asr10_load_destination_binding_sample_w", function(offset, data, mask)
    remember_write(binding_writes, "binding_ff8000_ff8100", offset, data, mask)
  end)
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  if not port then
    reg.fail("load_destination", string.format("missing_ioport %s", port_name))
    return
  end
  local field = port:field(mask)
  if not field then
    reg.fail("load_destination", string.format("missing_field code=%02X mask=%08X", code, mask))
    return
  end

  print(string.format("LOAD_DEST_PRESS t=%.6f code=%02X", emu.time(), code))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local function print_snapshot()
  print(string.format(
    "LOAD_DEST t=%04d total_reads=%u(+%u) dest_reads=%u(+%u) other_reads=%u(+%u) first_a1=%06X last_a1=%06X min_a1=%06X max_a1=%06X pc=%06X display=\"%s\"",
    seconds,
    total_reads, total_reads - previous_total_reads,
    destination_reads, destination_reads - previous_destination_reads,
    other_reads, other_reads - previous_other_reads,
    first_a1 or 0, last_a1 or 0, min_a1 or 0, max_a1 or 0, pc(), display.read_raw()))
  previous_total_reads = total_reads
  previous_destination_reads = destination_reads
  previous_other_reads = other_reads
end

local function finish_ranges()
  if current_range then
    ranges[#ranges + 1] = current_range
    current_range = nil
  end
end

local function dump_ranges()
  finish_ranges()
  print(string.format("LOAD_DEST_RANGES count=%u destination_reads=%u total_reads=%u", #ranges, destination_reads, total_reads))
  for index, range in ipairs(ranges) do
    local size = range.last >= range.first
      and (range.last - range.first + 1)
      or (0x01000000 - range.first + range.last + 1)
    print(string.format("LOAD_DEST_RANGE index=%u first=%06X last=%06X size=%u samples=%u",
      index, range.first, range.last, size, range.count))
  end
end

local function sorted_reader_list()
  local list = {}
  for source_pc, count in pairs(other_readers) do
    list[#list + 1] = { pc = source_pc, count = count }
  end
  table.sort(list, function(a, b)
    if a.count == b.count then
      return a.pc < b.pc
    end
    return a.count > b.count
  end)
  return list
end

local function dump_other_readers()
  print(string.format("LOAD_DEST_OTHER_READERS count=%u reads=%u", #sorted_reader_list(), other_reads))
  for _, entry in ipairs(sorted_reader_list()) do
    print(string.format("LOAD_DEST_OTHER_READER pc=%06X reads=%u", entry.pc, entry.count))
  end
end

local function dump_writes()
  print(string.format("LOAD_DEST_WRITES region=os_ffb0b0_ffb0f0 count=%u", #os_writes))
  print(string.format("LOAD_DEST_WRITES region=binding_ff8000_ff8100 count=%u", #binding_writes))
end

install_taps()
print("[RETRACTED] Destinationsgrupperna fran forsta A1-matningen. A1 ar destinationspekare endast vid $FB8AB6; ovriga observationer var A1:s ovidkommande innehall vid andra FDC-lasare.")
print("LOAD_DEST start wait_for=FILE1 sequence=0A,23,02")

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("load_destination", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

emu.wait(emu.attotime.from_msec(500))
print(string.format("LOAD_DEST_REACHED_FILE1 t=%.6f display=\"%s\"", emu.time(), text))
writes_enabled = true

last_read_time = nil
saw_load_read = false
previous_total_reads = total_reads
previous_destination_reads = destination_reads
previous_other_reads = other_reads
load_started = true
press_button(0x0a)
press_button(0x23)
press_button(0x02)
print(string.format("LOAD_DEST_SEQUENCE_DONE t=%.6f display=\"%s\"", emu.time(), display.read_raw()))

local deadline = emu.time() + 120
local next_second = math.floor(emu.time()) + 1
while emu.time() < deadline do
  emu.wait(emu.attotime.from_msec(10))
  if emu.time() >= next_second then
    seconds = seconds + 1
    print_snapshot()
    next_second = next_second + 1
  end
  if saw_load_read and last_read_time and (emu.time() - last_read_time) > 1.0 then
    print(string.format("LOAD_DEST_STOP t=%.6f last_read=%.6f gap=%.6f display=\"%s\"",
      emu.time(), last_read_time, emu.time() - last_read_time, display.read_raw()))
    print_snapshot()
    dump_ranges()
    dump_other_readers()
    dump_writes()
    manager.machine:exit()
    return
  end
end

print(string.format("FAIL load_destination no_stop reads=%u saw_load_read=%s display=\"%s\"",
  total_reads, tostring(saw_load_read), display.read_raw()))
dump_ranges()
dump_other_readers()
dump_writes()
manager.machine:exit()
