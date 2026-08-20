-- Discriminates "INTRQ never asserted" from "INTRQ asserted but not
-- delivered" during the 4.994s silence after READ DATA
-- (docs/asr10/investigations/tc-reentrancy-probe.md). Observation only.
--
-- Three independent signals, all passive:
--  1. Passively poll $FC4001 (MSR) -- upd765_family_device::msr_r() has no
--     side effects (no fifo_pop, no state mutation), confirmed by reading
--     its source. PHASE_RESULT reads back $D0 (RQM|DIO|CB) and can only
--     be reached via COMMAND_DONE -> command_end(), which unconditionally
--     sets irq=true and calls check_irq() in the same function
--     (upd765.cpp:1668-1678). If MSR ever reads $D0-shaped during the
--     silence, INTRQ WAS asserted -- command_end() ran -- and the failure
--     is in interrupt delivery, not the FDC state machine. If MSR stays
--     EXEC-shaped ($10, or $30 with EXM) for the whole window,
--     command_end() never ran -- the state machine itself never
--     completed.
--  2. SR interrupt-mask level, sampled continuously -- if it reaches 0
--     multiple times during the silence with no IACK following, that is
--     itself an opportunity INTRQ would have been taken had it been
--     pending.
--  3. Level-1 IACK tap (same calibrated cpu_space offset used throughout
--     this investigation) -- already known to be silent in this window;
--     included for completeness/cross-reference.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local sr_state = cpu.state["SR"]

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function sr() return sr_state and (sr_state.value & 0xffff) or 0xffff end
local function mask_level() return (sr() >> 8) & 0x7 end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local taps = {}
local watching = false
local last_msr = nil
local mask_zero_count = 0
local last_mask = nil

taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "intrq_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    print(string.format("INTRQP_FDC_W t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)

local irq1_tap = nil
if cpu_space then
  irq1_tap = cpu_space:install_read_tap(0x000000, 0xffffff, "intrq_iack_r", function(offset, data, mask)
    if offset ~= 0x00fffff2 then
      return nil
    end
    print(string.format("INTRQP_IACK1 t=%.6f vector=%02X pc=%06X", now(), data & 0xff, pc()))
    return nil
  end)
end

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

print("INTRQP start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("intrq_assertion_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("INTRQP_FILE1 t=%.6f", now()))

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

-- Poll MSR and SR mask together, tightly, from just after READ DATA is
-- issued through well past the known ~5s silence and recovery.
local deadline = now() + 12
while now() < deadline do
  local msr = prog:read_u8(0x00fc4001) & 0xff
  if msr ~= last_msr then
    print(string.format("INTRQP_MSR t=%.6f pc=%06X msr=%02X", now(), pc(), msr))
    last_msr = msr
  end
  local ml = mask_level()
  if ml ~= last_mask then
    print(string.format("INTRQP_MASK t=%.6f pc=%06X mask=%u", now(), pc(), ml))
    last_mask = ml
  end
  if ml == 0 then
    mask_zero_count = mask_zero_count + 1
  end
  emu.wait(emu.attotime.from_msec(2))
end

print(string.format("INTRQP_SUMMARY final_display=\"%s\" mask_zero_samples=%u t=%.6f",
  display.read_raw(), mask_zero_count, now()))
reg.pass("intrq_assertion_probe", string.format("mask_zero_samples=%u", mask_zero_count))
