-- Del 0 quarantine note: this script is written independently this
-- session, from measurement, per the explicit instruction not to start
-- from the background fork's material. $F87FD2 is ROM (static, in the
-- boot .bin images) -- unlike the OS-loaded RAM code from the previous
-- task, this can in principle be checked against a static ROM dump too,
-- but is read live here for consistency with the rest of this probe.
--
-- Dumps: the scheduler scan-loop tail area ($F87F80-$F87FE0, covering
-- the already-known $F87F92-$F87FD0 loop plus whatever follows at
-- $F87FD2), and the six scheduler slots themselves (base $23F6, stride
-- $16) after FILE LOADED.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  print(string.format("SFDD boot_timeout final_display=\"%s\"", text))
  manager.machine:exit()
  return
end
press(":panel:buttons_0", 1 << 0x0a)
press(":panel:buttons_32", 1 << 0x03)
press(":panel:buttons_0", 1 << 0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))

local function dump(label, addr, len)
  local bytes = {}
  for i = 0, len - 1 do
    bytes[#bytes + 1] = string.format("%02X", prog:read_u8(addr + i) & 0xff)
  end
  print(string.format("SFDD_DUMP label=%s addr=%06X len=%u bytes=%s", label, addr, len, table.concat(bytes, "")))
end

-- Independently re-read the scheduler base/limit from lowmem, not
-- assumed from any prior document.
local base = prog:read_u16(0x0000c6) & 0xffff
local limit = prog:read_u16(0x0000c8) & 0xffff
print(string.format("SFDD_TABLE base=%04X limit=%04X slots=%u", base, limit, (limit - base) // 0x16))

dump("SCAN_LOOP_TAIL", 0x00f87f80, 0x70)

for i = 0, 5 do
  local slot = base + i * 0x16
  dump(string.format("SLOT%d", i), slot, 0x16)
end

print("SFDD_DONE")
manager.machine:exit()
