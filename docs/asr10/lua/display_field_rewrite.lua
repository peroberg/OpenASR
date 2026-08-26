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
press(0x0a, 500)
local after_up, up_ul = display.read_raw(), underline()
press(0x0b, 500)
local after_down, down_ul = display.read_raw(), underline()

local expected_initial = "TEMP0?9?   L00P?0N    "
local expected_up = "TEMP0?91   L00P?0N    "
local expected_ul = "0000001110000000000000"
if initial ~= expected_initial or after_up ~= expected_up or after_down ~= expected_initial then
  reg.fail("display_field_rewrite", string.format(
    "transition initial=\"%s\" up=\"%s\" down=\"%s\"", initial, after_up, after_down))
  return
end
if initial_ul ~= expected_ul or up_ul ~= expected_ul or down_ul ~= expected_ul then
  reg.fail("display_field_rewrite", string.format(
    "underline initial=%s up=%s down=%s", initial_ul, up_ul, down_ul))
  return
end

reg.pass("display_field_rewrite", "tempo=90->91->90 anchor=6 underline=6-8 trailing=false")
