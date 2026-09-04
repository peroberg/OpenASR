-- ASR-10 MC68302 GIMR/IMR/ISR write probe.
--
-- Observation only. No C++ instrumentation, no mem_map change.
-- Question: does firmware unmask an external-IRQ1 bit in MC68302 IMR
-- ($FC6816) anywhere between FILE 1 and the instrument-load stall? See
-- docs/asr10/investigations/irq1-imr-unmask-probe.md for the answer and
-- discussion; this file is the instrument only.
--
-- Logs every write to $FC6812 (GIMR), $FC6816 (IMR), $FC6818 (ISR) with
-- timestamp, byte address, byte value, mem_mask, raw tap PC, and a
-- byte-pattern-calibrated PC (methods-static-analysis.md #6: search for the
-- instruction's own absolute-address literal rather than trusting the
-- predicted/raw offset).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "

local REGS = {
  [0x00fc6812] = "GIMR",
  [0x00fc6813] = "GIMR",
  [0x00fc6816] = "IMR",
  [0x00fc6817] = "IMR",
  [0x00fc6818] = "ISR",
  [0x00fc6819] = "ISR",
}
local REG_BASE = { GIMR = 0x00fc6812, IMR = 0x00fc6816, ISR = 0x00fc6818 }

local events = {}
local calibration_checked = false

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end

local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

-- methods-static-analysis.md #6: don't trust a predicted PC offset, search
-- for the instruction's own absolute-address literal bytes in memory near
-- the raw tap PC and report the calibrated start from that.
local function calibrate(raw_pc, reg_base)
  local addr_bytes = {
    (reg_base >> 24) & 0xff,
    (reg_base >> 16) & 0xff,
    (reg_base >> 8) & 0xff,
    reg_base & 0xff,
  }
  for back = 4, 24 do
    local p = raw_pc - back
    if p < 0 then break end
    local ok = true
    for i = 0, 3 do
      if (prog:read_u8(p + i) & 0xff) ~= addr_bytes[i + 1] then
        ok = false
        break
      end
    end
    if ok then
      local op_hi = prog:read_u8(p - 4) & 0xff
      local op_lo = prog:read_u8(p - 3) & 0xff
      local imm_hi = prog:read_u8(p - 2) & 0xff
      local imm_lo = prog:read_u8(p - 1) & 0xff
      return p - 4, string.format("%02X%02X", op_hi, op_lo), string.format("%02X%02X", imm_hi, imm_lo)
    end
  end
  return nil, nil, nil
end

local function log_write(reg_name, address, value, mask, raw_pc)
  local reg_base = REG_BASE[reg_name]
  local calib_pc, opcode_word, imm_word = calibrate(raw_pc, reg_base)
  local line
  if calib_pc then
    line = string.format(
      "IMRPROBE_WRITE t=%.6f reg=%s addr=%06X value=%02X mask=%04X raw_pc=%06X calib_pc=%06X opcode=%s imm=%s",
      now(), reg_name, address, value, mask, raw_pc, calib_pc, opcode_word, imm_word)
  else
    line = string.format(
      "IMRPROBE_WRITE t=%.6f reg=%s addr=%06X value=%02X mask=%04X raw_pc=%06X calib_pc=UNCALIBRATED",
      now(), reg_name, address, value, mask, raw_pc)
  end
  print(line)
  events[#events + 1] = { time = now(), reg = reg_name, address = address, value = value, mask = mask,
    raw_pc = raw_pc, calib_pc = calib_pc, opcode = opcode_word }
end

-- NOTE: the tap must be installed AFTER mc68302_device's internal SIB
-- window is stably mapped, not at script start. install_internal_window()
-- re-issues install_readwrite_handler() for FC6000-FC6FFF every time BAR is
-- written, which silently drops any tap installed beforehand -- confirmed
-- by imr_probe_calibration_check.lua/imr_probe_calibration_check2.lua: a
-- tap installed at script start sees zero writes anywhere in FC6800-FC68FF
-- through the whole boot, while the same tap installed at t=16.0s
-- immediately sees FC6829 (PBDAT) traffic. See
-- docs/asr10/investigations/irq1-imr-unmask-probe.md.
local function install_tap()
  return prog:install_write_tap(0x00fc6812, 0x00fc6819, "imr_probe_w", function(offset, data, mask)
    local address = byte_address(offset, mask)
    local reg_name = REGS[address]
    if reg_name then
      log_write(reg_name, address, byte_value(data, mask), mask, pc())
    end
    return nil
  end)
end

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

print("IMRPROBE start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("imr_unmask_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

local file1_time = now()
print(string.format("IMRPROBE_FILE1 t=%.6f events_so_far=%u", file1_time, #events))

local tap = install_tap()
print(string.format("IMRPROBE_TAP_INSTALLED t=%.6f", now()))

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local load_seen_time = nil
local deadline = now() + 15
while now() < deadline do
  if not load_seen_time and display.read_raw() == load_text then
    load_seen_time = now()
    print(string.format("IMRPROBE_LOADING t=%.6f", load_seen_time))
  end
  emu.wait(emu.attotime.from_msec(20))
end

-- Extra dwell past the stall point (known stable by ~18.3s) to catch any
-- late unmask that arrives after the display stops updating and the FDC
-- polling phase goes quiet.
emu.wait(emu.attotime.from_msec(10000))

local after_file1 = 0
for _, ev in ipairs(events) do
  if ev.time >= file1_time then
    after_file1 = after_file1 + 1
  end
end

print(string.format(
  "IMRPROBE_SUMMARY total_events=%u after_file1_events=%u file1_t=%.6f load_seen_t=%s final_display=\"%s\"",
  #events, after_file1, file1_time, load_seen_time and string.format("%.6f", load_seen_time) or "none",
  display.read_raw()))

reg.pass("imr_unmask_probe", string.format("total=%u after_file1=%u", #events, after_file1))
