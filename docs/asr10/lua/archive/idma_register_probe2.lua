-- Corrected IDMA register probe: logs raw (offset, data, mask) for the SIB
-- IDMA window with no byte-lane reconstruction, since the previous pass's
-- byte_address()/byte_value() helpers (designed for single-byte-lane FDC/
-- DUART accesses used everywhere else in this investigation) silently
-- discard half the data on a genuine word-wide write -- exactly what SAPR/
-- DAPR (32-bit registers) turned out to be. Observation only.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local taps = {}

local function install_sib_taps()
  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "idma2_sib_w", function(offset, data, mask)
    print(string.format("IDMA2_W t=%.6f pc=%06X offset=%06X data=%04X mask=%04X", now(), pc(), offset, data & 0xffff, mask & 0xffff))
    return nil
  end)
  taps[#taps + 1] = prog:install_read_tap(0x00fc6800, 0x00fc681f, "idma2_sib_r", function(offset, data, mask)
    print(string.format("IDMA2_R t=%.6f pc=%06X offset=%06X data=%04X mask=%04X", now(), pc(), offset, data & 0xffff, mask & 0xffff))
    return nil
  end)
  print(string.format("IDMA2_TAP_INSTALLED t=%.6f", now()))
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local file1 = "FILE 1  TUT0RIAL BNK  "

print("IDMA2 start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("idma_register_probe2", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("IDMA2_FILE1 t=%.6f", now()))

install_sib_taps()

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 35
while now() < deadline do
  emu.wait(emu.attotime.from_msec(50))
end

print(string.format("IDMA2_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("idma_register_probe2", string.format("display=\"%s\"", display.read_raw()))
