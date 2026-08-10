-- ASR-10 display probe.
--
-- Observation only: reads the 22 layout output values (digit0..digit21),
-- inverts the driver's ascii_to_14seg table for the glyphs used by ASR-10,
-- and prints the display text whenever it changes.

local GLYPH = {
  [0x0000] = " ",
  [0x00c0] = "-",
  [0x0008] = "_",
  [0x4000] = ".",
  [0x8000] = ",",
  [0x003f] = "0",
  [0x0006] = "1",
  [0x00db] = "2",
  [0x00cf] = "3",
  [0x00e6] = "4",
  [0x00ed] = "5",
  [0x00fd] = "6",
  [0x0007] = "7",
  [0x00ff] = "8",
  [0x00ef] = "9",
  [0x00f7] = "A",
  [0x03f9] = "B",
  [0x0039] = "C",
  [0x030f] = "D",
  [0x00f9] = "E",
  [0x00f1] = "F",
  [0x00bd] = "G",
  [0x00f6] = "H",
  [0x0309] = "I",
  [0x001e] = "J",
  [0x3030] = "K",
  [0x0038] = "L",
  [0x1836] = "M",
  [0x2836] = "N",
  [0x00f3] = "P",
  [0x203f] = "Q",
  [0x20f3] = "R",
  [0x0301] = "T",
  [0x003e] = "U",
  [0x2422] = "V",
  [0x2436] = "W",
  [0x3c00] = "X",
  [0x1a00] = "Y",
  [0x1409] = "Z",
}

local AMBIGUOUS_GLYPHS = {
  [0x003f] = { raw = "0", alternatives = "0/O" },
  [0x00ed] = { raw = "5", alternatives = "5/S" },
}

local NORMALIZED_TEXT = {
  ["   EN50NIQ  A5R-10    "] = "   ENSONIQ  ASR-10    ",
  ["    L0ADING 5Y5TEM    "] = "    LOADING SYSTEM    ",
  ["TUNING KBD - HAND5 0FF"] = "TUNING KBD - HANDS OFF",
  ["    KEYB0ARD TUNED    "] = "    KEYBOARD TUNED    ",
  ["N0 IN5T 0R BANK FILE5 "] = "NO INST OR BANK FILES ",
}

local display = {
  last = nil,
  changes = 0,
}

local function read_text()
  local chars = {}
  for index = 0, 21 do
    local value = manager.machine.output:get_indexed_value("digit", index)
    chars[#chars + 1] = GLYPH[value] or "?"
  end
  return table.concat(chars)
end

local function normalize_text(text)
  return NORMALIZED_TEXT[text] or text
end

display.frame_notifier = emu.add_machine_frame_notifier(function()
  local text = read_text()
  if text ~= display.last then
    display.changes = display.changes + 1
    display.last = text
    print(string.format(
      "DISPLAY_CHANGE seq=%u text=\"%s\" normalized=\"%s\"",
      display.changes, text, normalize_text(text)))
  end
end)

display.stop_notifier = emu.add_machine_stop_notifier(function()
  local final = display.last or ""
  print(string.format(
    "DISPLAY_SUMMARY changes=%u final=\"%s\" normalized=\"%s\"",
    display.changes, final, normalize_text(final)))
end)
