-- Ready-line investigation, Del 1: full FDC dialogue including AUX command
-- port ($FC4001, motor enable/rate/precomp/reset group) alongside the FIFO
-- port ($FC4003), to correlate motor on/off timing against the single IRQ1
-- IACK previously measured at t=15.032527s
-- (docs/asr10/investigations/irq1-handler-chain-probe.md). Runs on the
-- CURRENT unwired tree -- the FDC dialogue up to the crash point is
-- identical with or without the naive IRQ1 wiring (already proven
-- deterministic across every prior run in this line of investigation), so
-- no C++ change is needed for this half of the measurement.
--
-- Observation only.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local taps = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "ready_dialogue_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  local value = byte_value(data, mask)
  if address == 0x00fc4001 then
    print(string.format("READYDLG_AUX_W t=%.6f pc=%06X value=%02X", now(), pc(), value))
  elseif address == 0x00fc4003 then
    print(string.format("READYDLG_FIFO_W t=%.6f pc=%06X value=%02X", now(), pc(), value))
  end
  return nil
end)

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "

print("READYDLG start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("fdc_ready_dialogue_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("READYDLG_FILE1 t=%.6f", now()))

-- Also cover the instrument-load window, for full context.
emu.wait(emu.attotime.from_msec(500))
local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 15
while now() < deadline do
  emu.wait(emu.attotime.from_msec(50))
end

print(string.format("READYDLG_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("fdc_ready_dialogue_probe", string.format("display=\"%s\"", display.read_raw()))
