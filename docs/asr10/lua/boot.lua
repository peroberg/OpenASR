-- V3.50 must reach FILE 1 within 45 seconds of emulated time.

local test = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local expected = "FILE 1  TUT0RIAL BNK  "
local ok, text = test.wait_for_text(expected, 45)

if ok then
  test.pass("boot", string.format("display=\"%s\"", text))
else
  test.fail("boot", string.format("timeout final_display=\"%s\"", text))
end
