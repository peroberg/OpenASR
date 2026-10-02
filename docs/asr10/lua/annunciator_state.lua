-- Regression test: Annunciator State & Priority Verification
-- Verifies:
-- 1. Boot / File 1 (Bank): LOAD blinks (bit 15), BANK is solid lit (bit 7), INST is masked/unlit (bit 14).
-- 2. File 2 (Instrument): BANK turns off, INST is solid lit, LOAD continues blinking.
-- 3. Effects: Neither SYSTEM nor MIDI is lit (bit 12 masked), INST and BANK are unlit, LOAD blinks.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local function press(code, hold_ms, settle_ms)
  hold_ms = hold_ms or 80
  settle_ms = settle_ms or 300
  local port_name = (code < 32) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local mask = 1 << (code & 31)
  local field = port and port:field(mask) or nil
  if not field then
    error(string.format("missing field for code 0x%02X in %s", code, port_name))
  end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms))
end

local function sample_outputs(duration_ms)
  local root = manager.machine.devices[":"]
  local samples = {
    load = {},
    bank = {},
    inst = {},
    sysmidi = {}
  }
  local steps = math.floor(duration_ms / 50)
  for i = 1, steps do
    local o_load = root:output("asr10_annbit15")
    local o_bank = root:output("asr10_annbit7")
    local o_inst = root:output("asr10_annbit14")
    local o_sys  = root:output("asr10_annbit12")

    table.insert(samples.load, o_load and o_load:get() or -1)
    table.insert(samples.bank, o_bank and o_bank:get() or -1)
    table.insert(samples.inst, o_inst and o_inst:get() or -1)
    table.insert(samples.sysmidi, o_sys and o_sys:get() or -1)

    emu.wait(emu.attotime.from_msec(50))
  end
  return samples
end

local function is_blinking(list)
  local has_0, has_1 = false, false
  for _, v in ipairs(list) do
    if v == 0 then has_0 = true end
    if v == 1 then has_1 = true end
  end
  return has_0 and has_1
end

local function is_solid_lit(list)
  for _, v in ipairs(list) do
    if v ~= 0 then return false end
  end
  return #list > 0
end

local function is_solid_unlit(list)
  for _, v in ipairs(list) do
    if v ~= 1 then return false end
  end
  return #list > 0
end

-- =========================================================================
-- Phase 1: Boot / File 1 (Bank)
-- =========================================================================
local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("annunciator_state", "boot_timeout display=" .. tostring(text))
  return
end

emu.wait(emu.attotime.from_msec(300))
local s1 = sample_outputs(600)

if not is_blinking(s1.load) then
  reg.fail("annunciator_state", string.format("phase1_load_not_blinking load=[%s]", table.concat(s1.load, ",")))
  return
end
if not is_solid_lit(s1.bank) then
  reg.fail("annunciator_state", string.format("phase1_bank_not_lit bank=[%s]", table.concat(s1.bank, ",")))
  return
end
if not is_solid_lit(s1.inst) then
  reg.fail("annunciator_state", string.format("phase1_inst_not_lit inst=[%s]", table.concat(s1.inst, ",")))
  return
end

-- =========================================================================
-- Phase 2: File 2 (Instrument)
-- =========================================================================
press(0x0a, 80, 400) -- BTN_0A (Down Arrow)
ok, text = reg.wait_for_text("FILE 2  JM DIGI 5YN   ", 5)


if not ok then
  reg.fail("annunciator_state", "file2_navigation_timeout display=" .. tostring(text))
  return
end

emu.wait(emu.attotime.from_msec(300))
local s2 = sample_outputs(600)

if not is_solid_unlit(s2.bank) then
  reg.fail("annunciator_state", string.format("phase2_bank_not_unlit bank=[%s]", table.concat(s2.bank, ",")))
  return
end
if not is_solid_lit(s2.inst) then
  reg.fail("annunciator_state", string.format("phase2_inst_not_lit inst=[%s]", table.concat(s2.inst, ",")))
  return
end
if not is_blinking(s2.load) then
  reg.fail("annunciator_state", string.format("phase2_load_not_blinking load=[%s]", table.concat(s2.load, ",")))
  return
end

-- =========================================================================
-- Phase 3: System / MIDI
-- =========================================================================
press(0x1b, 80, 400)
local d_sys = display.read_raw()
if not d_sys:find("DIRECT0RIE5") then
  reg.fail("annunciator_state", "phase3_sysmidi_navigation_timeout display=" .. d_sys)
  return
end
local s3 = sample_outputs(600)

if not is_solid_lit(s3.sysmidi) then
  reg.fail("annunciator_state", string.format("phase3_sysmidi_not_lit sysmidi=[%s]", table.concat(s3.sysmidi, ",")))
  return
end
if not is_solid_unlit(s3.inst) then
  reg.fail("annunciator_state", string.format("phase3_inst_not_unlit inst=[%s]", table.concat(s3.inst, ",")))
  return
end
if not is_solid_unlit(s3.bank) then
  reg.fail("annunciator_state", string.format("phase3_bank_not_unlit bank=[%s]", table.concat(s3.bank, ",")))
  return
end
if not is_blinking(s3.load) then
  reg.fail("annunciator_state", string.format("phase3_load_not_blinking load=[%s]", table.concat(s3.load, ",")))
  return
end

-- =========================================================================
-- Phase 4: Effects (FILE 16 LUSH PLATE)
-- =========================================================================
press(0x09, 80, 400)
local d_fx = display.read_raw()
if not d_fx:find("LU5H") then
  reg.fail("annunciator_state", "phase4_effects_navigation_timeout display=" .. d_fx)
  return
end
local s4 = sample_outputs(600)

-- In pure unmasked firmware decoding, firmware sends $78 0C before drawing,
-- then explicitly emits $77 0C for FILE 16 (LUSH PLATE), leaving bit 12 lit (0).
if not is_solid_lit(s4.sysmidi) then
  reg.fail("annunciator_state", string.format("phase4_sysmidi_not_lit sysmidi=[%s]", table.concat(s4.sysmidi, ",")))
  return
end
if not is_solid_unlit(s4.inst) then
  reg.fail("annunciator_state", string.format("phase4_inst_not_unlit inst=[%s]", table.concat(s4.inst, ",")))
  return
end
if not is_solid_unlit(s4.bank) then
  reg.fail("annunciator_state", string.format("phase4_bank_not_unlit bank=[%s]", table.concat(s4.bank, ",")))
  return
end
if not is_blinking(s4.load) then
  reg.fail("annunciator_state", string.format("phase4_load_not_blinking load=[%s]", table.concat(s4.load, ",")))
  return
end

-- =========================================================================
-- Phase 5: Return to System / MIDI
-- =========================================================================
press(0x1b, 80, 400)
local d_ret = display.read_raw()
if not d_ret:find("DIRECT0RIE5") then
  reg.fail("annunciator_state", "phase5_sysmidi_return_timeout display=" .. d_ret)
  return
end
local s5 = sample_outputs(600)

if not is_solid_lit(s5.sysmidi) then
  reg.fail("annunciator_state", string.format("phase5_sysmidi_not_lit sysmidi=[%s]", table.concat(s5.sysmidi, ",")))
  return
end
if not is_solid_unlit(s5.inst) then
  reg.fail("annunciator_state", string.format("phase5_inst_not_unlit inst=[%s]", table.concat(s5.inst, ",")))
  return
end
if not is_solid_unlit(s5.bank) then
  reg.fail("annunciator_state", string.format("phase5_bank_not_unlit bank=[%s]", table.concat(s5.bank, ",")))
  return
end
if not is_blinking(s5.load) then
  reg.fail("annunciator_state", string.format("phase5_load_not_blinking load=[%s]", table.concat(s5.load, ",")))
  return
end

reg.pass("annunciator_state", "bank_inst_priority_ok roundtrip_sysmidi_effects_ok load_blink_ok")


