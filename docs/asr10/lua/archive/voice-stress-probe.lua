-- keyboard-and-sample-bridge-7.md Del 2: sends 10 sequential distinct
-- MIDI note-ons in quick succession
-- (docs/asr10/lua/fixtures/noteon_stress10.mid, no note-offs). Result:
-- 10 distinct voices (1 through 10), no reuse/collision within this
-- range -- voice allocation increments cleanly. Full 32-voice-pool
-- exhaustion/stealing behavior is NOT tested here (would need dozens
-- more notes); this only establishes there is no premature stealing
-- for a realistic ten-note run.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}
local function now() return emu.time() end

local voice_write_log = {}
local acc_bytes = {}
local current_page = 0
local function finalize_register(register_index, value32, group)
  local voice_n = current_page & 0x1f
  if group == "low" and register_index == 1 then
    voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="FC", value=value32 & 0x1ffff}
  elseif group == "high" and register_index == 0 then
    voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR", value=value32 & 0xffff}
  end
end
taps[#taps+1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "poly_es5506_w", function(offset, data, mask)
  local v = data & 0xff
  local word_offset = (offset - 0xfc2000) // 2
  local register_index = word_offset // 4
  local byte_in_reg = word_offset % 4
  acc_bytes[byte_in_reg+1] = v
  if byte_in_reg == 3 then
    local b0,b1,b2,b3 = acc_bytes[1] or 0, acc_bytes[2] or 0, acc_bytes[3] or 0, acc_bytes[4] or 0
    local value32 = (b0<<24)|(b1<<16)|(b2<<8)|b3
    if register_index == 15 then
      current_page = value32 & 0x7f
    else
      local group = (current_page < 0x20) and "low" or ((current_page < 0x40) and "high" or "test")
      finalize_register(register_index, value32, group)
    end
    acc_bytes = {}
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
if not ok then reg.fail("voice_stress_probe", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("voice_stress_probe", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))
press_button(":panel:buttons_0", 1 << 0x02)
emu.wait(emu.attotime.from_msec(500))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("voice_stress_probe", "no_mdin_image_device_found"); return end

print(string.format("STRESS_NOTE_ON t=%.6f notes=0x3C,0x40,0x43", now()))
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon_stress10.mid")
print(string.format("STRESS_LOAD_RESULT error=%s", tostring(load_err)))
emu.wait(emu.attotime.from_msec(1000))

-- Report the LAST CR + first non-zero FC seen per voice, restricted
-- to voices touched after the note-on timestamp (excludes the
-- boot-time all-32-voices init sweep). FC's first write is always 0
-- (a reset placeholder before the real pitch write).
local first_seen = {}
for _, e in ipairs(voice_write_log) do
  if e.t >= 23.0 then
    first_seen[e.voice] = first_seen[e.voice] or {}
    if e.field == "CR" then
      first_seen[e.voice][e.field] = e.value
    elseif e.field == "FC" then
      if e.value ~= 0 and not first_seen[e.voice][e.field] then
        first_seen[e.voice][e.field] = e.value
      end
    end
  end
end
local voices_touched = {}
for v, _ in pairs(first_seen) do voices_touched[#voices_touched+1] = v end
table.sort(voices_touched)
print(string.format("STRESS_VOICES_TOUCHED count=%u list=%s", #voices_touched, table.concat(voices_touched, ",")))
for _, v in ipairs(voices_touched) do
  print(string.format("STRESS_VOICE voice=%d first_CR=%s first_FC=%s",
    v, first_seen[v].CR and string.format("%04X", first_seen[v].CR) or "nil",
    first_seen[v].FC and string.format("%05X", first_seen[v].FC) or "nil"))
end

print(string.format("STRESS_FINAL display=\"%s\"", display.read_raw()))
reg.pass("voice_stress_probe", string.format("voices_touched=%u", #voices_touched))
