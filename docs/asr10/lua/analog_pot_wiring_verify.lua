-- Temporary post-implementation witness for the ASR-10 PBDAT-selected PAR
-- path.  It observes only firmware traffic and never writes firmware RAM or
-- registers.  Taps are retained by asr10_taps.lua.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local taps = reg.taps
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local last_selector = 0
local high_bits = 0
local reads = {}
local pb_writes = 0
local ram_writes = 0

local function reset_counts()
  reads, pb_writes, ram_writes = {}, 0, 0
end

local function record(value)
  local selector = last_selector & 7
  reads[selector] = reads[selector] or {}
  reads[selector][value] = (reads[selector][value] or 0) + 1
end

local function histogram(selector)
  local values = reads[selector] or {}
  local best_value, best_count = nil, 0
  for value, count in pairs(values) do
    if count > best_count then best_value, best_count = value, count end
  end
  return best_value or -1, best_count
end

local function report(label, selector, expected)
  local value, count = histogram(selector)
  print(string.format(
    "POT_WIRING phase=%s selector=%u expected=%03X observed=%03X n=%u pb_writes=%u ram_writes=%u",
    label, selector, expected, value, count, pb_writes, ram_writes))
  if value ~= expected or count == 0 or pb_writes == 0 or ram_writes == 0 then
    reg.fail("analog_pot_wiring", "witness_failed " .. label)
    return false
  end
  return true
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local field = manager.machine.ioport.ports[port_name]:field(1 << (code & 0x1f))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("analog_pot_wiring", "boot_timeout display=\"" .. text .. "\"")
  return
end

-- Installation follows the BAR/internal-window setup.  A low-RAM write is the
-- live witness for the entire acquisition window.
taps.write_tap(prog, 0x00fc6828, 0x00fc6829, "pot_wiring_pbdat", function(offset, data, mask)
  if (mask & 0x00ff) ~= 0 then
    last_selector = data & 7
    pb_writes = pb_writes + 1
  end
end)
taps.read_tap(prog, 0x00fc206c, 0x00fc206d, "pot_wiring_par_high", function(offset, data, mask)
  if (mask & 0x00ff) ~= 0 then high_bits = (data & 0x03) << 8 end
end)
taps.read_tap(prog, 0x00fc206e, 0x00fc206f, "pot_wiring_par_low", function(offset, data, mask)
  if (mask & 0x00ff) ~= 0 then record(high_bits | (data & 0xff)) end
end)
taps.write_tap(prog, 0x00000d80, 0x00000dff, "pot_wiring_ram_witness", function()
  ram_writes = ram_writes + 1
end)

reset_counts()
emu.wait(emu.attotime.from_msec(1000))
for _, case in ipairs({
  { "pitch_default", 0, 0x200 },
  { "mod_default", 2, 0x200 },
  { "volume_default", 3, 0x3ff },
  { "pedal_default", 4, 0x200 },
  { "data_entry_default", 5, 0x200 },
  { "reference", 7, 0x300 },
}) do
  if not report(case[1], case[2], case[3]) then return end
end

-- Existing service flow: Command -> Env1 -> four Right presses -> Examine
-- Analog Inputs -> Enter.  This is a viewer witness only; it does not steer
-- acquisition and all prior PAR/RAM witnesses remain active.
press_button(0x06)
press_button(0x0d)
for _ = 1, 4 do press_button(0x11) end
press_button(0x23, 600)
print(string.format("POT_WIRING_DIAGNOSTIC display=\"%s\"", display.read_raw()))

reg.pass("analog_pot_wiring", string.format("taps=%u", taps.tap_count()))
