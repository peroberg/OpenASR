-- Verifies "FILE LOADED" independently of the display string
-- (docs/asr10/investigations/file-loaded-verification-probe.md).
-- Observation only.
--
-- Captures every READ DATA command's C/H/R/N/EOT and result phase during
-- the JM DIGI SYN instrument load, and every IDMA DAPR value programmed.
-- Computes total bytes transferred and the disk byte-range(s) covered
-- using the already-verified CHS formula from disk-read-path.md:
--   track_index = C*2 + H
--   byte_offset = (track_index * 20 + (R-1)) * 512
-- Then reads the corresponding bytes directly from the .img file (host
-- file I/O, not through the emulated machine) and from the destination
-- memory range in the running machine, and compares them.

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

local taps = {}

-- 1. FDC FIFO command/result capture, opcode-length-aware -- NOT a plain
-- byte-count accumulator. A lone SIS byte ($08) between READ DATA
-- commands would otherwise desync a naive 9-byte counter, since R=$08 is
-- also a legitimate READ DATA parameter value observed in this dialogue.
-- Command length is decided from the FIRST byte of each new command
-- (opcode low 5 bits), matching upd765_family_device's own command
-- dispatch table (upd765.cpp check_command()), not guessed:
--   READ DATA (0x06 base, MT/MFM/SK bits ORed in) -> 9 bytes total
--   SENSE INTERRUPT STATUS (0x08)                 -> 1 byte, 2-byte result
--   RECALIBRATE (0x07)                            -> 2 bytes
--   SEEK (0x0F)                                   -> 3 bytes
--   SPECIFY (0x03)                                -> 3 bytes
--   SENSE DRIVE STATUS (0x04)                     -> 2 bytes
local pending_cmd = {}
local expected_len = nil
local current_kind = nil
local commands = {}
local reading_result = false
local skipping_other_result = 0
local result_buf = {}

local function classify(first_byte)
  local base = first_byte & 0x1f
  if base == 0x06 then return "READ_DATA", 9 end
  if base == 0x08 then return "SIS", 1 end
  if base == 0x07 then return "RECALIBRATE", 2 end
  if base == 0x0f then return "SEEK", 3 end
  if base == 0x03 then return "SPECIFY", 3 end
  if base == 0x04 then return "SENSE_DRIVE_STATUS", 2 end
  return "UNKNOWN", 1
end

taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "flv_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address ~= 0x00fc4003 then return nil end
  local v = byte_value(data, mask)

  if #pending_cmd == 0 then
    current_kind, expected_len = classify(v)
  end
  pending_cmd[#pending_cmd + 1] = v

  if #pending_cmd >= expected_len then
    if current_kind == "READ_DATA" then
      local cmd = { time = now(), bytes = { table.unpack(pending_cmd) } }
      commands[#commands + 1] = cmd
      -- bytes: [1]=CMD [2]=HD/US [3]=C [4]=H [5]=R [6]=N [7]=EOT [8]=GPL [9]=DTL
      print(string.format("FLV_CMD t=%.6f C=%02X H=%02X R=%02X N=%02X EOT=%02X",
        cmd.time, cmd.bytes[3], cmd.bytes[4], cmd.bytes[5], cmd.bytes[6], cmd.bytes[7]))
      reading_result = true
      result_buf = {}
    elseif current_kind == "SIS" then
      skipping_other_result = 2
    elseif current_kind == "SENSE_DRIVE_STATUS" then
      skipping_other_result = 1
    end
    -- SEEK/RECALIBRATE/SPECIFY produce no immediate result-phase bytes
    -- (their completion is reported later via a subsequent SIS).
    pending_cmd = {}
  end
  return nil
end)

taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "flv_fdc_r", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address ~= 0x00fc4003 then return nil end
  local v = byte_value(data, mask)

  if skipping_other_result > 0 then
    skipping_other_result = skipping_other_result - 1
    return nil
  end

  if reading_result then
    result_buf[#result_buf + 1] = v
    if #result_buf == 7 then
      local cmd = commands[#commands]
      if cmd then
        cmd.result = { table.unpack(result_buf) }
        print(string.format("FLV_RESULT t=%.6f ST0=%02X ST1=%02X ST2=%02X C=%02X H=%02X R=%02X N=%02X",
          now(), cmd.result[1], cmd.result[2], cmd.result[3], cmd.result[4], cmd.result[5], cmd.result[6], cmd.result[7]))
      end
      reading_result = false
      result_buf = {}
    end
  end
  return nil
end)

-- 2. IDMA DAPR+BCR capture at each CMR-arm (bit0 set), raw offset/data, no
-- lossy byte reconstruction (methods-static-analysis.md #-worthy lesson
-- from idma-register-map-probe.md). BCR is captured so the transferred
-- byte count per arm comes from the actually-programmed value
-- (idma_implementation_plan.md: BCR-1 is the live byte count), not an
-- assumed constant.
local dapr_hi, dapr_lo, bcr = 0, 0, 0
local arms = {}
local function install_idma_taps()
  taps[#taps + 1] = prog:install_write_tap(0x00fc6800, 0x00fc681f, "flv_idma_w", function(offset, data, mask)
    if offset == 0x00fc6808 then dapr_hi = data & 0xffff end
    if offset == 0x00fc680a then dapr_lo = data & 0xffff end
    if offset == 0x00fc680c then bcr = data & 0xffff end
    if offset == 0x00fc6802 and (data & 1) ~= 0 then
      local dapr = (dapr_hi << 16) | dapr_lo
      arms[#arms + 1] = { time = now(), dapr = dapr, bcr = bcr, bytes = (bcr > 0) and (bcr - 1) or 0 }
      print(string.format("FLV_ARM t=%.6f dapr=%08X bcr=%04X bytes=%u", now(), dapr, bcr, (bcr > 0) and (bcr - 1) or 0))
    end
    return nil
  end)
  print(string.format("FLV_IDMA_TAP_INSTALLED t=%.6f", now()))
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

local file1 = "FILE 1  TUT0RIAL BNK  "
local file_loaded = "FILE L0ADED           "

print("FLV start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("file_loaded_verification_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
local file1_time = now()
print(string.format("FLV_FILE1 t=%.6f", file1_time))

install_idma_taps()

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 20
local loaded = false
while now() < deadline do
  if display.read_raw() == file_loaded then
    loaded = true
    break
  end
  emu.wait(emu.attotime.from_msec(50))
end

print(string.format("FLV_LOAD_STATUS loaded=%s display=\"%s\" t=%.6f", tostring(loaded), display.read_raw(), now()))

-- 3. Validate each parsed READ DATA frame against known disk geometry
-- (80 tracks, 2 sides, 20 sectors/track, 1-indexed R, 512-byte sectors).
-- The FDC FIFO parser is a heuristic byte-run classifier, not a bus
-- decoder with guaranteed frame sync -- an invalid CHRN here is evidence
-- of a desynced frame, not a real command, and is reported, not hidden.
local valid_commands = {}
local rejected = 0
local rejected_pre_file1 = 0
for _, cmd in ipairs(commands) do
  -- bytes: [1]=CMD [2]=HD/US [3]=C [4]=H [5]=R [6]=N [7]=EOT [8]=GPL [9]=DTL
  local C, H, R, N = cmd.bytes[3], cmd.bytes[4], cmd.bytes[5], cmd.bytes[6]
  if cmd.time < file1_time then
    rejected_pre_file1 = rejected_pre_file1 + 1
  elseif C and H and R and N and C < 80 and (H == 0 or H == 1) and R >= 1 and R <= 20 and N == 2 then
    local track_index = C * 2 + H
    cmd.disk_offset = (track_index * 20 + (R - 1)) * 512
    cmd.C, cmd.H, cmd.R, cmd.N = C, H, R, N
    valid_commands[#valid_commands + 1] = cmd
  else
    rejected = rejected + 1
  end
end
print(string.format("FLV_PARSE_QUALITY raw_commands=%u valid=%u rejected_pre_file1=%u rejected_as_desync=%u",
  #commands, #valid_commands, rejected_pre_file1, rejected))

-- 4. Correlate each IDMA arm to the nearest READ DATA command whose 9-byte
-- write completed at or AFTER the arm's CMR-write timestamp (measured
-- order in this build, cross-checked against disk-not-responding-probe.md's
-- byte trace: firmware arms the IDMA channel via CMR *first*, then writes
-- the FDC command bytes a few hundred microseconds later -- the reverse of
-- the initially-assumed order. Nearest-preceding matching was tried first
-- and produced stale pre-FILE1 boot-dialogue mismatches; nearest-following
-- reproduces the measured ~200-300us gaps cleanly). Not a naive index
-- pairing -- command and arm counts are not guaranteed equal.
local MATCH_WINDOW = 0.05
for _, a in ipairs(arms) do
  local best = nil
  for _, cmd in ipairs(valid_commands) do
    if cmd.time >= a.time and cmd.time - a.time <= MATCH_WINDOW and (not best or cmd.time < best.time) then
      best = cmd
    end
  end
  a.cmd = best
end

-- 5. Totals, from the unambiguous IDMA arm/BCR data (SIB register taps,
-- no byte-run heuristic), not from the FDC command parse.
local total_bytes = 0
local min_offset, max_offset = nil, nil
for _, a in ipairs(arms) do
  total_bytes = total_bytes + a.bytes
  if a.cmd and a.cmd.disk_offset then
    local lo, hi = a.cmd.disk_offset, a.cmd.disk_offset + a.bytes
    if not min_offset or lo < min_offset then min_offset = lo end
    if not max_offset or hi > max_offset then max_offset = hi end
  end
end

print(string.format("FLV_TOTALS arms=%u total_bytes=%u disk_min_offset=%s disk_max_offset=%s",
  #arms, total_bytes, tostring(min_offset), tostring(max_offset)))

-- 6. Destination range from DAPR arms.
local dest_min, dest_max = nil, nil
for _, a in ipairs(arms) do
  if not dest_min or a.dapr < dest_min then dest_min = a.dapr end
  if not dest_max or a.dapr > dest_max then dest_max = a.dapr end
end
print(string.format("FLV_DEST arms=%u dest_min=%s dest_max=%s", #arms,
  dest_min and string.format("%08X", dest_min) or "nil",
  dest_max and string.format("%08X", dest_max) or "nil"))

-- 5. Root directory entry for "JM DIGI SYN" (index 2, base $544, stride 26).
local dir_base = 0x544 + 2 * 26
local dir_bytes = {}
for i = 0, 25 do
  dir_bytes[#dir_bytes + 1] = string.format("%02X", prog:read_u8(dir_base + i) & 0xff)
end
print(string.format("FLV_DIRENTRY addr=%06X bytes=%s", dir_base, table.concat(dir_bytes, " ")))

-- 7. Compare each arm's destination memory against its correlated disk
-- source (7's causal a.cmd link, not index pairing), using the arm's own
-- BCR-derived byte count.
local img_path = "floppies/asr10booth/V350.img"
local img = io.open(img_path, "rb")
if not img then
  print(string.format("FLV_IMG_OPEN_FAILED path=%s", img_path))
else
  print(string.format("FLV_IMG_OPENED path=%s", img_path))
  local mismatches = 0
  local compared = 0
  local all_zero_count = 0
  local skipped_uncorrelated = 0
  for idx, a in ipairs(arms) do
    if a.cmd and a.cmd.disk_offset and a.bytes > 0 then
      local n = a.bytes
      img:seek("set", a.cmd.disk_offset)
      local disk_bytes = img:read(n)
      local mem_bytes = {}
      local zero = true
      for i = 0, n - 1 do
        local b = prog:read_u8(a.dapr + i) & 0xff
        mem_bytes[#mem_bytes + 1] = b
        if b ~= 0 then zero = false end
      end
      if zero then all_zero_count = all_zero_count + 1 end
      local mismatch_here = false
      if disk_bytes and #disk_bytes == n then
        for i = 1, n do
          if string.byte(disk_bytes, i) ~= mem_bytes[i] then
            mismatch_here = true
            break
          end
        end
      else
        mismatch_here = true
      end
      compared = compared + 1
      if mismatch_here then
        mismatches = mismatches + 1
        print(string.format("FLV_SECTOR_MISMATCH idx=%u dapr=%08X disk_offset=%08X bytes=%u C=%02X H=%02X R=%02X",
          idx, a.dapr, a.cmd.disk_offset, n, a.cmd.C, a.cmd.H, a.cmd.R))
      end
    else
      skipped_uncorrelated = skipped_uncorrelated + 1
    end
  end
  img:close()
  print(string.format("FLV_COMPARE compared=%u mismatches=%u all_zero_sectors=%u skipped_uncorrelated=%u",
    compared, mismatches, all_zero_count, skipped_uncorrelated))
end

print(string.format("FLV_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("file_loaded_verification_probe", string.format("commands=%u total_bytes=%u", #commands, total_bytes))
