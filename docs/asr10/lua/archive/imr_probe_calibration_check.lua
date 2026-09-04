-- Positive-control check for imr_unmask_probe.lua's tap mechanism.
-- Observation only. Widens the tap to the whole SIB window 0xfc6800-0xfc68ff
-- and logs every write from reset to FILE 1, to confirm the tap mechanism
-- itself fires for already-documented writes (PBCNT/PBDDR/PADAT/PBDAT,
-- mc68302-status.md) before trusting a zero result on the narrower
-- GIMR/IMR/ISR range.

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
local tap = prog:install_write_tap(0x00fc6800, 0x00fc68ff, "imr_calib_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  local value = byte_value(data, mask)
  count = count + 1
  print(string.format("CALIB_WRITE t=%.6f addr=%06X value=%02X mask=%04X pc=%06X", now(), address, value, mask, pc()))
  return nil
end)

print("CALIB start wait_for=FILE1 window=FC6800-FC68FF")
local ok, text = reg.wait_for_text(file1, 45)
print(string.format("CALIB_SUMMARY reached_file1=%s total_writes=%u final_display=\"%s\"", tostring(ok), count, text))
reg.pass("imr_probe_calibration_check", string.format("total_writes=%u", count))
