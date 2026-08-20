-- keyboard-and-sample-bridge-8.md Del 1: does ES5506 actually fetch
-- from bank 1 at the address/rate the register math predicts, or is
-- there a word-vs-byte addressing unit bug in
-- es5506_wavetable_bank1_map()? Taps bank1's own address space
-- directly (es5506_host.spaces["bank1"], shift=-1, i.e. offsets are
-- plain WORD addresses matching the CR/START/END/ACCUM >>11
-- convention already used throughout this project -- confirmed
-- separately by matching a transient placeholder ACCUM value
-- (0xDC4BC000 >> 11 = 0x1B8978) exactly against an early fetch
-- address). One OTHER, unrelated voice dominates raw bank-1 traffic
-- with a static, non-advancing fetch address (~$1B8940) -- this
-- script's histogram buckets isolate our note's own voice by
-- excluding that dominant static bucket and tracking the runner-up
-- cluster's position over three time windows.
--
-- Result: measured advance matches the predicted rate (FC/2048 *
-- 31250Hz, ~13870 words/sec) to within ~1%, and the measured position
-- at each window matches predicted position (start_word + rate*t)
-- within ~40 words out of ~95,000 -- refutes the word-vs-byte
-- addressing bug hypothesis. See es5506-bank1-data-verify.lua for the
-- exact-value confirmation this position tracking motivated.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local function now() return emu.time() end

local es5506 = manager.machine.devices[":es5506_host"]
local bank1 = es5506.spaces["bank1"]

local function press_button(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then reg.fail("es5506_bank1_late_probe", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("es5506_bank1_late_probe", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))
press_button(":panel:buttons_0", 1 << 0x02)
emu.wait(emu.attotime.from_msec(500))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("es5506_bank1_late_probe", "no_mdin_image_device_found"); return end

print(string.format("LATE_NOTE_ON t=%.6f", now()))
mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")

-- Predicted position: voice1's own ACCUM initial value (0x0B278000)
-- right-shifted 11 bits (this project's established register->word
-- conversion), advancing at FC/2048 * sample_rate words/sec.
local start_word = 0x0B278000 >> 11
local rate_words_per_sec = (0x38D / 2048) * 31250
print(string.format("LATE_PREDICTED start_word=%06X rate_words_per_sec=%.1f", start_word, rate_words_per_sec))

-- The dominant, static, unrelated background voice's bucket --
-- excluded so the advancing cluster (our note) is visible.
local DOMINANT_BUCKET_ADDR = 0x1B8940
local BUCKET = 0x40

local function capture_window(label, wait_ms, dur_ms)
  emu.wait(emu.attotime.from_msec(wait_ms))
  local hist = {}
  local tap = bank1:install_read_tap(0x000000, bank1.address_mask, "late_" .. label, function(offset, data, mask)
    local b = offset // BUCKET
    if b * BUCKET ~= DOMINANT_BUCKET_ADDR then
      hist[b] = (hist[b] or 0) + 1
    end
    return nil
  end)
  emu.wait(emu.attotime.from_msec(dur_ms))
  tap:remove()
  local best_b, best_c = nil, -1
  for b, c in pairs(hist) do
    if c > best_c then best_b, best_c = b, c end
  end
  local addr = best_b and (best_b * BUCKET) or nil
  print(string.format("LATE_%s peak_addr=%s peak_count=%s", label, addr and string.format("%06X", addr) or "none", tostring(best_c)))
  return addr
end

local pos1 = capture_window("W1_at0.10s", 100, 20)
local pos2 = capture_window("W2_at0.30s", 180, 20)
local pos3 = capture_window("W3_at0.50s", 180, 20)

local function report_advance(label, a, b, dt)
  if a and b then
    print(string.format("LATE_ADVANCE %s delta_words=%d over_%.0fms implied_words_per_sec=%.1f",
      label, b - a, dt * 1000, (b - a) / dt))
  end
end
report_advance("w1_to_w2", pos1, pos2, 0.2)
report_advance("w2_to_w3", pos2, pos3, 0.2)

print(string.format("LATE_FINAL display=\"%s\"", display.read_raw()))
reg.pass("es5506_bank1_late_probe", "done")
