-- 13th regression test: locks in panel input mechanics that
-- panel-button-and-transport-map.md establishes -- separate from
-- display_protocol.lua, which locks in rendering given known inputs.
--
-- 1. Two buttons held simultaneously generate four distinct wire
--    events (press A, press B while A still held, release A, release
--    B), not a merged/ghosted pair -- the infrastructure "hold Record,
--    press Play" needs, verified here with the two buttons that
--    currently have keyboard bindings (BTN_0A/BTN_0B) since the real
--    Record/Play codes are not identified.
-- 2. BTN_0A (a confirmed-working navigation control) actually changes
--    REC SRC Field 2, i.e. a navigation button really does move/change
--    a field, not just click without effect.
--
-- Without this test, nothing in the suite would notice a regression in
-- MAME's own input-holding behavior for this device, or a change that
-- silently broke BTN_0A/0B's real effect on firmware state.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local stream = {}
taps[#taps + 1] = prog:install_read_tap(0x00fc4800, 0x00fc481f, "duart_r", function(offset, data, mask)
  if (mask & 0x00ff) == 0 then return nil end
  if (offset & 0xf) == 6 then
    stream[#stream + 1] = data & 0xff
  end
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("panel_input", string.format("boot_timeout display=\"%s\"", text))
  return
end

local port = manager.machine.ioport.ports[":panel:buttons_0"]
local field_a = port:field(1 << 0x0a)  -- BTN_0A
local field_b = port:field(1 << 0x0b)  -- BTN_0B

local before = #stream
field_a:set_value(1)
emu.wait(emu.attotime.from_msec(60))
field_b:set_value(1)
emu.wait(emu.attotime.from_msec(60))
field_a:clear_value()
emu.wait(emu.attotime.from_msec(60))
field_b:clear_value()
emu.wait(emu.attotime.from_msec(60))
local after = #stream

local seq = {}
for i = before + 1, after do seq[#seq + 1] = stream[i] end
local hex = {}
for _, b in ipairs(seq) do hex[#hex + 1] = string.format("%02X", b) end
print(string.format("PI_HOLD_SEQ %s", table.concat(hex, " ")))

local saw_press_a, saw_press_b, saw_release_a, saw_release_b = false, false, false, false
for i = 1, #seq - 1 do
  local b0, b1 = seq[i], seq[i + 1]
  if b0 == 0x8a and b1 == 0x00 then saw_press_a = true end
  if b0 == 0x8b and b1 == 0x00 then saw_press_b = true end
  if b0 == 0x0a and b1 == 0x00 then saw_release_a = true end
  if b0 == 0x0b and b1 == 0x00 then saw_release_b = true end
end

if not (saw_press_a and saw_press_b and saw_release_a and saw_release_b) then
  reg.fail("panel_input", string.format(
    "hold_sequence_incomplete press_a=%s press_b=%s release_a=%s release_b=%s",
    tostring(saw_press_a), tostring(saw_press_b), tostring(saw_release_a), tostring(saw_release_b)))
  return
end

-- Navigation check: BTN_0A genuinely changes REC SRC Field 2.
local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local pport = manager.machine.ioport.ports[port_name]
  local pfield = pport:field(1 << (code & 0x1f))
  pfield:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  pfield:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

press_button(0x20, 500)  -- Sample-Source Select, enters REC SRC
local mode_before = prog:read_u8(0x016f)
press_button(0x0a, 250)
local mode_after = prog:read_u8(0x016f)
print(string.format("PI_NAV mode_before=%u mode_after=%u", mode_before, mode_after))

if mode_after == mode_before then
  reg.fail("panel_input", string.format("navigation_no_effect mode=%u", mode_before))
  return
end

reg.pass("panel_input", string.format("hold_ok=true nav_mode=%u->%u", mode_before, mode_after))
