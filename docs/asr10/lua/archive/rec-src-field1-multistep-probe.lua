-- Del 2 probe: does cycling REC SRC Field 1 (INPUTDRY/INPUT+FX/MAIN-OUT/
-- DIGITAL) ever touch PBDAT/PBDDR? Also locate Field 1's own RAM storage
-- cell (undocumented so far -- only Field 2's $016F is known) by diffing
-- a bounded low-RAM window before/after each cursor-left + down press.

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
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("rec_src_field1", string.format("boot_timeout display=\"%s\"", text))
  return
end

-- Snapshot a bounded low-RAM window ($000000-$001FFF, comfortably covers
-- panel/UI state cells like $016F/$0D04 already known) to diff around
-- each cursor-left + field-cycle step.
local WIN_LO, WIN_HI = 0x000000, 0x001fff
local function snapshot()
  local snap = {}
  for a = WIN_LO, WIN_HI, 2 do
    snap[a] = prog:read_u16(a)
  end
  return snap
end
local function diff(a, b)
  local changes = {}
  for addr, v in pairs(b) do
    if a[addr] ~= v then
      changes[#changes + 1] = { addr = addr, before = a[addr], after = v }
    end
  end
  table.sort(changes, function(x, y) return x.addr < y.addr end)
  return changes
end

press_button(0x20, 500)  -- Sample-Source Select
print(string.format("RSF1_ENTER display=\"%s\"", display.read_raw()))

local before_left = snapshot()
press_button(0x0c, 300)  -- Left: move cursor to Field 1
local after_left = snapshot()
print(string.format("RSF1_AFTER_LEFT display=\"%s\"", display.read_raw()))
for _, c in ipairs(diff(before_left, after_left)) do
  print(string.format("RSF1_DIFF_LEFT addr=%06X before=%04X after=%04X", c.addr, c.before, c.after))
end

-- Cycle Field 1 three times with Down, capturing display + diff each step.
local prev = after_left
for step = 1, 3 do
  press_button(0x0a, 300)  -- Down
  local cur = snapshot()
  local text = display.read_raw()
  print(string.format("RSF1_STEP[%u] display=\"%s\"", step, text))
  for _, c in ipairs(diff(prev, cur)) do
    print(string.format("RSF1_DIFF_STEP[%u] addr=%06X before=%04X after=%04X", step, c.addr, c.before, c.after))
  end
  prev = cur
end

print(string.format("RSF1_SUMMARY witness=%u pb_events=%u", witness_writes, #pb_events))
for i, e in ipairs(pb_events) do
  print(string.format("RSF1_PB[%u] t=%.6f pc=%06X offset=%06X data=%04X mask=%04X",
    i, e.t, e.pc, e.offset, e.data, e.mask))
end

manager.machine:exit()
