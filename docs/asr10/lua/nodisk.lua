-- With no mounted disk image, firmware must ask for a disk.

local test = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local expected = "  PLEA5E IN5ERT DI5K  "
local ok, text = test.wait_for_text(expected, 45)

if ok then
  test.pass("nodisk", string.format("display=\"%s\"", text))
else
  test.fail("nodisk", string.format("timeout final_display=\"%s\"", text))
end
