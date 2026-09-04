-- IRQ1 vector/SR probe. Observation only, run against a build with the
-- naive FDC INTRQ -> MC68302 IRQ1 wiring TEMPORARILY re-enabled
-- (docs/asr10/investigations/irq1-vector-and-sr-probe.md).
--
-- Question: when the naive wiring drives boot into "ERROR 129 - REBOOT",
-- what vector does the level-1 IACK actually acknowledge? If it isn't $51,
-- the ERROR 129 failure in irq1-storage-completion-probe.md said nothing
-- about timing -- it was a different failure entirely.
--
-- For every level-1 IACK (m68k cpu_space address 0xfffff3), logs: the
-- vector byte actually returned, the vector-table target address and its
-- content, PC "before" (where the tap fires -- per 68k semantics this IS
-- the point execution would have continued had the interrupt not occurred)
-- and PC "after" (the vector-table target, since that's where the CPU goes
-- next), and SR at the IACK moment including the interrupt mask (bits
-- 10-8). This is the SR *observed at IACK*, not necessarily at whatever
-- earlier instant INTRQ was first asserted -- noted as a caveat in the
-- investigation doc, not something this script can resolve.

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

local events = {}

if not cpu_space then
  reg.fail("irq1_vector_probe", "no_cpu_space blocked: cpu.spaces[\"cpu_space\"] missing")
  return
end

-- Calibrated offset, not the driver's own byte address 0xfffff3:
-- cpu_space is a 16-bit-wide space (m_cpu_space_config, m68000.cpp), so the
-- Lua tap reports the word-aligned base 0xfffff2 with the vector byte in
-- data's low 8 bits (big-endian, odd byte address 0xfffff3 is the low byte
-- of word 0xfffff2). Confirmed by irq1_vector_probe_calibration.lua: level
-- 6 IACK reported as offset=FFFFFC data=56 (known-correct $56 for the
-- DUART), level 1 as offset=FFFFF2 data=51.
local tap = cpu_space:install_read_tap(0x000000, 0xffffff, "irq1_vector_probe_r", function(offset, data, mask)
  if offset ~= 0x00fffff2 then
    return nil
  end
  local vector = data & 0xff
  local target_addr = vector * 4
  local target = prog:read_u32(target_addr) & 0xffffffff
  local status = sr()
  local mask_level = (status >> 8) & 0x7
  local ev = {
    time = now(), vector = vector, target_addr = target_addr, target = target,
    pc_before = pc(), sr = status, mask_level = mask_level,
  }
  events[#events + 1] = ev
  print(string.format(
    "IRQ1VEC t=%.6f vector=%02X target_addr=%06X target=%08X pc_before=%06X pc_after=%06X sr=%04X mask=%u",
    ev.time, ev.vector, ev.target_addr, ev.target, ev.pc_before, ev.target & 0x00ffffff, ev.sr, ev.mask_level))
  return nil
end)

local error129 = "ERR0R 129 - REB00T    "

print("IRQ1VEC start wait_for=FILE1_or_ERROR129")
local deadline = now() + 45
local final_text = nil
while now() < deadline do
  local text = display.read_raw()
  if text == error129 then
    final_text = text
    break
  end
  if text == "FILE 1  TUT0RIAL BNK  " then
    final_text = text
    break
  end
  emu.wait(emu.attotime.from_msec(50))
end

if not final_text then
  final_text = display.read_raw()
end

-- Let a couple more IACKs happen if the error display is still settling.
emu.wait(emu.attotime.from_msec(500))

local vec51_count = 0
local other_vector_count = 0
for _, ev in ipairs(events) do
  if ev.vector == 0x51 then
    vec51_count = vec51_count + 1
  else
    other_vector_count = other_vector_count + 1
  end
end

print(string.format(
  "IRQ1VEC_SUMMARY total_iack1=%u vector_51_count=%u other_vector_count=%u final_display=\"%s\"",
  #events, vec51_count, other_vector_count, final_text))

reg.pass("irq1_vector_probe", string.format(
  "total=%u vec51=%u other=%u display=\"%s\"", #events, vec51_count, other_vector_count, final_text))
