-- Del 1 follow-up: the manual describes Sequencer Status (Stop/Play/
-- Record) as an indicator-light area, separate from the 22-char text
-- line. Sweep all 64 codes from idle-after-FILE-LOADED, watching the 5
-- annunciator registers (not just display text) for ANY change --
-- transport buttons may only show up here.

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

local function read_ann()
  local root = manager.machine.devices[":"]
  local v = {}
  for i = 0, 4 do
    local o = root:output(string.format("asr10_annreg%u", i))
    v[i] = o:exists() and o:get() or -1
  end
  return v
end

local function ann_key(v)
  return string.format("%02X,%02X,%02X,%02X,%02X", v[0], v[1], v[2], v[3], v[4])
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("transport_ann_sweep", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))

-- Select instrument 1 so a track/instrument actually exists for
-- transport buttons to act on.
press_button(0x02, 500)
print(string.format("TAS_AFTER_SELECT display=\"%s\" ann=%s", display.read_raw(), ann_key(read_ann())))

for code = 0, 0x3f do
  local before_text = display.read_raw()
  local before_ann = read_ann()
  local sent = press_button(code, 200)
  local after_text = display.read_raw()
  local after_ann = read_ann()
  local ann_changed = ann_key(before_ann) ~= ann_key(after_ann)
  local text_changed = before_text ~= after_text
  if sent and (ann_changed or text_changed) then
    print(string.format("TAS code=%02X text_changed=%s ann_changed=%s before_ann=%s after_ann=%s after_text=\"%s\"",
      code, tostring(text_changed), tostring(ann_changed), ann_key(before_ann), ann_key(after_ann), after_text))
  end
  -- Release-equivalent: press again to toggle back if it looked like a
  -- toggle (keeps subsequent codes' baseline closer to a known state).
end

manager.machine:exit()
