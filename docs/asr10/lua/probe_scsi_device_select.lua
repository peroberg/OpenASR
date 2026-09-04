-- docs/asr10/lua/probe_scsi_device_select.lua
-- Runtime probe to monitor SCSI device selection, CDB generation, and IDMA configuration
local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]

local function press(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 300))
end

local scsi_log = {}
local idma_log = {}

local reg_names = {
  [0x00] = "OWN_ID/CDB_SZ",
  [0x01] = "CONTROL",
  [0x02] = "TIMEOUT_PERIOD",
  [0x03] = "CDB_1",
  [0x04] = "CDB_2",
  [0x05] = "CDB_3",
  [0x06] = "CDB_4",
  [0x07] = "CDB_5",
  [0x08] = "CDB_6",
  [0x09] = "CDB_7",
  [0x0a] = "CDB_8",
  [0x0b] = "CDB_9",
  [0x0c] = "CDB_10",
  [0x0d] = "CDB_11",
  [0x0e] = "CDB_12",
  [0x0f] = "TARGET_LUN",
  [0x10] = "COMMAND_PHASE",
  [0x11] = "SYNCHRONOUS_XFR",
  [0x12] = "XFR_CNT_MSB",
  [0x13] = "XFR_CNT_MID",
  [0x14] = "XFR_CNT_LSB",
  [0x15] = "DESTINATION_ID",
  [0x16] = "SOURCE_ID",
  [0x17] = "SCSI_STATUS",
  [0x18] = "COMMAND",
  [0x19] = "DATA",
  [0x1f] = "AUX_STATUS"
}

local current_indir_reg = 0

-- Tap SCSI registers $FC5000-$FC5003
prog:install_write_tap(0x00fc5000, 0x00fc5003, "scsi_w", function(offset, data, mask)
  local addr = offset & 0x03
  local pc = cpu.state["PC"].value
  if addr == 0 or addr == 1 then
    current_indir_reg = data & 0x1f
    table.insert(scsi_log, {
      t = emu.time(), pc = pc, type = "SEL_REG", reg = current_indir_reg,
      name = reg_names[current_indir_reg] or "UNK", val = data
    })
  elseif addr == 2 or addr == 3 then
    table.insert(scsi_log, {
      t = emu.time(), pc = pc, type = "WRITE", reg = current_indir_reg,
      name = reg_names[current_indir_reg] or "UNK", val = data
    })
  end
  return nil
end)

prog:install_read_tap(0x00fc5000, 0x00fc5003, "scsi_r", function(offset, data, mask)
  local addr = offset & 0x03
  local pc = cpu.state["PC"].value
  if addr == 2 or addr == 3 then
    table.insert(scsi_log, {
      t = emu.time(), pc = pc, type = "READ", reg = current_indir_reg,
      name = reg_names[current_indir_reg] or "UNK", val = data
    })
  end
  return nil
end)

-- Tap MC68302 IDMA registers ($FC6800-$FC680F)
prog:install_write_tap(0x00fc6800, 0x00fc680f, "idma_w", function(offset, data, mask)
  local pc = cpu.state["PC"].value
  local reg_addr = 0x00fc6800 + offset
  local name = "UNK"
  if offset == 0x02 then name = "CMR"
  elseif offset == 0x04 or offset == 0x06 then name = "SAPR"
  elseif offset == 0x08 or offset == 0x0a then name = "DAPR"
  elseif offset == 0x0c then name = "BCR"
  elseif offset == 0x0e then name = "CSR"
  end
  table.insert(idma_log, {
    t = emu.time(), pc = pc, addr = reg_addr, name = name, data = data, mask = mask
  })
  return nil
end)

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
print(string.format("BOOT OK at t=%.2f: %s", emu.time(), text))

-- 1. Enter Command Mode ($06)
print("\n[Step 1] Pressing $06 (COMMAND)...")
press(0x06, 300)
print("Display: " .. display.read_raw())

-- 2. Select Disk / System Category ($1B)
print("\n[Step 2] Pressing $1B (DISK/SYSTEM)...")
press(0x1b, 300)
print("Display: " .. display.read_raw())

-- 3. Walk with Right Arrow ($11) until CHANGE STORAGE DEVICE
print("\n[Step 3] Walking commands with $11 (Right Arrow)...")
for i = 1, 5 do
  press(0x11, 200)
end
print("Display at target: " .. display.read_raw())

-- 4. Press Enter/Yes ($23) to enter parameter edit
print("\n[Step 4] Pressing $23 (ENTER/YES) to inspect storage device parameter...")
press(0x23, 400)
print("Display after ENTER: " .. display.read_raw())

-- 5. Change value to SCSI 0 using Up Arrow ($0A)
print("\n[Step 5] Pressing $0A (UP ARROW) to select SCSI 0...")
press(0x0a, 400)
print("Display after UP: " .. display.read_raw())

-- 6. Confirm with Enter/Yes ($23)
print("\n[Step 6] Pressing $23 (ENTER/YES) to activate SCSI 0...")
local num_scsi_before = #scsi_log
local num_idma_before = #idma_log
press(0x23, 1000)
print("Display after confirm: " .. display.read_raw())

-- 7. Try entering LOAD -> DISK to browse files on SCSI 0
print("\n[Step 7] Entering LOAD ($1A) -> DISK ($1C) to read directory from SCSI 0...")
press(0x1a, 400)
print("Display at LOAD: " .. display.read_raw())
press(0x1c, 1000)
print("Display at LOAD DISK: " .. display.read_raw())

-- Wait 2 seconds to let any async SCSI activity settle
emu.wait(emu.attotime.from_msec(2000))
print("Display after 2s settle: " .. display.read_raw())

print("\n=========================================================================")
print(string.format("=== SCSI OPERATIONS LOGGED (Total: %d, New since select: %d) ===",
  #scsi_log, #scsi_log - num_scsi_before))
print("=========================================================================")
for i = num_scsi_before + 1, #scsi_log do
  local op = scsi_log[i]
  print(string.format("  [%03d] t=%.6f PC=$%06X %-9s Reg $%02X (%-15s) = $%04X",
    i, op.t, op.pc, op.type, op.reg, op.name, op.val))
end

print("\n=========================================================================")
print(string.format("=== IDMA WRITES LOGGED (Total: %d, New since select: %d) ===",
  #idma_log, #idma_log - num_idma_before))
print("=========================================================================")
for i = num_idma_before + 1, #idma_log do
  local w = idma_log[i]
  print(string.format("  [%03d] t=%.6f PC=$%06X W addr=$%06X (%-4s) data=$%04X mask=$%04X",
    i, w.t, w.pc, w.addr, w.name, w.data, w.mask))
end

manager.machine:exit()
