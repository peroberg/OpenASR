-- Verify that the upper ASR-10 panel button port can be driven from Lua.
--
-- Presses BTN_23 through :panel:buttons_32 and verifies that channel B RHRB
-- consumes exactly the two-byte button frame.

local test = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local rhrb_reads = 0
local taps = {}

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "asr10_button_upper_rhrb_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4817 then
    rhrb_reads = rhrb_reads + 1
  end
  return nil
end)

local file1 = "FILE 1  TUT0RIAL BNK  "
local ok, text = test.wait_for_text(file1, 45)
if not ok then
  test.fail("button_upper", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

emu.wait(emu.attotime.from_msec(500))

local before = rhrb_reads
local port = manager.machine.ioport.ports[":panel:buttons_32"]
if not port then
  test.fail("button_upper", "missing_ioport :panel:buttons_32")
  return
end

local field = port:field(0x00000008)
if not field then
  test.fail("button_upper", "missing_field BTN_23 mask=0x00000008")
  return
end

field:set_value(1)
emu.wait(emu.attotime.from_msec(250))

local delta = rhrb_reads - before
field:clear_value()

if delta == 2 then
  test.pass("button_upper", string.format("button=23 rhrb_delta=%u", delta))
else
  test.fail("button_upper", string.format("button=23 rhrb_delta=%u expected=2", delta))
end
