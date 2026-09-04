-- Del 1/3 probe: trace PBDDR/PBDAT and the SCSI stub window ($FC5000-
-- $FC501F) across a full boot->load run, with PC, to find (a) whether
-- firmware ever reads the SCSI stub at all during the boot-time
-- detection step (before PLEASE INSERT DISK), and (b) what PBDAT/PBDDR
-- actually do at runtime, not just at ROM init.

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

local scsi_hits = {}
local pbddr_writes = {}
local pbdat_writes = {}
local pbdat_reads = 0
local pbdat_read_first_pc = nil
local first_display = nil

-- SS8.5: install after the last BAR write. BAR relocation happens at
-- t~=0 in the DPRAM bridge step (boot-sequence.md step 1b); the display
-- driver's first "ENSONIQ ASR-10" render already happens well after
-- that, so installing right after process start (before wait_for_text)
-- is safe here -- this matches the existing scc-idma-transfer-probe.lua
-- precedent of installing post-reset for the $FC6xxx window génerally,
-- but to be conservative we install these taps immediately and rely on
-- the witness-write check below to catch a dead tap rather than assume.
taps[#taps + 1] = prog:install_read_tap(0x00fc5000, 0x00fc501f, "scsi_r", function(offset, data, mask)
  scsi_hits[#scsi_hits + 1] = { t = now(), pc = pc(), dir = "R", offset = offset, data = data, mask = mask }
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00fc5000, 0x00fc501f, "scsi_w", function(offset, data, mask)
  scsi_hits[#scsi_hits + 1] = { t = now(), pc = pc(), dir = "W", offset = offset, data = data, mask = mask }
  return nil
end)

taps[#taps + 1] = prog:install_write_tap(0x00fc6826, 0x00fc6827, "pbddr_w", function(offset, data, mask)
  pbddr_writes[#pbddr_writes + 1] = { t = now(), pc = pc(), data = data }
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00fc6828, 0x00fc6829, "pbdat_w", function(offset, data, mask)
  pbdat_writes[#pbdat_writes + 1] = { t = now(), pc = pc(), data = data, mask = mask }
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(0x00fc6828, 0x00fc6829, "pbdat_r", function(offset, data, mask)
  pbdat_reads = pbdat_reads + 1
  if not pbdat_read_first_pc then pbdat_read_first_pc = pc() end
  return nil
end)

-- Witness: prove taps are alive for the whole window (SS8.7).
local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local display = dofile("docs/asr10/lua/lib/asr10_display.lua")

-- Sample the display every 50ms for the first 10 seconds to see the
-- actual boot message sequence this emulation produces (compare against
-- boot-sequence.md's real-hardware reference sequence).
local seen = {}
local deadline = now() + 12
while now() < deadline do
  local text = display.read_raw()
  if text ~= "" and (not seen[text]) then
    seen[text] = true
    print(string.format("BOOT_DISPLAY t=%.6f text=\"%s\"", now(), text))
  end
  emu.wait(emu.attotime.from_msec(50))
end

-- Continue to FILE 1 to also capture any SCSI-window activity later
-- (e.g. during storage completion dispatch, which is statically known
-- to have an SCSI branch).
local deadline2 = now() + 40
while now() < deadline2 do
  local t = display.read_raw()
  if t == "FILE 1  TUT0RIAL BNK  " then break end
  emu.wait(emu.attotime.from_msec(100))
end

print(string.format("PBSCSI_SUMMARY witness=%u scsi_hits=%u pbddr_writes=%u pbdat_writes=%u pbdat_reads=%u pbdat_read_first_pc=%s",
  witness_writes, #scsi_hits, #pbddr_writes, #pbdat_writes, pbdat_reads,
  pbdat_read_first_pc and string.format("%06X", pbdat_read_first_pc) or "none"))

for i, h in ipairs(scsi_hits) do
  print(string.format("SCSI_HIT[%u] t=%.6f pc=%06X dir=%s offset=%06X data=%04X mask=%04X",
    i, h.t, h.pc, h.dir, h.offset, h.data, h.mask or 0xffff))
end

for i, w in ipairs(pbddr_writes) do
  print(string.format("PBDDR_W[%u] t=%.6f pc=%06X data=%04X", i, w.t, w.pc, w.data))
end

for i, w in ipairs(pbdat_writes) do
  print(string.format("PBDAT_W[%u] t=%.6f pc=%06X data=%04X mask=%04X", i, w.t, w.pc, w.data, w.mask))
end

manager.machine:exit()
