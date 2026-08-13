-- ASR-10 system tick observer during instrument load.
--
-- Observation/in-panel-input only. The script waits for FILE 1, presses the
-- known GUI sequence 0A, 23, 02 to start an instrument load, then logs DUART
-- IRQ source bits, IMR writes, counter commands and display text once per
-- second. It does not use internal driver instrumentation or C++ env switches.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["CURPC"] or cpu.state["GENPC"] or cpu.state["PC"]

local file1 = "FILE 1  TUT0RIAL BNK  "

local counts = {
  isr_reads = 0,
  bit0 = 0,
  bit1 = 0,
  bit3 = 0,
  bit5 = 0,
  thrb_w = 0,
  rhrb_r = 0,
  fdc_4001 = 0,
  counter_start_r = 0,
  counter_start_w = 0,
  counter_stop_r = 0,
  counter_stop_w = 0,
}

local previous = {}
for key, value in pairs(counts) do
  previous[key] = value
end

local imr_writes = {}
local taps = {}
local phase = "boot"
local seconds = 0
local load_started_at = nil

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

local function delta(name)
  return counts[name] - previous[name]
end

local function remember_previous()
  for key in pairs(previous) do
    previous[key] = counts[key]
  end
end

local function record_isr(value)
  counts.isr_reads = counts.isr_reads + 1
  if (value & 0x01) ~= 0 then counts.bit0 = counts.bit0 + 1 end
  if (value & 0x02) ~= 0 then counts.bit1 = counts.bit1 + 1 end
  if (value & 0x08) ~= 0 then counts.bit3 = counts.bit3 + 1 end
  if (value & 0x20) ~= 0 then counts.bit5 = counts.bit5 + 1 end
end

local function install_taps()
  taps[#taps + 1] = prog:install_read_tap(0x00fc480a, 0x00fc480b, "asr10_tick_stall_isr_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc480b then
      record_isr(byte_value(data, mask))
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc480a, 0x00fc480b, "asr10_tick_stall_imr_w", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc480b then
      local event = { time = emu.time(), pc = pc(), value = byte_value(data, mask) }
      imr_writes[#imr_writes + 1] = event
      print(string.format("TICK_STALL_IMR t=%.6f pc=%06X value=%02X", event.time, event.pc, event.value))
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc481c, 0x00fc481f, "asr10_tick_stall_counter_r", function(offset, data, mask)
    local address = byte_address(offset, mask)
    if address == 0x00fc481d then
      counts.counter_start_r = counts.counter_start_r + 1
    elseif address == 0x00fc481f then
      counts.counter_stop_r = counts.counter_stop_r + 1
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc481c, 0x00fc481f, "asr10_tick_stall_counter_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    if address == 0x00fc481d then
      counts.counter_start_w = counts.counter_start_w + 1
    elseif address == 0x00fc481f then
      counts.counter_stop_w = counts.counter_stop_w + 1
    else
      return nil
    end
    print(string.format("TICK_STALL_COUNTER_WRITE t=%.6f address=%06X pc=%06X value=%02X", emu.time(), address, pc(), byte_value(data, mask)))
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc4816, 0x00fc4817, "asr10_tick_stall_thrb_w", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4817 then
      counts.thrb_w = counts.thrb_w + 1
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "asr10_tick_stall_rhrb_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4817 then
      counts.rhrb_r = counts.rhrb_r + 1
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4001, "asr10_tick_stall_fdc_status_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4001 then
      counts.fdc_4001 = counts.fdc_4001 + 1
    end
    return nil
  end)
end

local function print_snapshot()
  print(string.format(
    "TICK_STALL t=%04d phase=%s isr_reads=%u(+%u) bit3_counter=%u(+%u) bit5_rxB=%u(+%u) bit1_rxA=%u(+%u) bit0_txA=%u(+%u) fdc4001=%u(+%u) thrb_w=%u(+%u) rhrb_r=%u(+%u) ctr_start_r=%u(+%u) ctr_start_w=%u(+%u) ctr_stop_r=%u(+%u) ctr_stop_w=%u(+%u) display=\"%s\"",
    seconds, phase,
    counts.isr_reads, delta("isr_reads"),
    counts.bit3, delta("bit3"),
    counts.bit5, delta("bit5"),
    counts.bit1, delta("bit1"),
    counts.bit0, delta("bit0"),
    counts.fdc_4001, delta("fdc_4001"),
    counts.thrb_w, delta("thrb_w"),
    counts.rhrb_r, delta("rhrb_r"),
    counts.counter_start_r, delta("counter_start_r"),
    counts.counter_start_w, delta("counter_start_w"),
    counts.counter_stop_r, delta("counter_stop_r"),
    counts.counter_stop_w, delta("counter_stop_w"),
    display.read_raw()))
  remember_previous()
end

local function wait_seconds(n)
  for _ = 1, n do
    emu.wait(emu.attotime.from_seconds(1))
    seconds = seconds + 1
    print_snapshot()
  end
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  if not port then
    reg.fail("tick_stall", string.format("missing_ioport %s", port_name))
    return
  end
  local field = port:field(mask)
  if not field then
    reg.fail("tick_stall", string.format("missing_field code=%02X mask=%08X", code, mask))
    return
  end

  print(string.format("TICK_STALL_PRESS t=%.6f code=%02X", emu.time(), code))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

install_taps()
print("TICK_STALL start wait_for=FILE1 sequence=0A,23,02")

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("tick_stall", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

phase = "idle"
print(string.format("TICK_STALL_REACHED_FILE1 t=%.6f display=\"%s\"", emu.time(), text))
print_snapshot()
wait_seconds(5)

phase = "load"
load_started_at = emu.time()
press_button(0x0a)
press_button(0x23)
press_button(0x02)
print(string.format("TICK_STALL_LOAD_SEQUENCE_DONE t=%.6f load_started_at=%.6f", emu.time(), load_started_at))

wait_seconds(120)
print(string.format("TICK_STALL_SUMMARY imr_writes=%u final_display=\"%s\"",
  #imr_writes, display.read_raw()))
manager.machine:exit()
