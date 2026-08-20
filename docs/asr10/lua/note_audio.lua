-- 8th regression test (keyboard-and-sample-bridge-7.md): locks in real
-- audio output, not just register-level activity. Boots, loads the
-- instrument, selects it (BTN_02 from the idle FILE LOADED screen --
-- keyboard-and-sample-bridge-5.md's finding that load and select are
-- two distinct steps), plays a note via MIDI, and reports PASS/FAIL
-- for the structural preconditions (RHRA reception, ES5506 voice
-- writes reaching a mapped bank). The actual audio-nonzero/frequency
-- verdict is computed OUTSIDE this script by
-- docs/asr10/lua/check_note_audio.py against the -wavwrite capture
-- this test must be run under -- this script alone cannot inspect
-- the OSD audio output. regression-test.sh's run_test_audio() wires
-- the two together.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local voice_writes = 0
local samram_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "na_es5506_w", function(offset, data, mask)
  voice_writes = voice_writes + 1
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x100000, 0x1fffff, "na_samram_w", function(offset, data, mask)
  samram_writes = samram_writes + 1
  return nil
end)

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local rhra_reads = 0
taps[#taps + 1] = prog:install_read_tap(0x00fc4806, 0x00fc4807, "na_rhra_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4807 then
    rhra_reads = rhra_reads + 1
  end
  return nil
end)

local function press_button(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("note_audio", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("note_audio", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

press_button(":panel:buttons_0", 1 << 0x02)  -- select Instrument 1
emu.wait(emu.attotime.from_msec(500))
local after_select_display = display.read_raw()

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then
  reg.fail("note_audio", "no_mdin_image_device_found")
  return
end

local before_voice = voice_writes
local before_rhra = rhra_reads
print(string.format("NOTE_AUDIO_ONSET t=%.6f", emu.time()))
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
emu.wait(emu.attotime.from_msec(1500))

local rhra_delta = rhra_reads - before_rhra
local voice_delta = voice_writes - before_voice

print(string.format("NOTE_AUDIO_STRUCTURAL rhra=%u voice_writes=%u samram_writes=%u after_select_display=\"%s\" load_err=%s",
  rhra_delta, voice_delta, samram_writes, after_select_display, tostring(load_err)))

if rhra_delta < 3 then
  reg.fail("note_audio", string.format("rhra_delta=%u expected>=3 (MIDI note-on not received)", rhra_delta))
  return
end
if voice_delta < 4 then
  reg.fail("note_audio", string.format("voice_delta=%u expected>=4 (no ES5506 voice program written)", voice_delta))
  return
end
if after_select_display:find("JM DIGI") == nil then
  reg.fail("note_audio", string.format("instrument_not_selected display=\"%s\"", after_select_display))
  return
end

reg.pass("note_audio", string.format("rhra=%u voice_writes=%u", rhra_delta, voice_delta))
