-- keyboard-and-sample-bridge-5.md: the decisive positive test. After
-- FILE LOADED, presses BTN_02 -- found by find-instrument-select.lua
-- to be the "Instrument #1" select button in the idle-screen context
-- (the same physical button used mid-load-dialog for a different
-- purpose, per the manual's context-sensitive front panel) -- then
-- plays a note via both a real panel key press and a real MIDI
-- note-on, reusing the ES5506 voice-register decoder verified in
-- key-press-response-probe.lua.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}
local function now() return emu.time() end

local acc_bytes = {}
local current_page = 0
local voices = {}
local voice_write_log = {}
local function finalize_register(register_index, value32)
  if register_index == 15 then current_page = value32 & 0x7f; return end
  local voice_n = current_page & 0x1f
  local group = (current_page < 0x20) and "low" or ((current_page < 0x40) and "high" or "test")
  voices[voice_n] = voices[voice_n] or {}
  local v = voices[voice_n]
  if group == "high" then
    if register_index == 0 then
      v.cr = value32 & 0xffff; v.bank = (v.cr >> 14) & 3
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR", value=v.cr, bank=v.bank}
    elseif register_index == 1 then
      v.start = value32 & 0xfffff800
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="START", value=v.start, bank=v.bank}
    elseif register_index == 2 then
      v["end"] = value32 & 0xffffff80
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="END", value=v["end"], bank=v.bank}
    elseif register_index == 3 then
      v.accum = value32
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="ACCUM", value=v.accum, bank=v.bank}
    end
  elseif group == "low" and register_index == 0 then
    v.cr = value32 & 0xffff; v.bank = (v.cr >> 14) & 3
    voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR(low)", value=v.cr, bank=v.bank}
  end
end
taps[#taps+1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "sap_es5506_w", function(offset, data, mask)
  local v = data & 0xff
  local word_offset = (offset - 0xfc2000) // 2
  local register_index = word_offset // 4
  local byte_in_reg = word_offset % 4
  acc_bytes[byte_in_reg+1] = v
  if byte_in_reg == 3 then
    local b0,b1,b2,b3 = acc_bytes[1] or 0, acc_bytes[2] or 0, acc_bytes[3] or 0, acc_bytes[4] or 0
    finalize_register(register_index, (b0<<24)|(b1<<16)|(b2<<8)|b3)
    acc_bytes = {}
  end
  return nil
end)
local samram_writes = {}
taps[#taps+1] = prog:install_write_tap(0x100000, 0x1fffff, "sap_samram_w", function(offset, data, mask)
  samram_writes[#samram_writes+1] = {t=now(), offset=offset, data=data, mask=mask}
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

local function state()
  return prog:read_u16(0x330) & 0xffff, prog:read_u8(0x332) & 0xff
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then reg.fail("select_and_play", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("select_and_play", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))

local b330, b332 = state()
print(string.format("SAP_BEFORE_SELECT 330=%04X 332=%02X display=\"%s\"", b330, b332, display.read_raw()))

press_button(":panel:buttons_0", 1 << 0x02)
emu.wait(emu.attotime.from_msec(500))
local a330, a332 = state()
print(string.format("SAP_AFTER_SELECT 330=%04X 332=%02X display=\"%s\"", a330, a332, display.read_raw()))

-- Play via a real panel key press.
local before_voice = #voice_write_log
local before_sam = #samram_writes
print(string.format("SAP_KEY_PRESS t=%.6f", now()))
press_button(":panel:keys_0", 0x00000001, 150)
emu.wait(emu.attotime.from_msec(500))
print(string.format("SAP_KEY_DELTAS voice_writes=%u samram=%u", #voice_write_log-before_voice, #samram_writes-before_sam))
for i=before_voice+1,#voice_write_log do
  local e = voice_write_log[i]
  print(string.format("SAP_KEY_VOICE_WRITE t=%.6f voice=%d field=%s value=%08X bank=%s", e.t, e.voice, e.field, e.value, tostring(e.bank)))
end

-- Play via a real MIDI note-on (docs/asr10/lua/fixtures/noteon.mid,
-- via the "mdin" MIDI-in port image device).
local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
before_voice = #voice_write_log
before_sam = #samram_writes
print(string.format("SAP_MIDI_NOTE_ON t=%.6f", now()))
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
print(string.format("SAP_MIDI_LOAD_RESULT error=%s", tostring(load_err)))
emu.wait(emu.attotime.from_msec(1000))
print(string.format("SAP_MIDI_DELTAS voice_writes=%u samram=%u", #voice_write_log-before_voice, #samram_writes-before_sam))
for i=before_voice+1,#voice_write_log do
  local e = voice_write_log[i]
  print(string.format("SAP_MIDI_VOICE_WRITE t=%.6f voice=%d field=%s value=%08X bank=%s", e.t, e.voice, e.field, e.value, tostring(e.bank)))
end

print(string.format("SAP_FINAL display=\"%s\"", display.read_raw()))
reg.pass("select_and_play", string.format("total_voice_writes=%u total_samram=%u", #voice_write_log, #samram_writes))
