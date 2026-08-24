local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local stream = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc4800, 0x00fc481f, "duart_w", function(offset, data, mask)
  if (mask & 0x00ff) == 0 then return nil end
  if (offset & 0xf) == 6 then stream[#stream + 1] = data & 0xff end
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

local function dump_since(label, before)
  local hex = {}
  for i = before + 1, #stream do hex[#hex + 1] = string.format("%02X", stream[i]) end
  print(string.format("VA_%s n=%u [%s]", label, #hex, table.concat(hex, " ")))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("volume_append", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
press_button(0x02, 500)  -- select instrument -> VOLUME=99
print(string.format("VA_SELECT display=\"%s\"", display.read_raw()))

for i, code in ipairs({0x0a, 0x0a, 0x0b}) do
  local before = #stream
  press_button(code, 400)
  print(string.format("VA_STEP[%u] code=%02X display=\"%s\"", i, code, display.read_raw()))
  dump_since(string.format("STEP%u", i), before)
end

manager.machine:exit()
