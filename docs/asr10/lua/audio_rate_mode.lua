-- Acceptance for the [Likely functional] ASR effect-mode audio-rate policy.
-- It drives the established ROM HALL -> 44LUSH -> ROM HALL sequence and
-- leaves actual peak/frequency checks to regression-test.sh/-wavwrite.
local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press(port, mask)
	local field = manager.machine.ioport.ports[port]:field(mask)
	field:set_value(1)
	emu.wait(emu.attotime.from_msec(80))
	field:clear_value()
	emu.wait(emu.attotime.from_msec(300))
end

local function key(code)
	press(code < 0x20 and ":panel:buttons_0" or ":panel:buttons_32", 1 << (code & 0x1f))
end

local function mode()
	return prog:read_u8(0x00000ce3)
end

local mdin
for tag, image in pairs(manager.machine.images) do
	if tag:find("mdin") then
		mdin = image
	end
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok or not mdin then
	reg.fail("audio_rate_mode", "boot_or_mdin")
	return
end

key(0x0a); key(0x23); key(0x02)
ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
	reg.fail("audio_rate_mode", "instrument_load")
	return
end
emu.wait(emu.attotime.from_msec(500))
key(0x02)
emu.wait(emu.attotime.from_msec(500))

local function select_hall(label)
	key(0x07); key(0x0a); key(0x23)
	emu.wait(emu.attotime.from_msec(1500))
	if mode() ~= 0 then
		reg.fail("audio_rate_mode", label .. "_mode")
		return false
	end
	return true
end

local function select_44lush()
	key(0x07); key(0x09); key(0x23)
	emu.wait(emu.attotime.from_msec(2500))
	if mode() ~= 1 then
		reg.fail("audio_rate_mode", "B_mode")
		return false
	end
	return true
end

local function note(label)
	print(string.format("AUDIO_RATE_MODE_ONSET %s t=%.6f mode=%02X", label, emu.time(), mode()))
	mdin:load("docs/asr10/lua/fixtures/noteon.mid")
	emu.wait(emu.attotime.from_msec(1600))
end

if not select_hall("A") then return end
note("A")
if not select_44lush() then return end
note("B")
if not select_hall("A2") then return end
note("A2")
reg.pass("audio_rate_mode", "A=00 B=01 A2=00")
