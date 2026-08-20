-- Del 0: written and measured independently this session; no fork
-- material used as a starting assumption. Del 1+2 of the consumer
-- investigation (docs/asr10/investigations/keyboard-and-sample-bridge-3.md).
--
-- Watches the six scheduler slots (base/limit re-read fresh from lowmem
-- $C6/$C8, not assumed), $B6C (TRAP #3/#4's own pointer), A5 at the
-- moment TRAP #3 returns, and the dispatch/RTE point at $F87FC0
-- (A2 = the slot being dispatched), all correlated with a real key
-- press after FILE LOADED.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local a5_state = cpu.state["A5"]
local a2_state = cpu.state["A2"]
local taps = {}
local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function a5() return a5_state and (a5_state.value & 0x00ffffff) or 0xffffffff end
local function a2() return a2_state and (a2_state.value & 0x00ffffff) or 0xffffffff end

local function press(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("scheduler_key_press_correlation", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
press(":panel:buttons_0", 1 << 0x0a)
press(":panel:buttons_32", 1 << 0x03)
press(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("scheduler_key_press_correlation", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

-- Re-read base/limit fresh, independently.
local base = prog:read_u16(0x0000c6) & 0xffff
local limit = prog:read_u16(0x0000c8) & 0xffff
local nslots = (limit - base) // 0x16
print(string.format("SKPC_TABLE base=%04X limit=%04X slots=%u", base, limit, nslots))

local function slot_addr(i) return base + i * 0x16 end
local function in_slot_range(addr)
  if addr < base or addr >= limit then return nil end
  local rel = addr - base
  if rel % 0x16 ~= 0 then return "MISALIGNED(" .. rel .. ")" end
  return rel // 0x16
end

-- 1. Watch all six slots' +2/+3 (pending flags) and +6 (saved PC, 4
-- bytes) for writes.
local slot_writes = {}
for i = 0, nslots - 1 do
  local base_addr = slot_addr(i)
  taps[#taps + 1] = prog:install_write_tap(base_addr, base_addr + 0x15, string.format("skpc_slot%d_w", i), function(offset, data, mask)
    local rel = offset - base_addr
    slot_writes[#slot_writes + 1] = { t = now(), slot = i, rel = rel, data = data, mask = mask, pc = pc() }
    return nil
  end)
end

-- 2. Watch $B6C (TRAP #3/#4's own pointer) for writes.
local b6c_writes = {}
taps[#taps + 1] = prog:install_write_tap(0x00000b6c, 0x00000b6d, "skpc_b6c_w", function(offset, data, mask)
  b6c_writes[#b6c_writes + 1] = { t = now(), data = data, mask = mask, pc = pc() }
  return nil
end)

-- 3. Watch $F880A2 (TRAP #4's entry -- A5 at that moment is the node
-- TRAP #3 handed back, per the previous task's disassembly:
-- $FFB43E stores into (4,A5)/(6,A5) between the traps, so A5 is stable
-- across that whole window) and $F87FD2 (the routine called between the
-- traps) for whether the node address falls inside the slot table.
local trap4_entries = {}
taps[#taps + 1] = prog:install_read_tap(0x00f880a2, 0x00f880a3, "skpc_trap4_entry", function(offset, data, mask)
  trap4_entries[#trap4_entries + 1] = { t = now(), a5 = a5(), pc = pc() }
  return nil
end)
local f87fd2_entries = {}
taps[#taps + 1] = prog:install_read_tap(0x00f87fd2, 0x00f87fd3, "skpc_f87fd2_entry", function(offset, data, mask)
  f87fd2_entries[#f87fd2_entries + 1] = { t = now(), a5 = a5(), pc = pc() }
  return nil
end)

-- 4. Watch the dispatch/RTE point ($F87FC0) -- A2 at that moment is the
-- slot being dispatched.
local dispatch_events = {}
taps[#taps + 1] = prog:install_read_tap(0x00f87fc0, 0x00f87fc1, "skpc_dispatch", function(offset, data, mask)
  dispatch_events[#dispatch_events + 1] = { t = now(), a2 = a2() }
  return nil
end)

local function snapshot_slots(label)
  for i = 0, nslots - 1 do
    local addr = slot_addr(i)
    local b2 = prog:read_u8(addr + 2) & 0xff
    local b3 = prog:read_u8(addr + 3) & 0xff
    local pcfield = prog:read_u32(addr + 6) & 0x00ffffff
    print(string.format("SKPC_SLOT label=%s slot=%d addr=%04X b2=%02X b3=%02X pending=%s pcfield=%06X",
      label, i, addr, b2, b3, tostring(b2 ~= b3), pcfield))
  end
end

snapshot_slots("before")
local b6c_before = prog:read_u16(0x00000b6c) & 0xffff
print(string.format("SKPC_B6C_BEFORE value=%04X", b6c_before))

print(string.format("SKPC_KEY_PRESS t=%.6f", now()))
press(":panel:keys_0", 0x00000001, 150) -- KEY_C
emu.wait(emu.attotime.from_msec(1500))

snapshot_slots("after")
local b6c_after = prog:read_u16(0x00000b6c) & 0xffff
print(string.format("SKPC_B6C_AFTER value=%04X changed=%s", b6c_after, tostring(b6c_after ~= b6c_before)))

print(string.format("SKPC_SLOT_WRITES count=%u", #slot_writes))
for _, w in ipairs(slot_writes) do
  print(string.format("SKPC_SLOT_WRITE t=%.6f slot=%d rel=+%u data=%08X mask=%08X pc=%06X", w.t, w.slot, w.rel, w.data, w.mask, w.pc))
end

print(string.format("SKPC_B6C_WRITES count=%u", #b6c_writes))
for _, w in ipairs(b6c_writes) do
  print(string.format("SKPC_B6C_WRITE t=%.6f data=%08X mask=%08X pc=%06X", w.t, w.data, w.mask, w.pc))
end

print(string.format("SKPC_TRAP4_ENTRIES count=%u", #trap4_entries))
for _, e in ipairs(trap4_entries) do
  local slotnum = in_slot_range(e.a5)
  print(string.format("SKPC_TRAP4_ENTRY t=%.6f a5=%06X in_slot_range=%s", e.t, e.a5, tostring(slotnum)))
end

print(string.format("SKPC_F87FD2_ENTRIES count=%u", #f87fd2_entries))
for _, e in ipairs(f87fd2_entries) do
  local slotnum = in_slot_range(e.a5)
  print(string.format("SKPC_F87FD2_ENTRY t=%.6f a5=%06X in_slot_range=%s", e.t, e.a5, tostring(slotnum)))
end

print(string.format("SKPC_DISPATCH_EVENTS count=%u", #dispatch_events))
for _, e in ipairs(dispatch_events) do
  local slotnum = in_slot_range(e.a2)
  print(string.format("SKPC_DISPATCH t=%.6f a2=%06X in_slot_range=%s", e.t, e.a2, tostring(slotnum)))
end

print(string.format("SKPC_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("scheduler_key_press_correlation", string.format("slot_writes=%u trap4=%u dispatch=%u", #slot_writes, #trap4_entries, #dispatch_events))
