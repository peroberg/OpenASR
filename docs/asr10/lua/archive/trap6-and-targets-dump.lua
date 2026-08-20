-- Follow-up dump: TRAP #6 handler (vector38=$F880D6, per the earlier
-- live vector-table dump) and its two callers' branch targets ($740C,
-- $FF9650) -- slot2/slot3's dispatch targets both call TRAP #6
-- immediately, found this session via scheduler-key-press-correlation.lua.

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
  print(string.format("T6TD boot_timeout final_display=\"%s\"", text))
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
  print(string.format("T6TD_DUMP label=%s addr=%06X len=%u bytes=%s", label, addr, len, table.concat(bytes, "")))
end

local trap6_addr = prog:read_u32(38 * 4) & 0x00ffffff
print(string.format("T6TD_TRAP6_HANDLER addr=%06X", trap6_addr))
dump("TRAP6", trap6_addr, 0x60)
dump("TARGET_740C", 0x0000740c, 0x40)
dump("TARGET_FF9650", 0x00ff9650, 0x40)
-- Also the not-yet-disassembled bridge code at $F88138-$F88170
-- (TRAP #9's handler continuing past its entry, where the slot2/3
-- writes were observed to originate from -- $F88162).
dump("TRAP9_AND_BRIDGE", 0x00f88138, 0x50)

print("T6TD_DONE")
manager.machine:exit()
