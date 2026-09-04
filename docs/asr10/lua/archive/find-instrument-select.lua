-- keyboard-and-sample-bridge-5.md: sweeps all 64 panel buttons after
-- FILE LOADED, watching lowmem $330/$332 -- the instrument-slot-
-- selection state $FFB6C4 was already found reading
-- (keyboard-and-sample-bridge-4.md) -- and the display text, to find
-- which button is an "Instrument*Sequence Track" select button. Loading
-- an instrument and selecting it for play are two distinct ASR-10
-- operations (confirmed against the manual, docs/asr10/sources/
-- ASR10_manual.pdf); nothing in this project's prior work had pressed
-- a select button.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press_button(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(200))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("find_instrument_select", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("find_instrument_select", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

local function state()
  return prog:read_u16(0x330) & 0xffff, prog:read_u8(0x332) & 0xff, display.read_raw()
end

local base330, base332, base_disp = state()
print(string.format("FIS_BASELINE 330=%04X 332=%02X display=\"%s\"", base330, base332, base_disp))

local found = false
local found_btn, found_port
for btn = 0, 63 do
  local port_name = (btn < 32) and ":panel:buttons_0" or ":panel:buttons_32"
  local mask = 1 << (btn % 32)
  press_button(port_name, mask, 80)
  local v330, v332, disp = state()
  local changed = (v330 ~= base330) or (v332 ~= base332)
  if changed or disp ~= base_disp then
    print(string.format("FIS_BUTTON btn=%02X(%u) port=%s 330=%04X 332=%02X changed=%s display=\"%s\"",
      btn, btn, port_name, v330, v332, tostring(changed), disp))
  end
  if changed then
    found = true
    found_btn, found_port = btn, port_name
    print(string.format("FIS_FOUND btn=%02X(%u) port=%s", btn, btn, port_name))
    break
  end
end

if not found then
  print("FIS_NOT_FOUND swept all 64 buttons, $330/$332 never changed")
end

local f330, f332, fdisp = state()
print(string.format("FIS_FINAL 330=%04X 332=%02X display=\"%s\"", f330, f332, fdisp))
if found then
  reg.pass("find_instrument_select", string.format("btn=%02X port=%s", found_btn, found_port))
else
  reg.fail("find_instrument_select", "no_button_changed_330_332")
end
