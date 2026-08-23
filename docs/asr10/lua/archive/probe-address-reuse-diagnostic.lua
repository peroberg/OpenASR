-- Diagnostic (not a regression test): does anything besides the initial
-- ROM alias-probe sequence ever touch $008000/$408000/$808000/$C08000
-- during a full boot->FILE-LOADED run? Answers why a naive "wrap all four
-- into low RAM" mem_map change hung at LOADING SYSTEM.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

local ADDRS = { 0x00008000, 0x00408000, 0x00808000, 0x00c08000 }
local hits = {}
for _, addr in ipairs(ADDRS) do
  hits[addr] = { reads = 0, writes = 0, first_read_after = nil, first_write_after = nil }
end

local probe_done_time = nil

for _, addr in ipairs(ADDRS) do
  taps[#taps + 1] = prog:install_read_tap(addr, addr + 3, "diag_r_" .. string.format("%06x", addr), function(offset, data, mask)
    local h = hits[addr]
    h.reads = h.reads + 1
    if probe_done_time and now() > probe_done_time and not h.first_read_after then
      h.first_read_after = string.format("t=%.6f pc=%06X data=%04X", now(), pc(), data & 0xffff)
    end
    return nil
  end)
  taps[#taps + 1] = prog:install_write_tap(addr, addr + 3, "diag_w_" .. string.format("%06x", addr), function(offset, data, mask)
    local h = hits[addr]
    h.writes = h.writes + 1
    if probe_done_time and now() > probe_done_time and not h.first_write_after then
      h.first_write_after = string.format("t=%.6f pc=%06X data=%04X mask=%04X", now(), pc(), data & 0xffff, mask & 0xffff)
    end
    return nil
  end)
end

local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "diag_witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

-- The alias probe completes within the first few milliseconds (measured
-- previously at t~0.0018s). Anything after t=1.0s is unambiguously later
-- boot activity, not the probe itself.
probe_done_time = 1.0

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  print(string.format("DIAG_BOOT_TIMEOUT display=\"%s\"", text))
  manager.machine:exit()
  return
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(1 << (code & 0x1f))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_seconds(1))

for _, addr in ipairs(ADDRS) do
  local h = hits[addr]
  print(string.format("DIAG addr=%06X reads=%u writes=%u first_read_after_probe=%s first_write_after_probe=%s",
    addr, h.reads, h.writes,
    h.first_read_after or "none", h.first_write_after or "none"))
end
print(string.format("DIAG_WITNESS writes=%u", witness_writes))
print(string.format("DIAG_DISPLAY display=\"%s\"", display.read_raw()))
manager.machine:exit()
