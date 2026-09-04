-- Del 3: complete the stereo recording verified in
-- stereo-interleaved-pattern-probe.lua (round-trip already confirmed
-- byte-exact and channel-distinct) through STOP, root-key entry, and a
-- real note-on, captured under -wavwrite, to answer: is there audible
-- output, does it contain both channels, and do they differ the way the
-- injected LEFT/RIGHT patterns did?

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local rx_state = { [1] = cpu.state["SCC1RX"], [2] = cpu.state["SCC2RX"] }
local taps = {}

local function now() return emu.time() end

local function left_pattern(offset)
  if offset == 0x40 then return 0x7f end
  if offset == 0x41 then return 0xff end
  return offset & 0xff
end
local function right_pattern(offset)
  if offset == 0x40 then return 0x7f end
  if offset == 0x41 then return 0xff end
  return 0xaa
end

local iack = {}
local witness_writes = 0
local voice_writes = 0

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("spp_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local vector = data & 0xff
      iack[string.format("L%d:%02X", level, vector)] = (iack[string.format("L%d:%02X", level, vector)] or 0) + 1
      return nil
    end)
end

taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "spp_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "spp_voice_w", function()
  voice_writes = voice_writes + 1
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

for channel = 1, 2 do
  if not rx_state[channel] or not rx_state[channel].writeable then
    reg.fail("stereo_playback", string.format("missing_writeable_SCC%uRX_state", channel))
    return
  end
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("stereo_playback", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)
press_button(0x0a, 250)
press_button(0x0a, 250)
if prog:read_u8(0x016f) ~= 2 then
  reg.fail("stereo_playback", string.format("source_mode=%u expected=2", prog:read_u8(0x016f)))
  return
end
press_button(0x02, 1000)
for _ = 1, 24 do press_button(0x0a, 40) end
press_button(0x23, 100)
emu.wait(emu.attotime.from_seconds(1))
if prog:read_u16(0x0d04) ~= 2 then
  reg.fail("stereo_playback", string.format("pre_rx_state=%04X display=\"%s\"",
    prog:read_u16(0x0d04), display.read_raw()))
  return
end

local completion_before = iack["L4:4B"] or 0
for offset = 0, 0x031f do
  rx_state[2].value = right_pattern(offset)
  rx_state[1].value = left_pattern(offset)
end
local deadline = now() + 3
while now() < deadline and (iack["L4:4B"] or 0) < completion_before + 2 do
  emu.wait(emu.attotime.from_msec(10))
end
emu.wait(emu.attotime.from_seconds(1))
print(string.format("SPP_RECORDED state=%04X iack4b=%u display=\"%s\"",
  prog:read_u16(0x0d04), iack["L4:4B"] or 0, display.read_raw()))
if (iack["L4:4B"] or 0) < completion_before + 2 then
  reg.fail("stereo_playback", "recording_transfer_did_not_complete")
  return
end

-- STOP: BTN_22 is the established negative control (record-completion-
-- analysis.md: no state/display/metadata change), BTN_23 is what
-- actually stops recording and prompts for root key.
press_button(0x22, 400)
press_button(0x23, 800)
print(string.format("SPP_STOPPED state=%04X display=\"%s\"", prog:read_u16(0x0d04), display.read_raw()))

-- Root-key entry via the playable keyboard (same mechanism
-- record-completion-analysis.md used) -- this is also the same event
-- that made ES5506 fetch the recorded PCM in the mono reference case.
local key_port = manager.machine.ioport.ports[":panel:keys_0"]
local key_field = key_port and key_port:field(0x00000001) or nil
if not key_field then
  reg.fail("stereo_playback", "no_keys_0_port")
  return
end

local voice_before = voice_writes
print(string.format("SPP_ONSET t=%.6f", now()))
key_field:set_value(1)
emu.wait(emu.attotime.from_msec(300))
key_field:clear_value()
emu.wait(emu.attotime.from_seconds(2))

print(string.format("SPP_RESULT display=\"%s\" voice_writes=%u witness=%u",
  display.read_raw(), voice_writes - voice_before, witness_writes))

if witness_writes == 0 then
  reg.fail("stereo_playback", "no_witness_activity")
  return
end
if voice_writes - voice_before == 0 then
  reg.fail("stereo_playback", "no_es5506_voice_writes_after_root_key")
  return
end

reg.pass("stereo_playback", string.format("voice_writes=%u", voice_writes - voice_before))
