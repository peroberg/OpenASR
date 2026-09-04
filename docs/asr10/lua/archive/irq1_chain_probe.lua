-- ASR-10 IRQ1 storage-completion probe.
--
-- Observation only. No C++ instrumentation, no model changes.
-- Purpose: confirm/refute whether FDC RECALIBRATE (07 00) is issued during
-- the *normal* boot-to-FILE1 sequence (before any instrument-load button is
-- pressed), independent of the later instrument-load RECALIBRATE. This is
-- the evidence needed to explain why unconditionally wiring FDC INTRQ to
-- MC68302 external IRQ1 broke boot-to-FILE1 with "ERROR 129 - REBOOT"
-- (docs/asr10/investigations/irq1-storage-completion-probe.md).
--
-- Also logs FDC command bytes seen after FILE 1, through the 0A,23,02
-- instrument-load sequence, purely as a secondary record -- IRQ1 is NOT
-- wired in this build, so no vector-$51/vector-$4B chain observation is
-- possible here. That validation plan needs a build with a correct board
-- policy, not this one.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local fifo_window = {}
local recalibrate_events = {}
local phase = "boot"

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local function note_fifo_byte(value)
  fifo_window[#fifo_window + 1] = { time = now(), pc = pc(), value = value }
  while #fifo_window > 2 do
    table.remove(fifo_window, 1)
  end
  if #fifo_window == 2 and fifo_window[1].value == 0x07 and fifo_window[2].value == 0x00 then
    local ev = { time = fifo_window[1].time, pc = fifo_window[1].pc, phase = phase }
    recalibrate_events[#recalibrate_events + 1] = ev
    print(string.format("IRQ1PROBE_RECALIBRATE t=%.6f pc=%06X phase=%s", ev.time, ev.pc, ev.phase))
  end
end

local tap = prog:install_write_tap(0x00fc4000, 0x00fc4003, "irq1_probe_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    note_fifo_byte(byte_value(data, mask))
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

print("IRQ1PROBE start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("irq1_chain_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

print(string.format("IRQ1PROBE_FILE1 t=%.6f boot_recalibrate_events=%u", now(), #recalibrate_events))

phase = "instrument_load"
emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 15
while now() < deadline do
  emu.wait(emu.attotime.from_msec(50))
end

local boot_count = 0
local load_count = 0
for _, ev in ipairs(recalibrate_events) do
  if ev.phase == "boot" then boot_count = boot_count + 1 else load_count = load_count + 1 end
end

print(string.format("IRQ1PROBE_SUMMARY boot_recalibrate=%u instrument_load_recalibrate=%u final_display=\"%s\"",
  boot_count, load_count, display.read_raw()))

if boot_count > 0 then
  reg.pass("irq1_chain_probe", string.format("boot_recalibrate=%u confirms_shared_completion_path", boot_count))
else
  reg.fail("irq1_chain_probe", "no_boot_recalibrate_observed")
end
