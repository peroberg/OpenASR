-- Del 2 follow-up: use the now-working underline output as ground
-- truth to find which raw code, if any, moves the cursor within a
-- genuine EDIT-mode parameter screen (not REC SRC). Enter "EDIT PITCH
-- TABLE" (BTN_18 from idle) and sweep all 64 codes, watching for any
-- underline-position change or text change, forcing a redraw after
-- each candidate exactly like the REC SRC methodology required.

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
  emu.wait(emu.attotime.from_msec(settle_ms or 200))
  return true
end

local function read_underline()
  local root = manager.machine.devices[":"]
  local bits = {}
  for i = 22, 43 do
    local o = root:output(string.format("vfd%u", i))
    bits[#bits + 1] = (o:exists() and o:get() or 0) ~= 0 and "1" or "0"
  end
  return table.concat(bits)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("edit_cursor_sweep", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
press_button(0x02, 500)  -- select instrument 1

press_button(0x18, 500)  -- EDIT PITCH TABLE
local base_text = display.read_raw()
local base_underline = read_underline()
print(string.format("ECS_BASE display=\"%s\" underline=%s", base_text, base_underline))

for code = 0, 0x3f do
  press_button(0x18, 300)  -- reset to a clean, known entry each time
  local before_text = display.read_raw()
  local before_underline = read_underline()
  local sent = press_button(code, 200)
  press_button(0x18, 300)  -- force redraw, same trick as REC SRC
  local after_text = display.read_raw()
  local after_underline = read_underline()
  if sent and (after_text ~= before_text or after_underline ~= before_underline) then
    print(string.format("ECS code=%02X before_text=\"%s\" after_text=\"%s\" before_ul=%s after_ul=%s",
      code, before_text, after_text, before_underline, after_underline))
  end
end

manager.machine:exit()
