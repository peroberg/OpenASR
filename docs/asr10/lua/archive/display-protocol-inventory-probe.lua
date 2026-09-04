-- Del 1: inventory the full display byte stream (host -> panel, DUART
-- channel B THRB) across boot, instrument load, a note press, and
-- navigation across several parameter pages. Tap the whole $FC4800-
-- $FC481F DUART CS window (word-addressed, offset&0xf = SCN2681
-- register index) rather than assume THRB's exact byte address, so the
-- measurement is self-checking against the known register map instead
-- of guessing it.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

local stream = {}  -- {t=, data=, offset=, reg=}
taps[#taps + 1] = prog:install_write_tap(0x00fc4800, 0x00fc481f, "duart_w", function(offset, data, mask)
  -- offset is in words (0-15); low byte lane (odd CPU address) carries
  -- the actual register data per duart_panel_asr_candidate_w.
  if (mask & 0x00ff) == 0 then return nil end
  stream[#stream + 1] = { t = now(), data = data & 0xff, word = offset & 0xf }
  return nil
end)

local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return false end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
  return true
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("display_protocol", string.format("boot_timeout display=\"%s\"", text))
  return
end
print(string.format("DPI_MARK label=post_boot t=%.6f stream_len=%u", now(), #stream))

-- Instrument load (established sequence).
press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))
print(string.format("DPI_MARK label=post_load t=%.6f stream_len=%u", now(), #stream))

-- Select instrument, then a note press.
press_button(0x02, 500)
print(string.format("DPI_MARK label=post_select t=%.6f stream_len=%u", now(), #stream))

local key_port = manager.machine.ioport.ports[":panel:keys_0"]
local key_field = key_port and key_port:field(0x00000001) or nil
if key_field then
  key_field:set_value(1)
  emu.wait(emu.attotime.from_msec(300))
  key_field:clear_value()
  emu.wait(emu.attotime.from_msec(500))
end
print(string.format("DPI_MARK label=post_note t=%.6f stream_len=%u", now(), #stream))

-- Navigate a handful of distinct parameter pages (from the prior
-- session's 64-button sweep): Sample-Source Select, Create New
-- Instrument, Edit Pitch Table, FX Select, and REC SRC Field 2 cycling
-- (a known place with repeated field updates -- most likely to exercise
-- single-field/cursor update codes rather than full-line redraws).
for _, code in ipairs({0x20, 0x06, 0x18, 0x07}) do
  press_button(code, 300)
  print(string.format("DPI_MARK label=page_%02X t=%.6f stream_len=%u display=\"%s\"",
    code, now(), #stream, display.read_raw()))
end

-- REC SRC Field 2 cycling: from the REC SRC screen, press Down twice
-- and Up once (LEFT -> RIGHT -> L+R -> RIGHT), each a small update.
press_button(0x20, 300)
print(string.format("DPI_MARK label=recsrc_enter t=%.6f stream_len=%u", now(), #stream))
press_button(0x0a, 300)
print(string.format("DPI_MARK label=recsrc_down1 t=%.6f stream_len=%u", now(), #stream))
press_button(0x0a, 300)
print(string.format("DPI_MARK label=recsrc_down2 t=%.6f stream_len=%u", now(), #stream))
press_button(0x0b, 300)
print(string.format("DPI_MARK label=recsrc_up1 t=%.6f stream_len=%u", now(), #stream))
press_button(0x20, 300)
print(string.format("DPI_MARK label=recsrc_redraw t=%.6f stream_len=%u display=\"%s\"",
  now(), #stream, display.read_raw()))

print(string.format("DPI_SUMMARY witness=%u stream_len=%u", witness_writes, #stream))

-- Per-register-index histogram (which SCN2681 registers were written at
-- all, to confirm which one is actually THRB before trusting the
-- filtered stream below).
local by_word = {}
for _, e in ipairs(stream) do
  by_word[e.word] = (by_word[e.word] or 0) + 1
end
for word = 0, 15 do
  if by_word[word] then
    print(string.format("DPI_REGHIST word=%X count=%u", word, by_word[word]))
  end
end

-- Full byte stream for the register that dominates (expected: THRB,
-- word index 0x0B per the standard SCN2681 register map -- printed
-- generically so a wrong assumption shows up as a low count instead of
-- silently filtering the wrong register).
local best_word, best_count = nil, -1
for word, count in pairs(by_word) do
  if count > best_count then best_word, best_count = word, count end
end
print(string.format("DPI_BEST_WORD word=%X count=%u", best_word, best_count))

local index = 0
for _, e in ipairs(stream) do
  if e.word == best_word then
    local printable = (e.data >= 0x20 and e.data <= 0x7e) and string.char(e.data) or "."
    print(string.format("DPI_BYTE[%u] t=%.6f data=%02X ch=%s", index, e.t, e.data, printable))
    index = index + 1
  end
end

manager.machine:exit()
