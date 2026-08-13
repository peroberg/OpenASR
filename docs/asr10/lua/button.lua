-- From FILE 1, pressing BTN_0A through the panel ioport must show FILE 2.

local test = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local file1 = "FILE 1  TUT0RIAL BNK  "
local file2 = "FILE 2  JM DIGI 5YN   "

local ok, text = test.wait_for_text(file1, 45)
if not ok then
  test.fail("button", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

local port = manager.machine.ioport.ports[":panel:buttons_0"]
if not port then
  test.fail("button", "missing_ioport :panel:buttons_0")
  return
end

local field = port:field(0x00000400)
if not field then
  test.fail("button", "missing_field BTN_0A mask=0x00000400")
  return
end

field:set_value(1)
emu.wait(emu.attotime.from_msec(50))
field:clear_value()

ok, text = test.wait_for_text(file2, 5)
if ok then
  test.pass("button", string.format("display=\"%s\"", text))
else
  test.fail("button", string.format("no_file2 final_display=\"%s\"", text))
end
