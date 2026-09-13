-- Regression test for ES5510 host readback of address generator registers (F5-F7)
-- and authentic V3.50 ROM-11 COMP+DIST+REVERB download verification.
local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press(port, mask)
	local field = manager.machine.ioport.ports[port]:field(mask)
	field:set_value(1)
	emu.wait(emu.attotime.from_msec(80))
	field:clear_value()
	emu.wait(emu.attotime.from_msec(200))
end

local function key(code)
	press(code < 0x20 and ":panel:buttons_0" or ":panel:buttons_32", 1 << (code & 0x1f))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
	reg.fail("comp_dist_reverb", "boot_timeout display=\"" .. tostring(text) .. "\"")
	return
end

key(0x07) -- FX Select
emu.wait(emu.attotime.from_msec(800))

-- 11 advance edges to select ROM-10 CHOR+REV+DDL
for i = 1, 11 do
	key(0x0a)
	emu.wait(emu.attotime.from_msec(150))
end
emu.wait(emu.attotime.from_msec(800))

-- 1 advance edge to select ROM-11 CMP+DIST+REV
key(0x0a)

-- Wait for effect upload to complete (typically <1.0s)
local deadline = emu.time() + 10.0
local passed = false
while emu.time() < deadline do
	local display_str = reg.display.read_raw()
	local cur_fx = prog:read_u32(0x0e92)
	local retries = prog:read_u8(0x0e8c)

	if display_str:find("FAILED") or retries >= 10 then
		reg.fail("comp_dist_reverb", string.format("download_failed retries=%d display=\"%s\"", retries, display_str))
		return
	end

	if cur_fx == 0xfff9d334 and retries == 0 and display_str:find("CMP") then
		passed = true
		break
	end
	emu.wait(emu.attotime.from_msec(200))
end

if not passed then
	reg.fail("comp_dist_reverb", "timeout_waiting_for_active_state")
	return
end

reg.pass("comp_dist_reverb", string.format("cur_fx=%08X retries=0 display=\"%s\"",
	prog:read_u32(0x0e92), reg.display.read_raw()))
