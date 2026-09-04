-- Del 1.1 follow-up (docs/asr10/investigations/keyboard-and-sample-bridge.md):
-- are vector 4's and 11's table values ($FC6000/$FC6014) plausible code
-- addresses at all, or do they hold data, meaning no handler is
-- installed rather than "handler present but ambiguous to measure"?

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("vector_4_11_content_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

local function dump(vecnum)
  local addr = prog:read_u32(vecnum * 4) & 0x00ffffff
  print(string.format("V411_TABLE vector=%d addr=%06X even=%s", vecnum, addr, tostring(addr % 2 == 0)))
  local bytes = {}
  for i = 0, 15 do
    bytes[#bytes + 1] = string.format("%02X", prog:read_u8(addr + i) & 0xff)
  end
  print(string.format("V411_BYTES vector=%d addr=%06X bytes=%s", vecnum, addr, table.concat(bytes, " ")))
  -- printable-ASCII fraction, a quick data-vs-code signal
  local printable = 0
  for i = 0, 15 do
    local b = prog:read_u8(addr + i) & 0xff
    if b >= 0x20 and b < 0x7f then printable = printable + 1 end
  end
  print(string.format("V411_PRINTABLE_FRACTION vector=%d printable=%d/16", vecnum, printable))
end

dump(4)
dump(11)

-- Compare against a genuinely-real handler's opening bytes (vector 3,
-- ROM, already confirmed real) for a same-run reference point.
dump(3)

reg.pass("vector_4_11_content_probe", "see V411_ lines")
