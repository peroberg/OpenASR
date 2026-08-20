-- Del 2/3 (keyboard-and-sample-bridge-4.md): traces $FFB56E's own branch
-- structure (reached from $FFB43E's "proceed" path via
-- `bra.w $ffb56e`). Confirms which of $FFB56E's several exits a real
-- MIDI note-on takes: the early bcs-exit after trap#2, the bit15/$171/
-- $3BD-gated trap#4-only path, or the $B55A-record-write + trap#d
-- "post" path. Correlates with a real MIDI note-on injected via the
-- "mdin" MIDI-in port image device
-- (docs/asr10/lua/archive/midi-note-on-gate.lua's technique).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local d1_state = cpu.state["D1"]
local function now() return emu.time() end
local function d1() return d1_state.value & 0xffffffff end

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
  reg.fail("b56e_branch_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("b56e_branch_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

local events = {}
local taps = {}
local function tap(addr, name)
  taps[#taps + 1] = prog:install_read_tap(addr, addr + 1, name, function(offset, data, mask)
    events[#events + 1] = { t = now(), what = name, d1 = d1() }
    return nil
  end)
end
tap(0x00ffb56e, "B56E_ENTRY")            -- trap #2
tap(0x00ffb594, "B56E_EXIT_RTS")         -- bcs-taken early exit, or fallthrough exit
tap(0x00ffb572, "B56E_BCLR_BIT15")       -- reached only if trap#2 did NOT set carry
tap(0x00ffb576, "B56E_BIT15_WAS_CLEAR")  -- beq target check point (post-bclr)
tap(0x00ffb578, "B56E_171_TEST")
tap(0x00ffb584, "B56E_BSR_596")
tap(0x00ffb588, "B56E_TRAP4")
tap(0x00ffb58c, "B56E_BSR_55A_POST")     -- the "post a record" path
tap(0x00ffb592, "B56E_TRAP_D")

print(string.format("DB56E_LOWMEM_BEFORE 171=%02X 3BD=%02X 163=%02X",
  prog:read_u8(0x171) & 0xff, prog:read_u8(0x3bd) & 0xff, prog:read_u8(0x163) & 0xff))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then
  reg.fail("b56e_branch_probe", "no_mdin_image_device_found")
  return
end

local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
emu.wait(emu.attotime.from_msec(1000))

print(string.format("DB56E_LOAD_RESULT error=%s", tostring(load_err)))
print(string.format("DB56E_EVENTS count=%u", #events))
for _, e in ipairs(events) do
  print(string.format("DB56E_EVENT t=%.6f what=%s d1=%08X", e.t, e.what, e.d1))
end
reg.pass("b56e_branch_probe", string.format("events=%u", #events))
