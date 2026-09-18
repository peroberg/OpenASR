-- Regression test: interactive text editing on DISK LABEL screen.
-- Verifies that:
-- 1. Stepping with Right Arrow ($11) moves the cursor/underline from char 1 to char 2.
-- 2. Changing a character at position 2 leaves position 1 untouched.
-- 3. Stepping with Left Arrow ($10) moves the cursor/underline back to char 2 and char 1.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local function press(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 300))
end

local function underline()
  local root, bits = manager.machine.devices[":"], {}
  for output = 22, 43 do
    local value = root:output(string.format("vfd%u", output))
    bits[#bits + 1] = (value:exists() and value:get() or 0) ~= 0 and "1" or "0"
  end
  return table.concat(bits)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("disk_label_text_edit", string.format("boot_timeout display=\"%s\"", text))
  return
end

press(0x06, 350) -- COMMAND
press(0x1b, 350) -- DISK/SYSTEM
press(0x23, 350) -- ENTER -> DISK LABEL=DISK000

local expected_initial_text = "DI5K LABEL?DI5K???    "
local expected_ul_pos1      = "0000000000010000000000" -- column 11
local expected_ul_pos2      = "0000000000001000000000" -- column 12
local expected_ul_pos3      = "0000000000000100000000" -- column 13

local initial_text = display.read_raw()
local initial_ul = underline()
if initial_text ~= expected_initial_text or initial_ul ~= expected_ul_pos1 then
  reg.fail("disk_label_text_edit", string.format("initial_mismatch text=\"%s\" ul=%s", initial_text, initial_ul))
  return
end

-- Step 1: Up at position 1 (D -> E)
press(0x0a, 350)
local text_up1 = display.read_raw()
local ul_up1 = underline()
local expected_text_up1 = "DI5K LABEL?EI5K???    "
if text_up1 ~= expected_text_up1 or ul_up1 ~= expected_ul_pos1 then
  reg.fail("disk_label_text_edit", string.format("up_pos1_mismatch text=\"%s\" ul=%s", text_up1, ul_up1))
  return
end

-- Step 2: Right Arrow (move cursor to position 2)
press(0x11, 350)
local text_right1 = display.read_raw()
local ul_right1 = underline()
if text_right1 ~= expected_text_up1 or ul_right1 ~= expected_ul_pos2 then
  reg.fail("disk_label_text_edit", string.format("right_arrow_mismatch text=\"%s\" ul=%s", text_right1, ul_right1))
  return
end

-- Step 3: Up at position 2 (I -> J). Position 1 must remain 'E'!
press(0x0a, 350)
local text_up2 = display.read_raw()
local ul_up2 = underline()
local expected_text_up2 = "DI5K LABEL?EJ5K???    "
if text_up2 ~= expected_text_up2 or ul_up2 ~= expected_ul_pos2 then
  reg.fail("disk_label_text_edit", string.format("up_pos2_mismatch text=\"%s\" ul=%s", text_up2, ul_up2))
  return
end

-- Step 4: Right Arrow to position 3 (S)
press(0x11, 350)
local text_right2 = display.read_raw()
local ul_right2 = underline()
if text_right2 ~= expected_text_up2 or ul_right2 ~= expected_ul_pos3 then
  reg.fail("disk_label_text_edit", string.format("right_arrow_pos3_mismatch text=\"%s\" ul=%s", text_right2, ul_right2))
  return
end

-- Step 5: Left Arrow back to position 2 (J)
press(0x10, 350)
local text_left1 = display.read_raw()
local ul_left1 = underline()
if text_left1 ~= expected_text_up2 or ul_left1 ~= expected_ul_pos2 then
  reg.fail("disk_label_text_edit", string.format("left_arrow_pos2_mismatch text=\"%s\" ul=%s", text_left1, ul_left1))
  return
end

reg.pass("disk_label_text_edit", "char1=E char2=J right_and_left_move_cursor_ok")
