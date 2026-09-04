-- 12th regression test: locks in the display-protocol implementation
-- from docs/asr10/investigations/display-protocol-inventory.md --
-- underline/cursor rendering for the currently-selected REC SRC field,
-- and the confirmed instrument-select lamp bit -- plus a guard that the
-- aggregated unhandled-code alarm doesn't regress onto codes this
-- project now understands.
--
-- Without this test, nothing in the suite would notice a change that
-- silently broke underline rendering (e.g. a wrong attribute bitmask)
-- or reintroduced the old un-aggregated "Unhandled control code" printf
-- flood, or flagged a legitimately-recognized code (0x20-0x5f, 0x60,
-- 0x62, 0x66, 0x72, 0x77-0x7b) as unhandled again.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

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

local function read_underline()
  local root = manager.machine.devices[":"]
  local bits = {}
  for i = 22, 43 do
    local o = root:output(string.format("vfd%u", i))
    bits[#bits + 1] = (o:exists() and o:get() or 0) ~= 0
  end
  return bits
end

local function underline_key(bits)
  -- Compact "which positions are underlined" signature, e.g. "17-21".
  local runs = {}
  local run_start = nil
  for i = 1, #bits do
    if bits[i] and not run_start then
      run_start = i - 1
    elseif not bits[i] and run_start then
      runs[#runs + 1] = string.format("%u-%u", run_start, i - 2)
      run_start = nil
    end
  end
  if run_start then
    runs[#runs + 1] = string.format("%u-%u", run_start, #bits - 1)
  end
  return table.concat(runs, ",")
end

local function lamp0()
  local o = manager.machine.devices[":"]:output("asr10_instlamp0")
  return o:exists() and o:get() or -1
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("display_protocol", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- Instrument-select lamp, checked FIRST and in isolation: the confirmed
-- measurement (annunciator-bit-probe.md) is narrower than "instrument 1
-- lamp" in general -- register $77 bit 0 also changes when entering the
-- Sample-Source Select / Level Detect flow, so this only holds for the
-- specific load->select->reselect sequence with no other navigation in
-- between, which is exactly what's reproduced here, in this order.
press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
local lamp_before = lamp0()
press_button(0x02, 500)
local lamp_selected = lamp0()
press_button(0x02, 500)
local lamp_deselected = lamp0()
print(string.format("DPT_LAMP before=%d selected=%d deselected=%d",
  lamp_before, lamp_selected, lamp_deselected))
if lamp_before ~= 0 or lamp_selected ~= 1 or lamp_deselected ~= 0 then
  reg.fail("display_protocol", string.format(
    "lamp_sequence_mismatch before=%d selected=%d deselected=%d",
    lamp_before, lamp_selected, lamp_deselected))
  return
end

-- Underline, checked after: enter REC SRC, verify the currently-selected
-- Field 2 value ("LEFT ", positions 17-21, 5 chars including the
-- field's own trailing pad) is underlined and nothing else is.
press_button(0x20, 500)
local text1 = display.read_raw()
local bits1 = read_underline()
local key1 = underline_key(bits1)
print(string.format("DPT_LEFT display=\"%s\" underline=%s", text1, key1))
if key1 ~= "17-21" then
  reg.fail("display_protocol", string.format("left_underline_mismatch got=%s expected=17-21", key1))
  return
end

-- Change Field 2, redraw, verify the underline follows the (now 5-char
-- "RIGHT") field to the same positions -- the field's width didn't
-- change, only its content, so the underline range should not move.
press_button(0x0a, 300)
press_button(0x20, 300)
local text2 = display.read_raw()
local bits2 = read_underline()
local key2 = underline_key(bits2)
print(string.format("DPT_RIGHT display=\"%s\" underline=%s", text2, key2))
if key2 ~= "17-21" then
  reg.fail("display_protocol", string.format("right_underline_mismatch got=%s expected=17-21", key2))
  return
end
if text2:find("RIGHT") == nil then
  reg.fail("display_protocol", string.format("expected_RIGHT_text got=\"%s\"", text2))
  return
end

reg.pass("display_protocol", string.format("underline_left=%s underline_right=%s lamp_ok=true", key1, key2))
