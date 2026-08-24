-- Del 3: verify the infrastructure supports two buttons held
-- simultaneously (needed for "hold Record, press Play"), using the two
-- currently keyboard-mapped buttons (BTN_0A/BTN_0B) as a stand-in pair
-- since the real Record/Play codes are not identified. Confirms
-- firmware receives two distinct down events (not merged/ghosted) and
-- two distinct up events on release, via the DUART THRB byte stream.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local stream = {}
local by_word = {}
taps[#taps + 1] = prog:install_read_tap(0x00fc4800, 0x00fc481f, "duart_r", function(offset, data, mask)
  if (mask & 0x00ff) == 0 then return nil end
  local word = offset & 0xf
  by_word[word] = (by_word[word] or 0) + 1
  stream[#stream + 1] = { word = word, data = data & 0xff }
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("simultaneous_hold", string.format("boot_timeout display=\"%s\"", text))
  return
end

local port_a = manager.machine.ioport.ports[":panel:buttons_0"]
local field_a = port_a:field(1 << 0x0a)  -- BTN_0A
local field_b = port_a:field(1 << 0x0b)  -- BTN_0B

local before = #stream
field_a:set_value(1)
emu.wait(emu.attotime.from_msec(60))
field_b:set_value(1)
emu.wait(emu.attotime.from_msec(60))
local both_held = #stream

field_a:clear_value()
emu.wait(emu.attotime.from_msec(60))
field_b:clear_value()
emu.wait(emu.attotime.from_msec(60))
local after_release = #stream

for word, count in pairs(by_word) do
  print(string.format("SH_REGHIST word=%X count=%u", word, count))
end

local frame = {}
for i = before + 1, after_release do frame[#frame + 1] = stream[i] end
local hex = {}
for _, e in ipairs(frame) do hex[#hex + 1] = string.format("%X:%02X", e.word, e.data) end
print(string.format("SH_FRAME %s", table.concat(hex, " ")))
print(string.format("SH_COUNTS before=%u both_held=%u after_release=%u", before, both_held, after_release))

-- RHRB shares THRB's register slot (word 6, same as the display-write
-- side identified in display-protocol-inventory.md) -- NOT the most-
-- polled register, which is SRB/status (word A here), read far more
-- often than the data register itself.
local rhrb = {}
for _, e in ipairs(frame) do
  if e.word == 6 then rhrb[#rhrb + 1] = e.data end
end
local rhrb_hex = {}
for _, b in ipairs(rhrb) do rhrb_hex[#rhrb_hex + 1] = string.format("%02X", b) end
print(string.format("SH_RHRB word=6 seq=%s", table.concat(rhrb_hex, " ")))

-- Expect: press A (0x8A 00), press B while A still held (0x8B 00),
-- release A (0x0A 00), release B (0x0B 00) -- four distinct 2-byte
-- events, eight bytes total, both press codes present with bit7 set
-- and both release codes present with bit7 clear.
local saw_press_a, saw_press_b, saw_release_a, saw_release_b = false, false, false, false
for i = 1, #rhrb - 1 do
  local b0, b1 = rhrb[i], rhrb[i + 1]
  if b0 == 0x8a and b1 == 0x00 then saw_press_a = true end
  if b0 == 0x8b and b1 == 0x00 then saw_press_b = true end
  if b0 == 0x0a and b1 == 0x00 then saw_release_a = true end
  if b0 == 0x0b and b1 == 0x00 then saw_release_b = true end
end

print(string.format("SH_RESULT press_a=%s press_b=%s release_a=%s release_b=%s",
  tostring(saw_press_a), tostring(saw_press_b), tostring(saw_release_a), tostring(saw_release_b)))

if saw_press_a and saw_press_b and saw_release_a and saw_release_b then
  reg.pass("simultaneous_hold", "both_buttons_generate_distinct_events")
else
  reg.fail("simultaneous_hold", "missing_expected_event")
end
