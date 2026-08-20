-- Diagnose "DISK NOT RESPONDING" at t~23.4s, docs/asr10/investigations/
-- disk-not-responding-probe.md. Observation only.
--
-- Logs, from FILE1 through well past the error: FDC FIFO bytes both
-- directions, IDMA CMR writes (to count transfers requested vs the SIB
-- register programming pattern per sector), vector-$51 IACKs, and every
-- display text change with timestamp.

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

taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "dnr_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4001 then
    print(string.format("DNR_AUX_W t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  elseif address == 0x00fc4003 then
    print(string.format("DNR_FIFO_W t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "dnr_fdc_r", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    print(string.format("DNR_FIFO_R t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)

local function install_idma_taps()
  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "dnr_idma_w", function(offset, data, mask)
    print(string.format("DNR_IDMA_W t=%.6f pc=%06X offset=%06X data=%04X mask=%04X", now(), pc(), offset, data & 0xffff, mask & 0xffff))
    return nil
  end)
  print(string.format("DNR_IDMA_TAP_INSTALLED t=%.6f", now()))
end

if cpu_space then
  taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff, "dnr_iack_r", function(offset, data, mask)
    if offset ~= 0x00fffff2 then
      return nil
    end
    local vector = data & 0xff
    print(string.format("DNR_IACK1 t=%.6f vector=%02X pc=%06X", now(), vector, pc()))
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

print("DNR start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("disk_not_responding_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("DNR_FILE1 t=%.6f", now()))

install_idma_taps()

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local last = nil
local deadline = now() + 40
while now() < deadline do
  local cur = display.read_raw()
  if cur ~= last then
    print(string.format("DNR_DISPLAY t=%.6f text=\"%s\"", now(), cur))
    last = cur
  end
  emu.wait(emu.attotime.from_msec(20))
end

print(string.format("DNR_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("disk_not_responding_probe", string.format("display=\"%s\"", display.read_raw()))
