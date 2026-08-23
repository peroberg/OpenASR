-- Del 4 falsifiable prediction check (memory-size-belief-analysis.md
-- follow-up, base-relocation fix applied): with 2 MB wraparound modeled,
-- does the FULL stereo RECORD/start sequence (matching
-- full-record-start-probe.md's exact stimulus, not just Level Detect)
-- fail with an honest memory error instead of System Error 57?

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "shf_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(1 << (code & 0x1f))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  print(string.format("SHF_BOOT_TIMEOUT display=\"%s\"", text))
  manager.machine:exit()
  return
end

press_button(0x20, 500)  -- Sample-Source Select
for _ = 1, 2 do press_button(0x0a, 250) end  -- cycle source to L+R (source_mode=2)
local source_mode = prog:read_u8(0x016f)
print(string.format("SHF_SOURCE_MODE value=%u display=\"%s\"", source_mode, display.read_raw()))

press_button(0x02, 1000)  -- proceed to Level Detect
for _ = 1, 24 do press_button(0x0a, 40) end  -- lower threshold (Level-Detect screen)
print(string.format("SHF_PRE_RECORD display=\"%s\"", display.read_raw()))
press_button(0x23, 300)  -- Enter-Yes: RECORD/start
emu.wait(emu.attotime.from_seconds(2))

local err = prog:read_u16(0x00c0)
local state = prog:read_u16(0x0d04)
local text_final = display.read_raw()

print(string.format(
  "SHF_RESULT source_mode=%u state=%04X error=%04X witness=%u display=\"%s\"",
  source_mode, state, err, witness_writes, text_final))
print(string.format("SHF_ROOTS C4E=%08X C62=%08X C66=%08X C10=%08X",
  prog:read_u32(0x0c4e), prog:read_u32(0x0c62), prog:read_u32(0x0c66), prog:read_u32(0x0c10)))

manager.machine:exit()
