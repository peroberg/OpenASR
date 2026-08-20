-- Dumps raw bytes around the panel-dispatch targets named in
-- panel-protocol-state-machine.md ($FFB20A, $FFB0E0, $FFB3C0, $FFB43E,
-- $FFB488, $FFB4CC) plus $F82484/$F89CE8/$F89CEA (ROM path), for offline
-- disassembly. These addresses are live RAM (OS-loaded), not static ROM
-- content, so they must be read from a running, booted machine, not a
-- .bin file -- docs/asr10/investigations/keyboard-and-sample-bridge-2.md.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  print(string.format("PDMD boot_timeout final_display=\"%s\"", text))
  manager.machine:exit()
  return
end
emu.wait(emu.attotime.from_msec(500))

local function dump(label, addr, len)
  local bytes = {}
  for i = 0, len - 1 do
    bytes[#bytes + 1] = string.format("%02X", prog:read_u8(addr + i) & 0xff)
  end
  print(string.format("PDMD_DUMP label=%s addr=%06X len=%u bytes=%s", label, addr, len, table.concat(bytes, "")))
end

dump("FFB20A", 0x00ffb20a, 0x80)
dump("FFB0E0", 0x00ffb0e0, 0x80)
dump("FFB3C0", 0x00ffb3c0, 0x80)
dump("FFB43E", 0x00ffb43e, 0x60)
dump("FFB488", 0x00ffb488, 0x60)
dump("FFB4CC", 0x00ffb4cc, 0x60)
dump("F82484_TABLE", 0x00f82484, 0x100)
dump("F89CE0", 0x00f89ce0, 0x60)
dump("F89CC0", 0x00f89cc0, 0x60)
dump("CCD1", 0x0000ccd1, 0x02)

print("PDMD_DONE")
manager.machine:exit()
