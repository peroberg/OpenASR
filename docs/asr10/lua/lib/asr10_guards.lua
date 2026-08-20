-- MC68302 consolidation guards (docs/asr10/investigations/
-- mc68302-consolidation.md). Every allowlist here was calibrated by a
-- dedicated inventory run before being written -- see that document and
-- the archived calibration scripts (exception-vector-inventory.lua,
-- sib-coverage-inventory.lua) for how each number was measured. Alarms
-- are aggregated: first occurrence per distinct condition only, never
-- one line per access ("Ett larm som spammar blir avstängt").
--
-- All guards return the list of tap handles they installed -- the
-- caller MUST keep that list referenced for the guard's whole intended
-- lifetime (methods-static-analysis.md #8.6: an unreferenced tap handle
-- is silently GC'd).

local M = {}

-- ===================================================================
-- Del 1: exception vector guard.
--
-- Calibrated by exception-vector-inventory.lua over a full clean boot +
-- instrument load: only two (level, vector) pairs were ever observed --
-- level 1 -> $51 (FDC/SCSI storage completion, the vector-$51 dispatcher
-- this whole project is built around) and level 6 -> $56 (DUART). Levels
-- 2-5 and 7 never fired at all.
--
-- A first attempt tried to inventory TRAP/internal-CPU-exception
-- activity by tapping PROGRAM SPACE reads over the whole vector table
-- ($000000-$0000FF) and treating every read as a vector fetch. This
-- produced obvious nonsense (one vector showed 1,167,945 "fetches" in
-- ~22s, and a uniform baseline of exactly 6 reads appeared across many
-- unrelated vector numbers) -- proof firmware reuses part of that
-- address range for ordinary data, not evidence of exception activity.
-- No Lua-exposed API exists for MAME's own exception-point mechanism
-- either (device_debug::exceptionpoint_set exists in
-- src/emu/debug/debugcpu.h but has no luaengine_debug.cpp binding --
-- only bpset/wpset do). TRAP/internal-exception coverage is therefore
-- [OPEN], not guarded here -- guarding on an unreliable heuristic would
-- be worse than not guarding at all.
function M.install_exception_guard(cpu, on_alarm)
  local cpu_space = cpu.spaces["cpu_space"]
  local allowed = {
    [1] = { [0x51] = true },
    [6] = { [0x56] = true },
  }
  local seen = {}
  local taps = {}
  for level = 1, 7 do
    -- cpu_space taps report the even, word-aligned IACK address, not the
    -- odd byte address cpu_space_map() declares in C++ (calibrated by
    -- irq1_vector_probe.lua: level 1 reported as $FFFFF2, not $FFFFF3).
    local iack_offset = 0x00fffff0 + (level * 2)
    taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff, string.format("guard_iack_l%d", level), function(offset, data, mask)
      if offset ~= iack_offset then return nil end
      local vec = data & 0xff
      if not (allowed[level] and allowed[level][vec]) then
        local key = string.format("%d:%02X", level, vec)
        if not seen[key] then
          seen[key] = true
          on_alarm(string.format("unexpected_interrupt level=%d vector=%02X", level, vec))
        end
      end
      return nil
    end)
  end
  return taps
end

-- ===================================================================
-- Del 2: SIB coverage guard.
--
-- Calibrated by sib-coverage-inventory.lua: every byte-offset within
-- $FC6000-$FC6FFF firmware touched during a clean boot + load,
-- classified via mc68302_device's own classify_offset()/classify_full()
-- (mc68302.cpp:242-273, ported below by reading that source). Result:
-- zero "unknown"-classified accesses, and exactly 88 distinct
-- "known_unimplemented" offsets touched (SCC1/SCC2 parameter RAM,
-- GIMR/IPR/IMR/ISR, Port A, SCC1-3/SMC/SCP command/mode registers) --
-- all real, legitimate firmware behavior the device model doesn't
-- implement yet, not bugs. The guard alarms only on an offset OUTSIDE
-- this calibrated set (a genuinely new, never-before-observed access),
-- or on any "unknown"-classified offset at all (none were ever
-- observed, so any future one is worth knowing about immediately).
--
-- Tap-lifetime (methods-static-analysis.md #8.5): the SIB window is torn
-- down and reinstalled on every BAR write. This driver's BAR settles to
-- its final value within the first ~5.4s of boot (measured: first write
-- at t=0.000004 already sets the final value $0FC6; two more writes at
-- t=5.395043/5.395061 redundantly reconfirm the same value byte-by-byte,
-- last write at t=5.395061). The caller MUST install this guard only
-- after that point -- see install_sib_coverage_guard's own `after`
-- parameter -- an earlier install would be silently dropped by the next
-- BAR-triggered reinstall regardless of whether the value changes.
local KNOWN_UNIMPLEMENTED_CALIBRATED = {}
for _, o in ipairs({
  0x0400, 0x0402, 0x0404, 0x0406, 0x0408, 0x040A, 0x040C, 0x040E,
  0x0410, 0x0412, 0x0414, 0x0416, 0x0418, 0x041A, 0x041C, 0x041E,
  0x0420, 0x0422, 0x0424, 0x0426, 0x0428, 0x042A, 0x042C, 0x042E,
  0x0430, 0x0432, 0x0434, 0x0436, 0x0438, 0x043A, 0x043C, 0x043E,
  0x0480, 0x0482, 0x0486, 0x04AC, 0x04AE,
  0x0500, 0x0502, 0x0504, 0x0506, 0x0508, 0x050A, 0x050C, 0x050E,
  0x0510, 0x0512, 0x0514, 0x0516, 0x0518, 0x051A, 0x051C, 0x051E,
  0x0520, 0x0522, 0x0524, 0x0526, 0x0528, 0x052A, 0x052C, 0x052E,
  0x0530, 0x0532, 0x0534, 0x0536, 0x0538, 0x053A, 0x053C, 0x053E,
  0x0580, 0x0582, 0x0586, 0x05AC, 0x05AE,
  0x0814, 0x0816, 0x0818, 0x0820, 0x0822,
  0x0882, 0x0884, 0x0888, 0x088A, 0x0892, 0x0894, 0x0898, 0x089A, 0x08B4,
}) do
  KNOWN_UNIMPLEMENTED_CALIBRATED[o] = true
end

local OFFSET_PBCNT, OFFSET_PBDDR, OFFSET_PBDAT, OFFSET_FC6860 = 0x0824, 0x0826, 0x0828, 0x0860
local OFFSET_BR0, OFFSET_OR0, OFFSET_BR1, OFFSET_OR1 = 0x0830, 0x0832, 0x0834, 0x0836
local OFFSET_BR2, OFFSET_OR2, OFFSET_BR3, OFFSET_OR3 = 0x0838, 0x083a, 0x083c, 0x083e
local OFFSET_IDMA_CMR, OFFSET_IDMA_SAPR_HI, OFFSET_IDMA_SAPR_LO = 0x0802, 0x0804, 0x0806
local OFFSET_IDMA_DAPR_HI, OFFSET_IDMA_DAPR_LO, OFFSET_IDMA_BCR = 0x0808, 0x080a, 0x080c
local OFFSET_IDMA_CSR, OFFSET_IDMA_FCR = 0x080e, 0x0810
local KNOWN_OFFSETS = {}
for _, o in ipairs({ OFFSET_PBCNT, OFFSET_PBDDR, OFFSET_PBDAT, OFFSET_FC6860,
                      OFFSET_BR0, OFFSET_OR0, OFFSET_BR1, OFFSET_OR1,
                      OFFSET_BR2, OFFSET_OR2, OFFSET_BR3, OFFSET_OR3,
                      OFFSET_IDMA_CMR, OFFSET_IDMA_SAPR_HI, OFFSET_IDMA_SAPR_LO,
                      OFFSET_IDMA_DAPR_HI, OFFSET_IDMA_DAPR_LO, OFFSET_IDMA_BCR,
                      OFFSET_IDMA_CSR, OFFSET_IDMA_FCR }) do
  KNOWN_OFFSETS[o] = true
end

local function classify_offset(byte_offset)
  if byte_offset <= 0x03ff then return "internal_ram" end
  if byte_offset <= 0x07ff then return "known_unimplemented" end
  if byte_offset <= 0x0811 then return "known_unimplemented" end
  if byte_offset <= 0x0819 then return "known_unimplemented" end
  if byte_offset >= 0x081e and byte_offset <= 0x0823 then return "known_unimplemented" end
  if byte_offset >= 0x0840 and byte_offset <= 0x084d then return "known_unimplemented" end
  if byte_offset >= 0x0850 and byte_offset <= 0x085a then return "known_unimplemented" end
  if byte_offset >= 0x0880 and byte_offset <= 0x08b5 then return "known_unimplemented" end
  return "unknown"
end

local function classify_full(byte_offset)
  if KNOWN_OFFSETS[byte_offset] then return "known" end
  return classify_offset(byte_offset)
end
M.classify_full = classify_full -- exposed for Del 3 / callers that need it directly

-- install_sib_coverage_guard(prog, on_alarm): call only after BAR is
-- known settled (see comment above). Returns tap handles.
function M.install_sib_coverage_guard(prog, on_alarm)
  local seen = {}
  local taps = {}
  local function check(byte_offset)
    local cls = classify_full(byte_offset)
    if cls == "unknown" or (cls == "known_unimplemented" and not KNOWN_UNIMPLEMENTED_CALIBRATED[byte_offset]) then
      if not seen[byte_offset] then
        seen[byte_offset] = true
        on_alarm(string.format("uninventoried_sib_access offset=%04X abs=%06X class=%s", byte_offset, 0x00fc6000 + byte_offset, cls))
      end
    end
  end
  taps[#taps + 1] = prog:install_read_tap(0x00fc6000, 0x00fc6fff, "guard_sib_r", function(offset, data, mask)
    check(offset - 0x00fc6000)
    return nil
  end)
  taps[#taps + 1] = prog:install_write_tap(0x00fc6000, 0x00fc6fff, "guard_sib_w", function(offset, data, mask)
    check(offset - 0x00fc6000)
    return nil
  end)
  return taps
end

-- ===================================================================
-- Del 3: IDMA guards -- SAPR, CMR, BCR.
--
-- Calibrated by sib-coverage-inventory.lua's SCI_IDMA_VALUE output over
-- the same clean boot + load, 21 real transfers:
--   SAPR: always exactly $FFFC5803 (21/21) -- a fixed FDC I/O address,
--     not dereferenced by this device (idma-implementation-plan.md item
--     1/limitation 1) -- alarm on ANY other value; payable now (a Lua
--     guard, not a mem_map change).
--   CMR: exactly two values -- $0002 (57 times: RST=1,STR=0, a software
--     reset with no corresponding arm) and $0D51 (21 times: STR=1, the
--     one arm pattern this device's transfer logic is built around).
--     Field decode sourced from docs/mc68302/idma-spec.md's newly-added
--     CMR bit table (itself sourced from the manual's OCR text via a web
--     fetch, [Likely] not [Verified] -- see that document). One
--     validated value does not generalize to a field decoder (the
--     task's own caution); this guard alarms on any value outside the
--     two observed ones, it does not attempt to validate individual
--     bits.
--   BCR: exactly three values -- $0201 (1 sector, x3), $0E01 (7
--     sectors, x2), $2801 (20 sectors/one track, x16) -- matching
--     file-loaded-verification-probe.md's own arm/byte-count data
--     exactly (3+2+16=21 arms). Alarm on any other value; BCR-1's byte
--     count has never been tested outside these three.
--   DAPR (both halves) legitimately varies across the whole transfer --
--     it is the destination pointer -- no fixed-value alarm applies,
--     unlike SAPR.
--
-- Call after the same SIB-window-settled point as the coverage guard
-- (same $FC6000-$FC6FFF window, same #8.5 constraint).
local VALID_CMR = { [0x0002] = true, [0x0d51] = true }
local VALID_BCR = { [0x0201] = true, [0x0e01] = true, [0x2801] = true }
local VALID_SAPR = 0xfffc5803

function M.install_idma_guard(prog, on_alarm)
  local sapr_hi, sapr_lo = 0, 0
  local seen = {}
  local function once(key, msg)
    if not seen[key] then
      seen[key] = true
      on_alarm(msg)
    end
  end
  local taps = {}
  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "guard_idma_w", function(offset, data, mask)
    local rel = offset - 0x00fc6800
    if rel == 0x02 then -- CMR
      local v = data & 0xffff
      if not VALID_CMR[v] then
        once("cmr:" .. v, string.format("unvalidated_cmr value=%04X", v))
      end
    elseif rel == 0x04 then -- SAPR_HI
      sapr_hi = data & 0xffff
    elseif rel == 0x06 then -- SAPR_LO
      sapr_lo = data & 0xffff
      local combined = (sapr_hi << 16) | sapr_lo
      if combined ~= VALID_SAPR then
        once("sapr:" .. combined, string.format("unexpected_sapr value=%08X expected=%08X", combined, VALID_SAPR))
      end
    elseif rel == 0x0c then -- BCR
      local v = data & 0xffff
      if not VALID_BCR[v] then
        once("bcr:" .. v, string.format("unvalidated_bcr value=%04X", v))
      end
    end
    return nil
  end)
  return taps
end

return M
