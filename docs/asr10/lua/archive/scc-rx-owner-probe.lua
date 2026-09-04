-- Observe the two sampling control objects without fabricating SCC input.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function wait_seconds(seconds)
  emu.wait(emu.attotime.from_seconds(seconds))
end

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

local function snapshot(label)
  print(string.format(
    "SCC_RX_OWNER_SNAPSHOT label=%s display=\"%s\" source_mode=%02X",
    label, display.read_raw(), prog:read_u8(0x016f)))
  for channel, control in pairs({ SCC1 = 0x12d8, SCC2 = 0x1320 }) do
    local object = prog:read_u32(control) & 0x00ffffff
    print(string.format(
      "SCC_RX_OWNER_OBJECT label=%s channel=%s control=%06X object=%06X ring_count=%04X current=%04X descriptors=%06X destination=%06X remaining=%08X pending=%02X",
      label, channel, control, object,
      prog:read_u16(object + 0x08) & 0xffff,
      prog:read_u16(object + 0x10) & 0xffff,
      prog:read_u32(object + 0x18) & 0x00ffffff,
      prog:read_u32(object + 0x20) & 0x00ffffff,
      prog:read_u32(object + 0x24),
      prog:read_u8(object + 0xea)))
    for index = 0, 7 do
      local entry = object + 0x28 + index * 0x14
      print(string.format(
        "SCC_RX_OWNER_RANGE label=%s channel=%s index=%u start=%08X end=%08X",
        label, channel, index,
        prog:read_u32(entry + 0x0c), prog:read_u32(entry + 0x10)))
    end
  end
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("scc_rx_owner_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

snapshot("file_loaded")
press_button(0x20, 500)
snapshot("sample_source")
press_button(0x02, 1000)
snapshot("level_detect")
for _ = 1, 24 do press_button(0x0a, 40) end
snapshot("threshold_end")
press_button(0x23, 200)
wait_seconds(1)
snapshot("waiting")

reg.pass("scc_rx_owner_probe", string.format("display=\"%s\"", display.read_raw()))
