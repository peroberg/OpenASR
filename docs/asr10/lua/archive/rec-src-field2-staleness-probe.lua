-- Del 2 methodology check: does the REC SRC display text live-update
-- when a field's underlying value changes, or only on re-entry? Answer
-- (measured here): only on re-entry. $016F (Field 2) changes immediately
-- on each BTN_0A press, but the displayed text stays stale at
-- "INPUTDRY LEFT" until Sample-Source Select (BTN_20) is pressed again.
-- This caught a real bug in an earlier 64-button sweep that compared
-- display text immediately after each candidate press -- it could not
-- see any button whose only effect is a state change with no immediate
-- redraw. See rec-src-field1-sweep-corrected-probe.lua for the fixed
-- sweep built on this finding.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("sanity", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)
print(string.format("SANITY_ENTER display=\"%s\" mode=%u", display.read_raw(), prog:read_u8(0x016f)))
press_button(0x0a, 250)
print(string.format("SANITY_AFTER_0A display=\"%s\" mode=%u", display.read_raw(), prog:read_u8(0x016f)))
press_button(0x0a, 250)
print(string.format("SANITY_AFTER_0A_2 display=\"%s\" mode=%u", display.read_raw(), prog:read_u8(0x016f)))
press_button(0x20, 500)
print(string.format("SANITY_REENTER display=\"%s\" mode=%u", display.read_raw(), prog:read_u8(0x016f)))

manager.machine:exit()
