-- Narrower one-off diagnostic for the Stage A regression: does the second
-- alias-probe invocation select base=$600000 under 2MB wraparound, and
-- does firmware then get stuck? Uses the same narrow-tap technique as
-- memory-size-probe.lua (no wide-range taps).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

local decision_trace = {}
for _, addr in ipairs({ 0x00000c4e, 0x00000c62 }) do
  taps[#taps + 1] = prog:install_write_tap(addr, addr + 3, "diag_dec_" .. string.format("%06x", addr), function(offset, data, mask)
    decision_trace[#decision_trace + 1] = string.format("t=%.6f pc=%06X addr=%06X data=%04X", now(), pc(), offset, data & 0xffff)
    return nil
  end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
print(string.format("DIAG_BOOT ok=%s t=%.6f display=\"%s\"", tostring(ok), now(), text))
if not ok then
  for _, line in ipairs(decision_trace) do print("DIAG_DECISION " .. line) end
  manager.machine:exit()
  return
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(1 << (code & 0x1f))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
emu.wait(emu.attotime.from_seconds(3))

for _, line in ipairs(decision_trace) do print("DIAG_DECISION " .. line) end
print(string.format("DIAG_DECISION_TOTAL=%u", #decision_trace))
print(string.format("DIAG_FINAL t=%.6f pc=%06X display=\"%s\"", now(), pc(), display.read_raw()))
manager.machine:exit()
