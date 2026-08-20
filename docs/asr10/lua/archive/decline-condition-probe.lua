-- Del 2/3 (keyboard-and-sample-bridge-4.md): live-instruments $FFB43E's
-- own decline branch (`rts` at $FFB486, reached only when
-- lowmem[$171]==1 AND ($FFB6C4's D3 result & lowmem[$CDE])==0) and
-- $FFB6C4's key-range check (compares the note number in D2 against two
-- bytes at (lowmem[$330])+0x3C/+0x3E when lowmem[$31C]!=0). Correlates
-- with a real MIDI note-on injected via the "mdin" MIDI-in port image
-- device (docs/asr10/lua/archive/midi-note-on-gate.lua's technique).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local d2_state = cpu.state["D2"]
local d3_state = cpu.state["D3"]
local function now() return emu.time() end
local function d2() return d2_state.value & 0xff end
local function d3() return d3_state.value & 0xffffffff end

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
  reg.fail("decline_condition_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("decline_condition_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

local events = {}
local taps = {}
local function tap(addr, name, extra)
  taps[#taps + 1] = prog:install_read_tap(addr, addr + 1, name, function(offset, data, mask)
    events[#events + 1] = { t = now(), what = name, d2 = extra and d2() or nil, d3 = extra and d3() or nil }
    return nil
  end)
end
tap(0x00ffb6c4, "B6C4_ENTRY", true)
tap(0x00ffb6d0, "RANGE_PATH_TAKEN", true)
tap(0x00ffb6e6, "31C_ZERO_PATH_TAKEN", false)
tap(0x00ffb736, "B6C4_RETURN", true)
tap(0x00ffb472, "171_CHECK", true)
tap(0x00ffb486, "DECLINE_RTS", false)
tap(0x00ffb56e, "PROCEED_B56E", false)

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then
  reg.fail("decline_condition_probe", "no_mdin_image_device_found")
  return
end

print(string.format("DCP_LOWMEM_BEFORE 31C=%04X 330=%04X CDE=%02X 171=%02X",
  prog:read_u16(0x31c) & 0xffff, prog:read_u16(0x330) & 0xffff,
  prog:read_u8(0xcde) & 0xff, prog:read_u8(0x171) & 0xff))

local ptr330 = prog:read_u16(0x330) & 0xffff
if ptr330 ~= 0 then
  print(string.format("DCP_KEYRANGE ptr330=%04X low=%02X high=%02X",
    ptr330, prog:read_u8(ptr330 + 0x3c) & 0xff, prog:read_u8(ptr330 + 0x3e) & 0xff))
end

local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
emu.wait(emu.attotime.from_msec(1000))

print(string.format("DCP_LOAD_RESULT error=%s", tostring(load_err)))
print(string.format("DCP_EVENTS count=%u", #events))
for _, e in ipairs(events) do
  print(string.format("DCP_EVENT t=%.6f what=%s d2=%s d3=%s", e.t, e.what, tostring(e.d2), tostring(e.d3)))
end

print(string.format("DCP_LOWMEM_AFTER 31C=%04X 330=%04X CDE=%02X 171=%02X",
  prog:read_u16(0x31c) & 0xffff, prog:read_u16(0x330) & 0xffff,
  prog:read_u8(0xcde) & 0xff, prog:read_u8(0x171) & 0xff))

reg.pass("decline_condition_probe", string.format("events=%u", #events))
