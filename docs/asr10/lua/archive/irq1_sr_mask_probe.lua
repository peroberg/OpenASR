-- SR-mask-over-RECALIBRATE-window probe. Requires the naive FDC INTRQ ->
-- MC68302 IRQ1 wiring temporarily active (same build as
-- irq1_vector_probe.lua). Observation only.
--
-- docs/asr10/investigations/irq1-vector-and-sr-probe.md, Del 3: the SR-mask
-- hypothesis needs BOTH conditions measured, not assumed:
--   1. mask level >= 1 continuously from RECALIBRATE completion until
--      firmware clears INTRQ via SENSE INTERRUPT STATUS
--   2. firmware actually issues SENSE INTERRUPT STATUS before the mask
--      drops
-- This logs, from reset through the crash: every FDC FIFO byte at
-- $FC4003 (to see RECALIBRATE and SENSE INTERRUPT STATUS issuance in
-- context), every SR interrupt-mask-level transition, and the IRQ1 IACK
-- event itself (vector, target, timing) for cross-reference.

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
local function mask_level_of(sr_value) return (sr_value >> 8) & 0x7 end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

-- 1. FDC FIFO byte log.
local fdc_tap = prog:install_write_tap(0x00fc4000, 0x00fc4003, "sr_probe_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    local value = byte_value(data, mask)
    print(string.format("SRPROBE_FDC t=%.6f pc=%06X value=%02X", now(), pc(), value))
  end
  return nil
end)

-- 2. SR mask-level transition log (edge-triggered, not every sample).
local last_mask = mask_level_of(sr())
print(string.format("SRPROBE_MASK_INIT t=%.6f sr=%04X mask=%u", now(), sr(), last_mask))

-- 3. IRQ1 IACK event (same calibrated offset as irq1_vector_probe.lua).
-- NOTE: the returned tap handle MUST be kept referenced (e.g. as a
-- persisted local) for the tap's whole intended lifetime. A first version
-- of this script called install_read_tap() without keeping the return
-- value; the tap fired zero times despite a confirmed IACK occurring in the
-- same build/run (irq1_vector_probe.lua) -- the unreferenced handle was
-- reclaimed almost immediately. Logged as its own instrument trap in
-- docs/asr10/investigations/irq1-vector-and-sr-probe.md.
local irq1_tap = nil
if cpu_space then
  irq1_tap = cpu_space:install_read_tap(0x000000, 0xffffff, "sr_probe_irq1_r", function(offset, data, mask)
    if offset ~= 0x00fffff2 then
      return nil
    end
    local vector = data & 0xff
    local target = prog:read_u32(vector * 4) & 0xffffffff
    print(string.format("SRPROBE_IACK1 t=%.6f vector=%02X target=%08X pc=%06X sr=%04X mask=%u",
      now(), vector, target, pc(), sr(), mask_level_of(sr())))
    return nil
  end)
end

local error129 = "ERR0R 129 - REB00T    "
local file1 = "FILE 1  TUT0RIAL BNK  "

print("SRPROBE start wait_for=FILE1_or_ERROR129, polling SR every 5ms")
local deadline = now() + 40
local final_text = nil
while now() < deadline do
  local cur_mask = mask_level_of(sr())
  if cur_mask ~= last_mask then
    print(string.format("SRPROBE_MASK_EDGE t=%.6f pc=%06X sr=%04X mask=%u->%u", now(), pc(), sr(), last_mask, cur_mask))
    last_mask = cur_mask
  end
  local text = display.read_raw()
  if text == error129 or text == file1 then
    final_text = text
    break
  end
  emu.wait(emu.attotime.from_msec(5))
end

if not final_text then final_text = display.read_raw() end
emu.wait(emu.attotime.from_msec(300))

print(string.format("SRPROBE_SUMMARY final_display=\"%s\" t=%.6f", final_text, now()))
reg.pass("irq1_sr_mask_probe", string.format("display=\"%s\"", final_text))
