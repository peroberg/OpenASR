-- 9th regression test: locks in the priority-arbitrated level-4 interrupt
-- controller (mc68302_device::update_internal_irq()/irq4_ack_vector(),
-- src/devices/machine/mc68302.cpp) that the recording chain depends on.
-- Landed in the same commit as the SCC receive engine and the $37A1 IDMA
-- copy; until this test existed, nothing in the suite would go red if
-- priority arbitration or vector generation broke.
--
-- Drives the same recording-chain stimulus as
-- docs/asr10/investigations/scc-idma-transfer.md
-- (../lua/archive/scc-idma-transfer-probe.lua, unchanged) and asserts the
-- three facts from that investigation as hard pass/fail conditions instead
-- of narrative:
--   1. vector $4D delivered exactly once (SCC1 descriptor completion)
--   2. vector $4B delivered exactly once (IDMA completion), via a real
--      level-4 IACK -- not CMR/CSR polling
--   3. IMR genuinely transitions $E480 -> $EC80 -> $E480 around the
--      transfer (firmware unmasks the IDMA source, then re-masks it)
--
-- Witness requirement (methods-static-analysis.md SS8.7): level-6 (DUART)
-- IACKs are counted throughout as a liveness witness for the same tap
-- mechanism the level-4 assertions depend on.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local rx_state = cpu.state["SCC1RX"]
local taps = {}

local IMR_ADDR = 0x00fc6816
local EXPECTED_IMR_IDLE = 0xe480
local EXPECTED_IMR_DURING_4B = 0xec80

local iack4 = {}
local iack6_witness = 0
local imr_at_4b = nil

if not rx_state or not rx_state.writeable then
  reg.fail("interrupt_controller", "missing_writeable_SCC1RX_state")
  return
end

-- Persisted references (SS8.6): both taps live in the file-scope `taps`
-- table for the whole run, not a local that could be collected. Address
-- formula (0xfffff0 + level*2) matches the cpu_space convention already
-- validated live by scc-idma-transfer-probe.lua, not the byte address
-- literally written in asr10_boot.cpp's cpu_space_map -- the two differ by
-- one and only the tap formula below is confirmed to match real IACK reads.
local LEVEL4_IACK = 0x00fffff0 + 4 * 2
local LEVEL6_IACK = 0x00fffff0 + 6 * 2

taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff, "ic_iack4", function(offset, data)
  if offset ~= LEVEL4_IACK then return nil end
  local vector = data & 0xff
  iack4[vector] = (iack4[vector] or 0) + 1
  if vector == 0x4b then
    imr_at_4b = prog:read_u16(IMR_ADDR) & 0xffff
  end
  return nil
end)

taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff, "ic_iack6_witness", function(offset, data)
  if offset == LEVEL6_IACK then
    iack6_witness = iack6_witness + 1
  end
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(1 << (code & 0x1f))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

-- Identical to scc-idma-transfer-probe.lua's pattern(): the exact 800-byte
-- shape firmware's own pretrigger/threshold logic selects a full MRBLR
-- descriptor from.
local function pattern(offset)
  local value = ((offset & 0x0f) < 2) and 0 or (offset & 0xff)
  if offset == 0x40 then value = 0x7f end
  if offset == 0x41 then value = 0xff end
  return value
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("interrupt_controller", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)   -- Sample-Source Select
press_button(0x02, 1000)  -- Level Detect
for _ = 1, 24 do press_button(0x0a, 40) end
press_button(0x23, 100)   -- RECORD start
emu.wait(emu.attotime.from_seconds(1))

if prog:read_u16(0x0d04) ~= 2 then
  reg.fail("interrupt_controller", string.format(
    "did_not_reach_waiting state=%04X display=\"%s\"", prog:read_u16(0x0d04), display.read_raw()))
  return
end

local imr_before = prog:read_u16(IMR_ADDR) & 0xffff

for offset = 0, 0x031f do
  rx_state.value = pattern(offset)
end
emu.wait(emu.attotime.from_seconds(2))

local imr_after = prog:read_u16(IMR_ADDR) & 0xffff
local iack_4d = iack4[0x4d] or 0
local iack_4b = iack4[0x4b] or 0

print(string.format(
  "INTERRUPT_CONTROLLER_RESULT display=\"%s\" iack_4d=%u iack_4b=%u imr_before=%04X imr_at_4b=%s imr_after=%04X witness=%u",
  display.read_raw(), iack_4d, iack_4b, imr_before,
  imr_at_4b and string.format("%04X", imr_at_4b) or "none", imr_after, iack6_witness))

if iack6_witness == 0 then
  reg.fail("interrupt_controller", "no_level6_witness_activity -- tap may be dead, result uninterpretable")
  return
end
if iack_4d ~= 1 then
  reg.fail("interrupt_controller", string.format("iack_4d=%u expected=1 (SCC1 descriptor completion)", iack_4d))
  return
end
if iack_4b ~= 1 then
  reg.fail("interrupt_controller", string.format("iack_4b=%u expected=1 (IDMA completion via interrupt, not polling)", iack_4b))
  return
end
if imr_before ~= EXPECTED_IMR_IDLE then
  reg.fail("interrupt_controller", string.format("imr_before=%04X expected=%04X", imr_before, EXPECTED_IMR_IDLE))
  return
end
if imr_at_4b ~= EXPECTED_IMR_DURING_4B then
  reg.fail("interrupt_controller", string.format("imr_at_4b=%s expected=%04X",
    imr_at_4b and string.format("%04X", imr_at_4b) or "none", EXPECTED_IMR_DURING_4B))
  return
end
if imr_after ~= EXPECTED_IMR_IDLE then
  reg.fail("interrupt_controller", string.format("imr_after=%04X expected=%04X", imr_after, EXPECTED_IMR_IDLE))
  return
end

reg.pass("interrupt_controller", string.format(
  "iack_4d=%u iack_4b=%u imr=%04X->%04X->%04X witness=%u",
  iack_4d, iack_4b, imr_before, imr_at_4b, imr_after, iack6_witness))
