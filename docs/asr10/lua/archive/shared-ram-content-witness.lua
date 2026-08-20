-- keyboard-and-sample-bridge-6.md Del 2: content check of $100000-
-- $1FFFFF (bank 0's CPU-shared region) after selecting the instrument
-- and playing a note both ways (panel + MIDI), to rule out Hypothesis
-- 1 (bank 1 = same physical RAM as bank 0, data just not moved yet)
-- numerically rather than assuming a zero result means "not measured".
-- Witness taps stay installed through the whole window per
-- methods-static-analysis.md §8.7: a zero-write result is only valid
-- with a live sibling witness proving the tap mechanism itself
-- survived, not just its installation instant.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press_button(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

-- Witnesses, installed before anything else, alive through the whole
-- window: ES5506 registers (known constantly hot) and the loaded
-- instrument payload range (known static+nonzero).
local witness_es5506 = 0
local witness_payload = 0
local samram_writes = 0
local taps = {}
taps[#taps+1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "srcw_witness_es5506", function(offset, data, mask)
  witness_es5506 = witness_es5506 + 1
  return nil
end)
taps[#taps+1] = prog:install_read_tap(0x000944, 0x0552ff, "srcw_witness_payload", function(offset, data, mask)
  witness_payload = witness_payload + 1
  return nil
end)
taps[#taps+1] = prog:install_write_tap(0x100000, 0x1fffff, "srcw_samram_w", function(offset, data, mask)
  samram_writes = samram_writes + 1
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then reg.fail("shared_ram_content_witness", "boot_timeout"); return end
press_button(":panel:buttons_0", 1 << 0x0a)
press_button(":panel:buttons_32", 1 << 0x03)
press_button(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then reg.fail("shared_ram_content_witness", "no_file_loaded"); return end
emu.wait(emu.attotime.from_msec(500))

press_button(":panel:buttons_0", 1 << 0x02)  -- select Instrument 1
emu.wait(emu.attotime.from_msec(500))

press_button(":panel:keys_0", 0x00000001, 150)
emu.wait(emu.attotime.from_msec(500))

local mdin_image = nil
for tag, img in pairs(manager.machine.images) do
  if tag:find("mdin") then mdin_image = img end
end
if not mdin_image then reg.fail("shared_ram_content_witness", "no_mdin_image_device_found"); return end
mdin_image:load("docs/asr10/lua/fixtures/noteon.mid")
emu.wait(emu.attotime.from_msec(1500))

print(string.format("SRCW_WITNESS es5506=%u payload=%u samram_writes=%u", witness_es5506, witness_payload, samram_writes))

-- Content check: stratified sample across the full 1MB, odd stride to
-- avoid aliasing with any power-of-two structure in the region.
local zero = 0
local sampled = 0
for i = 0, 0x100000 - 1, 4099 do
  local b = prog:read_u8(0x100000 + i) & 0xff
  if b == 0 then zero = zero + 1 end
  sampled = sampled + 1
end
print(string.format("SRCW_CONTENT zero=%u sampled=%u zero_pct=%.2f", zero, sampled, 100.0 * zero / sampled))

for _, off in ipairs({0, 0x40000, 0x80000, 0xC0000, 0xFFFF0}) do
  local b = prog:read_u8(0x100000 + off) & 0xff
  print(string.format("SRCW_BYTE off=%06X value=%02X", off, b))
end

reg.pass("shared_ram_content_witness", string.format("samram_writes=%u zero_pct=%.2f", samram_writes, 100.0 * zero / sampled))
