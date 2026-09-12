-- Locks in that firmware's own memory-size belief (ROM $F8A166-$F8A244's
-- four-address alias probe) matches the distinct 16 MiB functional backing.
-- $008000/$408000/$808000/$C08000 must not collapse through the former
-- modulo-$200000 policy.  The ROM must therefore choose its $000000/$F80000
-- configuration without any patched firmware state.
--
-- Without this test, a reintroduced low-address alias could silently turn
-- the model back into the old 2 MiB configuration without any other test
-- going red.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

-- ROM's branch table (memory-size-belief-analysis.md, cross-checked against
-- $F8A166-$F8A244): independent $008000/$408000/$808000/$C08000 storage
-- yields D4=0 and D5=$2222, selecting base=$000000, size=$F80000.
local EXPECTED_SIZE = 0x00f80000
local EXPECTED_BASE = 0x00000000

-- Persisted references (SS8.6). Reassembles the first 32-bit value
-- written to each decision cell from its two 16-bit tap halves -- later
-- writes are the allocator's own subsequent reservations
-- ($C4E/C62 are subsequently adjusted for reservations), not the raw decision, so only the
-- first complete (hi, lo) pair is kept.
local size_hi, size_lo, size_first = nil, nil, nil
local base_hi, base_lo, base_first = nil, nil, nil

taps[#taps + 1] = prog:install_write_tap(0x00000c62, 0x00000c63, "ms_size_hi_w", function(offset, data, mask)
  if not size_first then size_hi = data & 0xffff end
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00000c64, 0x00000c65, "ms_size_lo_w", function(offset, data, mask)
  if not size_first and size_hi then
    size_first = (size_hi << 16) | (data & 0xffff)
  end
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00000c4e, 0x00000c4f, "ms_base_hi_w", function(offset, data, mask)
  if not base_first then base_hi = data & 0xffff end
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00000c50, 0x00000c51, "ms_base_lo_w", function(offset, data, mask)
  if not base_first and base_hi then
    base_first = (base_hi << 16) | (data & 0xffff)
  end
  return nil
end)

local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "ms_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("memory_size", string.format("boot_timeout display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(200))

print(string.format(
  "MEMORY_SIZE_RESULT display=\"%s\" size_first=%s base_first=%s witness=%u",
  display.read_raw(),
  size_first and string.format("%08X", size_first) or "none",
  base_first and string.format("%08X", base_first) or "none",
  witness_writes))

if witness_writes == 0 then
  reg.fail("memory_size", "no_witness_activity -- tap may be dead, result uninterpretable")
  return
end
if not size_first or not base_first then
  reg.fail("memory_size", "no_decision_store_write_observed")
  return
end

-- The defining check: the ROM stores base=$000000/size=$F80000 only when
-- its independent probe reads select the expanded-memory branch. Checking
-- the stored decision is more robust than
-- re-reading $008000 later: that address is legitimately reused as
-- ordinary low RAM within milliseconds (measured: 60 reads/22 writes
-- over a full run in memory-size-belief-analysis.md), so its content long
-- after boot reflects normal traffic, not the probe's own signature.
if size_first ~= EXPECTED_SIZE then
  reg.fail("memory_size", string.format("size_decision=%08X expected=%08X", size_first, EXPECTED_SIZE))
  return
end
if base_first ~= EXPECTED_BASE then
  reg.fail("memory_size", string.format("base_decision=%08X expected=%08X", base_first, EXPECTED_BASE))
  return
end

reg.pass("memory_size", string.format(
  "size=%08X base=%08X", size_first, base_first))
