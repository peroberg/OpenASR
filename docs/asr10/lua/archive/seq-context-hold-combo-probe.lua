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
  reg.fail("seq_context", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
press_button(0x02, 500)  -- select instrument 1
print(string.format("SC_SELECT display=\"%s\"", display.read_raw()))

for _, code in ipairs({0x15, 0x09, 0x19, 0x17}) do
  press_button(code, 400)
  print(string.format("SC_TRY code=%02X display=\"%s\"", code, display.read_raw()))
end

-- From whatever screen we landed on, try the same hold-combo candidates.
local candidates = {0x00, 0x01, 0x1d, 0x22, 0x0c, 0x0d, 0x21}
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
      if held_text ~= before_text or held_ann ~= before_ann or after_text ~= before_text then
        print(string.format("SC_COMBO a=%02X b=%02X before=\"%s\" held=\"%s\" after=\"%s\" before_ann=%s held_ann=%s",
          a, b, before_text, held_text, after_text, before_ann, held_ann))
      end
    end
  end
end

print("SC_DONE")
manager.machine:exit()
