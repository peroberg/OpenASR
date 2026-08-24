-- Corrected sweep: the REC SRC display text is static per-entry and only
-- redraws when Sample-Source Select (BTN_20) is pressed again -- proven
-- by rec-src-sanity.lua (BTN_0A changes $016F immediately, 0->1->2, but
-- the displayed text stays "INPUTDRY LEFT" until BTN_20 is pressed a
-- second time, after which it reads "INPUTDRY L+R"). The first 64-button
-- sweep compared display text immediately after each candidate press and
-- therefore could not see any button whose effect is state-only until
-- redraw. This version: reset (BTN_20) -> candidate -> redraw (BTN_20)
-- -> compare against the clean baseline text.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end

local pb_events = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc6826, 0x00fc6829, "pb_w", function(offset, data, mask)
  pb_events[#pb_events + 1] = { t = now(), pc = pc(), offset = offset, data = data, mask = mask }
  return nil
end)
local witness_writes = 0
taps[#taps + 1] = prog:install_write_tap(0x000000, 0x0fffff, "witness", function()
  witness_writes = witness_writes + 1
  return nil
end)

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return false end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 200))
  return true
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("rec_src_sweep2", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x20, 500)
local clean_baseline = display.read_raw()
local clean_016f = prog:read_u8(0x016f)
print(string.format("SWEEP2_BASELINE display=\"%s\" mode=%u", clean_baseline, clean_016f))

for code = 0, 0x3f do
  press_button(0x20, 300)          -- reset to clean REC SRC entry
  local before_pb = #pb_events
  local before_016f = prog:read_u8(0x016f)
  local sent = press_button(code, 200)  -- candidate
  local mid_016f = prog:read_u8(0x016f)
  press_button(0x20, 300)          -- force redraw
  local after = display.read_raw()
  local after_016f = prog:read_u8(0x016f)
  if sent and (after ~= clean_baseline or mid_016f ~= before_016f) then
    print(string.format("SWEEP2 code=%02X after=\"%s\" mode_before=%u mode_mid=%u mode_after=%u pb_delta=%u",
      code, after, before_016f, mid_016f, after_016f, #pb_events - before_pb))
  end
end

print(string.format("SWEEP2_SUMMARY witness=%u pb_events=%u", witness_writes, #pb_events))
for i, e in ipairs(pb_events) do
  print(string.format("SWEEP2_PB[%u] t=%.6f pc=%06X offset=%06X data=%04X mask=%04X",
    i, e.t, e.pc, e.offset, e.data, e.mask))
end

manager.machine:exit()
