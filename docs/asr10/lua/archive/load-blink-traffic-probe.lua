-- Del 2: is LOAD's blink host-driven (periodic annunciator traffic) or
-- panel-local (silent between blinks, needing a blink-mask protocol)?
-- Capture the display byte stream for a sustained window at idle
-- FILE 1 (LOAD mode, flashing) and report inter-event timing.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local stream = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc4800, 0x00fc481f, "duart_w", function(offset, data, mask)
  if (mask & 0x00ff) == 0 then return nil end
  if (offset & 0xf) == 6 then
    stream[#stream + 1] = { t = emu.time(), d = data & 0xff }
  end
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("load_blink", string.format("boot_timeout display=\"%s\"", text))
  return
end

print(string.format("LB_START t=%.6f display=\"%s\" stream_len=%u", emu.time(), display.read_raw(), #stream))
local before = #stream
emu.wait(emu.attotime.from_seconds(3))
local after = #stream
print(string.format("LB_AFTER_3S stream_len=%u delta=%u", after, after - before))

for i = before + 1, after do
  print(string.format("LB_BYTE[%u] t=%.6f d=%02X", i, stream[i].t, stream[i].d))
end

manager.machine:exit()
