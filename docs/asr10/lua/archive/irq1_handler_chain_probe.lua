-- Follows the vector-$51 handler chain past SIS, instead of asking whether
-- the interrupt was delivered correctly (already confirmed: yes, vector
-- $51, target $FFFF87CE -- irq1-vector-and-sr-probe.md).
--
-- docs/asr10/archive/troubleshoot.md documents ERROR 129 as the ASR-10
-- service manual's "odd address error" code -- a 68000 Address Error
-- exception (vector 3, table address $00000C), not necessarily an
-- application-level FDC-status-mismatch error path. This probe tests that
-- directly instead of assuming either explanation.
--
-- Requires the naive FDC INTRQ -> MC68302 IRQ1 wiring temporarily active
-- (same as irq1_vector_probe.lua). Observation only.
--
-- Logs, from reset through the crash:
--  - every FDC FIFO byte at $FC4003, both directions (command bytes out,
--    result bytes -- ST0/PCN after SIS, and READ DATA result phase -- in)
--  - every read of the low-RAM async continuation pointer $0402 (the
--    storage-completion-dispatch.md "$0402.w -> movea.l -> jmp (A0)"
--    mechanism), which the boot's own polled RECALIBRATE/READ-DATA path
--    never installs a value into (only the instrument-load path's FDC
--    RECALIBRATE issuer at $F1144A does, per storage-completion-dispatch.md)
--  - every read of the low 68000 internal-exception vector table entries
--    ($000000-$00001F: bus error, address error, illegal instruction,
--    zero divide, CHK, TRAPV), which would only be read if the CPU itself
--    takes one of those exceptions
--  - every write to the MC68302 IDMA registers $FC6802/$FC6804/$FC6808/
--    $FC680C/$FC6810 (installed after t=6s -- past the SIB window's last
--    BAR-driven remap, per methods-static-analysis.md #8.5/#8.7)
--  - the IRQ1 IACK event itself, for cross-reference

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

-- Keep every tap handle referenced for its whole intended lifetime
-- (methods-static-analysis.md #8.6/#8.7) -- table, not discarded locals.
local taps = {}

-- 1. FDC FIFO byte log, both directions.
taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "chain_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    print(string.format("CHAIN_FDC_W t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "chain_fdc_r", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4001 or address == 0x00fc4003 then
    print(string.format("CHAIN_FDC_R t=%.6f pc=%06X addr=%06X value=%02X", now(), pc(), address, byte_value(data, mask)))
  end
  return nil
end)

-- 2. $0402 continuation-pointer reads (4-byte longword read by movea.l).
taps[#taps + 1] = prog:install_read_tap(0x00000400, 0x00000407, "chain_0402_r", function(offset, data, mask)
  print(string.format("CHAIN_0402_R t=%.6f pc=%06X offset=%06X data=%08X mask=%08X", now(), pc(), offset, data, mask))
  return nil
end)

-- 3. Internal CPU exception vector table reads (bus error, address error,
-- illegal instruction, zero divide, CHK, TRAPV -- vectors 2-7, $08-$1F).
taps[#taps + 1] = prog:install_read_tap(0x00000008, 0x0000001f, "chain_exc_vec_r", function(offset, data, mask)
  local vecnum = offset // 4
  print(string.format("CHAIN_EXC_VECTOR_R t=%.6f pc=%06X vector=%u offset=%06X target=%08X", now(), pc(), vecnum, offset, data))
  return nil
end)

-- 4. IDMA registers, installed after the SIB window is known-stable
-- (last BAR write measured at t~5.4s in imr-imr-unmask-probe.md's witness;
-- 6s gives margin).
local function install_idma_tap()
  taps[#taps + 1] = prog:install_write_tap(0x00fc6802, 0x00fc6811, "chain_idma_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    print(string.format("CHAIN_IDMA_W t=%.6f pc=%06X addr=%06X value=%02X", now(), pc(), address, byte_value(data, mask)))
    return nil
  end)
  print(string.format("CHAIN_IDMA_TAP_INSTALLED t=%.6f", now()))
end

-- 5. IRQ1 IACK event.
if cpu_space then
  taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff, "chain_irq1_r", function(offset, data, mask)
    if offset ~= 0x00fffff2 then
      return nil
    end
    local vector = data & 0xff
    local target = prog:read_u32(vector * 4) & 0xffffffff
    print(string.format("CHAIN_IACK1 t=%.6f vector=%02X target=%08X pc=%06X", now(), vector, target, pc()))
    return nil
  end)
end

local error129 = "ERR0R 129 - REB00T    "
local file1 = "FILE 1  TUT0RIAL BNK  "

print("CHAIN start wait_for=FILE1_or_ERROR129")
local idma_installed = false
local deadline = now() + 40
local final_text = nil
while now() < deadline do
  if not idma_installed and now() >= 6.0 then
    install_idma_tap()
    idma_installed = true
  end
  local text = display.read_raw()
  if text == error129 or text == file1 then
    final_text = text
    break
  end
  emu.wait(emu.attotime.from_msec(20))
end

if not final_text then final_text = display.read_raw() end
-- Dwell a bit past the crash point to catch anything that happens shortly after.
emu.wait(emu.attotime.from_msec(1000))

print(string.format("CHAIN_SUMMARY final_display=\"%s\" t=%.6f", final_text, now()))
reg.pass("irq1_handler_chain_probe", string.format("display=\"%s\"", final_text))
