-- Calibration check for irq1_vector_probe.lua: does a read tap on
-- cpu.spaces["cpu_space"] fire at ALL? Logs every distinct offset seen on
-- that space, unconditionally, including known-working level-6 IACK
-- (0xfffffd) which this same build already uses successfully for the DUART.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local cpu_space = cpu.spaces["cpu_space"]

if not cpu_space then
  print("CALIB3 no cpu_space space; listing available space names:")
  for name, _ in pairs(cpu.spaces) do
    print("CALIB3_SPACE " .. tostring(name))
  end
  reg.fail("irq1_vector_probe_calibration", "no_cpu_space")
  return
end

local counts = {}
local total = 0
local tap = cpu_space:install_read_tap(0x000000, 0xffffff, "irq1_calib_r", function(offset, data, mask)
  total = total + 1
  counts[offset] = (counts[offset] or 0) + 1
  if counts[offset] <= 2 then
    print(string.format("CALIB3_READ t=%.6f offset=%06X data=%02X mask=%04X", emu.time(), offset, data & 0xff, mask))
  end
  return nil
end)

local error129 = "ERR0R 129 - REB00T    "
local file1 = "FILE 1  TUT0RIAL BNK  "
local deadline = emu.time() + 45
local final_text = nil
while emu.time() < deadline do
  local text = display.read_raw()
  if text == error129 or text == file1 then
    final_text = text
    break
  end
  emu.wait(emu.attotime.from_msec(50))
end
if not final_text then final_text = display.read_raw() end

local summary = {}
for offset, c in pairs(counts) do
  summary[#summary + 1] = string.format("%06X:%u", offset, c)
end
print(string.format("CALIB3_SUMMARY total_reads=%u distinct_offsets=%u final_display=\"%s\"", total, #summary, final_text))
print("CALIB3_OFFSETS " .. table.concat(summary, " "))
reg.pass("irq1_vector_probe_calibration", string.format("total=%u distinct=%u", total, #summary))
