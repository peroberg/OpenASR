-- 11th regression test: stereo RECORD/start produces genuinely distinct,
-- channel-correct data -- not the same content duplicated into both
-- channels (mono in two copies) and not a swapped/merged L/R.
--
-- investigations/stereo-round-trip-verification.md: the straightforward
-- per-channel sequential feed (scc_rx_record_probe.lua, proven for mono)
-- does not work for stereo -- completing SCC1's own descriptor (vector
-- $4D) immediately triggers TWO $37A1 IDMA starts/completions in the same
-- event, using whatever is already in SCC2's buffer at that instant, not
-- waiting for SCC2's own independent completion. This matches a real
-- synchronized stereo ADC delivering L+R in lockstep: firmware uses one
-- channel's completion as the "pair ready" signal. Fix: feed both
-- channels interleaved (one byte to SCC2RX, one to SCC1RX, repeating) so
-- channel 2's buffer already holds its own distinct pattern by the time
-- channel 1's last byte fires the combined completion.
--
-- LEFT (SCC1) = a 0-255 ramp carrying the $7FFF-style threshold-scan
-- trigger bytes at offset $40/$41. RIGHT (SCC2) = constant $AA, same
-- trigger bytes -- genuinely different content, not the same pattern in
-- both channels, so a channel swap or merge is distinguishable from a
-- correct result.
--
-- Without this test, nothing in the suite reads back and compares what a
-- stereo recording actually stores -- it was this gap that let the
-- F00000-F7FFFF off-by-one (both SCC buffers live there) go undetected.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
local rx_state = { [1] = cpu.state["SCC1RX"], [2] = cpu.state["SCC2RX"] }
local taps = {}

local function now() return emu.time() end

local function left_pattern(offset)
  if offset == 0x40 then return 0x7f end
  if offset == 0x41 then return 0xff end
  return offset & 0xff
end
local function right_pattern(offset)
  if offset == 0x40 then return 0x7f end
  if offset == 0x41 then return 0xff end
  return 0xaa
end

local objects = {
  [1] = { pointer = 0x12d8 },  -- LEFT / SCC1
  [2] = { pointer = 0x1320 },  -- RIGHT / SCC2
}

local iack = {}
local idma_transfers = {}
local witness_writes = 0

for level = 1, 7 do
  local expected = 0x00fffff0 + level * 2
  taps[#taps + 1] = cpu_space:install_read_tap(0, 0xffffff,
    string.format("srt_iack_%d", level), function(offset, data)
      if offset ~= expected then return nil end
      local key = string.format("L%d:%02X", level, data & 0xff)
      iack[key] = (iack[key] or 0) + 1
      return nil
    end)
end

taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "srt_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

for channel = 1, 2 do
  if not rx_state[channel] or not rx_state[channel].writeable then
    reg.fail("stereo_round_trip", string.format("missing_writeable_SCC%uRX_state", channel))
    return
  end
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("stereo_round_trip", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- SS8.5: $FC6000-$FC6FFF taps must be installed after the last BAR write
-- (install_internal_window() re-issues the handler on every BAR write and
-- silently drops any earlier tap). Installing here, after FILE 1, matches
-- the already-established safe pattern.
taps[#taps + 1] = prog:install_write_tap(0x00fc6802, 0x00fc6803,
  "srt_idma_start", function(offset, data, mask)
    if (data & 0xffff) == 0x37a1 then
      idma_transfers[#idma_transfers + 1] = {
        source = prog:read_u32(0x00fc6804) & 0x00ffffff,
        destination = prog:read_u32(0x00fc6808) & 0x00ffffff,
        length = prog:read_u16(0x00fc680c) & 0xffff,
      }
    end
    return nil
  end)

press_button(0x20, 500)       -- Sample-Source Select
press_button(0x0a, 250)       -- cycle mode 0 -> 1
press_button(0x0a, 250)       -- cycle mode 1 -> 2 (L+R)
local mode = prog:read_u8(0x016f)
if mode ~= 2 then
  reg.fail("stereo_round_trip", string.format("source_mode=%u expected=2", mode))
  return
end

press_button(0x02, 1000)      -- Level Detect
if prog:read_u16(0x0d04) ~= 1 then
  reg.fail("stereo_round_trip", string.format("level_detect_state=%04X display=\"%s\"",
    prog:read_u16(0x0d04), display.read_raw()))
  return
end
for _ = 1, 24 do press_button(0x0a, 40) end   -- lower threshold
press_button(0x23, 100)        -- Enter-Yes: RECORD/start
emu.wait(emu.attotime.from_seconds(1))
if prog:read_u16(0x0d04) ~= 2 then
  reg.fail("stereo_round_trip", string.format("pre_rx_state=%04X display=\"%s\"",
    prog:read_u16(0x0d04), display.read_raw()))
  return
end

-- Interleaved feed: RIGHT byte first, then LEFT byte, each iteration --
-- so channel 2's buffer is always at least as far filled as channel 1's
-- when channel 1's own completion (the observed combined trigger) fires.
for offset = 0, 0x031f do
  rx_state[2].value = right_pattern(offset)
  rx_state[1].value = left_pattern(offset)
end

local ok_wait = false
local deadline = now() + 3
while now() < deadline do
  if (iack["L4:4B"] or 0) >= 2 then ok_wait = true break end
  emu.wait(emu.attotime.from_msec(10))
end
emu.wait(emu.attotime.from_seconds(1))

print(string.format("SRT_RESULT wait_ok=%s iack4d=%u iack4a=%u iack4b=%u witness=%u display=\"%s\"",
  tostring(ok_wait), iack["L4:4D"] or 0, iack["L4:4A"] or 0, iack["L4:4B"] or 0,
  witness_writes, display.read_raw()))

if not ok_wait then
  reg.fail("stereo_round_trip", string.format(
    "iack4b=%u display=\"%s\"", iack["L4:4B"] or 0, display.read_raw()))
  return
end

local left_dest = prog:read_u32((prog:read_u32(objects[1].pointer) & 0x00ffffff) + 0x20) & 0x00ffffff
local right_dest = prog:read_u32((prog:read_u32(objects[2].pointer) & 0x00ffffff) + 0x20) & 0x00ffffff

-- scc-idma-transfer.md: firmware retains pretrigger history and selects
-- its own sub-window of the written buffer, so the transferred bytes are
-- NOT pattern(0..length-1) -- they are pattern(k..k+length-1) for some
-- firmware-chosen k. Comparing source against destination verifies the
-- IDMA copy exactly, with no offset ambiguity; comparing the *shape* of
-- the destination content against each candidate pattern (not an assumed
-- fixed offset) verifies which pattern actually arrived, and that the
-- other channel's pattern does NOT also fit (the swap/merge cross-check).
local function verify(label, transfer, pattern, other_pattern)
  if not transfer then
    print(string.format("SRT_VERIFY label=%s NO_TRANSFER_FOUND", label))
    return false
  end

  local copy_matches, copy_mismatches = 0, 0
  for offset = 0, transfer.length - 1 do
    local src = prog:read_u8(transfer.source + offset) & 0xff
    local dst = prog:read_u8(transfer.destination + offset) & 0xff
    if src == dst then copy_matches = copy_matches + 1 else copy_mismatches = copy_mismatches + 1 end
  end

  local function best_fit(candidate_pattern)
    local best_k, best_score = 0, -1
    for k = 0, 128 do
      local score = 0
      for offset = 0, math.min(63, transfer.length - 1) do
        if (prog:read_u8(transfer.destination + offset) & 0xff) == candidate_pattern(k + offset) then
          score = score + 1
        end
      end
      if score > best_score then best_score, best_k = score, k end
    end
    return best_k, best_score
  end

  local own_k, own_score = best_fit(pattern)
  local other_k, other_score = best_fit(other_pattern)
  local sample_size = math.min(64, transfer.length)

  print(string.format(
    "SRT_VERIFY label=%s destination=%06X length=%u copy_matches=%u copy_mismatches=%u own_score=%u/%u other_score=%u/%u",
    label, transfer.destination, transfer.length, copy_matches, copy_mismatches,
    own_score, sample_size, other_score, sample_size))

  return copy_mismatches == 0 and own_score == sample_size and other_score < sample_size
end

local function find_transfer_ending_at(dest_end)
  for _, t in ipairs(idma_transfers) do
    if t.destination + t.length == dest_end then return t end
  end
  return nil
end

local left_transfer = find_transfer_ending_at(left_dest)
local right_transfer = find_transfer_ending_at(right_dest)

local left_ok = verify("LEFT", left_transfer, left_pattern, right_pattern)
local right_ok = verify("RIGHT", right_transfer, right_pattern, left_pattern)

if witness_writes == 0 then
  reg.fail("stereo_round_trip", "no_witness_activity")
  return
end

if left_ok and right_ok then
  reg.pass("stereo_round_trip", string.format(
    "left_bytes=%u right_bytes=%u iack4b=%u witness=%u",
    left_transfer.length, right_transfer.length, iack["L4:4B"] or 0, witness_writes))
else
  reg.fail("stereo_round_trip", string.format(
    "left_ok=%s right_ok=%s witness=%u",
    tostring(left_ok), tostring(right_ok), witness_writes))
end
