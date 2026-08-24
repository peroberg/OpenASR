-- Del 1: capture the display byte stream in three cases and diff them.
-- (a) full-screen redraw of a parameter page
-- (b) a single parameter value change on the SAME page (Volume slider,
--     since "VOLUME=99" is the measured append-bug screen)
-- (c) an attempted field switch using the static raw-mapped-table
--     candidates for Left/Right (raw $10/$21 -- candidates only, not
--     yet named; see panel-raw-map.csv, mapped $22/$23 in the Data
--     Entry Controls block $20-$25)

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local stream = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc4800, 0x00fc481f, "duart_w", function(offset, data, mask)
  if (mask & 0x00ff) == 0 then return nil end
  if (offset & 0xf) == 6 then
    stream[#stream + 1] = data & 0xff
  end
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return false end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
  return true
end

local function dump(label, lo, hi)
  local bytes = {}
  for i = lo + 1, hi do bytes[#bytes + 1] = stream[i] end
  local hex = {}
  for _, b in ipairs(bytes) do hex[#hex + 1] = string.format("%02X", b) end
  print(string.format("D1_%s n=%u [%s]", label, #bytes, table.concat(hex, " ")))
  return bytes
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("del1_diff", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
press_button(0x02, 500)  -- select instrument -> shows "JM DIGI SYN  VOLUME=99"
print(string.format("D1_SELECT display=\"%s\"", display.read_raw()))

-- (a) full-screen redraw: leave to a different screen, then come back
-- (BTN_20 = Sample-Source Select, a clean, already-proven full redraw).
local before_a = #stream
press_button(0x20, 500)
local mid_a = #stream
print(string.format("D1_A display=\"%s\"", display.read_raw()))
dump("A_ENTER_RECSRC", before_a, mid_a)

-- Return to the instrument/volume screen the same way (select again).
press_button(0x02, 500)
local before_full = #stream
press_button(0x02, 500)  -- deselect
press_button(0x02, 500)  -- reselect: forces a genuine full redraw of the VOLUME screen
local after_full = #stream
print(string.format("D1_A2 display=\"%s\"", display.read_raw()))
dump("A_FULL_REDRAW_VOLUME", before_full, after_full)

-- (b) single parameter value change on the SAME page: move the Volume
-- slider while the VOLUME screen is showing, watch the raw bytes.
local vol_port = manager.machine.ioport.ports[":panel:analog_volume"]
if not vol_port then
  print("D1_NO_VOLUME_PORT")
else
  local vol_field = vol_port:field(0xffffffff)
  for _, v in ipairs({0x100, 0x200, 0x300}) do
    local before_b = #stream
    vol_field:set_value(v)
    emu.wait(emu.attotime.from_msec(300))
    local after_b = #stream
    print(string.format("D1_B display=\"%s\" slider=%03X", display.read_raw(), v))
    dump(string.format("B_VOLUME_CHANGE_%03X", v), before_b, after_b)
  end
end

-- (c) attempted field switch using static-table candidates for Left/
-- Right (raw $10, $21) -- on the REC SRC screen, where we already know
-- the field/attribute structure from prior verification.
press_button(0x20, 500)
print(string.format("D1_C_ENTER display=\"%s\"", display.read_raw()))
for _, code in ipairs({0x10, 0x21}) do
  local before_c = #stream
  local sent = press_button(code, 300)
  local after_c = #stream
  print(string.format("D1_C code=%02X sent=%s display=\"%s\"", code, tostring(sent), display.read_raw()))
  dump(string.format("C_CANDIDATE_%02X", code), before_c, after_c)
end

manager.machine:exit()
