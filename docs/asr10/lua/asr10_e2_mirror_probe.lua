-- ASR-10 E2 mirror probe.
--
-- Observation only: taps the high RAM alias candidate window during a V3.50
-- boot and records any reads. Passthrough taps observe data reads; opcode fetch
-- visibility depends on whether the CPU exposes an opcode space to Lua.

local HI_LO = 0x00FF8000
local HI_HI = 0x00FFFFFF
local MAX_LINES = 512

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"]

local state_pc = cpu.state["CURPC"] or cpu.state["GENPC"] or cpu.state["PC"]

local e2 = {
  total = 0,
  data_reads = 0,
  instr_reads = 0,
  lines = 0,
  hist = {},
  first = nil,
  opcode_tap_installed = false,
}

local function pc_value()
  if state_pc then
    return state_pc.value & 0x00ffffff
  end
  return 0xffffffff
end

local function byte_value(data, mask)
  if (mask & 0x00ff) ~= 0 then
    return data & 0xff, "8"
  end
  return (data >> 8) & 0xff, "8"
end

local function note(kind, address, data, mask)
  e2.total = e2.total + 1
  if kind == "instruction" then
    e2.instr_reads = e2.instr_reads + 1
  else
    e2.data_reads = e2.data_reads + 1
  end

  local pc = pc_value()
  local value, width = byte_value(data, mask)
  local low = address & 0x0000ffff
  local key = string.format("%06X", address)
  e2.hist[key] = (e2.hist[key] or 0) + 1
  if not e2.first then
    e2.first = { kind = kind, address = address, low = low, pc = pc, value = value }
  end

  if e2.lines < MAX_LINES then
    e2.lines = e2.lines + 1
    local low_value = prog:read_u8(low)
    print(string.format(
      "E2_ACCESS seq=%u kind=%s address=%06X width=%s pc=%06X value=%02X low_address=%06X low_value=%02X match=%u",
      e2.total, kind, address, width, pc, value, low, low_value, value == low_value and 1 or 0))
  end
end

local function install_read_tap(space, name, kind)
  local ok, result = pcall(function()
    return space:install_read_tap(HI_LO, HI_HI, name, function(offset, data, mask)
      note(kind, offset, data, mask)
      return nil
    end)
  end)
  if ok then
    print(string.format("E2_TAP_INSTALLED kind=%s range=%06X-%06X", kind, HI_LO, HI_HI))
    return result
  end
  print(string.format("E2_TAP_FAILED kind=%s error=\"%s\"", kind, tostring(result)))
  return nil
end

e2.program_tap = install_read_tap(prog, "asr10_e2_data_r", "data")
if opcodes then
  e2.opcode_tap = install_read_tap(opcodes, "asr10_e2_opcode_r", "instruction")
  e2.opcode_tap_installed = e2.opcode_tap ~= nil
else
  print("E2_OPCODE_SPACE missing")
end

e2.stop_notifier = emu.add_machine_stop_notifier(function()
  print(string.format(
    "E2_SUMMARY total=%u data_reads=%u instruction_reads=%u opcode_tap_installed=%u lines_emitted=%u max_lines=%u",
    e2.total, e2.data_reads, e2.instr_reads, e2.opcode_tap_installed and 1 or 0,
    e2.lines, MAX_LINES))

  if e2.first then
    print(string.format(
      "E2_FIRST kind=%s address=%06X pc=%06X low_address=%06X value=%02X",
      e2.first.kind, e2.first.address, e2.first.pc, e2.first.low, e2.first.value))
  end

  local keys = {}
  for key in pairs(e2.hist) do
    keys[#keys + 1] = key
  end
  table.sort(keys, function(a, b)
    if e2.hist[a] == e2.hist[b] then
      return a < b
    end
    return e2.hist[a] > e2.hist[b]
  end)

  local limit = math.min(#keys, 32)
  for i = 1, limit do
    print(string.format("E2_HIST rank=%u address=%s count=%u", i, keys[i], e2.hist[keys[i]]))
  end
end)
