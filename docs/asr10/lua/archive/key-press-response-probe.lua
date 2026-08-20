-- Del 3 of the keyboard task (docs/asr10/investigations/
-- keyboard-and-sample-bridge.md): after FILE LOADED, press one key and
-- measure everything downstream: panel bytes sent/received, ES5506
-- voice register writes (voice/bank/START/END), new $100000-$1FFFFF
-- writes, and whether voice registers actually CHANGE from the
-- $00000/$20000 boot defaults sample-topology-closure.md measured.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

-- 1. Panel RHRB consumption (button_upper.lua's established technique).
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local rhrb_reads = 0
taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "kprp_rhrb_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4817 then
    rhrb_reads = rhrb_reads + 1
  end
  return nil
end)

-- 2. ES5506 voice register decoder (verified in
-- sample-ram-and-voice-registers-probe.lua).
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

taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "kprp_es5506_w", function(offset, data, mask)
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

-- 3. $100000-$1FFFFF write witness (same technique as sample-ram-and-
-- voice-registers-probe.lua: sibling tap on a known-hot range proves
-- liveness for the whole window).
local samram_writes = {}
taps[#taps + 1] = prog:install_write_tap(0x100000, 0x1fffff, "kprp_samram_w", function(offset, data, mask)
  samram_writes[#samram_writes + 1] = { t = now(), offset = offset, data = data, mask = mask }
  return nil
end)
local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff, "kprp_witness_w", function(offset, data, mask)
  witness_writes = witness_writes + 1
  return nil
end)

-- 4. ROM voice-management table ($8000, 32 entries x $D8 stride,
-- current-status.md's "instrument-to-otto-runtime.md" boundary) --
-- watches whether firmware's OWN voice allocation logic ever runs at
-- all, independent of whether it reaches ES5506 hardware. Distinguishes
-- "firmware never recognized this as a note" from "firmware allocated a
-- voice but never programmed the chip."
local voice_table_writes = {}
taps[#taps + 1] = prog:install_write_tap(0x00008000, 0x00009aff, "kprp_voicetable_w", function(offset, data, mask)
  voice_table_writes[#voice_table_writes + 1] = { t = now(), offset = offset, data = data, mask = mask }
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
  reg.fail("key_press_response_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("key_press_response_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("KPRP_LOADED t=%.6f", now()))
emu.wait(emu.attotime.from_msec(500))

-- Snapshot voice state BEFORE the key press.
local function snapshot_voice(n)
  local v = voices[n]
  if not v then return "unset" end
  return string.format("cr=%04X bank=%s start=%08X end=%08X accum=%08X",
    v.cr or 0, tostring(v.bank), v.start or 0, v["end"] or 0, v.accum or 0)
end
print(string.format("KPRP_VOICE0_BEFORE %s", snapshot_voice(0)))
print(string.format("KPRP_VOICE1_BEFORE %s", snapshot_voice(1)))

local before_rhrb = rhrb_reads
local before_voice_log = #voice_write_log
local before_samram = #samram_writes
local before_witness = witness_writes
local before_voicetable = #voice_table_writes
local before_display = display.read_raw()

-- Press KEY_C (":panel:keys_0", bit 0, mask 0x00000001).
print(string.format("KPRP_KEY_PRESS t=%.6f", now()))
press_button(":panel:keys_0", 0x00000001, 150)
emu.wait(emu.attotime.from_msec(500))

print(string.format("KPRP_VOICE0_AFTER %s", snapshot_voice(0)))
print(string.format("KPRP_VOICE1_AFTER %s", snapshot_voice(1)))

local rhrb_delta = rhrb_reads - before_rhrb
local voice_log_delta = #voice_write_log - before_voice_log
local samram_delta = #samram_writes - before_samram
local witness_delta = witness_writes - before_witness
local voicetable_delta = #voice_table_writes - before_voicetable
local after_display = display.read_raw()

print(string.format("KPRP_DELTAS rhrb=%u voice_writes=%u samram_writes=%u witness_writes=%u voicetable_writes=%u display_changed=%s",
  rhrb_delta, voice_log_delta, samram_delta, witness_delta, voicetable_delta, tostring(before_display ~= after_display)))
for i = before_voicetable + 1, #voice_table_writes do
  local w = voice_table_writes[i]
  print(string.format("KPRP_VOICETABLE_WRITE t=%.6f offset=%06X data=%04X mask=%04X", w.t, w.offset, w.data, w.mask))
end
print(string.format("KPRP_DISPLAY before=\"%s\" after=\"%s\"", before_display, after_display))

for i = before_voice_log + 1, #voice_write_log do
  local e = voice_write_log[i]
  print(string.format("KPRP_VOICE_WRITE t=%.6f voice=%d field=%s value=%08X bank=%s",
    e.t, e.voice, e.field, e.value, tostring(e.bank)))
end

for i = before_samram + 1, #samram_writes do
  local w = samram_writes[i]
  print(string.format("KPRP_SAMRAM_WRITE t=%.6f offset=%06X data=%04X mask=%04X", w.t, w.offset, w.data, w.mask))
end

print(string.format("KPRP_SUMMARY final_display=\"%s\" t=%.6f", after_display, now()))
reg.pass("key_press_response_probe", string.format("rhrb=%u voice_writes=%u samram=%u", rhrb_delta, voice_log_delta, samram_delta))
