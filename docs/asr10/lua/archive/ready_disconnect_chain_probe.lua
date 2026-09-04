-- Del 3, positive test: with FDC ready disconnected (set_ready_line_connected
-- (false)) AND the naive FDC INTRQ -> MC68302 IRQ1 wiring both active, does
-- boot survive to FILE 1, and when the instrument-load path's own FDC
-- RECALIBRATE sets up $0402 properly, does the completion chain progress
-- past SIS to SEEK 0F 00 01, READ DATA $46, and IDMA register writes?
--
-- docs/asr10/investigations/ready-line-artifact-probe.md. Observation only;
-- both machine_config lines under test are temporary and removed after.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local cpu_space = cpu.spaces["cpu_space"]
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
local seek_seen, readdata_seen, idma_seen = false, false, false

taps[#taps + 1] = prog:install_write_tap(0x00fc4000, 0x00fc4003, "rdchain_fdc_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    local v = byte_value(data, mask)
    print(string.format("RDCHAIN_FDC_W t=%.6f pc=%06X value=%02X", now(), pc(), v))
  end
  return nil
end)

taps[#taps + 1] = prog:install_read_tap(0x00fc4000, 0x00fc4003, "rdchain_fdc_r", function(offset, data, mask)
  local address = byte_address(offset, mask)
  if address == 0x00fc4003 then
    print(string.format("RDCHAIN_FDC_R t=%.6f pc=%06X value=%02X", now(), pc(), byte_value(data, mask)))
  end
  return nil
end)

taps[#taps + 1] = prog:install_read_tap(0x00000400, 0x00000407, "rdchain_0402_r", function(offset, data, mask)
  print(string.format("RDCHAIN_0402_R t=%.6f pc=%06X offset=%06X data=%08X", now(), pc(), offset, data))
  return nil
end)

taps[#taps + 1] = prog:install_read_tap(0x00000008, 0x0000001f, "rdchain_exc_r", function(offset, data, mask)
  print(string.format("RDCHAIN_EXC_VECTOR_R t=%.6f pc=%06X vector=%u target=%08X", now(), pc(), offset // 4, data))
  return nil
end)

taps[#taps + 1] = prog:install_write_tap(0x00fc6802, 0x00fc6811, "rdchain_idma_w", function(offset, data, mask)
  local address = byte_address(offset, mask)
  print(string.format("RDCHAIN_IDMA_W t=%.6f pc=%06X addr=%06X value=%02X", now(), pc(), address, byte_value(data, mask)))
  idma_seen = true
  return nil
end)

if cpu_space then
  taps[#taps + 1] = cpu_space:install_read_tap(0x000000, 0xffffff, "rdchain_irq1_r", function(offset, data, mask)
    if offset ~= 0x00fffff2 then
      return nil
    end
    local vector = data & 0xff
    local target = prog:read_u32(vector * 4) & 0xffffffff
    print(string.format("RDCHAIN_IACK1 t=%.6f vector=%02X target=%08X pc=%06X", now(), vector, target, pc()))
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

local file1 = "FILE 1  TUT0RIAL BNK  "
local error129 = "ERR0R 129 - REB00T    "

print("RDCHAIN start wait_for=FILE1")
local ok, text = reg.wait_for_text(file1, 45)
if not ok then
  reg.fail("ready_disconnect_chain_probe", string.format("boot_timeout final_display=\"%s\"", text))
  return
end
print(string.format("RDCHAIN_FILE1 t=%.6f", now()))

emu.wait(emu.attotime.from_msec(500))
press_button(0x0a)
press_button(0x23)
press_button(0x02)

local deadline = now() + 30
local final_text = nil
while now() < deadline do
  local cur = display.read_raw()
  if cur == error129 then
    final_text = cur
    break
  end
  emu.wait(emu.attotime.from_msec(50))
end
if not final_text then final_text = display.read_raw() end

print(string.format("RDCHAIN_SUMMARY final_display=\"%s\" t=%.6f seek_bytes_seen=%s idma_seen=%s",
  final_text, now(), tostring(seek_seen), tostring(idma_seen)))
reg.pass("ready_disconnect_chain_probe", string.format("display=\"%s\"", final_text))
