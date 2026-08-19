-- Ready-line investigation, Del 2: does firmware consult SENSE DRIVE
-- STATUS's ST3 ready bit, or just issue the command and ignore the
-- result? Observation only, current unwired tree.
--
-- Logs the SENSE DRIVE STATUS command (04 xx) and its ST3 result byte,
-- the PC that reads it, and dumps opcode bytes at that PC plus a few
-- following bytes for manual disassembly (bit-test/branch on the ready
-- bit would appear as btst/andi/bcc immediately after loading D0 from the
-- FIFO read).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function byte_address(offset, mask)
  return (offset & 0xfffffe) | (((mask & 0x00ff) ~= 0) and 1 or 0)
end
local function byte_value(data, mask)
  return ((mask & 0x00ff) ~= 0) and (data & 0xff) or ((data >> 8) & 0xff)
end

local function hex_bytes(address, length)
  local parts = {}
  for offset = 0, length - 1 do
    parts[#parts + 1] = string.format("%02X", prog:read_u8(address + offset) & 0xff)
  end
  return table.concat(parts, "")
end

local sds_seq = {}
local taps = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "sds_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    local value = byte_value(data, mask)
    sds_seq[#sds_seq + 1] = { time = now(), pc = pc(), value = value }
    while #sds_seq > 2 do table.remove(sds_seq, 1) end
    if #sds_seq == 2 and sds_seq[1].value == 0x04 then
      print(string.format("SDS_COMMAND t=%.6f pc=%06X param=%02X", sds_seq[1].time, sds_seq[1].pc, sds_seq[2].value))
    end
  end
  return nil
end)

taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "sds_r", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    local value = byte_value(data, mask)
    local caller_pc = pc()
    -- dump 24 bytes starting at the caller PC (post-instruction, per this
    -- project's established tap-PC convention) for manual disassembly
    local dump = hex_bytes(caller_pc, 24)
    print(string.format("SDS_ST3_READ t=%.6f pc=%06X value=%02X bytes=%s", now(), caller_pc, value, dump))
  end
  return nil
end)

local file1 = "FILE 1  TUT0RIAL BNK  "
print("SDS_PROBE start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
print(string.format("SDS_PROBE_SUMMARY reached_file1=%s final_display=\"%s\"", tostring(ok), text))
reg.pass("sds_st3_probe", string.format("display=\"%s\"", text))
