-- Seventh regression test: a clean boot + instrument load must produce
-- zero unexpected exception vectors, zero hits on SIB registers outside
-- the calibrated inventory, and zero IDMA (SAPR/CMR/BCR) anomalies.
-- docs/asr10/investigations/mc68302-consolidation.md. Guards live in
-- docs/asr10/lua/lib/asr10_guards.lua so any future test can reuse them.
--
-- A regression that silently starts hitting a new SIB register, a new
-- interrupt vector, or a new IDMA register value will now fail this test
-- by name instead of only showing up as a garbled display string (or not
-- showing up as a display symptom at all).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local guards = dofile("docs/asr10/lua/lib/asr10_guards.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local alarms = {}
local function on_alarm(msg)
  alarms[#alarms + 1] = msg
  print(string.format("MC68302_GUARD_ALARM %s", msg))
end

-- Exception guard: cpu_space taps, no BAR-window constraint -- install
-- immediately.
for _, t in ipairs(guards.install_exception_guard(cpu, on_alarm)) do taps[#taps + 1] = t end

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

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

-- SIB coverage + IDMA guards: install only after BAR's own last write
-- (measured at t~5.395s in sib-coverage-inventory.lua; 7s gives margin,
-- matching the same established technique irq1_handler_chain_probe.lua
-- used for the same #8.5 constraint). This means the first ~7s of boot
-- is not covered by these two guards -- a known, documented gap, not an
-- oversight; see the guard library's own comments.
emu.wait(emu.attotime.from_seconds(7))
for _, t in ipairs(guards.install_sib_coverage_guard(prog, on_alarm)) do taps[#taps + 1] = t end
for _, t in ipairs(guards.install_idma_guard(prog, on_alarm)) do taps[#taps + 1] = t end
print(string.format("MC68302_GUARDS_INSTALLED t=%.6f", emu.time()))

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("mc68302_guards", string.format("boot_timeout final_display=\"%s\" alarms=%u", text, #alarms))
  return
end

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("mc68302_guards", string.format("no_file_loaded final_display=\"%s\" alarms=%u", text, #alarms))
  return
end

emu.wait(emu.attotime.from_msec(500))

if #alarms > 0 then
  reg.fail("mc68302_guards", string.format("display=\"%s\" alarms=%u first=\"%s\"", text, #alarms, alarms[1]))
  return
end

reg.pass("mc68302_guards", string.format("display=\"%s\" alarms=0", text))
