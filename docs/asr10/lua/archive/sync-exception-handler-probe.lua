-- Del 1 of the MC68302 consolidation follow-up
-- (docs/asr10/investigations/mc68302-consolidation-2.md): close [OPEN]
-- for synchronous (non-interrupt) exceptions.
--
-- exceptionpoint_set is not Lua-reachable, and a tap on the vector table
-- itself drowns in data traffic (mc68302-consolidation.md: one vector
-- "fired" 1,167,945 times). But the HANDLERS are reachable: an
-- instruction fetch is a read in program space, so a read tap on a
-- handler's own entry address fires exactly when that handler actually
-- executes, without the vector-table's contamination problem (code
-- memory is not the periodically-reused low-RAM region the vector table
-- turned out to be).
--
-- Method: read the vector table at a known-good moment (after FILE 1,
-- OS init complete) to get handler addresses for vectors 2 (bus error),
-- 3 (address error), 4 (illegal instruction), 8 (privilege violation),
-- 10/11 (line A/F emulator), install a read tap on each handler's first
-- word, and track hits (aggregated, first occurrence). Also periodically
-- re-reads the vector table to detect handler relocation and re-arms if
-- it changes.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

local VECTORS = {
  [2] = "bus_error", [3] = "address_error", [4] = "illegal_instruction",
  [8] = "privilege_violation", [10] = "line_a_emulator", [11] = "line_f_emulator",
}

local handler_addr = {} -- handler_addr[vecnum] = current handler address
local hit_count = {}    -- hit_count[vecnum] = count
local hit_first_t = {}  -- hit_first_t[vecnum] = first hit time
local handler_taps = {} -- handler_taps[vecnum] = tap handle (replaced on re-arm)
local relocations = {}  -- list of {vecnum, t, old, new}

local function read_vector_table()
  -- Masked to the CPU's real 24-bit address bus (calibration caught this
  -- via MAME's own error: an unmasked vector-table read can carry a
  -- nonzero top byte, e.g. $FFF882AE for a real handler at $F882AE, and
  -- installing a tap on the unmasked address fails outright).
  local table_now = {}
  for vecnum in pairs(VECTORS) do
    table_now[vecnum] = prog:read_u32(vecnum * 4) & 0x00ffffff
  end
  return table_now
end

local function arm_handler_tap(vecnum, addr)
  if handler_taps[vecnum] then
    -- Lua taps have no explicit remove API used elsewhere in this
    -- project; the old tap is simply dropped from the table (and thus
    -- eligible for GC) once no longer referenced, matching how this
    -- project already treats tap-handle lifetime (methods-static-
    -- analysis.md #8.6) -- the NEW tap below replaces it in `taps`.
    handler_taps[vecnum] = nil
  end
  local t = prog:install_read_tap(addr, addr + 1, string.format("sehp_handler_%d", vecnum), function(offset, data, mask)
    if not hit_first_t[vecnum] then hit_first_t[vecnum] = now() end
    hit_count[vecnum] = (hit_count[vecnum] or 0) + 1
    return nil
  end)
  handler_taps[vecnum] = t
  taps[#taps + 1] = t
end

local function install_all_handler_taps(label)
  local table_now = read_vector_table()
  local any_change = false
  for vecnum, name in pairs(VECTORS) do
    local addr = table_now[vecnum]
    local prev = handler_addr[vecnum]
    if prev == nil then
      -- first install
      handler_addr[vecnum] = addr
      arm_handler_tap(vecnum, addr)
      any_change = true
    elseif prev ~= addr then
      -- real relocation: re-arm only now, not on every periodic check --
      -- re-arming on an unchanged address would stack a fresh redundant
      -- tap at the same address every recheck and inflate hit counts.
      relocations[#relocations + 1] = { vecnum = vecnum, t = now(), old = prev, new = addr }
      print(string.format("SEHP_RELOCATION vector=%d(%s) t=%.6f old=%08X new=%08X", vecnum, name, now(), prev, addr))
      handler_addr[vecnum] = addr
      arm_handler_tap(vecnum, addr)
      any_change = true
    end
  end
  if any_change then
    print(string.format("SEHP_TABLE_SNAPSHOT label=%s t=%.6f", label, now()))
    for vecnum, name in pairs(VECTORS) do
      print(string.format("SEHP_HANDLER vector=%d(%s) addr=%08X", vecnum, name, handler_addr[vecnum]))
    end
  end
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

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("sync_exception_handler_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("SEHP_FILE1 t=%.6f", now()))

-- Known-good moment: OS init complete. Install taps now.
install_all_handler_taps("post_file1")

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("sync_exception_handler_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("SEHP_LOADED t=%.6f", now()))

-- Re-check the table periodically through to the end, re-arming on any
-- change (answers "does the table change during the run" directly,
-- rather than assuming either way).
local deadline = now() + 8
while now() < deadline do
  emu.wait(emu.attotime.from_msec(500))
  install_all_handler_taps("periodic_recheck")
end

print("SEHP_RESULTS vector name addr hits first_t")
for vecnum, name in pairs(VECTORS) do
  print(string.format("SEHP_RESULT vector=%d(%s) addr=%08X hits=%u first_t=%s",
    vecnum, name, handler_addr[vecnum], hit_count[vecnum] or 0,
    hit_first_t[vecnum] and string.format("%.6f", hit_first_t[vecnum]) or "never"))
end
print(string.format("SEHP_RELOCATIONS count=%u", #relocations))
print(string.format("SEHP_SUMMARY final_display=\"%s\" t=%.6f", text, now()))
reg.pass("sync_exception_handler_probe", string.format("relocations=%u", #relocations))
