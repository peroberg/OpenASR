-- Phase 5B: observe the firmware heap and the L+R recording allocation.
-- This probe only reads state and supplies the panel sequence under test.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog
local state = cpu.state
local taps = {}
local source_mode = _G.RECORD_STEREO_ALLOCATOR_SOURCE_MODE or 2
local measuring = false
local witness_writes = 0
local heap_writes = {}
local plan_writes = {}
local path_events = {}
local allocator_events = {}

local aliases = {
  PC = state["CURPC"] or state["GENPC"] or state["PC"],
  A7 = state["A7"] or state["SP"] or state["GENSP"],
}

local function value(name)
  local item = aliases[name] or state[name]
  return item and (item.value & 0xffffffff) or 0xffffffff
end

local function pc() return value("PC") & 0x00ffffff end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local function hex_bytes(address, count)
  local bytes = {}
  for offset = 0, count - 1 do
    bytes[#bytes + 1] = string.format("%02X", prog:read_u8(address + offset) & 0xff)
  end
  return table.concat(bytes, "")
end

local function decode_header(header)
  local decoded = (header & 0xffff0000) | ((header & 0x0000ffff) >> 4)
  decoded = ((decoded & 0xffff) << 16) | ((decoded >> 16) & 0xffff)
  decoded = decoded >> 4
  return decoded & 0xfffffffe, decoded & 1
end

local function add_path(label, address)
  local a0 = value("A0") & 0x00ffffff
  local a1 = value("A1") & 0x00ffffff
  local a2 = value("A2") & 0x00ffffff
  path_events[#path_events + 1] = string.format(
    "%s@%06X:D0=%08X,D1=%08X,D2=%08X,D4=%08X,A0=%06X,A1=%06X,A2=%06X,BD6=%08X,C10=%08X,BA4=%04X,D484=%04X,D486=%04X",
    label, address, value("D0"), value("D1"), value("D2"), value("D4"),
    a0, a1, a2, prog:read_u32(0x0bd6), prog:read_u32(0x0c10),
    prog:read_u16(0x0ba4), prog:read_u16(0xd484), prog:read_u16(0xd486))
end

taps[#taps + 1] = prog:install_write_tap(0x000100, 0x00ffff,
  "record_stereo_allocator_witness", function()
    if measuring then witness_writes = witness_writes + 1 end
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(0x000c4e, 0x000c7f,
  "record_stereo_allocator_heap_globals", function(offset, data, mask)
    heap_writes[#heap_writes + 1] = string.format(
      "addr=%04X,data=%08X,mask=%08X,pc=%06X", offset, data & 0xffffffff,
      mask & 0xffffffff, pc())
    return nil
  end)

for first, last in pairs({ [0x000bd6] = 0x000bd9, [0x000c10] = 0x000c13,
    [0x00d470] = 0x00d487 }) do
  taps[#taps + 1] = prog:install_write_tap(first, last,
    "record_stereo_allocator_plan_" .. string.format("%06x", first),
    function(offset, data, mask)
      if measuring then
        plan_writes[#plan_writes + 1] = string.format(
          "addr=%06X,data=%08X,mask=%08X,pc=%06X", offset,
          data & 0xffffffff, mask & 0xffffffff, pc())
      end
      return nil
    end)
end

for address, label in pairs({
  [0x00f8a166] = "memory_probe_entry",
  [0x00f8a1f4] = "memory_probe_zero_branch",
  [0x00f8a210] = "memory_probe_f8m_choice",
  [0x000174b4] = "record_capacity_call",
  [0x000174d2] = "stereo_object_before_extent",
  [0x000174dc] = "stereo_extent_field_write",
  [0x000174ec] = "first_extent_call",
  [0x000174f4] = "companion_extent_call",
  [0x00f8b1f0] = "extent_allocator_entry",
  [0x00f8b238] = "extent_scan",
  [0x00f8b250] = "packed_allocator_call",
}) do
  taps[#taps + 1] = opcodes:install_read_tap(address, address + 1,
    "record_stereo_allocator_path_" .. label, function()
      if measuring or address < 0x00f8a240 then add_path(label, address) end
      return nil
    end)
end

taps[#taps + 1] = opcodes:install_read_tap(0x00f8a44e, 0x00f8a44f,
  "record_stereo_allocator_entry", function()
    if measuring then
      local sp = value("A7") & 0x00ffffff
      allocator_events[#allocator_events + 1] = string.format(
        "D1=%08X,A0=%06X,H=%08X,SP=%06X,RET=%06X",
        value("D1"), value("A0") & 0x00ffffff,
        prog:read_u32(value("A0") & 0x00ffffff), sp,
        prog:read_u32(sp) & 0x00ffffff)
    end
    return nil
  end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("record_stereo_allocator", "boot_timeout display=\"" .. text .. "\"")
  return
end

press_button(0x20, 500)
for _ = 1, source_mode do press_button(0x0a, 250) end
if prog:read_u8(0x016f) ~= source_mode then
  reg.fail("record_stereo_allocator", string.format("source_mode=%u", prog:read_u8(0x016f)))
  return
end

measuring = true
press_button(0x02, 1000)
measuring = false

local blocks = {}
local cursor = prog:read_u32(0x0c5e) & 0x00ffffff
local heap_end = prog:read_u32(0x0c66) & 0x00ffffff
for _ = 1, 128 do
  if cursor == heap_end then
    blocks[#blocks + 1] = string.format("END=%06X", cursor)
    break
  end
  local header = prog:read_u32(cursor)
  local size, free = decode_header(header)
  local prefix = (cursor <= 0x1fffff and size > 0)
    and hex_bytes(cursor, math.min(size, 0x40)) or ""
  blocks[#blocks + 1] = string.format("%06X:H=%08X,S=%06X,F=%u,P=%s",
    cursor, header, size, free, prefix)
  if size == 0 or (size & 0xf) ~= 0 or cursor + size > 0x00ffffff then break end
  cursor = cursor + size
end

local object = prog:read_u32(0x0c10) & 0x00ffffff
print(string.format(
  "STEREO_ALLOC_RESULT source=%u state=%04X error=%04X witness=%u display=\"%s\"",
  source_mode, prog:read_u16(0x0d04), prog:read_u16(0x00c0), witness_writes,
  display.read_raw()))
print(string.format(
  "STEREO_ALLOC_ROOTS C4E=%08X C52=%08X C5E=%08X C62=%08X C66=%08X C6A=%08X C6E=%08X BD6=%08X C10=%08X",
  prog:read_u32(0x0c4e), prog:read_u32(0x0c52), prog:read_u32(0x0c5e),
  prog:read_u32(0x0c62), prog:read_u32(0x0c66), prog:read_u32(0x0c6a),
  prog:read_u32(0x0c6e), prog:read_u32(0x0bd6), prog:read_u32(0x0c10)))
print("STEREO_ALLOC_HEAP_WRITES " .. table.concat(heap_writes, "|"))
print("STEREO_ALLOC_PLAN_WRITES " .. table.concat(plan_writes, "|"))
print("STEREO_ALLOC_PATH " .. table.concat(path_events, "|"))
print("STEREO_ALLOC_CALLS " .. table.concat(allocator_events, "|"))
print("STEREO_ALLOC_BLOCKS " .. table.concat(blocks, "|"))
print(string.format("STEREO_ALLOC_OBJECT addr=%06X bytes=%s", object,
  hex_bytes(object, 0x120)))
print("STEREO_ALLOC_HIGH_CODE addr=FFBE80 bytes=" .. hex_bytes(0xffbe80, 0x100))

if source_mode == 2 and prog:read_u16(0x00c0) == 0x0039 and witness_writes > 0 then
  reg.pass("record_stereo_allocator", "stereo_error57_with_live_heap_witness")
elseif source_mode == 0 and prog:read_u16(0x0d04) == 1 and witness_writes > 0 then
  reg.pass("record_stereo_allocator", "left_reached_level_detect")
else
  reg.fail("record_stereo_allocator", "unexpected_destination_result")
end
