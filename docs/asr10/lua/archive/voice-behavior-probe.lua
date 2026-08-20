-- keyboard-and-sample-bridge-7.md Del 2: plays $3C, holds 1.5s, then
-- sends a real MIDI note-off ($80 $3C $00) and watches ES5506 register
-- traffic in the 1.5s after. Found LVRAMP/RVRAMP (the left/right
-- volume-ramp registers) go negative (a release-envelope ramp) ~46ms
-- after note-off -- silencing happens via the volume envelope, not the
-- CR STOP bits. Compare against voice-no-noteoff-probe.lua's -wavwrite
-- capture for the audible confirmation (much faster decay with
-- note-off than the sample's own held/sustain decay).

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
  local names_low = {[0]="CR(low)", [1]="FC", [2]="LVOL", [3]="LVRAMP", [4]="RVOL", [5]="RVRAMP", [6]="ECOUNT"}
  local names_high = {[0]="CR", [1]="START", [2]="END", [3]="ACCUM"}
  local name = (group == "low") and names_low[register_index] or ((group=="high") and names_high[register_index])
  if name then
    voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field=name, value=value32}
  end
end
taps[#taps+1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "vbp2_es5506_w", function(offset, data, mask)
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

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local rhra_reads = {}
taps[#taps+1] = prog:install_read_tap(0x00fc4806, 0x00fc4807, "vbp2_rhra_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4807 then
    rhra_reads[#rhra_reads+1] = { t = now() }
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
if not ok then reg.fail("voice_behavior_probe2", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("voice_behavior_probe2", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))
press_button(":panel:buttons_0", 1 << 0x02)
emu.wait(emu.attotime.from_msec(500))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("voice_behavior_probe2", "no_mdin_image_device_found"); return end

print(string.format("VBP2_NOTE_ON t=%.6f", now()))
mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
emu.wait(emu.attotime.from_msec(1500))

local before_rhra = #rhra_reads
local before_writes = #voice_write_log
print(string.format("VBP2_NOTE_OFF t=%.6f", now()))
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteoff_3c.mid")
print(string.format("VBP2_NOTEOFF_LOAD_RESULT error=%s", tostring(load_err)))
emu.wait(emu.attotime.from_msec(1500))

print(string.format("VBP2_NOTEOFF_DELTAS rhra=%u writes=%u", #rhra_reads - before_rhra, #voice_write_log - before_writes))
for i = before_writes+1, #voice_write_log do
  local e = voice_write_log[i]
  print(string.format("VBP2_POST_NOTEOFF_WRITE t=%.6f voice=%d field=%s value=%08X", e.t, e.voice, e.field, e.value))
end

print(string.format("VBP2_FINAL display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("voice_behavior_probe2", string.format("writes=%u rhra=%u", #voice_write_log, #rhra_reads))
