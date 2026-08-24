-- Del 5: test Record/Play as a state machine, not single clicks. Try
-- hold-combinations among the codes that show NO visible single-press
-- effect from idle (candidates from button-routine-sweep-v350.csv,
-- excluding already-identified Instrument/Enter-Yes codes): $00, $01,
-- $1D, $22. For each pair, hold A, hold B while A still held, release
-- B, release A -- and watch display text AND all 5 annunciator
-- registers for a combined effect neither single press alone produces.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

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

local function get_field(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  return port:field(1 << (code & 0x1f))
end

local function read_ann()
  local root = manager.machine.devices[":"]
  local v = {}
  for i = 0, 4 do
    local o = root:output(string.format("asr10_annreg%u", i))
    v[i] = o:exists() and o:get() or -1
  end
  return string.format("%02X,%02X,%02X,%02X,%02X", v[0], v[1], v[2], v[3], v[4])
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("transport_hold_combo", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
press_button(0x02, 500)  -- select instrument 1

local candidates = {0x00, 0x01, 0x1d, 0x22}
for _, a in ipairs(candidates) do
  for _, b in ipairs(candidates) do
    if a ~= b then
      local before_text = display.read_raw()
      local before_ann = read_ann()
      local fa, fb = get_field(a), get_field(b)
      fa:set_value(1)
      emu.wait(emu.attotime.from_msec(100))
      fb:set_value(1)
      emu.wait(emu.attotime.from_msec(150))
      local held_text = display.read_raw()
      local held_ann = read_ann()
      fb:clear_value()
      emu.wait(emu.attotime.from_msec(100))
      fa:clear_value()
      emu.wait(emu.attotime.from_msec(250))
      local after_text = display.read_raw()
      local after_ann = read_ann()
      if held_text ~= before_text or held_ann ~= before_ann or after_text ~= before_text then
        print(string.format("THC a=%02X b=%02X before_text=\"%s\" held_text=\"%s\" after_text=\"%s\" before_ann=%s held_ann=%s after_ann=%s",
          a, b, before_text, held_text, after_text, before_ann, held_ann, after_ann))
      end
    end
  end
end

print("THC_DONE")
manager.machine:exit()
