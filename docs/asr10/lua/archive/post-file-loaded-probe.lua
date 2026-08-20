-- Del 3, file-loaded-verification-probe.md: pure observation of firmware
-- state after FILE LOADED. No ES5506/ES5510 code, no mem_map change --
-- this script only taps existing register ranges and reads existing
-- display/output state.
--
-- Question: what does firmware do once FILE LOADED is reached? Does the
-- display change on its own; do annunciators change; does the panel still
-- respond to button input; does anything touch ES5506 voice registers
-- ($FC2000-$FC207F) or the ES5510 host window ($FC3000-$FC303F)?
--
-- Note: asr10panel_device (esqpanel.h) has no piano-keyboard ioport at
-- all -- only buttons_0/buttons_32 (panel buttons) and analog data-entry/
-- volume. There is no key_down/key_up wiring for this panel class. A
-- literal "press a musical key" stimulus cannot be produced from Lua
-- without first modeling that input, which is out of scope this task.
-- This script instead presses an available panel button post-load and
-- observes the same register ranges, as the closest obtainable proxy.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local es5506_events = {}
local es5510_events = {}

taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "pflp_es5506_w", function(offset, data, mask)
  local addr = byte_address(offset, mask)
  es5506_events[#es5506_events + 1] = { t = now(), dir = "W", addr = addr, v = byte_value(data, mask) }
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(0x00fc2000, 0x00fc207f, "pflp_es5506_r", function(offset, data, mask)
  local addr = byte_address(offset, mask)
  es5506_events[#es5506_events + 1] = { t = now(), dir = "R", addr = addr }
  return nil
end)
taps[#taps + 1] = prog:install_write_tap(0x00fc3000, 0x00fc303f, "pflp_es5510_w", function(offset, data, mask)
  local addr = byte_address(offset, mask)
  es5510_events[#es5510_events + 1] = { t = now(), dir = "W", addr = addr, v = byte_value(data, mask) }
  return nil
end)
taps[#taps + 1] = prog:install_read_tap(0x00fc3000, 0x00fc303f, "pflp_es5510_r", function(offset, data, mask)
  local addr = byte_address(offset, mask)
  es5510_events[#es5510_events + 1] = { t = now(), dir = "R", addr = addr }
  return nil
end)

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
  reg.fail("post_file_loaded_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

press_button(0x0a)
press_button(0x23)
press_button(0x02)

ok, text = reg.wait_for_text(file_loaded, 20)
if not ok then
  reg.fail("post_file_loaded_probe", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end

print(string.format("PFLP_LOADED t=%.6f display=\"%s\"", now(), text))

-- 1. Passive window: does anything change on its own for 5s after load,
-- with zero further input?
local t_idle_start = now()
local before_es5506 = #es5506_events
local before_es5510 = #es5510_events
local display_changes = {}
local last_display = text
local deadline = now() + 5
while now() < deadline do
  local d = display.read_raw()
  if d ~= last_display then
    display_changes[#display_changes + 1] = { t = now(), text = d }
    last_display = d
  end
  emu.wait(emu.attotime.from_msec(100))
end
print(string.format("PFLP_IDLE_WINDOW t0=%.6f t1=%.6f display_changes=%u es5506_events=%u es5510_events=%u",
  t_idle_start, now(), #display_changes, #es5506_events - before_es5506, #es5510_events - before_es5510))
for _, c in ipairs(display_changes) do
  print(string.format("PFLP_IDLE_DISPLAY_CHANGE t=%.6f text=\"%s\"", c.t, c.text))
end

-- 2. Panel responsiveness: does pressing an available panel button (BTN_00,
-- the FILE-1-equivalent leftmost data-entry button already exercised by
-- button.lua's BTN_0A) still change the display now, post-load?
local before_display = display.read_raw()
local before_es5506_2 = #es5506_events
local before_es5510_2 = #es5510_events
press_button(0x0a)
emu.wait(emu.attotime.from_msec(500))
local after_display = display.read_raw()
print(string.format("PFLP_BUTTON_PROBE t=%.6f before=\"%s\" after=\"%s\" changed=%s es5506_delta=%u es5510_delta=%u",
  now(), before_display, after_display, tostring(before_display ~= after_display),
  #es5506_events - before_es5506_2, #es5510_events - before_es5510_2))

-- 3. Full event dump for both ranges across the whole post-load observation
-- (idle window + button probe), for manual inspection.
for _, e in ipairs(es5506_events) do
  if e.dir == "W" then
    print(string.format("PFLP_ES5506 t=%.6f %s addr=%06X v=%02X", e.t, e.dir, e.addr, e.v))
  else
    print(string.format("PFLP_ES5506 t=%.6f %s addr=%06X", e.t, e.dir, e.addr))
  end
end
for _, e in ipairs(es5510_events) do
  if e.dir == "W" then
    print(string.format("PFLP_ES5510 t=%.6f %s addr=%06X v=%02X", e.t, e.dir, e.addr, e.v))
  else
    print(string.format("PFLP_ES5510 t=%.6f %s addr=%06X", e.t, e.dir, e.addr))
  end
end

print(string.format("PFLP_SUMMARY final_display=\"%s\" total_es5506_events=%u total_es5510_events=%u t=%.6f",
  display.read_raw(), #es5506_events, #es5510_events, now()))
reg.pass("post_file_loaded_probe", string.format("es5506_events=%u es5510_events=%u display_changes=%u",
  #es5506_events, #es5510_events, #display_changes))
