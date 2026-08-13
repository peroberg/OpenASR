-- ASR-10 vector table compare during V3.50 instrument-load stall.
--
-- Observation only. Compares live low-RAM vectors $000000-$0003FF with the
-- ROM copy at $F82000 after the display reaches "LOADING JM DIGI SYN".

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local file1 = "FILE 1  TUT0RIAL BNK  "
local load_text = "L0ADING JM DIGI 5YN   "

local function now()
  return emu.time()
end

local function pc()
  return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff
end

local function press_button(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local bit = code & 0x1f
  local mask = 1 << bit
  local port = manager.machine.ioport.ports[port_name]
  if not port then
    reg.fail("vector_table_compare", string.format("missing_ioport %s", port_name))
    return
  end
  local field = port:field(mask)
  if not field then
    reg.fail("vector_table_compare", string.format("missing_field code=%02X mask=%08X", code, mask))
    return
  end
  print(string.format("VECTOR_COMPARE_PRESS t=%.6f code=%02X pc=%06X", now(), code, pc()))
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local function hex_bytes(address, length)
  local parts = {}
  for offset = 0, length - 1 do
    parts[#parts + 1] = string.format("%02X", prog:read_u8(address + offset) & 0xff)
  end
  return table.concat(parts, "")
end

local function compare_vectors()
  local changed = 0
  local changed_vectors = {}
  for vector = 0, 255 do
    local address = vector * 4
    local live = prog:read_u32(address) & 0xffffffff
    local rom = prog:read_u32(0x00f82000 + address) & 0xffffffff
    if live ~= rom then
      changed = changed + 1
      changed_vectors[#changed_vectors + 1] = { vector = vector, live = live, rom = rom }
      print(string.format("VECTOR_COMPARE_DIFF vector=%02X offset=%03X live=%08X rom=%08X", vector, address, live, rom))
    end
  end

  print(string.format("VECTOR_COMPARE_SUMMARY changed=%u compared_bytes=1024 live_base=000000 rom_base=F82000 display=\"%s\"",
    changed, display.read_raw()))

  for _, item in ipairs(changed_vectors) do
    local target = item.live & 0x00ffffff
    local ok = target <= 0x00ffffc0
    if ok then
      print(string.format("VECTOR_COMPARE_TARGET vector=%02X target=%06X bytes64=%s",
        item.vector, target, hex_bytes(target, 64)))
      local scan_len = 1024
      for offset = 0, scan_len - 6 do
        local b0 = prog:read_u8(target + offset + 0) & 0xff
        local b1 = prog:read_u8(target + offset + 1) & 0xff
        local b2 = prog:read_u8(target + offset + 2) & 0xff
        local b3 = prog:read_u8(target + offset + 3) & 0xff
        local b4 = prog:read_u8(target + offset + 4) & 0xff
        local b5 = prog:read_u8(target + offset + 5) & 0xff
        if b0 == 0xff and b1 == 0xfc and b2 == 0x40 and b3 <= 0x03 then
          print(string.format("VECTOR_COMPARE_FDC_PATTERN vector=%02X target=%06X at=%06X pattern=FFFC40%02X",
            item.vector, target, target + offset, b3))
        elseif b0 == 0x00 and b1 == 0xfc and b2 == 0x40 and b3 <= 0x03 then
          print(string.format("VECTOR_COMPARE_FDC_PATTERN vector=%02X target=%06X at=%06X pattern=00FC40%02X",
            item.vector, target, target + offset, b3))
        elseif b0 == 0x12 and b1 == 0x39 and b2 == 0xff and b3 == 0xfc and b4 == 0x40 and b5 <= 0x03 then
          print(string.format("VECTOR_COMPARE_FDC_PATTERN vector=%02X target=%06X at=%06X pattern=move.b_FFFC40%02X",
            item.vector, target, target + offset, b5))
        elseif b0 == 0x13 and b1 == 0xfc and b4 == 0xff and b5 == 0xfc then
          local b6 = prog:read_u8(target + offset + 6) & 0xff
          local b7 = prog:read_u8(target + offset + 7) & 0xff
          if b6 == 0x40 and b7 <= 0x03 then
            print(string.format("VECTOR_COMPARE_FDC_PATTERN vector=%02X target=%06X at=%06X pattern=move.b_imm_FFFC40%02X",
              item.vector, target, target + offset, b7))
          end
        end
      end
    else
      print(string.format("VECTOR_COMPARE_TARGET vector=%02X target=%06X bytes64=OUT_OF_RANGE", item.vector, target))
    end
  end
end

print("VECTOR_COMPARE start wait_for=FILE1 sequence=0A,23,02")

local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("vector_table_compare", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

emu.wait(emu.attotime.from_msec(500))
print(string.format("VECTOR_COMPARE_REACHED_FILE1 t=%.6f display=\"%s\"", now(), text))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 10
while now() < deadline do
  if display.read_raw() == load_text then
    print(string.format("VECTOR_COMPARE_LOADING t=%.6f display=\"%s\"", now(), display.read_raw()))
    emu.wait(emu.attotime.from_msec(500))
    compare_vectors()
    manager.machine:exit()
    return
  end
  emu.wait(emu.attotime.from_msec(5))
end

reg.fail("vector_table_compare", string.format("load_display_timeout final_display=\"%s\"", display.read_raw()))
manager.machine:exit()
