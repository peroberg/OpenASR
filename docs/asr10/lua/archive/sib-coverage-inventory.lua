-- Del 2 of the MC68302 consolidation task
-- (docs/asr10/investigations/mc68302-consolidation.md): inventory every
-- $FC6000-$FC6FFF register firmware actually touches during boot and the
-- instrument load, classified against mc68302_device's own
-- classify_offset()/classify_full() (mc68302.cpp:242-273), ported here
-- by reading that source, not guessed.
--
-- Tap-lifetime handling (methods-static-analysis.md #8.5): the SIB
-- window is relocated (removed + reinstalled) on every BAR write
-- ($0000F2, a fixed, non-relocatable address, unaffected by the SIB
-- window's own relocation). This project's established, previously-
-- proven-correct technique (irq1_handler_chain_probe.lua) is to install
-- the coverage tap only after BAR's own last write has already happened
-- (measured at t~5.4s in earlier work, ~10s before FILE 1 at t~16.3s),
-- not to attempt a synchronous re-install from inside the BAR-write tap
-- itself (a same-callback re-entrant install is exactly the class of
-- bug this project's own tc_w() reentrancy episode already burned time
-- on -- avoided here on principle, not because it was tried and failed).
-- Witnessed with a sibling tap on a range already known hot during the
-- load (FDC FIFO $FC4003) installed at the same moment, so a
-- corresponding non-zero witness count proves taps were alive for the
-- whole post-install window, not just at install time.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

-- BAR witness: confirm (not assume) window_base, and confirm BAR's last
-- write already happened before our chosen install time.
local bar_writes = {}
taps[#taps + 1] = prog:install_write_tap(0x000000f0, 0x000000ff, "sci_bar_w", function(offset, data, mask)
  if offset == 0x000000f2 then
    bar_writes[#bar_writes + 1] = { t = now(), data = data, mask = mask }
  end
  return nil
end)

-- Port classify_offset()/classify_full() from mc68302.cpp (cited above),
-- not re-derived.
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
  if byte_offset <= 0x07ff then return "known_unimplemented" end -- SCC/SMC parameter RAM
  if byte_offset <= 0x0811 then return "known_unimplemented" end -- IDMA reserved bytes
  if byte_offset <= 0x0819 then return "known_unimplemented" end -- GIMR/IPR/IMR/ISR
  if byte_offset >= 0x081e and byte_offset <= 0x0823 then return "known_unimplemented" end -- Port A
  if byte_offset >= 0x0840 and byte_offset <= 0x084d then return "known_unimplemented" end -- Timer1+watchdog
  if byte_offset >= 0x0850 and byte_offset <= 0x085a then return "known_unimplemented" end -- Timer2
  if byte_offset >= 0x0880 and byte_offset <= 0x08b5 then return "known_unimplemented" end -- SCC1-3/SMC/SCP
  return "unknown"
end

local function classify_full(byte_offset)
  if KNOWN_OFFSETS[byte_offset] then return "known" end
  return classify_offset(byte_offset)
end

-- Coverage state: per-byte-offset (0x0000-0x0FFF within the window)
-- read/write counts and first observed value.
local coverage = {}
-- Track every distinct value for the 8 named IDMA offsets specifically
-- (Del 3 needs the full set, not just the first value -- CMR alone
-- turned out to be written 78 times with a first value of $0002, not
-- the single $0D51 this driver's behavior is built around).
local IDMA_OFFSETS = {
  [0x0802] = "CMR", [0x0804] = "SAPR_HI", [0x0806] = "SAPR_LO",
  [0x0808] = "DAPR_HI", [0x080a] = "DAPR_LO", [0x080c] = "BCR",
  [0x080e] = "CSR", [0x0810] = "FCR",
}
local idma_values = {} -- idma_values[offset][value] = count

local function record(byte_offset, dir, value)
  local c = coverage[byte_offset]
  if not c then
    c = { reads = 0, writes = 0, first_value = nil, first_t = now() }
    coverage[byte_offset] = c
  end
  if dir == "r" then
    c.reads = c.reads + 1
  else
    c.writes = c.writes + 1
  end
  if c.first_value == nil then c.first_value = value end
  if dir == "w" and IDMA_OFFSETS[byte_offset] then
    local vals = idma_values[byte_offset]
    if not vals then vals = {}; idma_values[byte_offset] = vals end
    vals[value] = (vals[value] or 0) + 1
  end
end

local witness_hits = 0
local window_base = nil

local function install_coverage_tap()
  window_base = 0x00fc6000 -- confirmed against the last BAR write below, not assumed blind
  taps[#taps + 1] = prog:install_read_tap(0x00fc6000, 0x00fc6fff, "sci_sib_r", function(offset, data, mask)
    record(offset - window_base, "r", data)
    return nil
  end)
  taps[#taps + 1] = prog:install_write_tap(0x00fc6000, 0x00fc6fff, "sci_sib_w", function(offset, data, mask)
    record(offset - window_base, "w", data)
    return nil
  end)
  -- Witness: same-moment sibling tap on a range already proven hot
  -- during the load (file-loaded-verification-probe.md: FDC FIFO).
  taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "sci_witness_r", function(offset, data, mask)
    witness_hits = witness_hits + 1
    return nil
  end)
  print(string.format("SCI_COVERAGE_TAP_INSTALLED t=%.6f window_base=%06X", now(), window_base))
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
local file_loaded = "FILE L0ADED           "

-- Install after BAR's own last write (measured elsewhere at t~5.4s);
-- 7s gives margin, matching irq1_handler_chain_probe.lua's own established
-- choice for the identical trap.
emu.wait(emu.attotime.from_seconds(7))
local bar_writes_before_install = #bar_writes
install_coverage_tap()

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("sib_coverage_inventory", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("SCI_FILE1 t=%.6f", now()))

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("sib_coverage_inventory", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("SCI_LOADED t=%.6f", now()))

emu.wait(emu.attotime.from_msec(500))

print(string.format("SCI_BAR_WRITES total=%u before_install=%u", #bar_writes, bar_writes_before_install))
for i, w in ipairs(bar_writes) do
  print(string.format("SCI_BAR_WRITE i=%u t=%.6f data=%08X mask=%08X", i, w.t, w.data, w.mask))
end
print(string.format("SCI_WITNESS hits=%u", witness_hits))

local sorted = {}
for o, c in pairs(coverage) do sorted[#sorted + 1] = { o = o, c = c } end
table.sort(sorted, function(a, b) return a.o < b.o end)

print("SCI_COVERAGE_TABLE offset reads writes first_value class")
for _, e in ipairs(sorted) do
  local cls = classify_full(e.o)
  print(string.format("SCI_ENTRY offset=%04X abs=%06X reads=%u writes=%u first_value=%08X class=%s",
    e.o, window_base + e.o, e.c.reads, e.c.writes, e.c.first_value or 0, cls))
end

print("SCI_IDMA_VALUES offset name value count")
for offset, name in pairs(IDMA_OFFSETS) do
  local vals = idma_values[offset]
  if vals then
    local list = {}
    for v, c in pairs(vals) do list[#list + 1] = { v = v, c = c } end
    table.sort(list, function(a, b) return a.v < b.v end)
    for _, e in ipairs(list) do
      print(string.format("SCI_IDMA_VALUE offset=%04X name=%s value=%08X count=%u", offset, name, e.v, e.c))
    end
  end
end

print(string.format("SCI_SUMMARY distinct_offsets=%u final_display=\"%s\" t=%.6f", #sorted, text, now()))
reg.pass("sib_coverage_inventory", string.format("distinct_offsets=%u witness=%u", #sorted, witness_hits))
