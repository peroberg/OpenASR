-- Feed already-framed SCC1 RX bytes through mc68302_device and observe which
-- receive-engine effects are produced by the device rather than by Lua.

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
local buffer_writes = 0
local destination_writes = 0
local witness_writes = 0

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function count(t, key) t[key] = (t[key] or 0) + 1 end

local function wait_seconds(seconds)
  emu.wait(emu.attotime.from_seconds(seconds))
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  print(string.format("SCC_CP_BUTTON t=%.6f phase=%s code=%02X", now(), phase, code))
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

local function scc1_context()
  local object = prog:read_u32(0x12d8) & 0x00ffffff
  local index = prog:read_u16(object + 0x0c) & 7
  local descriptor = 0x00fc6400 + index * 8
  return object, index, descriptor
end

local function snapshot(label)
  local object, index, descriptor = scc1_context()
  print(string.format(
    "SCC_CP_SNAPSHOT label=%s t=%.6f phase=%s display=\"%s\" pc=%06X state=%04X index=%u descriptor=%06X status=%04X length=%04X buffer=%06X MRBLR=%04X SCM=%04X SCCE=%04X SCCM=%04X IPR=%04X IMR=%04X ISR=%04X destination=%06X",
    label, now(), phase, display.read_raw(), pc(), prog:read_u16(0x0d04) & 0xffff,
    index, descriptor, prog:read_u16(descriptor) & 0xffff,
    prog:read_u16(descriptor + 2) & 0xffff,
    prog:read_u32(descriptor + 4) & 0x00ffffff,
    prog:read_u16(0x00fc6482) & 0xffff,
    prog:read_u16(0x00fc6884) & 0xffff,
    prog:read_u16(0x00fc6888) & 0xffff,
    prog:read_u16(0x00fc688a) & 0xffff,
    prog:read_u16(0x00fc6814) & 0xffff,
    prog:read_u16(0x00fc6816) & 0xffff,
    prog:read_u16(0x00fc6818) & 0xffff,
    prog:read_u32(object + 0x20) & 0x00ffffff))
end

if not rx_state or not rx_state.writeable then
  reg.fail("scc_cp_rx_minimal_engine", "missing_writeable_SCC1RX_state")
  return
end

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("scc_cp_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local key = string.format("L%d:%02X", level, data & 0xff)
      count(iack, key)
      if level == 4 then
        print(string.format("SCC_CP_IACK t=%.6f phase=%s vector=%02X pc=%06X",
          now(), phase, data & 0xff, pc()))
      end
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("scc_cp_rx_minimal_engine", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- Install SIB taps only after firmware has performed its final BAR move.
taps[#taps + 1] = prog:install_write_tap(0x00fc6400, 0x00fc68b5,
  "scc_cp_sib_w", function(offset, data, mask)
    local address = offset & 0x00fffffe
    local descriptor_completion = address == 0x00fc6400
      or (address == 0x00fc6402 and (data & 0xffff) == 0x0320)
    if descriptor_completion or address == 0x00fc6814 or address == 0x00fc6818
        or address == 0x00fc6888 then
      print(string.format(
        "SCC_CP_SIB_WRITE t=%.6f phase=%s addr=%06X data=%08X mask=%08X pc=%06X",
        now(), phase, address, data, mask, pc()))
    end
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(0x00f76600, 0x00f7691f,
  "scc_cp_buffer_w", function()
    buffer_writes = buffer_writes + 1
    return nil
  end)

-- This broad but already-hot RAM tap is the live witness for all negative
-- downstream observations and separately counts the dynamic recording range.
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff,
  "scc_cp_lowram_w", function(offset)
    witness_writes = witness_writes + 1
    local object = prog:read_u32(0x12d8) & 0x00ffffff
    local destination = prog:read_u32(object + 0x20) & 0x00ffffff
    if destination ~= 0 and offset >= destination and offset < destination + 0x0320 then
      destination_writes = destination_writes + 1
    end
    return nil
  end)

local PATHS = {
  [0x00643c] = "scc_rx_common",
  [0x0064ba] = "scc_received_range_continue",
  [0x0065cc] = "waiting_state_3",
  [0x00665c] = "post_90e8",
  [0x005c6c] = "recording_setup",
  [0x00b478] = "range_to_idma",
  [0x00b4c0] = "idma_start",
  [0x00aa48] = "idma_complete",
}
for address, name in pairs(PATHS) do
  for _, candidate in ipairs({ address, address | 0x00ff0000 }) do
    taps[#taps + 1] = opcodes:install_read_tap(candidate, candidate + 1,
      "scc_cp_path_" .. name .. string.format("_%06x", candidate), function()
        count(paths, name)
        print(string.format("SCC_CP_PATH t=%.6f phase=%s name=%s pc=%06X",
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
  reg.fail("scc_cp_rx_minimal_engine", string.format(
    "did_not_reach_waiting state=%04X display=\"%s\"", prog:read_u16(0x0d04), display.read_raw()))
  return
end

snapshot("before_rx")
phase = "rx_feed"
local supplied = 0
for offset = 0, 0x031f do
  rx_state.value = pattern(offset)
  supplied = supplied + 1
end
snapshot("after_rx_synchronous")
phase = "firmware_consume"
wait_seconds(2)
snapshot("after_firmware")

local descriptor_buffer = prog:read_u32(0x00fc6404) & 0x00ffffff
local source_matches = 0
local source_mismatches = 0
for offset = 0, 0x031f do
  if (prog:read_u8(descriptor_buffer + offset) & 0xff) == pattern(offset) then
    source_matches = source_matches + 1
  else
    source_mismatches = source_mismatches + 1
  end
end

local object = prog:read_u32(0x12d8) & 0x00ffffff
print(string.format(
  "SCC_CP_IDMA CMR=%04X SAPR=%08X DAPR=%08X BCR=%04X CSR=%04X FCR=%04X",
  prog:read_u16(0x00fc6802) & 0xffff, prog:read_u32(0x00fc6804),
  prog:read_u32(0x00fc6808), prog:read_u16(0x00fc680c) & 0xffff,
  prog:read_u16(0x00fc680e) & 0xffff, prog:read_u16(0x00fc6810) & 0xffff))
for key, hits in pairs(iack) do
  print(string.format("SCC_CP_IACK_SUMMARY key=%s count=%u", key, hits))
end
for name, hits in pairs(paths) do
  print(string.format("SCC_CP_PATH_SUMMARY name=%s count=%u", name, hits))
end
print(string.format(
  "SCC_CP_RESULT display=\"%s\" state=%04X supplied=%u buffer_writes=%u source_matches=%u source_mismatches=%u iack4_4d=%u iack4_4b=%u destination=%06X destination_writes=%u witness_writes=%u",
  display.read_raw(), prog:read_u16(0x0d04) & 0xffff, supplied, buffer_writes,
  source_matches, source_mismatches, iack["L4:4D"] or 0, iack["L4:4B"] or 0,
  prog:read_u32(object + 0x20) & 0x00ffffff, destination_writes, witness_writes))

local passed = source_mismatches == 0
  and buffer_writes >= 800
  and (iack["L4:4D"] or 0) >= 1
  and (paths.scc_rx_common or 0) >= 1
  and (paths.scc_received_range_continue or 0) >= 1
  and (paths.waiting_state_3 or 0) >= 1
  and (paths.range_to_idma or 0) >= 1
  and (paths.idma_start or 0) >= 1
  and prog:read_u16(0x0d04) == 3
  and display.read_raw():find("REC0RDING", 1, true) ~= nil
  and witness_writes > 0

if passed then
  reg.pass("scc_cp_rx_minimal_engine", string.format(
    "iack4_4d=%u source_matches=%u idma_start=%u destination_writes=%u witness=%u",
    iack["L4:4D"] or 0, source_matches, paths.idma_start or 0,
    destination_writes, witness_writes))
else
  reg.fail("scc_cp_rx_minimal_engine", string.format(
    "first_stop iack4_4d=%u source_mismatches=%u scc_common=%u range=%u state3=%u setup=%u idma=%u recording_state=%04X witness=%u",
    iack["L4:4D"] or 0, source_mismatches, paths.scc_rx_common or 0,
    paths.scc_received_range_continue or 0, paths.waiting_state_3 or 0,
    paths.recording_setup or 0, paths.idma_start or 0,
    prog:read_u16(0x0d04) & 0xffff, witness_writes))
end
