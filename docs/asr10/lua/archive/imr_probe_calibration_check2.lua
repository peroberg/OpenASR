-- Second calibration check: does installing the tap AFTER a delay (past the
-- expected BAR write / internal-window install point) change anything, vs
-- installing at script load (before boot even starts)? Observation only.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local count = 0
local function install()
  return prog:install_write_tap(0x00fc6800, 0x00fc68ff, "imr_calib2_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    local value = byte_value(data, mask)
    count = count + 1
    print(string.format("CALIB2_WRITE t=%.6f addr=%06X value=%02X mask=%04X pc=%06X", now(), address, value, mask, pc()))
    return nil
  end)
end

print("CALIB2 start: waiting 15.5s emulated before installing tap (past known init activity)")
emu.wait(emu.attotime.from_seconds(15.5))
local tap = install()
print(string.format("CALIB2_TAP_INSTALLED t=%.6f", now()))

emu.wait(emu.attotime.from_seconds(20))
print(string.format("CALIB2_SUMMARY total_writes=%u final_display=\"%s\"", count, reg.display.read_raw()))
reg.pass("imr_probe_calibration_check2", string.format("total_writes=%u", count))
