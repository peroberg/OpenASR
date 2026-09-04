-- Del 1 of the sample-residency-hypothesis task
-- (docs/asr10/investigations/keyboard-and-sample-bridge-4.md): the MIDI
-- hard gate. DUART channel A is now wired to a MIDI In/Out port pair
-- (the one machine-config change this task is scoped to). This script
-- injects a note-on ($90 note velocity) via a minimal Standard MIDI
-- File loaded into the "mdin" port's image device -- the same route a
-- real MIDI cable would use, entering the voice allocator through a
-- front end the panel/scheduler chain never touches -- and measures
-- whether it produces ES5506 voice register writes or $100000-$1FFFFF
-- writes. Reuses the ES5506 register decoder verified in
-- key-press-response-probe.lua (keyboard-and-sample-bridge.md).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

-- ES5506 voice register decoder (verified in key-press-response-probe.lua).
local acc_bytes = {}
local current_page = 0
local voices = {}
local voice_write_log = {}

local function finalize_register(register_index, value32)
  if register_index == 15 then
    current_page = value32 & 0x7f
    return
  end
  local voice_n = current_page & 0x1f
  local group = (current_page < 0x20) and "low" or ((current_page < 0x40) and "high" or "test")
  voices[voice_n] = voices[voice_n] or {}
  local v = voices[voice_n]
  if group == "high" then
    if register_index == 0 then
      v.cr = value32 & 0xffff
      v.bank = (v.cr >> 14) & 3
      voice_write_log[#voice_write_log + 1] = { t = now(), voice = voice_n, field = "CR", value = v.cr, bank = v.bank }
    elseif register_index == 1 then
      v.start = value32 & 0xfffff800
      voice_write_log[#voice_write_log + 1] = { t = now(), voice = voice_n, field = "START", value = v.start }
    elseif register_index == 2 then
      v["end"] = value32 & 0xffffff80
      voice_write_log[#voice_write_log + 1] = { t = now(), voice = voice_n, field = "END", value = v["end"] }
    elseif register_index == 3 then
      v.accum = value32
      voice_write_log[#voice_write_log + 1] = { t = now(), voice = voice_n, field = "ACCUM", value = v.accum }
    end
  elseif group == "low" and register_index == 0 then
    v.cr = value32 & 0xffff
    v.bank = (v.cr >> 14) & 3
    voice_write_log[#voice_write_log + 1] = { t = now(), voice = voice_n, field = "CR(low)", value = v.cr, bank = v.bank }
  end
end

taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "mnog_es5506_w", function(offset, data, mask)
  local v = data & 0xff
  local word_offset = (offset - 0xfc2000) // 2
  local register_index = word_offset // 4
  local byte_in_reg = word_offset % 4
  acc_bytes[byte_in_reg + 1] = v
  if byte_in_reg == 3 then
    local b0, b1, b2, b3 = acc_bytes[1] or 0, acc_bytes[2] or 0, acc_bytes[3] or 0, acc_bytes[4] or 0
    finalize_register(register_index, (b0 << 24) | (b1 << 16) | (b2 << 8) | b3)
    acc_bytes = {}
  end
  return nil
end)

local samram_writes = {}
taps[#taps + 1] = prog:install_write_tap(0x100000, 0x1fffff, "mnog_samram_w", function(offset, data, mask)
  samram_writes[#samram_writes + 1] = { t = now(), offset = offset, data = data, mask = mask }
  return nil
end)

-- Witness that rx_a_w is actually reaching the DUART at all (channel A
-- RHRA, register index 0x0b within duart_panel_asr_candidate_r/w's
-- word=offset&0xf decode -- same formula that places RHRB, register
-- 0x0b, at $FC4816/17 in key-press-response-probe.lua; RHRA is register
-- 0x03, giving byte address $FC4806/07).
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local rhra_reads = 0
taps[#taps + 1] = prog:install_read_tap(0x00fc4806, 0x00fc4807, "mnog_rhra_r", function(offset, data, mask)
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

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("midi_note_on_gate", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("midi_note_on_gate", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("MNOG_LOADED t=%.6f", now()))
emu.wait(emu.attotime.from_msec(500))

-- Locate the MIDI In image device (tag ":mdin:midiinimg", the "midiin"
-- default option's own subdevice) via the image enumerator rather than
-- hardcoding, so a slot-naming mismatch fails loudly instead of finding
-- the wrong device.
local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then
    mdin_image = img
    print(string.format("MNOG_FOUND_IMAGE tag=%s", tag))
  end
end
if not mdin_image then
  reg.fail("midi_note_on_gate", "no_mdin_image_device_found")
  return
end

local before_voice_log = #voice_write_log
local before_samram = #samram_writes
local before_rhra = rhra_reads
local before_display = display.read_raw()

print(string.format("MNOG_NOTE_ON t=%.6f", now()))
-- image:load() returns nil on success, an error string on failure
-- (luaengine.cpp's image_type.load binding) -- a single value, not
-- (ok, err).
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
print(string.format("MNOG_LOAD_RESULT error=%s", tostring(load_err)))
emu.wait(emu.attotime.from_msec(1500))

local voice_log_delta = #voice_write_log - before_voice_log
local samram_delta = #samram_writes - before_samram
local rhra_delta = rhra_reads - before_rhra
local after_display = display.read_raw()

print(string.format("MNOG_DELTAS rhra=%u voice_writes=%u samram_writes=%u display_changed=%s",
  rhra_delta, voice_log_delta, samram_delta, tostring(before_display ~= after_display)))

for i = before_voice_log + 1, #voice_write_log do
  local e = voice_write_log[i]
  print(string.format("MNOG_VOICE_WRITE t=%.6f voice=%d field=%s value=%08X bank=%s",
    e.t, e.voice, e.field, e.value, tostring(e.bank)))
end
for i = before_samram + 1, #samram_writes do
  local w = samram_writes[i]
  print(string.format("MNOG_SAMRAM_WRITE t=%.6f offset=%06X data=%04X mask=%04X", w.t, w.offset, w.data, w.mask))
end

print(string.format("MNOG_DISPLAY before=\"%s\" after=\"%s\"", before_display, after_display))
print(string.format("MNOG_SUMMARY final_display=\"%s\" t=%.6f", after_display, now()))
reg.pass("midi_note_on_gate", string.format("rhra=%u voice_writes=%u samram=%u", rhra_delta, voice_log_delta, samram_delta))
