-- Closes the sample topology question numerically
-- (docs/asr10/investigations/sample-ram-and-voice-registers.md follow-up):
-- (a) re-confirm voice 0's START/END/ACCUM and test membership in the
-- word range corresponding to CPU $100000-$1FFFFF ($00000-$7FFFF);
-- (b) characterize what the loaded 172544-byte JM DIGI SYN payload
-- actually contains (sample-like vs parameter-like), so the "$100000
-- stays empty" finding isn't mistaken for an anomaly if this particular
-- file barely carries sample data in the first place; (c) dump the full
-- root directory so a sample-heavier instrument can be picked instead if
-- needed. Reuses the already-verified voice-register decoder
-- (sample-ram-and-voice-registers-probe.lua) and IDMA-arm tap
-- (file-loaded-verification-probe.lua) techniques, not re-derived.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

-- ===== voice register decoder (verified in sample-ram-and-voice-registers-probe.lua) =====
local acc_bytes = {}
local current_page = 0
local voices = {}

local function finalize_register(register_index, value32)
  if register_index == 15 then
    current_page = value32 & 0x7f
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
    elseif register_index == 1 then
      v.start = value32 & 0xfffff800
    elseif register_index == 2 then
      v["end"] = value32 & 0xffffff80
    elseif register_index == 3 then
      v.accum = value32
    end
  elseif group == "low" and register_index == 0 then
    v.cr = value32 & 0xffff
    v.bank = (v.cr >> 14) & 3
  end
end

taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "stp_es5506_w", function(offset, data, mask)
  local v = data & 0xff
  local word_offset = (offset - 0xfc2000) // 2
  local register_index = word_offset // 4
  local byte_in_reg = word_offset % 4
  acc_bytes[byte_in_reg + 1] = v
  if byte_in_reg == 3 then
    local b0, b1, b2, b3 = acc_bytes[1] or 0, acc_bytes[2] or 0, acc_bytes[3] or 0, acc_bytes[4] or 0
    finalize_register(register_index, (b0 << 24) | (b1 << 16) | (b2 << 8) | b3)
    acc_bytes = {}
  end
  return nil
end)

-- ===== IDMA arm tap (verified in file-loaded-verification-probe.lua) =====
-- mc68302_device::install_internal_window() re-issues
-- install_readwrite_handler() for the WHOLE $FC6000-$FC6FFF SIB window on
-- every BAR write (methods-static-analysis.md #8.5), silently dropping
-- any tap installed on that range beforehand. The BAR write happens at
-- t~5.4s, well before FILE 1 (~t=16.3s) -- an earlier version of this
-- script installed the tap at script start and got zero calls for the
-- entire run (idma_tap_calls=0) as a direct result. Fixed by installing
-- only after FILE 1, matching file-loaded-verification-probe.lua's own
-- established fix for the same trap.
local dapr_hi, dapr_lo, bcr = 0, 0, 0
local arms = {}
local function install_idma_tap()
  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "stp_idma_w", function(offset, data, mask)
    if offset == 0x00fc6808 then dapr_hi = data & 0xffff end
    if offset == 0x00fc680a then dapr_lo = data & 0xffff end
    if offset == 0x00fc680c then bcr = data & 0xffff end
    if offset == 0x00fc6802 and (data & 1) ~= 0 and bcr > 0 then
      local dapr = (dapr_hi << 16) | dapr_lo
      arms[#arms + 1] = { dapr = dapr, bytes = bcr - 1 }
    end
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
local file_loaded = "FILE L0ADED           "

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("sample_topology_and_payload_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

-- Full root directory dump (base $544, stride 26, up to 40 entries) --
-- disk-read-path.md established the base/stride; per-entry field
-- semantics beyond the name were never decoded, so only raw bytes and
-- the printable name substring are reported, not a size interpretation.
print("STP_DIRECTORY")
for i = 0, 39 do
  local base = 0x544 + i * 26
  local bytes = {}
  local printable = {}
  local any_nonzero = false
  for j = 0, 25 do
    local b = prog:read_u8(base + j) & 0xff
    bytes[#bytes + 1] = string.format("%02X", b)
    if b ~= 0 then any_nonzero = true end
    printable[#printable + 1] = (b >= 0x20 and b < 0x7f) and string.char(b) or "."
  end
  if any_nonzero then
    print(string.format("STP_DIRENTRY idx=%d addr=%06X text=\"%s\" bytes=%s", i, base, table.concat(printable), table.concat(bytes, " ")))
  end
end

install_idma_tap()

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("sample_topology_and_payload_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
print(string.format("STP_LOADED t=%.6f arms=%u", now(), #arms))

-- Let the ~100ms voice-register sweep (sample-ram-and-voice-registers.md:
-- runs from boot through ~FILE LOADED, then stops) finish settling.
emu.wait(emu.attotime.from_msec(500))

-- ===== (a) voice 0 numeric topology check =====
local v0 = voices[0]
if v0 then
  local function word_to_cpu_byte(word_addr) return 0x100000 + word_addr * 2 end
  local start_word = (v0.start or 0) >> 11
  local end_word = (v0["end"] or 0) >> 11
  local accum_word = (v0.accum or 0) >> 11
  print(string.format("STP_VOICE0 cr=%04X bank=%s start_raw=%08X end_raw=%08X accum_raw=%08X",
    v0.cr or 0, tostring(v0.bank), v0.start or 0, v0["end"] or 0, v0.accum or 0))
  print(string.format("STP_VOICE0_WORDS start_word=%05X end_word=%05X accum_word=%05X in_range_0_7FFFF: start=%s end=%s accum=%s",
    start_word, end_word, accum_word,
    tostring(start_word <= 0x7ffff), tostring(end_word <= 0x7ffff), tostring(accum_word <= 0x7ffff)))
  print(string.format("STP_VOICE0_CPU_BYTES start=%06X end=%06X accum=%06X",
    word_to_cpu_byte(start_word), word_to_cpu_byte(end_word), word_to_cpu_byte(accum_word)))
else
  print("STP_VOICE0 not_captured")
end

-- ===== (b) payload content characterization, read from live memory (the
-- already-verified byte-identical-to-disk destination), not from the
-- disk image directly (arm destinations are not disk-contiguous -- other
-- files' sectors interleave in the disk offset range, but not in the
-- IDMA destination addresses, which are the file's own bytes only). =====
local total_bytes = 0
local zero_bytes = 0
local value_counts = {}
local prev_byte = nil
local delta_sum = 0
local delta_n = 0

for _, a in ipairs(arms) do
  prev_byte = nil -- do not compute a delta across a jump to a new arm's
                   -- (possibly non-contiguous) destination address
  for i = 0, a.bytes - 1 do
    local b = prog:read_u8(a.dapr + i) & 0xff
    total_bytes = total_bytes + 1
    if b == 0 then zero_bytes = zero_bytes + 1 end
    value_counts[b] = (value_counts[b] or 0) + 1
    if prev_byte ~= nil then
      delta_sum = delta_sum + math.abs(b - prev_byte)
      delta_n = delta_n + 1
    end
    prev_byte = b
  end
end

local distinct_values = 0
for _ in pairs(value_counts) do distinct_values = distinct_values + 1 end

print(string.format("STP_PAYLOAD arms=%u total_bytes=%u zero_bytes=%u zero_pct=%.2f distinct_values=%u/256 mean_abs_delta=%.2f",
  #arms, total_bytes, zero_bytes, (total_bytes > 0) and (100.0 * zero_bytes / total_bytes) or 0,
  distinct_values, (delta_n > 0) and (delta_sum / delta_n) or 0))

print(string.format("STP_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("sample_topology_and_payload_probe", string.format("voice0_captured=%s payload_bytes=%u", tostring(v0 ~= nil), total_bytes))
