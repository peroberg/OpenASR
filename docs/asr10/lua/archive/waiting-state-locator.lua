-- Observe the firmware state behind the sampling display
-- "WAITING...nnn SEC LEFT" and its statically identified SCC exit producer.
-- Observation only; the only state changes are documented front-panel presses.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local opcodes = cpu.spaces["opcodes"] or cpu.spaces["decrypted_opcodes"] or prog
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local a5_state = cpu.state["A5"]
local a7_state = cpu.state["A7"] or cpu.state["SP"] or cpu.state["GENSP"]
local a0_state = cpu.state["A0"]
local a1_state = cpu.state["A1"]
local a2_state = cpu.state["A2"]
local d0_state = cpu.state["D0"]
local d1_state = cpu.state["D1"]
local d2_state = cpu.state["D2"]
local d3_state = cpu.state["D3"]
local taps = {}
local phase = "boot"
local hits = {}
local hardware_reads = {}
local hardware_detail = {}
local sample_ram_writes = {}
local sibling_ram_writes = {}
local pc_witness = 0

local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function a5() return a5_state and (a5_state.value & 0x00ffffff) or 0xffffffff end
local function state(register) return register and (register.value & 0xffffffff) or 0xffffffff end
local function count(table_, key) table_[key] = (table_[key] or 0) + 1 end

local function context(label)
  local sp = state(a7_state) & 0x00ffffff
  print(string.format(
    "WAIT_CONTEXT t=%.6f phase=%s label=%s pc=%06X A0=%08X A1=%08X A2=%08X A5=%08X A7=%08X STACK0=%08X D0=%08X D1=%08X D2=%08X D3=%08X D04=%04X CE8=%08X BD6=%08X D00=%08X FFD15C=%08X SCC1_CB=%08X SCC2_CB=%08X THRESHOLD_INDEX=%04X",
    now(), phase, label, pc(), state(a0_state), state(a1_state), state(a2_state), a5(),
    state(a7_state), prog:read_u32(sp),
    state(d0_state), state(d1_state), state(d2_state), state(d3_state),
    prog:read_u16(0x0d04), prog:read_u32(0x0ce8), prog:read_u32(0x0bd6),
    prog:read_u32(0x0d00), prog:read_u32(0x00ffd15c), prog:read_u32(0x12d8),
    prog:read_u32(0x1320), prog:read_u16(0x017c)))
end

local function wait_seconds(seconds)
  local deadline = now() + seconds
  while now() < deadline do
    pc_witness = pc_witness + 1
    emu.wait(emu.attotime.from_msec(10))
  end
end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then error(string.format("missing panel button %02X", code)) end
  print(string.format("WAIT_LOCATOR_BUTTON t=%.6f phase=%s code=%02X", now(), phase, code))
  field:set_value(1)
  wait_seconds(0.080)
  field:clear_value()
  wait_seconds((settle_ms or 250) / 1000)
end

-- Negative-control candidate: this ROM routine also uses the WAITING string,
-- but static analysis places the sampling state in the loaded V3.50 code.
taps[#taps + 1] = opcodes:install_read_tap(
  0x00f92fb0, 0x00f9311f, "waiting_rom_candidate", function(offset, data, mask)
    count(hits, phase .. ":rom_candidate")
    return nil
  end)

taps[#taps + 1] = opcodes:install_read_tap(
  0x00ffbf6c, 0x00ffc117, "waiting_state_code", function(offset, data, mask)
    count(hits, phase .. ":waiting")
    if pc() == 0x00ffbf6c or pc() == 0x00ffbfa4 or pc() == 0x00ffbfb4 or pc() == 0x00ffc006
        or pc() == 0x00ffc02e or pc() == 0x00ffc04c then
      context(string.format("waiting_pc_%06X", pc()))
    end
    return nil
  end)

taps[#taps + 1] = opcodes:install_read_tap(
  0x00ff643c, 0x00ff67ab, "waiting_scc_producer", function(offset, data, mask)
    count(hits, phase .. ":scc")
    if pc() == 0x00ff65cc or pc() == 0x00ff6654 or pc() == 0x00ff665c then
      context(string.format("scc_pc_%06X", pc()))
    end
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(
  0x00000d04, 0x00000d05, "waiting_state_w", function(offset, data, mask)
    print(string.format(
      "WAIT_STATE_W t=%.6f phase=%s pc=%06X addr=%06X data=%08X mask=%08X",
      now(), phase, pc(), offset, data, mask))
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(
  0x0000017c, 0x0000017d, "waiting_threshold_w", function(offset, data, mask)
    print(string.format(
      "WAIT_THRESHOLD_W t=%.6f phase=%s pc=%06X data=%08X mask=%08X",
      now(), phase, pc(), data, mask))
    return nil
  end)

taps[#taps + 1] = prog:install_write_tap(
  0x00ffd15c, 0x00ffd15f, "waiting_trigger_level_w", function(offset, data, mask)
    print(string.format(
      "WAIT_TRIGGER_LEVEL_W t=%.6f phase=%s pc=%06X addr=%06X data=%08X mask=%08X",
      now(), phase, pc(), offset, data, mask))
    return nil
  end)

local function install_runtime_taps()
  -- Install after FILE 1: the firmware has relocated the MC68302 window by then,
  -- so later BAR writes cannot silently invalidate these SIB taps.
  for _, range in ipairs({
    { 0x00fc6888, 0x00fc6889, "scc_event" },
    { 0x00fc6898, 0x00fc6899, "scc_event" },
    { 0x00fc6824, 0x00fc6829, "port_b" },
    { 0x00fc2000, 0x00fc20ff, "es5506" },
    { 0x00fc3000, 0x00fc31ff, "es5510" },
  }) do
    taps[#taps + 1] = prog:install_read_tap(
      range[1], range[2], "waiting_hw_" .. range[3], function(offset, data, mask)
        count(hardware_reads, phase .. ":" .. range[3])
        count(hardware_detail, string.format(
          "%s:%s:%06X:%06X", phase, range[3], offset & 0x00ffffff, pc()))
        return nil
      end)
  end

  for _, range in ipairs({
    { 0x00fc6888, 0x00fc6889, "scc_event_w" },
    { 0x00fc6898, 0x00fc6899, "scc_event_w" },
    { 0x00fc6824, 0x00fc6829, "port_b_w" },
  }) do
    taps[#taps + 1] = prog:install_write_tap(
      range[1], range[2], "waiting_hw_" .. range[3], function(offset, data, mask)
        count(hardware_reads, phase .. ":" .. range[3])
        count(hardware_detail, string.format(
          "%s:%s:%06X:%06X:%08X:%08X", phase, range[3],
          offset & 0x00ffffff, pc(), data, mask))
        return nil
      end)
  end

  taps[#taps + 1] = prog:install_write_tap(
    0x00100000, 0x001fffff, "waiting_sample_ram_w", function(offset, data, mask)
      count(sample_ram_writes, phase)
      return nil
    end)

  taps[#taps + 1] = prog:install_write_tap(
    0x00000000, 0x0005ffff, "waiting_sibling_ram_w", function(offset, data, mask)
      count(sibling_ram_writes, phase)
      return nil
    end)
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("waiting_state_locator", string.format("boot_timeout display=\"%s\"", text))
  return
end

install_runtime_taps()

phase = "sample_source"
press_button(0x20, 500)
phase = "level_detect"
press_button(0x02, 1000)
phase = "threshold_max"
context("threshold_before")
for _ = 1, 24 do press_button(0x0a, 40) end
context("threshold_after")
phase = "record_start"
press_button(0x23, 200)
context("record_start")
wait_seconds(2.0)

for key, value in pairs(hardware_detail) do
  if key:sub(1, 13) == "record_start:" then
    print(string.format("WAIT_HW_DETAIL key=%s count=%u", key, value))
  end
end

print(string.format(
  "WAIT_FINAL display=\"%s\" waiting_exec=%u scc_exec=%u rom_candidate_exec=%u pc_witness=%u D04=%04X CE8=%08X THRESHOLD_INDEX=%04X SCC_EVENT_READS=%u SCC_EVENT_WRITES=%u PORT_B_READS=%u PORT_B_WRITES=%u ES5506_READS=%u ES5510_READS=%u SAMPLE_RAM_WRITES=%u SIBLING_RAM_WRITES=%u",
  display.read_raw(), hits["record_start:waiting"] or 0, hits["record_start:scc"] or 0,
  hits["record_start:rom_candidate"] or 0, pc_witness,
  prog:read_u16(0x0d04), prog:read_u32(0x0ce8), prog:read_u16(0x017c),
  hardware_reads["record_start:scc_event"] or 0,
  hardware_reads["record_start:scc_event_w"] or 0,
  hardware_reads["record_start:port_b"] or 0,
  hardware_reads["record_start:port_b_w"] or 0,
  hardware_reads["record_start:es5506"] or 0,
  hardware_reads["record_start:es5510"] or 0,
  sample_ram_writes.record_start or 0, sibling_ram_writes.record_start or 0))

reg.pass("waiting_state_locator", string.format(
  "display=\"%s\" waiting=%u scc=%u witness=%u", display.read_raw(),
  hits["record_start:waiting"] or 0, hits["record_start:scc"] or 0, pc_witness))
