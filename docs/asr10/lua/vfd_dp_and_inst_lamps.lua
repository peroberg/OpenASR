-- VFD Integrated Decimal Point and Instrument Selection Lamps Test
-- Verifies:
-- 1. Instrument Select Lamps (asr10_instlamp0..7):
--    - Direct Level 2 indicator commands ($74, $75, $76) drive selection state
--    - Switching between instruments activates target lamp and clears others
--    - Deselecting turns off all lamps
-- 2. Integrated decimal point decoding in VFD (BAR=001.01):
--    - Digit with DP (ROM $F824BE: 0x21-0x5D) sets DP segment (bit 14 in led14segsc)
--    - Value at column 19 is 0x4300 ('1' + DP), eliminating 'B' corruption (0x03CE)

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local function press(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 300))
end

local function read_instlamps()
  local root = manager.machine.devices[":"]
  local states = {}
  for i = 0, 7 do
    local out = root:output(string.format("asr10_instlamp%u", i))
    table.insert(states, out and out:get() or 0)
  end
  return states
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("vfd_dp_and_inst_lamps", "boot_timeout display=" .. text)
  return
end

-- =========================================================================
-- Part 1: Instrument Select Lamps (asr10_instlamp0..7) Verification
-- =========================================================================
local function load_inst(file_num, slot_code)
  press(0x1a, 300) -- LOAD
  for i = 1, file_num - 1 do
    press(0x0a, 150)
  end
  press(0x23, 300) -- YES
  press(slot_code, 300) -- Slot button
  while true do
    local d = display.read_raw()
    if d:find("L0ADED") then break end
    emu.wait(emu.attotime.from_msec(200))
  end
  emu.wait(emu.attotime.from_msec(300))
end

-- Load File 2 into Slot 1 ($02)
load_inst(2, 0x02)
-- Load File 3 into Slot 2 ($08)
load_inst(3, 0x08)

-- Both loaded, neither selected yet
local lamps_init = read_instlamps()
if lamps_init[1] ~= 0 or lamps_init[2] ~= 0 then
  reg.fail("vfd_dp_and_inst_lamps", string.format("init_lamps_not_zero lamps=[%s]", table.concat(lamps_init, " ")))
  return
end

-- Select Slot 1 (button $02)
press(0x02, 400)
local lamps_slot1 = read_instlamps()
if lamps_slot1[1] ~= 1 or lamps_slot1[2] ~= 0 then
  reg.fail("vfd_dp_and_inst_lamps", string.format("slot1_select_fail lamps=[%s]", table.concat(lamps_slot1, " ")))
  return
end

-- Switch directly to Slot 2 (button $08)
press(0x08, 400)
local lamps_slot2 = read_instlamps()
if lamps_slot2[1] ~= 0 or lamps_slot2[2] ~= 1 then
  reg.fail("vfd_dp_and_inst_lamps", string.format("slot2_select_fail lamps=[%s]", table.concat(lamps_slot2, " ")))
  return
end

-- Deselect Slot 2 (button $08)
press(0x08, 400)
local lamps_desel = read_instlamps()
for i = 1, 8 do
  if lamps_desel[i] ~= 0 then
    reg.fail("vfd_dp_and_inst_lamps", string.format("desel_fail lamp%d=%d", i - 1, lamps_desel[i]))
    return
  end
end

-- =========================================================================
-- Part 2: VFD Integrated Decimal Point Verification
-- =========================================================================
press(0x1a, 400)   -- LOAD mode
press(0x15, 500)   -- Filter to SEQ/SONG (shows FILE 9 TUTORIAL SEQ)
press(0x23, 3500)  -- ENTER / YES to load sequence
press(0x05, 400)   -- Edit control
press(0x15, 400)   -- Seq/Song category
press(0x11, 400)   -- Right
press(0x11, 400)   -- Right
press(0x11, 400)   -- Right (TEMPO page)
press(0x10, 400)   -- Left to BAR page

-- Read raw segment values from VFD
local vals = display.read_values()
local bar1_val = vals[20] -- Column 19 (1-based index 20): bar unit digit with DP

print(string.format("VFD BAR 1 col 19 val: %04X", bar1_val))

-- Assertions for Bar 1:
-- 1. Decimal point segment (bit 14: 0x4000) MUST be lit
if (bar1_val & 0x4000) == 0 then
  reg.fail("vfd_dp_and_inst_lamps", string.format("bar1_missing_dp_bit val=%04X", bar1_val))
  return
end
-- 2. Must NOT be the old corrupt '#' / 'B' glyph (0x03CE)
if bar1_val == 0x03ce then
  reg.fail("vfd_dp_and_inst_lamps", "bar1_corrupt_b_glyph_03CE")
  return
end
-- 3. Must be exact 0x4300 ('1' = 0x0300 | DP = 0x4000)
if bar1_val ~= 0x4300 then
  reg.fail("vfd_dp_and_inst_lamps", string.format("bar1_val_mismatch expected=4300 got=%04X", bar1_val))
  return
end

reg.pass("vfd_dp_and_inst_lamps", string.format(
  "bar1=%04X dp_lit=true b_corrupt=false slot1_lamp=%d slot2_lamp=%d desel_ok=true",
  bar1_val, lamps_slot1[1], lamps_slot2[2]))
manager.machine:exit()
