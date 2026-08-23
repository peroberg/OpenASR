-- Phase 5A: localize the L+R System Error 57 producer. This is observation-only;
-- the only inputs are the same panel presses used by the Phase 4B run.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog
local state = cpu.state
local taps = {}
local source_mode = _G.RECORD_COMPLETION_SOURCE_MODE or 2
local measuring = false
local trap = nil
local writes = {}
local witness_writes = 0
local allocator_events = {}
local layer_events = {}
local gap_writes = {}

local function now() return emu.time() end
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

local before = {}
for address = 0x0100, 0xffff do before[address] = prog:read_u8(address) & 0xff end

taps[#taps + 1] = prog:install_write_tap(0x000100, 0x00ffff,
  "record_completion_error57_witness", function(offset, data, mask)
    if not measuring then return nil end
    witness_writes = witness_writes + 1
    local item = writes[offset] or { count = 0 }
    item.count = item.count + 1
    item.pc = pc()
    item.data = data & 0xffffffff
    item.mask = mask & 0xffffffff
    writes[offset] = item
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(0x7ce510, 0x7ce513,
  "record_completion_stereo_heap_header", function(offset, data, mask)
    if measuring then
      gap_writes[#gap_writes + 1] = string.format(
        "addr=%06X,data=%08X,mask=%08X,pc=%06X", offset, data & 0xffffffff,
        mask & 0xffffffff, pc())
    end
    return nil
  end)

for address, name in pairs({
  [0x00f88280] = "hard_error_handler",
  [0x00f89d46] = "error_formatter",
  [0x00f95eb2] = "record_destination_setup",
}) do
  taps[#taps + 1] = opcodes:install_read_tap(address, address + 1,
    "record_completion_error57_" .. name, function()
      if name ~= "hard_error_handler" or trap then return nil end
      local sp = value("A7") & 0x00ffffff
      local return_pc = prog:read_u32(sp + 2) & 0x00ffffff
      trap = { code = value("D0") & 0xffff, sp = sp, return_pc = return_pc }
      local regs = {}
      for index = 0, 7 do regs[#regs + 1] = string.format("D%u=%08X", index, value("D" .. index)) end
      for index = 0, 7 do regs[#regs + 1] = string.format("A%u=%08X", index, value("A" .. index)) end
      print(string.format(
        "RECORD57_TRAP t=%.6f code=%04X handler_pc=%06X saved_sr=%04X return_pc=%06X trap_pc=%06X %s",
        now(), trap.code, pc(), prog:read_u16(sp) & 0xffff, return_pc,
        (return_pc - 2) & 0x00ffffff, table.concat(regs, " ")))
      print(string.format("RECORD57_STACK sp=%06X bytes=%s", sp, hex_bytes(sp, 96)))
      local usp = value("USP") & 0x00ffffff
      print(string.format("RECORD57_USP_STACK usp=%06X bytes=%s", usp, hex_bytes(usp, 160)))
      local slots = {}
      for address = 0x0100, 0xffff - 3, 2 do
        if (prog:read_u32(address) & 0x00ffffff) == 0x00f8a44e then
          slots[#slots + 1] = string.format("%04X", address)
        end
      end
      print("RECORD57_ALLOCATOR_SLOTS " .. table.concat(slots, ","))
      print("RECORD57_CALLER_CODE start=017400 bytes=" .. hex_bytes(0x017400, 0x180))
      print("RECORD57_PARENT_CODE start=003F80 bytes=" .. hex_bytes(0x003f80, 0x180))
      print("RECORD57_SLOT_CODE start=008960 bytes=" .. hex_bytes(0x008960, 0x30))
      print("RECORD57_HIGH_SLOT_CODE start=FF8960 bytes=" .. hex_bytes(0x00ff8960, 0x300))
      print(string.format("RECORD57_CODE start=%06X bytes=%s",
        (return_pc - 66) & 0x00ffffff, hex_bytes((return_pc - 66) & 0x00ffffff, 160)))
      return nil
    end)
end


for address, name in pairs({
  [0x000174ec] = "left_layer_call",
  [0x000174f4] = "right_layer_call",
  [0x00f8b1f0] = "layer_record_entry",
  [0x00f8b200] = "record_offset_ready",
  [0x00f8b238] = "record_scan_done",
  [0x00f8b250] = "record_allocator_call",
}) do
  taps[#taps + 1] = opcodes:install_read_tap(address, address + 1,
    "record_completion_layer_" .. name, function()
      if not measuring then return nil end
      local a0 = value("A0") & 0x00ffffff
      local a1 = value("A1") & 0x00ffffff
      layer_events[#layer_events + 1] = string.format(
        "%s@%06X:D0=%08X,D1=%08X,D2=%08X,A0=%06X,H0=%08X,A1=%06X,H1=%08X,BA4=%04X,D486=%04X",
        name, address, value("D0"), value("D1"), value("D2"), a0,
        prog:read_u32(a0), a1, prog:read_u32(a1), prog:read_u16(0x0ba4),
        prog:read_u16(0xd486))
      return nil
    end)
end

for address, name in pairs({
  [0x00f8a44e] = "allocator_entry",
  [0x00f8a4aa] = "allocator_scan_end",
  [0x00f8a4b4] = "candidate_pointer_check",
  [0x00f8a4c6] = "candidate_header_check",
  [0x00f8a4da] = "requested_size_check",
  [0x00f8a55e] = "allocator_hard_error",
}) do
  taps[#taps + 1] = opcodes:install_read_tap(address, address + 1,
    "record_completion_allocator_" .. name, function()
      if not measuring then return nil end
      local a0 = value("A0") & 0x00ffffff
      allocator_events[#allocator_events + 1] = string.format(
        "%s@%06X:D0=%08X,D1=%08X,A0=%06X,H=%08X,SP=%06X,USP=%06X",
        name, address, value("D0"), value("D1"), a0, prog:read_u32(a0),
        value("A7") & 0x00ffffff, value("USP") & 0x00ffffff)
      while #allocator_events > 24 do table.remove(allocator_events, 1) end
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then reg.fail("record_completion_error57", "boot_timeout display=\"" .. text .. "\""); return end

press_button(0x20, 500)
for _ = 1, source_mode do press_button(0x0a, 250) end
if prog:read_u8(0x016f) ~= source_mode then
  reg.fail("record_completion_error57", string.format("source_mode=%u", prog:read_u8(0x016f)))
  return
end

for address = 0x0100, 0xffff do before[address] = prog:read_u8(address) & 0xff end
measuring = true
press_button(0x02, 1000)
measuring = false

local changed = {}
for address = 0x0100, 0xffff do
  local after = prog:read_u8(address) & 0xff
  if before[address] ~= after then
    local item = writes[address] or writes[address & 0xfffffe] or {}
    changed[#changed + 1] = string.format("%04X:%02X>%02X@%06Xx%u",
      address, before[address], after, item.pc or 0, item.count or 0)
  end
end
print(string.format(
  "RECORD57_RESULT source=%u state=%04X error_word=%04X trap=%u code=%04X return_pc=%06X changed=%u witness=%u display=\"%s\"",
  prog:read_u8(0x016f), prog:read_u16(0x0d04), prog:read_u16(0x00c0),
  trap and 1 or 0, trap and trap.code or 0, trap and trap.return_pc or 0,
  #changed, witness_writes, display.read_raw()))
print("RECORD57_CHANGED " .. table.concat(changed, " "))
print("RECORD57_ALLOCATOR " .. table.concat(allocator_events, " | "))
print("RECORD57_LAYER " .. table.concat(layer_events, " | "))
print(string.format("RECORD57_GAP writes=%u final=%08X events=%s",
  #gap_writes, prog:read_u32(0x7ce510), table.concat(gap_writes, "|")))
print(string.format("RECORD57_HEAP c5e=%08X c66=%08X c10=%08X bf4=%08X",
  prog:read_u32(0x0c5e), prog:read_u32(0x0c66), prog:read_u32(0x0c10),
  prog:read_u32(0x0bf4)))

if source_mode == 2 and trap and trap.code == 0x0039 and witness_writes > 0
    and display.read_raw():find("ERR0R ?57", 1, true) then
  reg.pass("record_completion_error57", "trap0_code_0039_with_live_witness")
elseif source_mode == 0 and not trap and witness_writes > 0
    and prog:read_u16(0x0d04) == 1 then
  reg.pass("record_completion_error57", "left_control_reached_level_detect")
else
  reg.fail("record_completion_error57", "expected_trap0_code_0039_not_observed")
end
