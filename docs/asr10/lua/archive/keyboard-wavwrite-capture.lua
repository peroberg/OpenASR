-- Del 3 of the keyboard task: -wavwrite capture across a real key press,
-- for objective measurement (peak/RMS via Python, same technique as
-- sample-topology-closure.md's Del 4).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local function press(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  print(string.format("WAVCAP boot_timeout final_display=\"%s\"", text))
  manager.machine:exit()
  return
end

press(":panel:buttons_0", 1 << 0x0a)
press(":panel:buttons_32", 1 << 0x03)
press(":panel:buttons_0", 1 << 0x02)

ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
print(string.format("WAVCAP loaded=%s display=\"%s\" t=%.6f", tostring(ok), text, emu.time()))

emu.wait(emu.attotime.from_seconds(2))
print(string.format("WAVCAP key_press t=%.6f", emu.time()))
press(":panel:keys_0", 0x00000001, 500) -- KEY_C, held 500ms
emu.wait(emu.attotime.from_seconds(3))
print(string.format("WAVCAP done t=%.6f", emu.time()))
manager.machine:exit()
