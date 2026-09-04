-- Static memory dump supporting keyboard-and-sample-bridge-4.md's Del 2/3
-- trace: MIDI note-on byte parsing ($F88960-$F88A60), the message-type
-- jump table ($FF871E table entries / $FF877E-$FF87A4 vector list), the
-- shared panel+MIDI completion consumer ($FFB43E/$FFB6C4/$FFB56E), and
-- the TRAP #D deferred-continuation primitive ($F881F6/$F884FC). Dumped
-- once after FILE 1 (all of this is static ROM/firmware code, unaffected
-- by runtime state) and disassembled offline via the project's
-- scratchpad-only Capstone pipeline -- not part of this repo.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function dump(label, addr, len)
  local bytes = {}
  for i = 0, len - 1 do
    bytes[#bytes + 1] = string.format("%02X", prog:read_u8(addr + i) & 0xff)
  end
  print(string.format("DUMP label=%s addr=%06X len=%u bytes=%s", label, addr, len, table.concat(bytes, "")))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("completion_consumer_static_dump", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

-- MIDI byte reception and status/data classification.
dump("F8899X", 0x00f88960, 0x100)
-- Note-on data-byte handler (stores note into $87C4, dispatches on
-- nonzero note value to the message-type vector table).
dump("F88AA2", 0x00f88aa2, 0xC0)
-- The message-type jump-table body (indices computed from (status&0x70)
-- >> 2, table entries at $FF871E + 4*index, longword pointers).
dump("FF8700", 0x00ff8700, 0x40)
-- The parallel short-jump vector list right after $FF877E's single
-- entry jump -- $FF8788 (one of its slots) is the actual target our
-- note-on's nonzero-note path reaches, landing on $FFB43E, the
-- documented panel completion consumer (panel-completion-consumer-v350.md).
dump("FF877E", 0x00ff877e, 0x80)
-- The shared panel+MIDI completion consumer and its two callees.
dump("FFB43E", 0x00ffb43e, 0x60)
dump("FFB6C4", 0x00ffb6c4, 0x40)
dump("FFB56E", 0x00ffb56e, 0x40)
-- TRAP #D (vector 45) -- the deferred-continuation queue primitive
-- $FFB56E's $B55A/trap#d path posts into.
local trapd_addr = prog:read_u32((32 + 13) * 4) & 0x00ffffff
print(string.format("TRAPD_HANDLER addr=%06X", trapd_addr))
dump("TRAPD", trapd_addr, 0x60)
dump("14C0", 0x0014c0, 0x20)
-- The continuation $14C0's function pointer led to.
dump("F884FC", 0x00f884fc, 0x80)

print("DONE")
manager.machine:exit()
