-- Safe, code-free stimulus: forces the ROM alias probe to read D4=$3333
-- (the 2MB/base=$600000 branch) by overwriting $008000 right after the
-- probe's own C08000 write, using the UNMODIFIED, known-safe driver (the
-- isolated probe-shadow registers cannot corrupt anything, unlike the
-- reverted Stage A code). Purpose: observe, without any C++ change or
-- crash risk, what firmware actually does once it believes base=$600000,
-- before committing to any mem_map change that has to back that belief.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

local injected = false
taps[#taps + 1] = prog:install_write_tap(0x00c08000, 0x00c08003, "stim_trigger", function(offset, data, mask)
  if not injected then
    injected = true
    prog:write_u16(0x00008000, 0x3333)
    print(string.format("STIM_INJECTED t=%.6f pc=%06X", now(), pc()))
  end
  return nil
end)

local decision_trace = {}
for _, addr in ipairs({ 0x00000c4e, 0x00000c62 }) do
  taps[#taps + 1] = prog:install_write_tap(addr, addr + 3, "stim_dec_" .. string.format("%06x", addr), function(offset, data, mask)
    decision_trace[#decision_trace + 1] = string.format("t=%.6f pc=%06X addr=%06X data=%04X", now(), pc(), offset, data & 0xffff)
    return nil
  end)
end

local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "stim_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
print(string.format("STIM_BOOT ok=%s t=%.6f display=\"%s\"", tostring(ok), now(), text))

for _, line in ipairs(decision_trace) do print("STIM_DECISION " .. line) end
print(string.format("STIM_DECISION_TOTAL=%u", #decision_trace))
print(string.format("STIM_WITNESS writes=%u", witness_writes))
print(string.format("STIM_FINAL t=%.6f pc=%06X display=\"%s\"", now(), pc(), display.read_raw()))
manager.machine:exit()
