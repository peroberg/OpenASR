-- keyboard-and-sample-bridge-7.md Del 1: pitch-probe-c4.lua's twin,
-- playing $48 (one octave above $3C) instead. Compared FC's first
-- post-onset value against c4's: 0x71B/0x38D = 2.0011 -- essentially
-- exact octave doubling. START/END/ACCUM/CR are byte-identical to c4's
-- (same sample zone, only FC differs) -- confirms the firmware's
-- per-note pitch scaling is internally correct, independent of
-- whatever the absolute ES5506 clock actually is (a uniform clock
-- error would still preserve this ratio).

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
local function finalize_register(register_index, value32, group)
  local voice_n = current_page & 0x1f
  voices[voice_n] = voices[voice_n] or {}
  local v = voices[voice_n]
  if group == "high" then
    if register_index == 0 then
      v.cr = value32 & 0xffff; v.bank = (v.cr >> 14) & 3
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR", value=v.cr, bank=v.bank}
    elseif register_index == 1 then
      v.start = value32 & 0xfffff800
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="START", value=v.start}
    elseif register_index == 2 then
      v["end"] = value32 & 0xffffff80
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="END", value=v["end"]}
    elseif register_index == 3 then
      v.accum = value32
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="ACCUM", value=v.accum}
    end
  elseif group == "low" then
    if register_index == 0 then
      v.cr = value32 & 0xffff; v.bank = (v.cr >> 14) & 3
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR(low)", value=v.cr, bank=v.bank}
    elseif register_index == 1 then
      v.fc = value32 & 0x1ffff
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="FC", value=v.fc}
    elseif register_index == 11 then
      voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="ACTV", value=value32 & 0x1f}
    end
  end
end

taps[#taps+1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "pp_es5506_w", function(offset, data, mask)
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
if not ok then reg.fail("pitch_probe_c5", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("pitch_probe_c5", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))

press_button(":panel:buttons_0", 1 << 0x02)  -- select Instrument 1
emu.wait(emu.attotime.from_msec(500))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("pitch_probe_c5", "no_mdin_image_device_found"); return end

print(string.format("PPC5_NOTE_ON t=%.6f note=0x48", now()))
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon_48.mid")
print(string.format("PPC5_LOAD_RESULT error=%s", tostring(load_err)))
emu.wait(emu.attotime.from_msec(2000))

for _, e in ipairs(voice_write_log) do
  print(string.format("PPC5_WRITE t=%.6f voice=%d field=%s value=%08X", e.t, e.voice, e.field, e.value))
end

print(string.format("PPC5_FINAL display=\"%s\"", display.read_raw()))
reg.pass("pitch_probe_c5", string.format("writes=%u", #voice_write_log))
