-- IDMA register-map derivation probe. Observation only. Runs against the
-- current landed build (set_ready_line_connected(false) + IRQ1 wiring are
-- now permanent, docs/asr10/investigations/ready-line-artifact-probe.md),
-- so no C++ toggling is needed this time.
--
-- Logs every read and write in $FC6800-$FC681F (the whole documented SIB
-- IDMA+interrupt-controller region, not just the five suspected registers,
-- in case firmware touches something outside that specific list), plus the
-- FDC FIFO command dialogue and the vector-$51/$4B IACK events, from before
-- the instrument-load RECALIBRATE through the READ DATA overrun.
--
-- Tap installed after t=6s -- past the SIB window's last BAR-driven remap
-- (methods-static-analysis.md #8.5/#8.7; witnessed at t~5.4s in
-- irq1-imr-unmask-probe.md) -- not at script start.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local taps = {}
local events = {}
local seq = 0

local function record(kind, line)
  seq = seq + 1
  events[#events + 1] = { seq = seq, time = now(), line = line }
  print(line)
end

local function install_sib_taps()
  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "idma_sib_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    record("w", string.format("IDMAREG_W t=%.6f seq=%04d pc=%06X addr=%06X value=%02X mask=%04X",
      now(), seq + 1, pc(), address, byte_value(data, mask), mask))
    return nil
  end)
  taps[#taps + 1] = prog:install_read_tap(0x00fc6800, 0x00fc681f, "idma_sib_r", function(offset, data, mask)
    local address = byte_address(offset, mask)
    record("r", string.format("IDMAREG_R t=%.6f seq=%04d pc=%06X addr=%06X value=%02X mask=%04X",
      now(), seq + 1, pc(), address, byte_value(data, mask), mask))
    return nil
  end)
  print(string.format("IDMAREG_TAP_INSTALLED t=%.6f", now()))
end

-- FDC FIFO dialogue, both directions, for context/correlation.
taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "idma_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    record("fdc_w", string.format("IDMA_FDC_W t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "idma_fdc_r", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    record("fdc_r", string.format("IDMA_FDC_R t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)

-- IACK events, both level 1 (vector $51) and level 4 autovector-style
-- internal INRQ (vector $4B would come through the *internal* level-4
-- path if ever reached -- not through cpu_space at all, since INRQ vectors
-- are supplied by the (currently known_unimplemented) internal controller,
-- not an external IACK. Logged here defensively in case something
-- unexpected happens; level 1 is the one we know fires.
if cpu_space then
  taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff, "idma_iack_r", function(offset, data, mask)
    if offset ~= 0x00fffff2 and offset ~= 0x00fffff8 then
      return nil
    end
    local vector = data & 0xff
    local target = prog:read_u32(vector * 4) & 0xffffffff
    record("iack", string.format("IDMA_IACK t=%.6f offset=%06X vector=%02X target=%08X pc=%06X",
      now(), offset, vector, target, pc()))
    return nil
  end)
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

print("IDMAREG start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("idma_register_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("IDMAREG_FILE1 t=%.6f", now()))

-- Install SIB taps now -- well past t=6s at this point, and well past the
-- last BAR write.
install_sib_taps()

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 35
while now() < deadline do
  emu.wait(emu.attotime.from_msec(50))
end

print(string.format("IDMAREG_SUMMARY total_events=%u final_display=\"%s\" t=%.6f",
  #events, display.read_raw(), now()))
reg.pass("idma_register_probe", string.format("total=%u display=\"%s\"", #events, display.read_raw()))
