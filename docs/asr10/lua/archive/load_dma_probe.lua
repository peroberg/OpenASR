-- ASR-10 V3.50 instrument-load DMA setup observer.
--
-- Observation only. No C++ instrumentation, no model changes.
-- Starts at FILE 1, presses 0A, 23, 02, then logs writes in candidate DMA
-- windows and dumps all FCxxxx writes from the 50 ms before SPECIFY
-- command bytes 03 E1 08.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "
local taps = {}
local load_started = false
local measuring = false
local last_activity = nil
local ring = {}
local ring_limit = 20000
local candidate_counts = {
  sib = 0,
  idma = 0,
  cs_unknown_low = 0,
  cs_unknown_high = 0,
}
local candidate_events = {}
local fdc_fifo_sequence = {}
local specify_seen = false
local specify_start_time = nil

local function now()
  return emu.time()
end

local function pc()
  return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff
end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local function remember_ring(address, value)
  if not load_started then
    return
  end
  ring[#ring + 1] = { time = now(), pc = pc(), address = address, value = value }
  while #ring > ring_limit do
    table.remove(ring, 1)
  end
  local cutoff = now() - 0.050
  while ring[1] and ring[1].time < cutoff do
    table.remove(ring, 1)
  end
end

local function record_candidate(region, address, value)
  if not load_started then
    return
  end
  last_activity = now()
  candidate_counts[region] = candidate_counts[region] + 1
  local offset = address & 0x000fff
  if region == "sib" then
    offset = address - 0x00fc6800
  end
  local line = string.format(
    "LOAD_DMA_WRITE t=%.6f region=%s pc=%06X address=%06X offset=%04X value=%02X",
    now(), region, pc(), address, offset, value)
  candidate_events[#candidate_events + 1] = line
  print(line)
end

local function check_specify(address, value)
  if address ~= 0x00fc4003 or not load_started or specify_seen then
    return
  end
  fdc_fifo_sequence[#fdc_fifo_sequence + 1] = { time = now(), pc = pc(), value = value }
  while #fdc_fifo_sequence > 3 do
    table.remove(fdc_fifo_sequence, 1)
  end
  if #fdc_fifo_sequence == 3 and
      fdc_fifo_sequence[1].value == 0x03 and
      fdc_fifo_sequence[2].value == 0xe1 and
      fdc_fifo_sequence[3].value == 0x08 then
    specify_seen = true
    specify_start_time = fdc_fifo_sequence[1].time
    print(string.format("LOAD_DMA_SPECIFY t=%.6f pc=%06X bytes=03,E1,08", specify_start_time, fdc_fifo_sequence[1].pc))
    print("LOAD_DMA_PRE_SPECIFY_BEGIN window_ms=50")
    for _, item in ipairs(ring) do
      if item.time < specify_start_time and item.time >= specify_start_time - 0.050 then
        print(string.format("LOAD_DMA_PRE_SPECIFY t=%.6f pc=%06X address=%06X value=%02X",
          item.time, item.pc, item.address, item.value))
      end
    end
    print("LOAD_DMA_PRE_SPECIFY_END")
  end
end

local function handle_fc_write(offset, data, mask)
  local address = byte_address(offset, mask)
  local value = byte_value(data, mask)
  remember_ring(address, value)
  check_specify(address, value)
  return nil
end

local function install_taps()
  taps[#taps + 1] = prog:install_write_tap(0x00fc0000, 0x00fcffff, "asr10_load_dma_all_fc_w", handle_fc_write)

  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc68ff, "asr10_load_dma_sib_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    local value = byte_value(data, mask)
    record_candidate("sib", address, value)
    if address >= 0x00fc6800 and address <= 0x00fc6811 then
      record_candidate("idma", address, value)
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc0000, 0x00fc1fff, "asr10_load_dma_cs_unknown_low_w", function(offset, data, mask)
    record_candidate("cs_unknown_low", byte_address(offset, mask), byte_value(data, mask))
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc5020, 0x00fc5fff, "asr10_load_dma_cs_unknown_high_w", function(offset, data, mask)
    record_candidate("cs_unknown_high", byte_address(offset, mask), byte_value(data, mask))
    return nil
  end)
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  if not port then
    reg.fail("load_dma_probe", string.format("missing_ioport %s", port_name))
    return
  end
  local field = port:field(mask)
  if not field then
    reg.fail("load_dma_probe", string.format("missing_field code=%02X mask=%08X", code, mask))
    return
  end
  print(string.format("LOAD_DMA_PRESS t=%.6f code=%02X", now(), code))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

install_taps()
print("LOAD_DMA start wait_for=FILE1 sequence=0A,23,02")

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("load_dma_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

emu.wait(emu.attotime.from_msec(500))
load_started = true
print(string.format("LOAD_DMA_REACHED_FILE1 t=%.6f display=\"%s\"", now(), text))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local wait_deadline = now() + 10
while now() < wait_deadline do
  if display.read_raw() == load_text then
    measuring = true
    print(string.format("LOAD_DMA_LOADING t=%.6f display=\"%s\"", now(), display.read_raw()))
    break
  end
  emu.wait(emu.attotime.from_msec(5))
end

if not measuring then
  reg.fail("load_dma_probe", string.format("load_display_timeout final_display=\"%s\"", display.read_raw()))
  manager.machine:exit()
  return
end

local deadline = now() + 120
while now() < deadline do
  emu.wait(emu.attotime.from_msec(10))
  if specify_seen and last_activity and (now() - last_activity) > 1.0 then
    break
  end
end

print(string.format(
  "LOAD_DMA_SUMMARY sib_writes=%u idma_writes=%u cs_unknown_low_writes=%u cs_unknown_high_writes=%u specify_seen=%u final_display=\"%s\"",
  candidate_counts.sib, candidate_counts.idma, candidate_counts.cs_unknown_low,
  candidate_counts.cs_unknown_high, specify_seen and 1 or 0, display.read_raw()))
manager.machine:exit()
