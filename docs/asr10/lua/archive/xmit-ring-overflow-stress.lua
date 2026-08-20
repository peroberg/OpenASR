-- Del 2's XMIT_RING_SIZE=16 stress test: presses many keys with zero
-- wait between them (the worst case a scripted/fast player could
-- produce) and checks whether firmware receives the exact expected byte
-- count. XMIT_RING_SIZE=16 bytes = 8 two-byte events; 13 note keys
-- pressed back-to-back is 26 key_down bytes alone, already past the
-- ring's capacity if triggered faster than tra_complete() drains it.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}
local function now() return emu.time() end
local function byte_address(offset, mask) return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0) end

local rhrb_bytes = {}
taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "xros_rhrb_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4817 then
    rhrb_bytes[#rhrb_bytes + 1] = now()
  end
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("xmit_ring_overflow_stress", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

-- Press all 13 note-key bits simultaneously (one write to the port
-- setting all 13 bits at once -- the closest a scripted test can get to
-- "zero delay between presses"), hold briefly, release all at once.
local port = manager.machine.ioport.ports[":panel:keys_0"]
local ALL_13_NOTES = 0x00001fff -- bits 0-12
local field_masks = {}
for bit = 0, 12 do
  field_masks[#field_masks + 1] = port:field(1 << bit)
end

local before = #rhrb_bytes
print(string.format("XROS_PRESS_ALL t=%.6f", now()))
for _, f in ipairs(field_masks) do
  f:set_value(1)
end
emu.wait(emu.attotime.from_msec(150))
for _, f in ipairs(field_masks) do
  f:clear_value()
end
emu.wait(emu.attotime.from_msec(500))

local received = #rhrb_bytes - before
local expected = 13 * 2 * 2 -- 13 keys x (key_down + key_up) x 2 bytes each
print(string.format("XROS_RESULT expected_bytes=%u received_bytes=%u lost=%d", expected, received, expected - received))
if received < expected then
  print(string.format("XROS_OVERFLOW_DETECTED lost=%d", expected - received))
else
  print("XROS_NO_OVERFLOW_DETECTED")
end

reg.pass("xmit_ring_overflow_stress", string.format("expected=%u received=%u", expected, received))
