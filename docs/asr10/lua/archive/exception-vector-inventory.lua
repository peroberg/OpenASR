-- Del 1 of the MC68302 consolidation task
-- (docs/asr10/investigations/mc68302-consolidation.md): calibrate before
-- guarding.
--
-- First attempt (superseded, kept in this file's history for the
-- methodological finding): tapping PROGRAM SPACE reads over the whole
-- $000000-$0000FF vector table, on the theory that a plain MC68000 (no
-- VBR) always fetches an exception's handler address via a normal
-- longword read at $000000+vector*4 -- true in principle, and
-- irq1-handler-chain-probe.md used exactly this technique successfully,
-- but only for a narrow window around one known crash, not a full clean
-- run. Over a full ~22s boot+load window this produced obvious nonsense:
-- vector 50 alone showed 1,167,945 "fetches", and a uniform baseline of
-- exactly 6 reads appeared across many unrelated vector numbers --
-- proof firmware reuses at least part of this address range for
-- ordinary data (a periodic sweep/scan, unrelated to exception
-- dispatch), not proof of exception activity. No Lua-exposed API exists
-- for MAME's own exception-point mechanism either
-- (device_debug::exceptionpoint_set exists in C++, src/emu/debug/
-- debugcpu.h, but has no luaengine_debug.cpp binding -- only bpset/wpset
-- do). TRAP/internal-exception inventory is therefore [OPEN] -- left
-- unguarded rather than shipped on an unreliable heuristic.
--
-- This version: cpu_space IACK taps for interrupt levels 1-7 (the
-- reliable, already-proven technique this whole project uses for $51/
-- IRQ1/IRQ6), which directly answers "autovektorer, $51" -- the part of
-- Del 1's ask this project's own completion architecture is actually
-- built on.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local cpu_space = cpu.spaces["cpu_space"]
local taps = {}

local function now() return emu.time() end

local levels = {} -- levels[n] = { first_t=, count=, vectors={[v]=count} }

for level = 1, 7 do
  -- cpu_space taps report the even, word-aligned address, not the odd
  -- byte address cpu_space_map() declares (irq1_vector_probe.lua's own
  -- calibration: level 1 IACK reported as offset=$FFFFF2, level 6 as
  -- $FFFFFC -- not F3/FD). An earlier version of this script added a
  -- spurious +1 and got zero hits on every level as a direct result.
  local iack_offset = 0x00fffff0 + (level * 2) -- $FFFFF2=L1, F4=L2, ..., FFFFFE=L7
  taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff, string.format("evi_iack_l%d", level), function(offset, data, mask)
    if offset ~= iack_offset then return nil end
    local t = now()
    local l = levels[level]
    if not l then
      l = { first_t = t, count = 0, vectors = {} }
      levels[level] = l
    end
    l.count = l.count + 1
    local vec = data & 0xff
    l.vectors[vec] = (l.vectors[vec] or 0) + 1
    return nil
  end)
end

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

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

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("exception_vector_inventory", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("EVI_FILE1 t=%.6f", now()))

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("exception_vector_inventory", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("EVI_LOADED t=%.6f", now()))

emu.wait(emu.attotime.from_msec(500))

for level = 1, 7 do
  local l = levels[level]
  if l then
    print(string.format("EVI_LEVEL level=%d first_t=%.6f count=%u", level, l.first_t, l.count))
    for vec, c in pairs(l.vectors) do
      print(string.format("EVI_LEVEL_VECTOR level=%d vector=0x%02X count=%u", level, vec, c))
    end
  else
    print(string.format("EVI_LEVEL level=%d never_fired", level))
  end
end

print(string.format("EVI_SUMMARY final_display=\"%s\" t=%.6f", text, now()))
reg.pass("exception_vector_inventory", "see EVI_LEVEL lines")
