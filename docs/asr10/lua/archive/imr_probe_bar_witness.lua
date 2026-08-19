-- Post-hoc witness for irq1-imr-unmask-probe.md: does the BAR register get
-- written again after the point the IMR/GIMR/ISR tap was installed (right
-- after FILE 1, t~16.3s in that run)? If so, mc68302_device::
-- install_internal_window() would have silently dropped that tap partway
-- through the measurement window, per methods-static-analysis.md #8.5, and
-- the "zero writes" result there is unwitnessed from that point on.
--
-- Observation only. BAR lives in the CPU's own internal bootstrap_map
-- (0x000000f0-0x000000ff), a static map installed once in device_start(),
-- not dynamically re-installed like the SIB window -- so a tap here should
-- not suffer the same silent-drop failure mode. Installed at script start
-- (t=0) for maximum coverage, which trivially covers "from the original
-- tap's install point onward" as a subset.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local REFERENCE_INSTALL_T = 16.3 -- when irq1-imr-unmask-probe.md's IMR tap was installed (right after FILE1)
local bar_writes = {}

local tap = prog:install_write_tap(0x000000f0, 0x000000ff, "bar_witness_w", function(offset, data, mask)
  local address = (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
  if address == 0x000000f2 or address == 0x000000f3 then
    local value = ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
    local ev = { time = now(), address = address, value = value, mask = mask, pc = pc() }
    bar_writes[#bar_writes + 1] = ev
    print(string.format("BARWITNESS_WRITE t=%.6f addr=%06X value=%02X mask=%04X pc=%06X after_imr_tap_point=%s",
      ev.time, ev.address, ev.value, ev.mask, ev.pc, tostring(ev.time > REFERENCE_INSTALL_T)))
  end
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

print("BARWITNESS start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("imr_probe_bar_witness", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

local file1_time = now()
print(string.format("BARWITNESS_FILE1 t=%.6f writes_so_far=%u", file1_time, #bar_writes))

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local load_seen_time = nil
local deadline = now() + 15
while now() < deadline do
  if not load_seen_time and display.read_raw() == load_text then
    load_seen_time = now()
    print(string.format("BARWITNESS_LOADING t=%.6f", load_seen_time))
  end
  emu.wait(emu.attotime.from_msec(20))
end

emu.wait(emu.attotime.from_msec(10000))

local after_ref = 0
for _, ev in ipairs(bar_writes) do
  if ev.time > REFERENCE_INSTALL_T then
    after_ref = after_ref + 1
  end
end

print(string.format(
  "BARWITNESS_SUMMARY total_bar_writes=%u after_reference_point_t_gt_%.1f=%u final_display=\"%s\"",
  #bar_writes, REFERENCE_INSTALL_T, after_ref, display.read_raw()))

reg.pass("imr_probe_bar_witness", string.format("total=%u after_ref=%u", #bar_writes, after_ref))
