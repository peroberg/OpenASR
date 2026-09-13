-- Generic ES5510 host register read/write and representation test.
-- Verifies host write -> internal conversion -> host readback formatting
-- for SIGREG (F9), CCR (FA), CMR (FB), and address registers (F5-F8).
local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function es5510_write(reg_idx, val24)
	prog:write_u8(0xfc3001, (val24 >> 16) & 0xff)
	prog:write_u8(0xfc3003, (val24 >> 8) & 0xff)
	prog:write_u8(0xfc3005, val24 & 0xff)
	prog:write_u8(0xfc3141, reg_idx)
end

local function es5510_read(reg_idx)
	prog:write_u8(0xfc3101, reg_idx)
	local b0 = prog:read_u8(0xfc3001)
	local b1 = prog:read_u8(0xfc3003)
	local b2 = prog:read_u8(0xfc3005)
	return (b0 << 16) | (b1 << 8) | b2
end

-- Wait 50ms for machine reset state to initialize
emu.wait(emu.attotime.from_msec(50))

-- 1. SIGREG (F9): bit 22 = Product Shift Mode, bits 21:16 unused (read as 1s)
es5510_write(0xf9, 0x000000)
local sig0 = es5510_read(0xf9)
if sig0 ~= 0x3f0000 then
	reg.fail("es5510_special_regs", string.format("sigreg_0 exp=0x3F0000 act=0x%06X", sig0))
	return
end

es5510_write(0xf9, 0x400000)
local sig1 = es5510_read(0xf9)
if sig1 ~= 0x7f0000 then
	reg.fail("es5510_special_regs", string.format("sigreg_1 exp=0x7F0000 act=0x%06X", sig1))
	return
end

-- 2. CCR (FA): flags N,C,V,LT,Z in bits 23:19, bits 18:16 unused (read as 1s)
es5510_write(0xfa, 0xf80000)
local ccr_f8 = es5510_read(0xfa)
if ccr_f8 ~= 0xff0000 then
	reg.fail("es5510_special_regs", string.format("ccr_f8 exp=0xFF0000 act=0x%06X", ccr_f8))
	return
end

es5510_write(0xfa, 0x000000)
local ccr_0 = es5510_read(0xfa)
if ccr_0 ~= 0x070000 then
	reg.fail("es5510_special_regs", string.format("ccr_0 exp=0x070000 act=0x%06X", ccr_0))
	return
end

-- 3. CMR (FB): flags N,C,V,LT,Z,NOT in bits 23:18, bits 17:16 unused (read as 1s)
es5510_write(0xfb, 0x180000) -- GT condition (LT=1, Z=1, NOT=0)
local cmr_18 = es5510_read(0xfb)
if cmr_18 ~= 0x1b0000 then
	reg.fail("es5510_special_regs", string.format("cmr_18 exp=0x1B0000 act=0x%06X", cmr_18))
	return
end

es5510_write(0xfb, 0x000000)
local cmr_0 = es5510_read(0xfb)
if cmr_0 ~= 0x030000 then
	reg.fail("es5510_special_regs", string.format("cmr_0 exp=0x030000 act=0x%06X", cmr_0))
	return
end

-- 4. Address Registers (F5-F7 vs F8): 20-bit address generator registers (bits 23:4 used, bits 3:0 read 1s)
es5510_write(0xf5, 0x123400)
local f5 = es5510_read(0xf5)
if f5 ~= 0x12340f then
	reg.fail("es5510_special_regs", string.format("dlength exp=0x12340F act=0x%06X", f5))
	return
end

es5510_write(0xf6, 0x567800)
local f6 = es5510_read(0xf6)
if f6 ~= 0x56780f then
	reg.fail("es5510_special_regs", string.format("abase exp=0x56780F act=0x%06X", f6))
	return
end

es5510_write(0xf7, 0x9abc00)
local f7 = es5510_read(0xf7)
if f7 ~= 0x9abc0f then
	reg.fail("es5510_special_regs", string.format("bbase exp=0x9ABC0F act=0x%06X", f7))
	return
end

es5510_write(0xf8, 0xdef000)
local f8 = es5510_read(0xf8)
if f8 ~= 0xdef000 then
	reg.fail("es5510_special_regs", string.format("dbase exp=0xDEF000 act=0x%06X", f8))
	return
end

reg.pass("es5510_special_regs", "sigreg=3F/7F ccr=FF/07 cmr=1B/03 addr_f5_f8=ok")
