-- Observe the opt-in virtual SCC1 RX experiment. The C++ experiment bridge
-- changes machine state; this script only drives the panel and measures it.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}
local phase = "boot"
local iack = {}
local paths = {}
local writes = {}
local destination_writes = 0
local witness_writes = 0
local first_destination_write = nil

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
  print(string.format("VIRTUAL_SCC_BUTTON t=%.6f phase=%s code=%02X", now(), phase, code))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local function scc1_context()
  local object = prog:read_u32(0x12d8) & 0x00ffffff
  local index = prog:read_u16(object + 0x0c) & 7
  local descriptor = 0x00fc6400 + index * 8
  return object, index, descriptor
end

local function snapshot(label)
  local object, index, descriptor = scc1_context()
  local buffer = prog:read_u32(descriptor + 4) & 0x00ffffff
  print(string.format(
    "VIRTUAL_SCC_SNAPSHOT label=%s t=%.6f phase=%s display=\"%s\" pc=%06X state=%04X object=%06X index=%u descriptor=%06X status=%04X length=%04X buffer=%06X MRBLR=%04X destination=%06X pending=%02X",
    label, now(), phase, display.read_raw(), pc(), prog:read_u16(0x0d04) & 0xffff,
    object, index, descriptor, prog:read_u16(descriptor) & 0xffff,
    prog:read_u16(descriptor + 2) & 0xffff, buffer,
    prog:read_u16(0x00fc6482) & 0xffff,
    prog:read_u32(object + 0x20) & 0x00ffffff, prog:read_u8(object + 0xea)))
  print(string.format(
    "VIRTUAL_SCC_IDMA label=%s CMR=%04X SAPR=%08X DAPR=%08X BCR=%04X CSR=%04X FCR=%04X IMR=%04X ISR=%04X SCCE=%04X SCCM=%04X",
    label, prog:read_u16(0x00fc6802) & 0xffff, prog:read_u32(0x00fc6804),
    prog:read_u32(0x00fc6808), prog:read_u16(0x00fc680c) & 0xffff,
    prog:read_u16(0x00fc680e) & 0xffff, prog:read_u16(0x00fc6810) & 0xffff,
    prog:read_u16(0x00fc6816) & 0xffff, prog:read_u16(0x00fc6818) & 0xffff,
    prog:read_u16(0x00fc6888) & 0xffff, prog:read_u16(0x00fc688a) & 0xffff))
end

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("virtual_scc_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local key = string.format("L%d:%02X", level, data & 0xff)
      count(iack, key)
      if level == 4 or (level == 6 and iack[key] <= 3) then
        print(string.format("VIRTUAL_SCC_IACK t=%.6f phase=%s level=%d vector=%02X pc=%06X",
          now(), phase, level, data & 0xff, pc()))
      end
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("virtual_scc_rx_chain_probe", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- Install SIB taps after the final boot-time BAR relocation.
taps[#taps + 1] = prog:install_write_tap(0x00fc6400, 0x00fc68b5,
  "virtual_scc_sib_w", function(offset, data, mask)
    local address = offset & 0x00fffffe
    count(writes, string.format("%06X", address))
    if address <= 0x00fc6410 or (address >= 0x00fc6802 and address <= 0x00fc6818)
        or address == 0x00fc6888 then
      print(string.format(
        "VIRTUAL_SCC_SIB_WRITE t=%.6f phase=%s addr=%06X data=%08X mask=%08X pc=%06X",
        now(), phase, address, data, mask, pc()))
    end
    return nil
  end)

-- The hot low-RAM range is both the dynamic destination classifier and the
-- positive witness proving that the write tap remains alive.
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff,
  "virtual_scc_lowram_w", function(offset, data, mask)
    witness_writes = witness_writes + 1
    local object = prog:read_u32(0x12d8) & 0x00ffffff
    local destination = prog:read_u32(object + 0x20) & 0x00ffffff
    if destination ~= 0 and offset >= destination and offset < destination + 0x0320 then
      destination_writes = destination_writes + 1
      if not first_destination_write then
        first_destination_write = { t = now(), address = offset, data = data, mask = mask, pc = pc() }
      end
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
      "virtual_scc_path_" .. name .. string.format("_%06x", candidate), function()
        count(paths, name)
        print(string.format("VIRTUAL_SCC_PATH t=%.6f phase=%s name=%s addr=%06X pc=%06X",
          now(), phase, name, candidate, pc()))
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
snapshot("before_injection")

phase = "record_start"
press_button(0x23, 100)
wait_seconds(2)
snapshot("after_injection")

local object = prog:read_u32(0x12d8) & 0x00ffffff
local destination = prog:read_u32(object + 0x20) & 0x00ffffff
local source = prog:read_u32(0x00fc6804) & 0x00ffffff
local byte_count = prog:read_u16(0x00fc680c) & 0xffff
local descriptor_buffer = prog:read_u32(0x00fc6404) & 0x00ffffff
local matches = 0
local mismatches = 0
local inferred_start = destination - byte_count
local source_offset = source - descriptor_buffer
for offset = 0, byte_count - 1 do
  local input_offset = source_offset + offset
  local expected = ((input_offset & 0x0f) < 2) and 0 or (input_offset & 0xff)
  if input_offset == 0x40 then expected = 0x7f end
  if input_offset == 0x41 then expected = 0xff end
  local actual = prog:read_u8(inferred_start + offset) & 0xff
  if actual == expected then matches = matches + 1 else mismatches = mismatches + 1 end
end

for key, hits in pairs(iack) do
  print(string.format("VIRTUAL_SCC_IACK_SUMMARY key=%s count=%u", key, hits))
end
for name, hits in pairs(paths) do
  print(string.format("VIRTUAL_SCC_PATH_SUMMARY name=%s count=%u", name, hits))
end
for address, hits in pairs(writes) do
  print(string.format("VIRTUAL_SCC_WRITE_SUMMARY addr=%s count=%u", address, hits))
end
if first_destination_write then
  print(string.format(
    "VIRTUAL_SCC_DEST_FIRST t=%.6f addr=%06X data=%08X mask=%08X pc=%06X",
    first_destination_write.t, first_destination_write.address,
    first_destination_write.data, first_destination_write.mask, first_destination_write.pc))
else
  print("VIRTUAL_SCC_DEST_FIRST none")
end
print(string.format(
  "VIRTUAL_SCC_RESULT display=\"%s\" state=%04X source=%06X source_offset=%04X byte_count=%04X destination_after=%06X inferred_start=%06X destination_writes=%u pattern_matches=%u pattern_mismatches=%u witness_writes=%u",
  display.read_raw(), prog:read_u16(0x0d04) & 0xffff,
  source, source_offset, byte_count, destination, inferred_start & 0x00ffffff,
  destination_writes, matches, mismatches, witness_writes))

reg.pass("virtual_scc_rx_chain_probe", string.format(
  "display=\"%s\" iack4_4d=%u iack4_4b=%u destination_writes=%u matches=%u witness=%u",
  display.read_raw(), iack["L4:4D"] or 0, iack["L4:4B"] or 0,
  destination_writes, matches, witness_writes))
