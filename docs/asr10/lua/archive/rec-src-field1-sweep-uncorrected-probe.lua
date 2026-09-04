-- Del 2, FIRST-PASS SWEEP -- superseded by
-- rec-src-field1-sweep-corrected-probe.lua, kept as provenance of the
-- methodological trap it fell into. Sweeps all 64 panel buttons from the
-- REC SRC=INPUTDRY LEFT screen, comparing display text immediately after
-- each candidate press. This missed BTN_0A/0B entirely (they change
-- $016F immediately but the display doesn't redraw without a second
-- BTN_20 press -- see rec-src-field2-staleness-probe.lua), which is
-- exactly the failure mode a button changing Field 1 without a visible
-- redraw could also have hit. Still useful: every OTHER button's
-- single-hop navigation target (e.g. $0C -> "COPY AUDIO TRACK") is
-- accurate, since those really do repaint immediately.

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
  reg.fail("rec_src_sweep", string.format("boot_timeout display=\"%s\"", text))
  return
end

for code = 0, 0x3f do
  press_button(0x20, 400)  -- (re)enter Sample-Source Select
  local baseline = display.read_raw()
  local before_pb = #pb_events
  local sent = press_button(code, 250)
  local after = display.read_raw()
  if sent and after ~= baseline then
    print(string.format("RSS_SWEEP code=%02X baseline=\"%s\" after=\"%s\" pb_delta=%u",
      code, baseline, after, #pb_events - before_pb))
  end
end

print(string.format("RSS_SUMMARY witness=%u pb_events=%u", witness_writes, #pb_events))
for i, e in ipairs(pb_events) do
  print(string.format("RSS_PB[%u] t=%.6f pc=%06X offset=%06X data=%04X mask=%04X",
    i, e.t, e.pc, e.offset, e.data, e.mask))
end

manager.machine:exit()
