-- Decisive check for vector 4/11: content analysis alone was ambiguous
-- ($46FC $2700 = MOVE #$2700,SR, a textbook supervisor prologue -- looks
-- like real code). This correlates each tap hit with the CPU's own PC
-- at that instant: PC == the tapped address means a genuine instruction
-- fetch; PC elsewhere means an ordinary data read from unrelated code.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("vector_4_11_pc_correlation_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

local handler4 = prog:read_u32(4 * 4) & 0x00ffffff
local handler11 = prog:read_u32(11 * 4) & 0x00ffffff
print(string.format("V411PC_HANDLERS vec4=%06X vec11=%06X", handler4, handler11))

local hits4, hits11 = {}, {}
taps[#taps + 1] = prog:install_read_tap(handler4, handler4 + 1, "v411pc_4", function(offset, data, mask)
  hits4[#hits4 + 1] = { t = now(), pc = pc() }
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(handler11, handler11 + 1, "v411pc_11", function(offset, data, mask)
  hits11[#hits11 + 1] = { t = now(), pc = pc() }
  return nil
end)

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("vector_4_11_pc_correlation_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(1000))

local function report(name, addr, hits)
  print(string.format("V411PC_RESULT name=%s addr=%06X total_hits=%u", name, addr, #hits))
  local shown = 0
  for _, h in ipairs(hits) do
    if shown < 10 then
      print(string.format("V411PC_HIT name=%s t=%.6f pc=%06X pc_equals_addr=%s", name, h.t, h.pc, tostring(h.pc == addr or h.pc == (addr - 1))))
      shown = shown + 1
    end
  end
end
report("vector4", handler4, hits4)
report("vector11", handler11, hits11)

print(string.format("V411PC_SUMMARY final_display=\"%s\" t=%.6f", text, now()))
reg.pass("vector_4_11_pc_correlation_probe", string.format("hits4=%u hits11=%u", #hits4, #hits11))
