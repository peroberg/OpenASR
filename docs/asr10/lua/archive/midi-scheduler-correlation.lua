-- Del 2 (keyboard-and-sample-bridge-4.md): given Del 1's result (MIDI
-- note-on reaches RHRA -- 3 reads, exactly the 3-byte message -- but
-- produces zero ES5506/samram writes), find out whether MIDI reception
-- wakes the SAME six-slot scheduler the panel path does, and if so
-- which slot and via what PC. This determines whether MIDI and panel
-- converge on a shared blocker (strengthening the case for a single
-- downstream cause) or diverge (MIDI silently dropped even earlier than
-- the panel path ever reached).
--
-- Reuses the six-slot scheduler technique from
-- scheduler-key-press-correlation.lua (keyboard-and-sample-bridge-3.md),
-- substituting a MIDI note-on load for a key press as the stimulus.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local a1_state = cpu.state["A1"]
local a2_state = cpu.state["A2"]
local a5_state = cpu.state["A5"]
local taps = {}
local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function a1() return a1_state and (a1_state.value & 0x00ffffff) or 0xffffffff end
local function a2() return a2_state and (a2_state.value & 0x00ffffff) or 0xffffffff end
local function a5() return a5_state and (a5_state.value & 0x00ffffff) or 0xffffffff end

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

local function press_button(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("midi_scheduler_correlation", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("midi_scheduler_correlation", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

-- Re-read base/limit fresh, independently (per this project's own rule:
-- never assume a prior task's addresses without re-deriving).
local base = prog:read_u16(0x0000c6) & 0xffff
local limit = prog:read_u16(0x0000c8) & 0xffff
local nslots = (limit - base) // 0x16
print(string.format("MSC_TABLE base=%04X limit=%04X slots=%u", base, limit, nslots))

local function slot_addr(i) return base + i * 0x16 end
local function in_slot_range(addr)
  if addr < base or addr >= limit then return nil end
  local rel = addr - base
  if rel % 0x16 ~= 0 then return "MISALIGNED(" .. rel .. ")" end
  return rel // 0x16
end

-- 1. Watch all six slots' +2/+3 (pending flags) and +6 (saved PC).
local slot_writes = {}
for i = 0, nslots - 1 do
  local base_addr = slot_addr(i)
  taps[#taps + 1] = prog:install_write_tap(base_addr, base_addr + 0x15, string.format("msc_slot%d_w", i), function(offset, data, mask)
    local rel = offset - base_addr
    slot_writes[#slot_writes + 1] = { t = now(), slot = i, rel = rel, data = data, mask = mask, pc = pc() }
    return nil
  end)
end

-- 2. RHRA reads with PC (who is consuming the MIDI bytes).
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local rhra_reads = {}
taps[#taps + 1] = prog:install_read_tap(0x00fc4806, 0x00fc4807, "msc_rhra_r", function(offset, data, mask)
  if byte_address(offset, mask) == 0x00fc4807 then
    rhra_reads[#rhra_reads + 1] = { t = now(), pc = pc() }
  end
  return nil
end)

-- 3. TRAP #9 entry (A1 = target scheduler slot address) and TRAP #6
-- entry -- both previously identified as scheduler-adjacent primitives.
local trap9_entries = {}
taps[#taps + 1] = prog:install_read_tap(0x00f88138, 0x00f88139, "msc_trap9_entry", function(offset, data, mask)
  trap9_entries[#trap9_entries + 1] = { t = now(), a1 = a1(), a5 = a5() }
  return nil
end)
local trap6_entries = {}
taps[#taps + 1] = prog:install_read_tap(0x00f880d6, 0x00f880d7, "msc_trap6_entry", function(offset, data, mask)
  trap6_entries[#trap6_entries + 1] = { t = now() }
  return nil
end)

-- 4. Dispatch/RTE point ($F87FC0, A2 = slot being dispatched).
local dispatch_events = {}
taps[#taps + 1] = prog:install_read_tap(0x00f87fc0, 0x00f87fc1, "msc_dispatch", function(offset, data, mask)
  dispatch_events[#dispatch_events + 1] = { t = now(), a2 = a2() }
  return nil
end)

-- 5. IRQ6 taken (DUART irq_cb -> inputline 6) -- cpu_space IACK tap on
-- level 6, per the project's established exception-guard technique
-- (asr10_guards.lua's install_exception_guard).
local cpu_space = cpu.spaces["cpu_space"]
local irq6_acks = {}
if cpu_space then
  -- IACK convention established in sync-exception-handler-probe.lua:
  -- reported address is the even word $FFFFF0 + level*2, not level*2+1.
  -- Level 6 -> $FFFFFC.
  taps[#taps + 1] = cpu_space:install_read_tap(0xfffffc, 0xfffffd, "msc_irq6_iack", function(offset, data, mask)
    irq6_acks[#irq6_acks + 1] = { t = now(), pc = pc() }
    return nil
  end)
end

local function snapshot_slots(label)
  for i = 0, nslots - 1 do
    local addr = slot_addr(i)
    local b2 = prog:read_u8(addr + 2) & 0xff
    local b3 = prog:read_u8(addr + 3) & 0xff
    local pcfield = prog:read_u32(addr + 6) & 0x00ffffff
    print(string.format("MSC_SLOT label=%s slot=%d addr=%04X b2=%02X b3=%02X pending=%s pcfield=%06X",
      label, i, addr, b2, b3, tostring(b2 ~= b3), pcfield))
  end
end

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then
    mdin_image = img
  end
end
if not mdin_image then
  reg.fail("midi_scheduler_correlation", "no_mdin_image_device_found")
  return
end

snapshot_slots("before")

print(string.format("MSC_NOTE_ON t=%.6f", now()))
local load_err = mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
print(string.format("MSC_LOAD_RESULT error=%s", tostring(load_err)))
emu.wait(emu.attotime.from_msec(1500))

snapshot_slots("after")

print(string.format("MSC_RHRA_READS count=%u", #rhra_reads))
for _, e in ipairs(rhra_reads) do
  print(string.format("MSC_RHRA_READ t=%.6f pc=%06X", e.t, e.pc))
end

print(string.format("MSC_SLOT_WRITES count=%u", #slot_writes))
for _, w in ipairs(slot_writes) do
  print(string.format("MSC_SLOT_WRITE t=%.6f slot=%d rel=+%u data=%08X mask=%08X pc=%06X", w.t, w.slot, w.rel, w.data, w.mask, w.pc))
end

print(string.format("MSC_TRAP9_ENTRIES count=%u", #trap9_entries))
for _, e in ipairs(trap9_entries) do
  local slotnum = in_slot_range(e.a1)
  print(string.format("MSC_TRAP9_ENTRY t=%.6f a1=%06X in_slot_range=%s a5=%06X", e.t, e.a1, tostring(slotnum), e.a5))
end

print(string.format("MSC_TRAP6_ENTRIES count=%u", #trap6_entries))
for _, e in ipairs(trap6_entries) do
  print(string.format("MSC_TRAP6_ENTRY t=%.6f", e.t))
end

print(string.format("MSC_DISPATCH_EVENTS count=%u", #dispatch_events))
for _, e in ipairs(dispatch_events) do
  local slotnum = in_slot_range(e.a2)
  print(string.format("MSC_DISPATCH t=%.6f a2=%06X in_slot_range=%s", e.t, e.a2, tostring(slotnum)))
end

print(string.format("MSC_IRQ6_ACKS count=%u", #irq6_acks))
for _, e in ipairs(irq6_acks) do
  print(string.format("MSC_IRQ6_ACK t=%.6f pc=%06X", e.t, e.pc))
end

print(string.format("MSC_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("midi_scheduler_correlation", string.format("rhra=%u slot_writes=%u trap9=%u trap6=%u dispatch=%u irq6=%u",
  #rhra_reads, #slot_writes, #trap9_entries, #trap6_entries, #dispatch_events, #irq6_acks))
