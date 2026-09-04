-- Checks the actual outcome of the instrument-load sequence with the
-- minimal IDMA implementation now wired. Observation only.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

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

print("IDMARESULT start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("idma_result_check", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("IDMARESULT_FILE1 t=%.6f", emu.time()))

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local last = nil
local deadline = emu.time() + 40
while emu.time() < deadline do
  local cur = display.read_raw()
  if cur ~= last then
    print(string.format("IDMARESULT_DISPLAY t=%.6f text=\"%s\"", emu.time(), cur))
    last = cur
  end
  emu.wait(emu.attotime.from_msec(100))
end

print(string.format("IDMARESULT_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), emu.time()))
reg.pass("idma_result_check", string.format("display=\"%s\"", display.read_raw()))
