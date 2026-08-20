-- keyboard-and-sample-bridge-7.md Del 2: voice-behavior-probe.lua's
-- control run -- same note ($3C), same total hold duration (6s), but
-- NEVER sends note-off. Isolates note-off's own contribution from the
-- sample's built-in decay: at t=28.9-29.05s, held-with-note-off
-- (voice-behavior-probe.lua's wav) measures rms=3.3; this run (held
-- continuously) measures rms=56.8 at the identical timestamp -- ~17x
-- louder. Note-off measurably accelerates the release, distinct from
-- the sample's own natural decay.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local function press_button(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end
local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then reg.fail("voice_no_noteoff_probe", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("voice_no_noteoff_probe", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))
press_button(":panel:buttons_0", 1 << 0x02)
emu.wait(emu.attotime.from_msec(500))
local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("voice_no_noteoff_probe", "no_mdin_image_device_found"); return end
mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
-- Hold for the SAME total duration as voice_behavior_probe.lua's
-- note-on-then-note-off run (4s + 2s = 6s), but never send note-off,
-- to isolate note-off's own contribution from the sample's built-in
-- decay envelope.
emu.wait(emu.attotime.from_msec(6000))
reg.pass("voice_no_noteoff_probe", string.format("final_display=%s", display.read_raw()))
