-- Del 2: does the Data Entry Slider (:panel:analog_data_entry, an
-- IPT_ADJUSTER ioport 0-$3FF) change REC SRC Field 1? Sweeps its full
-- range from the REC SRC screen, forcing a redraw (BTN_20) after each
-- step per rec-src-field2-staleness-probe.lua's finding, and diffs a
-- bounded low-RAM window around each step. Result: no display change at
-- any sampled value; the RAM cells that do change look like generic
-- UI/animation counters plus one raw-slider-value cache, not Field 1.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

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
  reg.fail("dataentry2", string.format("boot_timeout display=\"%s\"", text))
  return
end

local de_port = manager.machine.ioport.ports[":panel:analog_data_entry"]
local de_field = de_port:field(0xffffffff)

local WIN_LO, WIN_HI = 0x000000, 0x001fff
local function snapshot()
  local snap = {}
  for a = WIN_LO, WIN_HI, 2 do snap[a] = prog:read_u16(a) end
  return snap
end
local function diff(a, b)
  local changes = {}
  for addr, v in pairs(b) do
    if a[addr] ~= v then changes[#changes + 1] = { addr = addr, before = a[addr], after = v } end
  end
  table.sort(changes, function(x, y) return x.addr < y.addr end)
  return changes
end

press_button(0x20, 500)
print(string.format("DE2_ENTER display=\"%s\"", display.read_raw()))

for _, v in ipairs({0x000, 0x3ff, 0x100, 0x200, 0x300}) do
  local before = snapshot()
  de_field:set_value(v)
  emu.wait(emu.attotime.from_msec(300))
  local mid = snapshot()
  press_button(0x20, 300)  -- force redraw
  local after_text = display.read_raw()
  print(string.format("DE2_STEP value=%03X display=\"%s\"", v, after_text))
  for _, c in ipairs(diff(before, mid)) do
    print(string.format("DE2_DIFF value=%03X addr=%06X before=%04X after=%04X", v, c.addr, c.before, c.after))
  end
end

manager.machine:exit()
