-- Del 3 item 1 of the MC68302 consolidation follow-up
-- (docs/asr10/investigations/mc68302-consolidation-2.md): inventory
-- which parts of mem_map's catch-all map(0xfc5020, 0xffffff).ram() --
-- ~716KB where missing hardware presents as working RAM, minus the
-- SIB's own dynamically-installed 4KB at $FC6000-$FC6FFF, which always
-- takes priority over this static range when installed -- actually get
-- touched during a normal boot + instrument load. Observation only, no
-- mem_map change. Bucketed at 4KB granularity (176 buckets) to keep
-- output manageable; a per-byte table over 716KB would not be
-- reportable.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

local BUCKET_SIZE = 0x1000
local buckets = {} -- buckets[bucket_base] = { reads=, writes=, first_t= }

local function record(addr, dir)
  local bucket = addr - (addr % BUCKET_SIZE)
  local b = buckets[bucket]
  if not b then
    b = { reads = 0, writes = 0, first_t = now() }
    buckets[bucket] = b
  end
  if dir == "r" then b.reads = b.reads + 1 else b.writes = b.writes + 1 end
end

-- $FC6000-$FC6FFF is excluded: it is not part of the catch-all once the
-- SIB window is installed (dynamic installs always override a static
-- address_map entry for the same range in MAME's memory system), and is
-- already fully inventoried by sib-coverage-inventory.lua.
taps[#taps + 1] = prog:install_read_tap(0x00fc5020, 0x00fc5fff, "cri_r_lo", function(offset, data, mask) record(offset, "r") return nil end)
taps[#taps + 1] = prog:install_write_tap(0x00fc5020, 0x00fc5fff, "cri_w_lo", function(offset, data, mask) record(offset, "w") return nil end)
taps[#taps + 1] = prog:install_read_tap(0x00fc7000, 0x00ffffff, "cri_r_hi", function(offset, data, mask) record(offset, "r") return nil end)
taps[#taps + 1] = prog:install_write_tap(0x00fc7000, 0x00ffffff, "cri_w_hi", function(offset, data, mask) record(offset, "w") return nil end)

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

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("catchall_ram_inventory", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("CRI_FILE1 t=%.6f", now()))

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("catchall_ram_inventory", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("CRI_LOADED t=%.6f", now()))
emu.wait(emu.attotime.from_msec(500))

local sorted = {}
for base, b in pairs(buckets) do sorted[#sorted + 1] = { base = base, b = b } end
table.sort(sorted, function(a, b) return a.base < b.base end)

print(string.format("CRI_RESULTS touched_buckets=%u of possible=%u", #sorted, (0x1000000 - 0xfc7000 + (0xfc5fff - 0xfc5020 + 1)) // BUCKET_SIZE))
for _, e in ipairs(sorted) do
  print(string.format("CRI_BUCKET base=%06X reads=%u writes=%u first_t=%.6f", e.base, e.b.reads, e.b.writes, e.b.first_t))
end

print(string.format("CRI_SUMMARY final_display=\"%s\" t=%.6f", text, now()))
reg.pass("catchall_ram_inventory", string.format("touched_buckets=%u", #sorted))
