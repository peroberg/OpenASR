-- 10th regression test: locks in that firmware's own memory-size belief
-- (ROM $F8A166-$F8A244's four-address alias probe) matches the machine's
-- actually-configured RAM size, not the previous always-maximum result.
-- docs/asr10/investigations/memory-size-belief-analysis.md: the four
-- probe addresses used to be modeled as isolated, never-aliasing shadow
-- registers, so ROM always concluded a fully-expanded ~15.5 MB machine
-- regardless of what mem_map actually backed. asr10_boot_state's unified
-- system_ram_alias_r/w now wraps the whole $200000-$EFFFFF window (not
-- just the four probe bytes -- ROM's own allocator base for the 2 MB
-- branch is $600000, not $000000, so the wrap has to be wide enough to
-- fold that back into the same backing store) against a hardcoded 2 MB
-- (SYSTEM_RAM_BYTES), matching the stock, out-of-the-box ASR-10.
--
-- Without this test, a broken priority order, a wrong SYSTEM_RAM_BYTES
-- value, or a narrowed wraparound window could silently regress the whole
-- category fix back to "always reports maximum" without any other test
-- going red.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

-- ROM's own branch table (memory-size-belief-analysis.md, cross-checked
-- against the live disassembly of $F8A166-$F8A244): D4=$3333 alone
-- selects base=$600000, size=$200000 -- the 2 MB stock-machine branch,
-- matching SYSTEM_RAM_BYTES. D4 is a longword read of $008000, which
-- only reads $3333 if all four probe addresses now alias together.
local EXPECTED_SIZE = 0x00200000
local EXPECTED_BASE = 0x00600000

-- Persisted references (SS8.6). Reassembles the first 32-bit value
-- written to each decision cell from its two 16-bit tap halves -- later
-- writes are the allocator's own subsequent reservations
-- ($C4E += $10000, $C62 -= $18000), not the raw decision, so only the
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

-- The defining, load-bearing check: ROM stores base=$600000/size=$200000
-- only when D4 (the longword read back from $008000) equals $3333 -- the
-- signature that only occurs when all four probe addresses genuinely
-- alias together. Checking the stored decision is more robust than
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
