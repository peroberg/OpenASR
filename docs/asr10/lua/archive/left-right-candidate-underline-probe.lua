-- Confirm raw $10 = Left Arrow (moves underline REC SRC Field2 -> Field1)
-- and find which of the remaining Data-Entry-block candidates ($21,
-- $11 -- panel-raw-map.csv mapped $23/$24) is Right Arrow, by checking
-- whether it moves the underline back from Field 1 to Field 2.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return false end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
  return true
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

local function test_candidate(code, label)
  press_button(0x20, 400)  -- fresh REC SRC entry, cursor on Field2
  print(string.format("LRT_%s_before display=\"%s\" ul=%s", label, display.read_raw(), read_underline()))
  press_button(0x10, 300)  -- Left (confirmed): move to Field1
  print(string.format("LRT_%s_after_left display=\"%s\" ul=%s", label, display.read_raw(), read_underline()))
  local sent = press_button(code, 300)
  print(string.format("LRT_%s_after_candidate code=%02X sent=%s display=\"%s\" ul=%s",
    label, code, tostring(sent), display.read_raw(), read_underline()))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("left_right_candidate", string.format("boot_timeout display=\"%s\"", text))
  return
end

test_candidate(0x21, "R21")
test_candidate(0x11, "R11")

manager.machine:exit()
