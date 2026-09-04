-- Dumps TRAP #3/#4 handler bytes ($F88078/$F880A2, found via
-- trap-3-4-key-press-verify.lua) for offline disassembly.

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
  print(string.format("THMD boot_timeout final_display=\"%s\"", text))
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
  print(string.format("THMD_DUMP label=%s addr=%06X len=%u bytes=%s", label, addr, len, table.concat(bytes, "")))
end

dump("TRAP3_F88078", 0x00f88078, 0x80)
dump("TRAP4_F880A2", 0x00f880a2, 0x80)
-- Also the full $3(exception vector table) trap dispatch prologue
-- (vector 32-47 = $80-$BF in the table), to see how TRAP #n reaches
-- these specific handlers -- shared stub or distinct per-trap entries.
dump("VEC32_47_TABLE", 0x00000080, 0x40)

print("THMD_DONE")
manager.machine:exit()
