-- From FILE 1, pressing BTN_0A/BTN_23/BTN_02 must reach FILE LOADED, and the
-- IDMA transfer that produces it must move the full measured instrument
-- payload (172544 bytes / 337 sectors across 21 IDMA arms), not just show
-- the right display string. A test that only reads the display would go
-- silently wrong the same way an unwitnessed tap does -- see
-- docs/asr10/investigations/file-loaded-verification-probe.md for how this
-- number was derived and independently verified byte-for-byte against the
-- source disk image.

local test = dofile("docs/asr10/lua/lib/asr10_regression.lua")

local EXPECTED_TOTAL_BYTES = 172544

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

local ok, text = test.wait_for_text(file1, 45)
if not ok then
  test.fail("file_loaded", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

-- IDMA DAPR/BCR/CMR tap, raw offset/data, no byte-lane reconstruction
-- (methods-static-analysis.md's lossy-reconstruction lesson). Installed
-- only after FILE 1 -- boot-time disk loading is already known (imr_probe_
-- calibration_check.lua) to never touch $FC6800-$FC68FF.
local dapr_hi, dapr_lo, bcr = 0, 0, 0
local total_bytes = 0
local arm_count = 0

taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "file_loaded_idma_w", function(offset, data, mask)
  if offset == 0x00fc6808 then dapr_hi = data & 0xffff end
  if offset == 0x00fc680a then dapr_lo = data & 0xffff end
  if offset == 0x00fc680c then bcr = data & 0xffff end
  if offset == 0x00fc6802 and (data & 1) ~= 0 and bcr > 0 then
    total_bytes = total_bytes + (bcr - 1)
    arm_count = arm_count + 1
  end
  return nil
end)

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

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = test.wait_for_text(file_loaded, 20)
if not ok then
  test.fail("file_loaded", string.format("no_file_loaded final_display=\"%s\" idma_bytes=%u arms=%u", text, total_bytes, arm_count))
  return
end

if total_bytes ~= EXPECTED_TOTAL_BYTES then
  test.fail("file_loaded", string.format("display_ok_but_idma_bytes_mismatch got=%u expected=%u arms=%u",
    total_bytes, EXPECTED_TOTAL_BYTES, arm_count))
  return
end

test.pass("file_loaded", string.format("display=\"%s\" idma_bytes=%u arms=%u", text, total_bytes, arm_count))
