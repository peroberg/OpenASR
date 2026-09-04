-- The FILE 1 display must be complete, with no missing character holes in
-- the meaningful text span.

local test = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local expected = "FILE 1  TUT0RIAL BNK  "
local ok, text = test.wait_for_text(expected, 45)
if not ok then
  test.fail("display", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

if #text ~= 22 then
  test.fail("display", string.format("length=%u display=\"%s\"", #text, text))
  return
end

if text:find("?", 1, true) then
  test.fail("display", string.format("unknown_glyph display=\"%s\"", text))
  return
end

if text ~= expected then
  test.fail("display", string.format("unexpected_display display=\"%s\"", text))
  return
end

test.pass("display", string.format("display=\"%s\"", text))
