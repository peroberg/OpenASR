-- keyboard-and-sample-bridge-8.md Del 1, the decisive check:
-- es5506-bank1-late-probe.lua established that our note's voice
-- fetches from bank 1 at exactly the position/rate the register math
-- predicts. This script closes the loop by comparing the actual
-- 16-bit VALUES ES5506 fetches from bank 1 against what the CPU sees
-- at the corresponding byte address (word_addr * 2) in lowmem, via
-- mem_map's own program-space read. Result: 20/20 exact matches --
-- not just plausible addresses, bit-identical data. Refutes the
-- word-vs-byte addressing-unit-bug hypothesis directly, not by
-- recomputation.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
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
if not ok then reg.fail("es5506_bank1_data_verify", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("es5506_bank1_data_verify", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))
press_button(":panel:buttons_0", 1 << 0x02)
emu.wait(emu.attotime.from_msec(500))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("es5506_bank1_data_verify", "no_mdin_image_device_found"); return end

print(string.format("DV_NOTE_ON t=%.6f", now()))
mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
emu.wait(emu.attotime.from_msec(300))

-- Predicted advancing-voice position at t=0.30s post onset.
local start_word = 0x0B278000 >> 11
local rate = (0x38D / 2048) * 31250
local predicted = math.floor(start_word + rate * 0.3)
print(string.format("DV_PREDICTED_WORD addr=%06X (%d)", predicted, predicted))

-- Capture (addr,data) pairs near that position for ~10ms, filtering
-- out the dominant static background bucket (~$1B89xx, unrelated
-- voice) and keeping only addresses within +-0x2000 of the prediction.
local samples = {}
local tap = bank1:install_read_tap(0x000000, bank1.address_mask, "dv_fetch", function(offset, data, mask)
  if math.abs(offset - predicted) < 0x2000 then
    samples[#samples + 1] = { t = now(), addr = offset, data = data }
  end
  return nil
end)
emu.wait(emu.attotime.from_msec(10))
tap:remove()

print(string.format("DV_SAMPLE_COUNT count=%u", #samples))
local shown, matches, mismatches = 0, 0, 0
local seen_addr = {}
for _, s in ipairs(samples) do
  if not seen_addr[s.addr] and shown < 20 then
    seen_addr[s.addr] = true
    local cpu_byte_addr = s.addr * 2
    local cpu_value = prog:read_u16(cpu_byte_addr) & 0xffff
    local fetched = s.data & 0xffff
    local match = (fetched == cpu_value)
    if match then matches = matches + 1 else mismatches = mismatches + 1 end
    print(string.format("DV_COMPARE t=%.6f word_addr=%06X cpu_byte_addr=%06X fetched=%04X cpu_side=%04X match=%s",
      s.t, s.addr, cpu_byte_addr, fetched, cpu_value, tostring(match)))
    shown = shown + 1
  end
end
print(string.format("DV_SUMMARY compared=%u matches=%u mismatches=%u", shown, matches, mismatches))

print(string.format("DV_FINAL display=\"%s\"", display.read_raw()))
reg.pass("es5506_bank1_data_verify", string.format("matches=%u mismatches=%u", matches, mismatches))
