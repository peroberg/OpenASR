-- Del 4 of the sound-path-topology task: boot, load JM DIGI SYN, then let
-- the machine idle (no key press -- asr10panel_device has no
-- piano-keyboard ioport, see sample-ram-and-voice-registers.md's Del 4
-- sketch) so -wavwrite can capture whatever the now-connected ES5506
-- output produces on its own, if anything.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  print(string.format("WAVCAP boot_timeout final_display=\"%s\"", text))
  manager.machine:exit()
  return
end

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
print(string.format("WAVCAP loaded=%s display=\"%s\" t=%.6f", tostring(ok), text, emu.time()))

emu.wait(emu.attotime.from_seconds(8))
print(string.format("WAVCAP done t=%.6f", emu.time()))
manager.machine:exit()
