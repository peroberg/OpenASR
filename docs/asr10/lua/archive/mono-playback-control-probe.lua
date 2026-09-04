-- Del 3 mono control, same technique as stereo-playback-probe.lua: record
-- a known pattern (counter_trigger, the same pattern the existing mono
-- reference test already uses), stop, root-key, and capture playback
-- under -wavwrite -- to compare directly against the stereo case rather
-- than re-trust the old "mono verified" claim, which (Del 1) never
-- checked audible output either, only chain/object correctness.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local rx_state = cpu.state["SCC1RX"]
local taps = {}

local function now() return emu.time() end
local function pattern(offset)
  if offset == 0x40 then return 0x7f end
  if offset == 0x41 then return 0xff end
  return offset & 0xff
end

local iack = {}
local witness_writes = 0
local voice_writes = 0

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("mpc_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local key = string.format("L%d:%02X", level, data & 0xff)
      iack[key] = (iack[key] or 0) + 1
      return nil
    end)
end

taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "mpc_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "mpc_voice_w", function()
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

if not rx_state or not rx_state.writeable then
  reg.fail("mono_playback_control", "missing_writeable_SCC1RX_state")
  return
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("mono_playback_control", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- source_steps=0: mode stays 0 (mono LEFT), matching
-- record-completion-mono-probe.lua's own reference configuration.
press_button(0x20, 500)
if prog:read_u8(0x016f) ~= 0 then
  reg.fail("mono_playback_control", string.format("source_mode=%u expected=0", prog:read_u8(0x016f)))
  return
end
press_button(0x02, 1000)
for _ = 1, 24 do press_button(0x0a, 40) end
press_button(0x23, 100)
emu.wait(emu.attotime.from_seconds(1))
if prog:read_u16(0x0d04) ~= 2 then
  reg.fail("mono_playback_control", string.format("pre_rx_state=%04X display=\"%s\"",
    prog:read_u16(0x0d04), display.read_raw()))
  return
end

local completion_before = iack["L4:4B"] or 0
for offset = 0, 0x031f do
  rx_state.value = pattern(offset)
end
local deadline = now() + 3
while now() < deadline and (iack["L4:4B"] or 0) < completion_before + 1 do
  emu.wait(emu.attotime.from_msec(10))
end
emu.wait(emu.attotime.from_seconds(1))
print(string.format("MPC_RECORDED state=%04X iack4b=%u display=\"%s\"",
  prog:read_u16(0x0d04), iack["L4:4B"] or 0, display.read_raw()))
if (iack["L4:4B"] or 0) < completion_before + 1 then
  reg.fail("mono_playback_control", "recording_transfer_did_not_complete")
  return
end

press_button(0x22, 400)
press_button(0x23, 800)
print(string.format("MPC_STOPPED state=%04X display=\"%s\"", prog:read_u16(0x0d04), display.read_raw()))

local key_port = manager.machine.ioport.ports[":panel:keys_0"]
local key_field = key_port and key_port:field(0x00000001) or nil
if not key_field then
  reg.fail("mono_playback_control", "no_keys_0_port")
  return
end

local voice_before = voice_writes
print(string.format("MPC_ONSET t=%.6f", now()))
key_field:set_value(1)
emu.wait(emu.attotime.from_msec(300))
key_field:clear_value()
emu.wait(emu.attotime.from_seconds(2))

print(string.format("MPC_RESULT display=\"%s\" voice_writes=%u witness=%u",
  display.read_raw(), voice_writes - voice_before, witness_writes))

if witness_writes == 0 then
  reg.fail("mono_playback_control", "no_witness_activity")
  return
end
if voice_writes - voice_before == 0 then
  reg.fail("mono_playback_control", "no_es5506_voice_writes_after_root_key")
  return
end

reg.pass("mono_playback_control", string.format("voice_writes=%u", voice_writes - voice_before))
