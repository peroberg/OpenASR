-- One-off measurement probe (not a regression test): does firmware's
-- ROM alias-based memory-size decision, and the separate $100000-$1FFFFF
-- pattern sweep, extend into or depend on genuinely unmapped address
-- space? Answers docs/asr10/reference/handoff-2026-08-23.md's
-- memory-size-category task, Del 1/Del 2. No code change, no mem_map
-- change, no stereo/factor-two/clock/bank1/ES5510 touch.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

-- Wide segmentation above the already-known $100000-$1FFFFF sweep, up to
-- the ROM alias window at $F80000. Anything landing here is currently
-- unmapped in mem_map (aside from the four isolated 4-byte probe windows
-- at $408000/$808000/$c08000, which are already independently understood).
local SEGMENTS = {
  { 0x00200000, 0x003fffff, "seg_200000" },
  { 0x00400000, 0x007fffff, "seg_400000" },
  { 0x00800000, 0x00bfffff, "seg_800000" },
  { 0x00c00000, 0x00efffff, "seg_c00000" },
}

local seg_stats = {}
for _, s in ipairs(SEGMENTS) do
  local lo, hi, name = s[1], s[2], s[3]
  seg_stats[name] = { count = 0, first_addr = nil, last_addr = nil, first_pc = nil }
  taps[#taps + 1] = prog:install_write_tap(lo, hi, "msp_" .. name, function(offset, data, mask)
    local st = seg_stats[name]
    st.count = st.count + 1
    st.first_addr = st.first_addr or offset
    st.last_addr = offset
    st.first_pc = st.first_pc or pc()
    return nil
  end)
end

-- Known sweep range, for cross-check against prior documentation.
local sweep_stats = { count = 0, first_addr = nil, last_addr = nil, first_pc = nil, last_pc = nil }
taps[#taps + 1] = prog:install_write_tap(0x00100000, 0x001fffff, "msp_sweep", function(offset, data, mask)
  sweep_stats.count = sweep_stats.count + 1
  sweep_stats.first_addr = sweep_stats.first_addr or offset
  sweep_stats.last_addr = offset
  sweep_stats.first_pc = sweep_stats.first_pc or pc()
  sweep_stats.last_pc = pc()
  return nil
end)

-- Liveness witness (SS8.7): identically-installed tap on a range known to
-- be hit continuously (low RAM), proving the tap mechanism itself stays
-- alive for the whole window, not just at install time.
local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "msp_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

-- The four alias-probe shadow addresses themselves, plus the ROM decision
-- outputs (initial allocator base/size, per stereo-ram-decode-analysis.md).
local alias_writes = {}
for _, addr in ipairs({ 0x00008000, 0x00408000, 0x00808000, 0x00c08000 }) do
  taps[#taps + 1] = prog:install_write_tap(addr, addr + 3, "msp_alias_" .. string.format("%06x", addr), function(offset, data, mask)
    alias_writes[#alias_writes + 1] = string.format("t=%.6f addr=%06X data=%04X pc=%06X", now(), offset, data & 0xffff, pc())
    return nil
  end)
end

local decision_trace = {}
for _, addr in ipairs({ 0x00008000, 0x00808000, 0x00000c62, 0x00000c4e }) do
  taps[#taps + 1] = prog:install_read_tap(addr, addr + 3, "msp_decrd_" .. string.format("%06x", addr), function(offset, data, mask)
    decision_trace[#decision_trace + 1] = string.format("READ  t=%.6f addr=%06X data=%08X pc=%06X", now(), offset, data, pc())
    return nil
  end)
  taps[#taps + 1] = prog:install_write_tap(addr, addr + 3, "msp_decwr_" .. string.format("%06x", addr), function(offset, data, mask)
    decision_trace[#decision_trace + 1] = string.format("WRITE t=%.6f addr=%06X data=%08X mask=%08X pc=%06X", now(), offset, data, mask, pc())
    return nil
  end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  print(string.format("MEMSIZE_TIMEOUT display=\"%s\"", text))
  manager.machine:exit()
  return
end
emu.wait(emu.attotime.from_seconds(2))

-- Del 1: read back the ROM's own decision outputs (established addresses
-- from stereo-ram-decode-analysis.md: $F8A210 writes $0C62, $F8A228 writes
-- $0C4E).
print(string.format("MEMSIZE_ALLOC_DECISION base_field_0C62=%08X size_field_0C4E=%08X",
  prog:read_u32(0x00000c62), prog:read_u32(0x00000c4e)))
print("MEMSIZE_ALIAS_TRACE")
for _, line in ipairs(alias_writes) do print("  " .. line) end

print(string.format("MEMSIZE_D8000=%08X D408000=%08X D808000=%08X DC08000=%08X",
  prog:read_u32(0x00008000), prog:read_u32(0x00408000),
  prog:read_u32(0x00808000), prog:read_u32(0x00c08000)))

print(string.format("MEMSIZE_SWEEP count=%u first_addr=%s last_addr=%s first_pc=%s last_pc=%s",
  sweep_stats.count,
  sweep_stats.first_addr and string.format("%06X", sweep_stats.first_addr) or "none",
  sweep_stats.last_addr and string.format("%06X", sweep_stats.last_addr) or "none",
  sweep_stats.first_pc and string.format("%06X", sweep_stats.first_pc) or "none",
  sweep_stats.last_pc and string.format("%06X", sweep_stats.last_pc) or "none"))

for _, s in ipairs(SEGMENTS) do
  local name = s[3]
  local st = seg_stats[name]
  print(string.format("MEMSIZE_SEG name=%s count=%u first_addr=%s last_addr=%s first_pc=%s",
    name, st.count,
    st.first_addr and string.format("%06X", st.first_addr) or "none",
    st.last_addr and string.format("%06X", st.last_addr) or "none",
    st.first_pc and string.format("%06X", st.first_pc) or "none"))
end

print(string.format("MEMSIZE_WITNESS writes=%u", witness_writes))

-- Del 2: manual characterization of unmapped-space read/write behavior in
-- this build -- not firmware activity, a direct probe of the model itself.
local UNMAPPED_TEST = 0x00500000
local before = prog:read_u16(UNMAPPED_TEST)
prog:write_u16(UNMAPPED_TEST, 0xbeef)
local after = prog:read_u16(UNMAPPED_TEST)
print(string.format("MEMSIZE_UNMAPPED_CHARACTERIZATION addr=%06X before=%04X after_write_beef=%04X",
  UNMAPPED_TEST, before, after))

print("MEMSIZE_DECISION_TRACE (first 40 entries)")
for i, line in ipairs(decision_trace) do
  if i > 40 then break end
  print("  " .. line)
end
print(string.format("MEMSIZE_DECISION_TRACE_TOTAL=%u", #decision_trace))

print(string.format("MEMSIZE_DISPLAY display=\"%s\"", display.read_raw()))
manager.machine:exit()
