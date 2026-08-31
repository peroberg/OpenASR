-- Bank 11 Semantic True Load and Playback Test
-- Flöde: RESET -> BOOT -> Ladda File 12 (BLUES DRUMS) -> Ladda File 13 (BLUES BASS) -> Ladda File 14 (BLUES ORGAN) -> Ladda File 11 (ATRK TUT BNK) -> PLAY
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
  emu.wait(emu.attotime.from_msec(settle_ms or 500))
end

local function nav_to_file_name(pattern)
  press(0x1a, 400) -- LOAD to enter file browser
  for step = 1, 20 do
    local d = display.read_raw()
    if d:find(pattern) then
      return true
    end
    press(0x0a, 200) -- UP
  end
  return false
end

local function load_instrument(target_pattern, slot_btn, expected_name)
  print(string.format("Navigating to Instrument '%s'...", expected_name))
  if not nav_to_file_name(target_pattern) then
    error("Could not find file matching " .. target_pattern)
  end
  print(string.format("Found file: %s", display.read_raw()))
  press(0x23, 400) -- ENTER / YES
  press(slot_btn, 400) -- Assign to Slot
  local deadline = emu.time() + 45
  local loaded = false
  while emu.time() < deadline do
    local d = display.read_raw()
    if d:find("FILE L0ADED") or d:find("L0AD C0MPLETED") then
      print(string.format("Loaded '%s' into Slot button $%02X at t=%.2f: display=%s",
        expected_name, slot_btn, emu.time(), d))
      loaded = true
      break
    end
    emu.wait(emu.attotime.from_msec(200))
  end
  if not loaded then
    error(string.format("Timeout loading '%s'", expected_name))
  end
  emu.wait(emu.attotime.from_msec(500))
end

local function load_bank(target_pattern, expected_name)
  print(string.format("Navigating to Bank '%s'...", expected_name))
  if not nav_to_file_name(target_pattern) then
    error("Could not find bank matching " .. target_pattern)
  end
  print(string.format("Found bank: %s", display.read_raw()))
  press(0x23, 400) -- ENTER / YES
  press(0x23, 400) -- YES confirm
  press(0x02, 400) -- Bank slot 1
  local deadline = emu.time() + 45
  local loaded = false
  while emu.time() < deadline do
    local d = display.read_raw()
    if d:find("BANK L0AD C0MPLETED") or d:find("FILE L0ADED") then
      print(string.format("Loaded Bank '%s' at t=%.2f: display=%s", expected_name, emu.time(), d))
      loaded = true
      break
    end
    emu.wait(emu.attotime.from_msec(200))
  end
  if not loaded then
    error(string.format("Timeout loading Bank '%s'", expected_name))
  end
  emu.wait(emu.attotime.from_msec(500))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("bank11_true_load_play", "boot_timeout display=" .. text)
  return
end
print("BOOT OK: " .. text)

-- 1. Ladda File 12 (BLUES DRUMS) -> Slot 1 ($02)
load_instrument("BLUE5 DRUM", 0x02, "BLUES DRUMS")

-- 2. Ladda File 13 (BLUES BASS) -> Slot 2 ($08)
load_instrument("BLUE5 BA55", 0x08, "BLUES BASS")

-- 3. Ladda File 14 (BLUES ORGAN) -> Slot 3 ($0E)
load_instrument("BLUE5 0RGAN", 0x0e, "BLUES ORGAN")

-- 4. Ladda File 11 (ATRK TUT BNK) -> Bank 1 ($02)
load_bank("ATRK TUT BNK", "ATRK TUT BNK")

-- 5. Navigera till Sequencer och starta uppspelning
print("Navigating to Sequencer...")
press(0x15, 500) -- SEQ • SONG category
print("Display at Sequencer: " .. display.read_raw())

print("Pressing PLAY ($1D)...")
press(0x1d, 500) -- PLAY

-- Monitor sequencer bar progression
local bars_observed = {}
local deadline = emu.time() + 10
while emu.time() < deadline do
  local d = display.read_raw()
  local bar_match = d:match("BAR=(%d+)")
  if bar_match and not bars_observed[bar_match] then
    bars_observed[bar_match] = true
    print(string.format("Sequencer running: %s at t=%.2f", d, emu.time()))
  end
  emu.wait(emu.attotime.from_msec(200))
end

press(0x17, 500) -- STOP
print("Sequencer STOPPED: " .. display.read_raw())

reg.pass("bank11_true_load_play", "loaded prerequisites and played sequencer")
