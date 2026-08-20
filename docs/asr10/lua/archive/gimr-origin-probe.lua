-- Del 2 of the MC68302 consolidation follow-up
-- (docs/asr10/investigations/mc68302-consolidation-2.md): does $51/$56
-- derive from the GIMR state firmware actually writes, or is the
-- delivered vector independent of it?
--
-- GIMR ($FC6812, offset $0812) sits inside the SIB window, subject to
-- the same BAR-reinstall tap-drop trap (#8.5) as everything else there.
-- Rather than fight that with a write tap, this polls GIMR's CURRENT
-- value directly via prog:read_u16() at fine intervals throughout the
-- run -- a plain read always goes through whatever window mapping is
-- CURRENTLY live, so it is immune to the tap-lifetime problem entirely
-- (no tap object to tear down). Fine enough polling answers "does it
-- change, and when" without needing a surviving tap.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function now() return emu.time() end

local samples = {}
local last_value = nil

local function poll()
  local v = prog:read_u16(0x00fc6812) & 0xffff
  if v ~= last_value then
    samples[#samples + 1] = { t = now(), value = v }
    print(string.format("GOP_GIMR_CHANGE t=%.6f value=%04X", now(), v))
    last_value = v
  end
end

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

-- Fine polling (1ms) through the first 8s -- covers BAR's own settle
-- window (measured elsewhere at t~5.395s) plus margin, where GIMR is
-- most likely to be set if it is set at all.
local t0 = now()
poll()
while now() < t0 + 8 do
  emu.wait(emu.attotime.from_msec(1))
  poll()
end
print(string.format("GOP_EARLY_WINDOW_DONE t=%.6f", now()))

-- Coarser polling (100ms) through the rest of boot + load, in case GIMR
-- changes later.
local ok, text
local deadline1 = now() + 60
while now() < deadline1 do
  poll()
  if reg.display.read_raw() == file1 then break end
  emu.wait(emu.attotime.from_msec(100))
end
if reg.display.read_raw() ~= file1 then
  reg.fail("gimr_origin_probe", string.format("boot_timeout final_display=\"%s\"", reg.display.read_raw()))
  return
end
print(string.format("GOP_FILE1 t=%.6f", now()))

press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline2 = now() + 20
while now() < deadline2 do
  poll()
  if reg.display.read_raw() == file_loaded then break end
  emu.wait(emu.attotime.from_msec(100))
end
if reg.display.read_raw() ~= file_loaded then
  reg.fail("gimr_origin_probe", string.format("no_file_loaded final_display=\"%s\"", reg.display.read_raw()))
  return
end
print(string.format("GOP_LOADED t=%.6f", now()))

poll()
print(string.format("GOP_RESULTS distinct_values=%u", #samples))
for _, s in ipairs(samples) do
  print(string.format("GOP_SAMPLE t=%.6f value=%04X", s.t, s.value))
end
print(string.format("GOP_SUMMARY final_display=\"%s\" t=%.6f", reg.display.read_raw(), now()))
reg.pass("gimr_origin_probe", string.format("distinct_values=%u", #samples))
