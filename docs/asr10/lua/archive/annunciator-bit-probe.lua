-- Del 4: capture the 5 annunciator registers ($77-$7b) at several
-- semantically-known checkpoints to find confident bit->function
-- mappings, verified against states where we know what should be lit.

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

local function read_annunciators(label)
  local root = manager.machine.devices[":"]
  local vals = {}
  for i = 0, 4 do
    local o = root:output(string.format("asr10_annreg%u", i))
    vals[i] = o:exists() and o:get() or -1
  end
  print(string.format("ANN[%s] reg77=%02X reg78=%02X reg79=%02X reg7a=%02X reg7b=%02X",
    label, vals[0], vals[1], vals[2], vals[3], vals[4]))
  return vals
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("annunciator_probe", string.format("boot_timeout display=\"%s\"", text))
  return
end
read_annunciators("fresh_file1")

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
read_annunciators("file_loaded")

press_button(0x02, 500)
read_annunciators("instrument1_selected")

press_button(0x02, 500)
read_annunciators("instrument1_pressed_again")

-- RECORD chain: Sample-Source Select -> Level Detect -> lower threshold
-- -> RECORD/start -> WAITING. Manual describes RECORD-related lamps.
press_button(0x20, 400)
press_button(0x02, 800)
read_annunciators("level_detect")
for _ = 1, 24 do press_button(0x0a, 40) end
press_button(0x23, 100)
emu.wait(emu.attotime.from_msec(800))
read_annunciators("recording_waiting")

manager.machine:exit()
