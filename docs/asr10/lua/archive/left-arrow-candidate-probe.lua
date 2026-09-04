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
  reg.fail("left_reconcile", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)
print(string.format("LC_ENTER display=\"%s\" ul=%s stream_len=%u", display.read_raw(), read_underline(), #stream))

local before = #stream
press_button(0x10, 800)  -- generous settle
local after = #stream
print(string.format("LC_AFTER_10 display=\"%s\" ul=%s stream_len=%u delta=%u",
  display.read_raw(), read_underline(), #stream, after - before))

local hex = {}
for i = before + 1, after do hex[#hex + 1] = string.format("%02X", stream[i]) end
print(string.format("LC_BYTES [%s]", table.concat(hex, " ")))

manager.machine:exit()
