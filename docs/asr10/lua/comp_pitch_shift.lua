
-- Regression test for ES5510 condition mask register (CMR/FB) semantics
-- and authentic V3.50 ROM-39 PITCH SHIFT download verification.
local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press(port, mask)
	local field = manager.machine.ioport.ports[port]:field(mask)
	field:set_value(1)
	emu.wait(emu.attotime.from_msec(70))
	field:clear_value()
	emu.wait(emu.attotime.from_msec(100))
end

local function key(code)
	press(code < 0x20 and ":panel:buttons_0" or ":panel:buttons_32", 1 << (code & 0x1f))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
	reg.fail("comp_pitch_shift", "boot_timeout display=\"" .. tostring(text) .. "\"")
	return
end

key(0x07) -- FX Select
emu.wait(emu.attotime.from_msec(600))

-- Advance 40 edges to select ROM-39 PITCH SHIFT
for i = 1, 40 do
	key(0x0a)
end

-- Wait for effect upload to complete (typically <1.0s)
local deadline = emu.time() + 10.0
local passed = false
while emu.time() < deadline do
	local display_str = reg.display.read_raw()
	local cur_fx = prog:read_u32(0x0e92)
	local retries = prog:read_u8(0x0e8c)

	if display_str:find("FAILED") or retries >= 10 then
		reg.fail("comp_pitch_shift", string.format("download_failed retries=%d display=\"%s\"", retries, display_str))
		return
	end

	if cur_fx == 0xfffae6f6 and retries == 0 and display_str:find("PITCH") then
		passed = true
		break
	end
	emu.wait(emu.attotime.from_msec(200))
end

if not passed then
	reg.fail("comp_pitch_shift", string.format("timeout cur_fx=%08X retries=%d display=\"%s\"",
		prog:read_u32(0x0e92), prog:read_u8(0x0e8c), reg.display.read_raw()))
	return
end

reg.pass("comp_pitch_shift", string.format("cur_fx=%08X retries=0 display=\"%s\"",
	prog:read_u32(0x0e92), reg.display.read_raw()))
