-- Runtime acceptance for the verified ASR-10 $62/$63 selected-field state.
-- Uses the same V3.50 firmware path as the protocol capture; no raw display
-- bytes or firmware state are injected.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local function press(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 500))
end

local function underline()
  local root, bits = manager.machine.devices[":"], {}
  for output = 22, 43 do
    local value = root:output(string.format("vfd%u", output))
    bits[#bits + 1] = (value:exists() and value:get() or 0) ~= 0 and "1" or "0"
  end
  return table.concat(bits)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("display_field_rewrite", string.format("boot_timeout display=\"%s\"", text))
  return
end

press(0x15, 500)   -- FILE 9 TUTORIAL SEQ
press(0x23, 4000)  -- load sequence
press(0x05, 500)   -- existing Edit control
press(0x15, 500)   -- Seq/Song category
press(0x11, 500)
press(0x11, 500)
press(0x11, 500)   -- TEMPO page

local initial, initial_ul = display.read_raw(), underline()
press(0x10, 500)
local bar_page, bar_ul = display.read_raw(), underline()
press(0x11, 500)
local after_return, return_ul = display.read_raw(), underline()
press(0x0a, 500)
local after_up_91, up_91_ul = display.read_raw(), underline()
press(0x0a, 500)
local after_up_92, up_92_ul = display.read_raw(), underline()
press(0x0b, 500)
local after_down_91, down_91_ul = display.read_raw(), underline()
press(0x0b, 500)
local after_down_90, down_90_ul = display.read_raw(), underline()

local expected_initial = "TEMP0?9?   L00P?0N    "
local expected_91 = "TEMP0?91   L00P?0N    "
local expected_92 = "TEMP0?92   L00P?0N    "
local expected_tempo_ul = "0000001110000000000000"
local expected_bar = "TUT0RIAL 5EQ BAR?????1"
local expected_bar_ul = "0000000000000000000011"
if initial ~= expected_initial or bar_page ~= expected_bar or
    after_return ~= expected_initial or after_up_91 ~= expected_91 or
    after_up_92 ~= expected_92 or after_down_91 ~= expected_91 or
    after_down_90 ~= expected_initial then
  reg.fail("display_field_rewrite", string.format(
    "transition initial=\"%s\" bar=\"%s\" return=\"%s\" up91=\"%s\" up92=\"%s\" down91=\"%s\" down90=\"%s\"",
    initial, bar_page, after_return, after_up_91, after_up_92, after_down_91, after_down_90))
  return
end
if initial_ul ~= expected_tempo_ul or bar_ul ~= expected_bar_ul or
    return_ul ~= expected_tempo_ul or up_91_ul ~= expected_tempo_ul or
    up_92_ul ~= expected_tempo_ul or down_91_ul ~= expected_tempo_ul or
    down_90_ul ~= expected_tempo_ul then
  reg.fail("display_field_rewrite", string.format(
    "underline initial=%s bar=%s return=%s up91=%s up92=%s down91=%s down90=%s",
    initial_ul, bar_ul, return_ul, up_91_ul, up_92_ul, down_91_ul, down_90_ul))
  return
end

reg.pass("display_field_rewrite",
  "bar_roundtrip=true tempo=90->91->92->91->90 anchor=6 underline=6-8 trailing=false")
