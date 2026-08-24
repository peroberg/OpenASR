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
    bits[#bits+1] = o:exists() and o:get() or -1
  end
  return bits
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("underline_verify", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)
print(string.format("UV_REC_SRC display=\"%s\"", display.read_raw()))
local u1 = read_underline()
print("UV_UNDERLINE_bits: " .. table.concat(u1, ""))

press_button(0x0a, 300)
press_button(0x20, 300)  -- redraw
print(string.format("UV_AFTER_DOWN display=\"%s\"", display.read_raw()))
local u2 = read_underline()
print("UV_UNDERLINE_bits: " .. table.concat(u2, ""))

manager.machine:exit()
