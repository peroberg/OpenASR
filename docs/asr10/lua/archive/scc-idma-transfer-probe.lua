-- Phase 4A: feed SCC1 RX bytes only, then observe the model-owned internal
-- IDMA read/write cycles and firmware's normal $4B completion continuation.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local rx_state = cpu.state["SCC1RX"]
local taps = {}
local phase = "boot"
local iack = {}
local paths = {}
local idma_started = false
local idma_completed = false
local source_read_bytes = 0
local destination_write_bytes = 0
local witness_writes = 0
local first_source_read = nil
local last_source_read = nil
local first_destination_write = nil
local last_destination_write = nil
local iack4b_snapshot = nil

local SOURCE_START = 0x00f76606
local SOURCE_END = 0x00f7691f
local DESTINATION_START = 0x0002c110
local DESTINATION_END = 0x0002c429
local TRANSFER_BYTES = 0x031a

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function count(t, key) t[key] = (t[key] or 0) + 1 end
local function access_bytes(mask) return (mask & 0xffff) == 0xffff and 2 or 1 end

local function wait_seconds(seconds)
  emu.wait(emu.attotime.from_seconds(seconds))
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  print(string.format("SCC_IDMA_BUTTON t=%.6f phase=%s code=%02X", now(), phase, code))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local function pattern(offset)
  local value = ((offset & 0x0f) < 2) and 0 or (offset & 0xff)
  if offset == 0x40 then value = 0x7f end
  if offset == 0x41 then value = 0xff end
  return value
end

local function registers(label)
  print(string.format(
    "SCC_IDMA_REGS label=%s t=%.6f CMR=%04X SAPR=%08X DAPR=%08X BCR=%04X CSR=%04X FCR=%04X IPR=%04X IMR=%04X ISR=%04X",
    label, now(), prog:read_u16(0x00fc6802) & 0xffff,
    prog:read_u32(0x00fc6804), prog:read_u32(0x00fc6808),
    prog:read_u16(0x00fc680c) & 0xffff, prog:read_u16(0x00fc680e) & 0xffff,
    prog:read_u16(0x00fc6810) & 0xffff, prog:read_u16(0x00fc6814) & 0xffff,
    prog:read_u16(0x00fc6816) & 0xffff, prog:read_u16(0x00fc6818) & 0xffff))
end

if not rx_state or not rx_state.writeable then
  reg.fail("scc_idma_transfer", "missing_writeable_SCC1RX_state")
  return
end

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("scc_idma_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local vector = data & 0xff
      local key = string.format("L%d:%02X", level, vector)
      count(iack, key)
      if level == 4 then
        print(string.format("SCC_IDMA_IACK t=%.6f phase=%s vector=%02X pc=%06X",
          now(), phase, vector, pc()))
        if vector == 0x4b then
          idma_completed = true
          iack4b_snapshot = {
            cmr = prog:read_u16(0x00fc6802) & 0xffff,
            sapr = prog:read_u32(0x00fc6804),
            dapr = prog:read_u32(0x00fc6808),
            bcr = prog:read_u16(0x00fc680c) & 0xffff,
            csr = prog:read_u16(0x00fc680e) & 0xffff,
            ipr = prog:read_u16(0x00fc6814) & 0xffff,
            imr = prog:read_u16(0x00fc6816) & 0xffff,
            isr = prog:read_u16(0x00fc6818) & 0xffff,
          }
        end
      end
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("scc_idma_transfer", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- Install all taps after firmware's final BAR relocation.
taps[#taps + 1] = prog:install_write_tap(0x00fc6802, 0x00fc6819,
  "scc_idma_regs_w", function(offset, data, mask)
    local address = offset & 0x00fffffe
    if address == 0x00fc6802 and (data & 0xffff) == 0x37a1 then
      idma_started = true
      print(string.format("SCC_IDMA_START t=%.6f pc=%06X data=%04X mask=%04X",
        now(), pc(), data & 0xffff, mask & 0xffff))
    end
    return nil
  end)

taps[#taps + 1] = prog:install_read_tap(SOURCE_START, SOURCE_END,
  "scc_idma_source_r", function(offset, _, mask)
    if idma_started and not idma_completed then
      local bytes = access_bytes(mask)
      source_read_bytes = source_read_bytes + bytes
      first_source_read = first_source_read or offset
      last_source_read = offset + bytes - 1
    end
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(DESTINATION_START, DESTINATION_END,
  "scc_idma_destination_w", function(offset, _, mask)
    if idma_started then
      local bytes = access_bytes(mask)
      destination_write_bytes = destination_write_bytes + bytes
      first_destination_write = first_destination_write or offset
      last_destination_write = offset + bytes - 1
    end
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff,
  "scc_idma_lowram_witness", function()
    witness_writes = witness_writes + 1
    return nil
  end)

local PATHS = {
  [0x00643c] = "scc_rx_common",
  [0x0064ba] = "scc_received_range_continue",
  [0x0065cc] = "waiting_state_3",
  [0x00b478] = "range_to_idma",
  [0x00b4c0] = "idma_start",
  [0x00aa48] = "idma_complete",
}
for address, name in pairs(PATHS) do
  for _, candidate in ipairs({ address, address | 0x00ff0000 }) do
    taps[#taps + 1] = opcodes:install_read_tap(candidate, candidate + 1,
      "scc_idma_path_" .. name .. string.format("_%06x", candidate), function()
        count(paths, name)
        print(string.format("SCC_IDMA_PATH t=%.6f phase=%s name=%s pc=%06X",
          now(), phase, name, pc()))
        return nil
      end)
  end
end

phase = "sample_source"
press_button(0x20, 500)
phase = "level_detect"
press_button(0x02, 1000)
phase = "threshold_endpoint"
for _ = 1, 24 do press_button(0x0a, 40) end
phase = "record_start"
press_button(0x23, 100)
wait_seconds(1)

if prog:read_u16(0x0d04) ~= 2 then
  reg.fail("scc_idma_transfer", string.format(
    "did_not_reach_waiting state=%04X display=\"%s\"", prog:read_u16(0x0d04), display.read_raw()))
  return
end

registers("before_rx")
phase = "rx_feed"
for offset = 0, 0x031f do
  rx_state.value = pattern(offset)
end
phase = "firmware_and_idma"
wait_seconds(2)
registers("after_completion")

local source_matches = 0
local source_mismatches = 0
for offset = 0, 0x031f do
  if (prog:read_u8(0x00f76600 + offset) & 0xff) == pattern(offset) then
    source_matches = source_matches + 1
  else
    source_mismatches = source_mismatches + 1
  end
end

local destination_matches = 0
local destination_mismatches = 0
for offset = 0, TRANSFER_BYTES - 1 do
  if (prog:read_u8(DESTINATION_START + offset) & 0xff) == pattern(offset + 6) then
    destination_matches = destination_matches + 1
  else
    destination_mismatches = destination_mismatches + 1
  end
end

local object = prog:read_u32(0x12d8) & 0x00ffffff
if iack4b_snapshot then
  print(string.format(
    "SCC_IDMA_4B_STATE CMR=%04X SAPR=%08X DAPR=%08X BCR=%04X CSR=%04X IPR=%04X IMR=%04X ISR=%04X",
    iack4b_snapshot.cmr, iack4b_snapshot.sapr, iack4b_snapshot.dapr,
    iack4b_snapshot.bcr, iack4b_snapshot.csr, iack4b_snapshot.ipr,
    iack4b_snapshot.imr, iack4b_snapshot.isr))
else
  print("SCC_IDMA_4B_STATE none")
end
for key, hits in pairs(iack) do
  print(string.format("SCC_IDMA_IACK_SUMMARY key=%s count=%u", key, hits))
end
for name, hits in pairs(paths) do
  print(string.format("SCC_IDMA_PATH_SUMMARY name=%s count=%u", name, hits))
end
print(string.format(
  "SCC_IDMA_RESULT display=\"%s\" state=%04X source_reads=%u source_first=%06X source_last=%06X destination_writes=%u destination_first=%06X destination_last=%06X source_matches=%u source_mismatches=%u destination_matches=%u destination_mismatches=%u iack4_4d=%u iack4_4b=%u object_destination=%06X witness_writes=%u",
  display.read_raw(), prog:read_u16(0x0d04) & 0xffff, source_read_bytes,
  first_source_read or 0, last_source_read or 0, destination_write_bytes,
  first_destination_write or 0, last_destination_write or 0,
  source_matches, source_mismatches, destination_matches, destination_mismatches,
  iack["L4:4D"] or 0, iack["L4:4B"] or 0,
  prog:read_u32(object + 0x20) & 0x00ffffff, witness_writes))

local passed = source_matches == 800
  and source_mismatches == 0
  and source_read_bytes == TRANSFER_BYTES
  and destination_write_bytes == TRANSFER_BYTES
  and destination_matches == TRANSFER_BYTES
  and destination_mismatches == 0
  and first_source_read == SOURCE_START
  and last_source_read == SOURCE_END
  and first_destination_write == DESTINATION_START
  and last_destination_write == DESTINATION_END
  and (iack["L4:4D"] or 0) == 1
  and (iack["L4:4B"] or 0) == 1
  and (paths.scc_rx_common or 0) >= 1
  and (paths.idma_start or 0) >= 1
  and (paths.idma_complete or 0) >= 1
  and iack4b_snapshot ~= nil
  and iack4b_snapshot.bcr == 0
  and iack4b_snapshot.csr == 0x0100
  and (prog:read_u32(object + 0x20) & 0x00ffffff) == 0x02c42a
  and witness_writes > 0

if passed then
  reg.pass("scc_idma_transfer", string.format(
    "source=%u destination=%u matches=%u iack4d=%u iack4b=%u object_destination=%06X witness=%u",
    source_read_bytes, destination_write_bytes, destination_matches,
    iack["L4:4D"] or 0, iack["L4:4B"] or 0,
    prog:read_u32(object + 0x20) & 0x00ffffff, witness_writes))
else
  reg.fail("scc_idma_transfer", string.format(
    "first_stop source=%u destination=%u mismatches=%u iack4d=%u iack4b=%u complete=%u object_destination=%06X witness=%u",
    source_read_bytes, destination_write_bytes, destination_mismatches,
    iack["L4:4D"] or 0, iack["L4:4B"] or 0, paths.idma_complete or 0,
    prog:read_u32(object + 0x20) & 0x00ffffff, witness_writes))
end
