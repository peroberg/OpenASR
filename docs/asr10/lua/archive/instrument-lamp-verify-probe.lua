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

local function lamp0()
  local o = manager.machine.devices[":"]:output("asr10_instlamp0")
  return o:exists() and o:get() or -1
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("lamp_verify", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
print(string.format("LV_before_select lamp0=%d", lamp0()))

press_button(0x02, 500)
print(string.format("LV_after_select lamp0=%d", lamp0()))

press_button(0x02, 500)
print(string.format("LV_after_reselect lamp0=%d", lamp0()))

manager.machine:exit()
