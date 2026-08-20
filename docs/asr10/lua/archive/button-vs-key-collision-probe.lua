-- Tests the hypothesis that key_down()'s generic byte encoding
-- (0x80|(key&0x3f)) collides with the button wire protocol
-- (0x80|button_code), both already 0x00-0x3F, so firmware may be
-- interpreting a KEY_C press (default octave 2, key=24) as "BTN_18
-- pressed" rather than a note event. If a real, direct BTN_18 press
-- produces the SAME (zero) reaction as the key press did, that's
-- consistent with the collision; if BTN_18 does something visible, the
-- collision theory needs revisiting.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}
local function now() return emu.time() end
local function byte_address(offset, mask) return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0) end

local rhrb_reads = 0
taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "bvk_rhrb_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4817 then rhrb_reads = rhrb_reads + 1 end
  return nil
end)

local function press(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("button_vs_key_collision_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

press(":panel:buttons_0", 1 << 0x0a)
press(":panel:buttons_32", 1 << 0x03)
press(":panel:buttons_0", 1 << 0x02)

ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("button_vs_key_collision_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

-- BTN_18 (0x18 = 24, the same numeric code KEY_C at default octave 2
-- would send: 2*12+0=24=0x18): buttons_0 bit 0x18.
local before_rhrb = rhrb_reads
local before_display = display.read_raw()
print(string.format("BVK_BTN18_PRESS t=%.6f", now()))
press(":panel:buttons_0", 1 << 0x18, 150)
emu.wait(emu.attotime.from_msec(500))
local after_display = display.read_raw()
print(string.format("BVK_BTN18_RESULT rhrb_delta=%u display_before=\"%s\" display_after=\"%s\" changed=%s",
  rhrb_reads - before_rhrb, before_display, after_display, tostring(before_display ~= after_display)))

print(string.format("BVK_SUMMARY final_display=\"%s\" t=%.6f", after_display, now()))
reg.pass("button_vs_key_collision_probe", "see BVK_ lines")
