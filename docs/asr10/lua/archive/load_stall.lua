-- ASR-10 live load observer.
--
-- Observation only. This script presses nothing and never exits by itself.
-- Start it with -autoboot_script, wait for FILE 1 in the GUI, press buttons
-- manually, and watch the once-per-second counters.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["CURPC"] or cpu.state["GENPC"] or cpu.state["PC"]

local counts = {
  fdc_4000 = 0,
  fdc_4001 = 0,
  fdc_4003 = 0,
  thrb_w = 0,
  rhrb_r = 0,
  idma = 0,
}

local previous = {
  fdc_4000 = 0,
  fdc_4001 = 0,
  fdc_4003 = 0,
  thrb_w = 0,
  rhrb_r = 0,
  idma = 0,
}

local first_idma_write = nil
local pc_samples = {}
local taps = {}

local function pc()
  if pc_state then
    return pc_state.value & 0x00ffffff
  end
  return 0xffffffff
end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  if (mask & 0x00ff) ~= 0 then
    return data & 0xff
  end
  return (data >> 8) & 0xff
end

local function count_fdc(offset, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4000 then counts.fdc_4000 = counts.fdc_4000 + 1 end
  if address == 0x00fc4001 then counts.fdc_4001 = counts.fdc_4001 + 1 end
  if address == 0x00fc4003 then counts.fdc_4003 = counts.fdc_4003 + 1 end
end

local function count_idma(offset, data, mask, is_write)
  local address = byte_address(offset, mask)
  if address < 0x00fc6840 or address > 0x00fc6870 then
    return
  end
  counts.idma = counts.idma + 1
  if is_write and not first_idma_write then
    first_idma_write = {
      address = address,
      value = byte_value(data, mask),
      pc = pc(),
      time = emu.time(),
    }
    print(string.format("LOAD_STALL_IDMA_FIRST_WRITE t=%.6f address=%06X value=%02X pc=%06X",
      first_idma_write.time, first_idma_write.address, first_idma_write.value,
      first_idma_write.pc))
  end
end

local function install_taps()
  taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "asr10_load_stall_fdc_r", function(offset, data, mask)
    count_fdc(offset, mask)
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "asr10_load_stall_fdc_w", function(offset, data, mask)
    count_fdc(offset, mask)
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc4816, 0x00fc4817, "asr10_load_stall_rhrb_r", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4817 then
      counts.rhrb_r = counts.rhrb_r + 1
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc4816, 0x00fc4817, "asr10_load_stall_thrb_w", function(offset, data, mask)
    if byte_address(offset, mask) == 0x00fc4817 then
      counts.thrb_w = counts.thrb_w + 1
    end
    return nil
  end)

  taps[#taps + 1] = prog:install_read_tap(0x00fc6840, 0x00fc6871, "asr10_load_stall_idma_r", function(offset, data, mask)
    count_idma(offset, data, mask, false)
    return nil
  end)

  taps[#taps + 1] = prog:install_write_tap(0x00fc6840, 0x00fc6871, "asr10_load_stall_idma_w", function(offset, data, mask)
    count_idma(offset, data, mask, true)
    return nil
  end)
end

local function read_word(address)
  return prog:read_u16(address) & 0xffff
end

local function delta(name)
  return counts[name] - previous[name]
end

local function snapshot(seconds)
  local text = display.read_raw()
  print(string.format(
    "LOAD_STALL t=%04d fc4000=%u(+%u) fc4001=%u(+%u) fc4003=%u(+%u) thrb_w=%u(+%u) rhrb_r=%u(+%u) idma=%u(+%u) mem049d=%04X mem04ae=%04X display=\"%s\"",
    seconds,
    counts.fdc_4000, delta("fdc_4000"),
    counts.fdc_4001, delta("fdc_4001"),
    counts.fdc_4003, delta("fdc_4003"),
    counts.thrb_w, delta("thrb_w"),
    counts.rhrb_r, delta("rhrb_r"),
    counts.idma, delta("idma"),
    read_word(0x049d), read_word(0x04ae), text))

  for key in pairs(previous) do
    previous[key] = counts[key]
  end
end

local function remember_pc()
  local now = emu.time()
  pc_samples[#pc_samples + 1] = { time = now, pc = pc() }
  local cutoff = now - 5
  local first = 1
  while pc_samples[first] and pc_samples[first].time < cutoff do
    first = first + 1
  end
  if first > 1 then
    local kept = {}
    for index = first, #pc_samples do
      kept[#kept + 1] = pc_samples[index]
    end
    pc_samples = kept
  end
end

local function print_pc_profile(seconds)
  local hist = {}
  for _, sample in ipairs(pc_samples) do
    hist[sample.pc] = (hist[sample.pc] or 0) + 1
  end

  local keys = {}
  for key in pairs(hist) do
    keys[#keys + 1] = key
  end
  table.sort(keys, function(a, b)
    if hist[a] == hist[b] then
      return a < b
    end
    return hist[a] > hist[b]
  end)

  print(string.format("LOAD_STALL_PC_PROFILE t=%04d window_seconds=5 samples=%u distinct_pc=%u",
    seconds, #pc_samples, #keys))
  for rank = 1, math.min(#keys, 20) do
    local address = keys[rank]
    local count = hist[address]
    print(string.format("LOAD_STALL_PC t=%04d rank=%02u pc=%06X count=%u pct=%.2f",
      seconds, rank, address, count, count * 100.0 / math.max(#pc_samples, 1)))
  end
end

install_taps()
print("LOAD_STALL passive_observer=1 press_sequence_manually=1A,23,02")

local seconds = 0
snapshot(seconds)
while true do
  for _ = 1, 100 do
    remember_pc()
    emu.wait(emu.attotime.from_msec(10))
  end
  seconds = seconds + 1
  snapshot(seconds)
  if seconds % 10 == 0 then
    print_pc_profile(seconds)
  end
end
