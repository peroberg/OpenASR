-- Del 4: run the correlated probe across all 64 codes in three
-- contexts: idle after FILE LOADED, a parameter page (REC SRC), and
-- Load mode (flashing, file browser).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local probe = dofile("docs/asr10/lua/lib/panel_correlated_probe.lua")

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

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("correlated_sweep", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
press_button(0x02, 500)

-- Context 1: idle after FILE LOADED (instrument selected).
for code = 0, 0x25 do
  probe.press_and_correlate(code, "idle")
end

-- Context 2: REC SRC parameter page.
press_button(0x20, 400)
for code = 0, 0x25 do
  press_button(0x20, 300)  -- reset to a clean, known entry each time
  probe.press_and_correlate(code, "recsrc")
end

-- Context 3: Load mode file browser (flashing LOAD).
press_button(0x02, 500)  -- deselect back toward idle/browsing context
for code = 0, 0x25 do
  probe.press_and_correlate(code, "load_mode")
end

print("CORRELATED_SWEEP_DONE")
manager.machine:exit()
