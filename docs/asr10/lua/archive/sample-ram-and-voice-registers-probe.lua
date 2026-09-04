-- Del 1 + Del 2 of the sound-path mapping task
-- (docs/asr10/investigations/sample-ram-and-voice-registers.md). Pure
-- observation, no ES5506/ES5510/mem_map code changed.
--
-- Del 1: does anything ever write to the CPU's own $100000-$1FFFFF RAM
-- (mem_map's "sample RAM candidate"), from reset to >=10s past FILE
-- LOADED? Tap bracketed with a self-triggered write/read at both ends of
-- the window to prove liveness (methods-static-analysis.md #8.7 -- a
-- zero-result tap is invalid without a witness for the whole window).
--
-- Del 2: decode the ES5506 host register writes ($FC2000-$FC207F) into
-- per-voice CR/START/END/ACCUM, using the exact page/register dispatch in
-- es5506_device::write()/reg_write_low()/reg_write_high()
-- (src/devices/sound/es5506.cpp, read but not modified this task):
--   write(): m_current_page<0x20 -> reg_write_low, <0x40 -> reg_write_high,
--   else -> reg_write_test. Register index = word_offset/4, byte lane
--   accumulates big-endian across 4 consecutive word slots.
--   reg_write_high: CR=0 (&0xffff), START=1 (&0xfffff800),
--   END=2 (&0xffffff80), ACCUM=3 (no mask, straight through
--   get_address_acc_shifted_val). get_bank(control)=(control>>14)&3.
--   get_accum_mask(21,11) in device_start() gives m_address_acc_shift =
--   ADDRESS_FRAC_BIT(11) - 11 = 0 for ES5506 specifically -- the shift is
--   a no-op, so the raw masked register value *is* the internal
--   accumulator value, and integer_addr = raw >> 11 (11 fraction bits,
--   per ADDRESS_FRAC_BIT_ES5506) is the word address read_sample() will
--   actually index into the selected bank's own address space.
-- Reported as raw captured values plus this sourced arithmetic, not a
-- free interpretation.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

-- ===== Del 1: $100000-$1FFFFF write witness =====
-- No known writer exists for this range (that is the open question), so a
-- self-triggered debug write cannot be trusted as a witness -- it is
-- unclear whether space:write_u8() from Lua even routes through installed
-- taps the same way a real CPU bus write does. Instead: install an
-- identical tap, at the same time, with the same method, on a range
-- already independently proven hot in this exact load sequence
-- (file-loaded-verification-probe.md: IDMA destinations $000944-$0552FF,
-- 21 arms, 172544 bytes). If that sibling tap fires as expected while
-- $100000-$1FFFFF stays silent, the zero result is proven real, not a
-- dead tap -- same install method, same script, same run, one arm hot,
-- one arm cold.
local samram_writes = {}
taps[#taps + 1] = prog:install_write_tap(0x100000, 0x1fffff, "srvr_samram_w", function(offset, data, mask)
  samram_writes[#samram_writes + 1] = { t = now(), offset = offset, data = data, mask = mask }
  return nil
end)

local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x05ffff, "srvr_witness_w", function(offset, data, mask)
  witness_writes = witness_writes + 1
  return nil
end)

-- ===== Del 2: ES5506 voice register decoder =====
-- Mirrors es5506_device::write()'s own accumulation exactly (single
-- shared 4-byte big-endian latch, register selected by word_offset/4,
-- byte lane by word_offset%4), not a guess at the protocol.
local acc_bytes = {}
local acc_register = nil
local current_page = 0
local voices = {} -- voices[n] = { cr=, bank=, start=, start_int=, end_=, end_int=, accum=, accum_int= }
local page_writes = {}
local voice_writes = {}

local function finalize_register(register_index, value32, t)
  if register_index == 15 then
    current_page = value32 & 0x7f
    page_writes[#page_writes + 1] = { t = t, page = current_page, voice = current_page & 0x1f, group = (current_page < 0x20) and "low" or ((current_page < 0x40) and "high" or "test") }
    return
  end

  local voice_n = current_page & 0x1f
  local group = (current_page < 0x20) and "low" or ((current_page < 0x40) and "high" or "test")
  voices[voice_n] = voices[voice_n] or {}
  local v = voices[voice_n]

  if group == "high" then
    if register_index == 0 then
      v.cr = value32 & 0xffff
      v.bank = (v.cr >> 14) & 3
      voice_writes[#voice_writes + 1] = { t = t, voice = voice_n, field = "CR", raw = v.cr, note = string.format("bank=%d", v.bank) }
    elseif register_index == 1 then
      v.start = value32 & 0xfffff800
      v.start_int = v.start >> 11
      voice_writes[#voice_writes + 1] = { t = t, voice = voice_n, field = "START", raw = v.start, note = string.format("int_addr=%08X", v.start_int) }
    elseif register_index == 2 then
      v["end"] = value32 & 0xffffff80
      v.end_int = v["end"] >> 11
      voice_writes[#voice_writes + 1] = { t = t, voice = voice_n, field = "END", raw = v["end"], note = string.format("int_addr=%08X", v.end_int) }
    elseif register_index == 3 then
      v.accum = value32
      v.accum_int = v.accum >> 11
      voice_writes[#voice_writes + 1] = { t = t, voice = voice_n, field = "ACCUM", raw = v.accum, note = string.format("int_addr=%08X", v.accum_int) }
    end
  elseif group == "low" then
    if register_index == 0 then
      v.cr = value32 & 0xffff
      v.bank = (v.cr >> 14) & 3
      voice_writes[#voice_writes + 1] = { t = t, voice = voice_n, field = "CR(low-page)", raw = v.cr, note = string.format("bank=%d", v.bank) }
    end
  end
end

taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "srvr_es5506_w", function(offset, data, mask)
  -- Diagnostic run (es5506-offset-diagnostic.lua) established that for
  -- THIS range, MAME's tap delivers offset as the raw, EVEN CPU byte
  -- address (never odd), mask constantly 0x000000FF, data's low byte the
  -- real value -- unlike the odd-lane FDC/DUART/IDMA taps used elsewhere
  -- in this project. Verified against the PAGE register: FC2078/7A/7C/7E
  -- (raw, even) divide by 2 to device offsets 60/61/62/63 -> register
  -- index 15 (PAGE), byte_in_reg 0/1/2/3 in order -- exactly matching
  -- es5506_device::write()'s own offset&3 accumulation requirement.
  -- byte_address()/byte_value() (the odd-lane convention) must NOT be
  -- used here; an earlier draft of this script did, and silently dropped
  -- >99% of writes as accumulator desyncs (194 of ~3170 expected
  -- register-completions) before this was caught and fixed.
  local v = data & 0xff
  local word_offset = (offset - 0xfc2000) // 2
  local register_index = word_offset // 4
  local byte_in_reg = word_offset % 4

  -- es5506_device::write() (es5506.cpp:1328-1353): m_write_latch is a
  -- SINGLE SHARED accumulator across every register, not per-register --
  -- there is no "start of burst" gate. Every write updates only its own
  -- byte lane (shift = 8*(offset&3)); a dispatch fires whenever
  -- offset&3==3, using whatever is *currently* in the other three lanes
  -- (stale values from an earlier, possibly different, register write --
  -- harmless for registers masked down to their low byte(s), like PAGE,
  -- where firmware only ever sends lane 3). The accumulator is zeroed
  -- only *after* a dispatch. An earlier draft of this decoder required
  -- byte_in_reg==0 to "start" a fresh accumulation and silently dropped
  -- every lane-3-only write as a desync -- undercounting PAGE writes from
  -- (at minimum) dozens to 2, and by extension losing every high-page
  -- START/END/ACCUM write that followed a page change written this way.
  acc_bytes[byte_in_reg + 1] = v
  if byte_in_reg == 3 then
    local b0 = acc_bytes[1] or 0
    local b1 = acc_bytes[2] or 0
    local b2 = acc_bytes[3] or 0
    local b3 = acc_bytes[4] or 0
    local value32 = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3
    finalize_register(register_index, value32, now())
    acc_bytes = {}
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

print(string.format("SRVR_WITNESS label=start t=%.6f witness_writes=%u", now(), witness_writes))

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("sample_ram_and_voice_registers_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("SRVR_FILE1 t=%.6f samram_writes=%u", now(), #samram_writes))

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("sample_ram_and_voice_registers_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
local t_loaded = now()
print(string.format("SRVR_LOADED t=%.6f samram_writes=%u witness_writes=%u", t_loaded, #samram_writes, witness_writes))

-- 10s beyond FILE LOADED, not just to it -- a post-load copy could happen
-- after the display already updated.
local deadline = t_loaded + 10
while now() < deadline do
  emu.wait(emu.attotime.from_msec(200))
end

print(string.format("SRVR_WITNESS label=end t=%.6f witness_writes=%u", now(), witness_writes))

print(string.format("SRVR_SAMRAM_TOTAL writes=%u t=%.6f", #samram_writes, now()))
for _, w in ipairs(samram_writes) do
  print(string.format("SRVR_SAMRAM_WRITE t=%.6f offset=%06X data=%04X mask=%04X", w.t, w.offset, w.data, w.mask))
end

print(string.format("SRVR_PAGE_WRITES count=%u", #page_writes))
for _, p in ipairs(page_writes) do
  print(string.format("SRVR_PAGE t=%.6f page=%02X voice=%d group=%s", p.t, p.page, p.voice, p.group))
end

print(string.format("SRVR_VOICE_WRITES count=%u", #voice_writes))
for _, w in ipairs(voice_writes) do
  print(string.format("SRVR_VOICE_WRITE t=%.6f voice=%d field=%s raw=%08X %s", w.t, w.voice, w.field, w.raw, w.note))
end

print("SRVR_FINAL_VOICE_STATE")
for n = 0, 31 do
  local v = voices[n]
  if v then
    print(string.format("SRVR_VOICE n=%d cr=%04X bank=%s start=%08X start_int=%08X end=%08X end_int=%08X accum=%08X accum_int=%08X",
      n, v.cr or 0, tostring(v.bank), v.start or 0, v.start_int or 0, v["end"] or 0, v.end_int or 0, v.accum or 0, v.accum_int or 0))
  end
end

print(string.format("SRVR_SUMMARY final_display=\"%s\" samram_writes=%u voices_touched=%u page_writes=%u t=%.6f",
  display.read_raw(), #samram_writes, (function() local c=0 for _ in pairs(voices) do c=c+1 end return c end)(), #page_writes, now()))
reg.pass("sample_ram_and_voice_registers_probe", string.format("samram_writes=%u voices_touched=%u", #samram_writes,
  (function() local c=0 for _ in pairs(voices) do c=c+1 end return c end)()))
