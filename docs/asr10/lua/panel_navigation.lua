-- 14th regression test: locks in the confirmed Left/Right Arrow
-- identity (raw $10/$11) actually moving the underlined field, using
-- the display's own underline output as ground truth --
-- docs/asr10/investigations/partial-update-position-probe.md Del 5.
--
-- Without this test, nothing in the suite would notice a regression
-- that broke the cursor-position opcode ($00-$1f, Del 1 of the same
-- investigation) or silently swapped which raw code is Left vs Right.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 300))
end

local function read_underline()
  local root = manager.machine.devices[":"]
  local bits = {}
  for i = 22, 43 do
    local o = root:output(string.format("vfd%u", i))
    bits[#bits + 1] = (o:exists() and o:get() or 0) ~= 0 and "1" or "0"
  end
  return table.concat(bits)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("panel_navigation", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)  -- REC SRC: Field 2 ("LEFT ") starts underlined
local ul_field2 = read_underline()
print(string.format("PN_FIELD2 display=\"%s\" ul=%s", display.read_raw(), ul_field2))
if ul_field2 ~= "0000000000000000011111" then
  reg.fail("panel_navigation", string.format("unexpected_field2_underline got=%s", ul_field2))
  return
end

press_button(0x10, 400)  -- Left Arrow (confirmed): move to Field 1 ("INPUTDRY")
local ul_field1 = read_underline()
print(string.format("PN_AFTER_LEFT display=\"%s\" ul=%s", display.read_raw(), ul_field1))
if ul_field1 ~= "0000000011111111000000" then
  reg.fail("panel_navigation", string.format("left_did_not_move_underline got=%s", ul_field1))
  return
end

press_button(0x11, 400)  -- Right Arrow (confirmed): move back to Field 2
local ul_back = read_underline()
print(string.format("PN_AFTER_RIGHT display=\"%s\" ul=%s", display.read_raw(), ul_back))
if ul_back ~= ul_field2 then
  reg.fail("panel_navigation", string.format("right_did_not_return_to_field2 got=%s expected=%s", ul_back, ul_field2))
  return
end

reg.pass("panel_navigation", "left_and_right_move_underline_correctly")
