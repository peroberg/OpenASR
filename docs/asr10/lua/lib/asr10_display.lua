-- Shared ASR-10 VFD display reader.
--
-- Observation only: reads the 22 VFD output values and inverts the glyph table
-- used by the existing display probe. read_raw() returns exactly what the
-- output glyphs decode to, without normalising ambiguous segments.

local M = {}

M.GLYPH = {
  [0x0000] = " ",
  [0x00c0] = "-",
  [0x0008] = "_",
  [0x4000] = ".",
  [0x8000] = ",",
  [0x003f] = "0",
  [0x0006] = "1",
  [0x0300] = "1",
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
  [0x038f] = "B",
  [0x0039] = "C",
  [0x030f] = "D",
  [0x00f9] = "E",
  [0x0079] = "E",
  [0x00f1] = "F",
  [0x0071] = "F",
  [0x00bd] = "G",
  [0x00f6] = "H",
  [0x0309] = "I",
  [0x001e] = "J",
  [0x3030] = "K",
  [0x3070] = "K",
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

M.AMBIGUOUS_GLYPHS = {
  [0x003f] = { raw = "0", alternatives = "0/O" },
  [0x00ed] = { raw = "5", alternatives = "5/S" },
}

M.NORMALIZED_TEXT = {
  ["   EN50NIQ  A5R-10    "] = "   ENSONIQ  ASR-10    ",
  ["    L0ADING 5Y5TEM    "] = "    LOADING SYSTEM    ",
  ["TUNING KBD - HAND5 0FF"] = "TUNING KBD - HANDS OFF",
  ["    KEYB0ARD TUNED    "] = "    KEYBOARD TUNED    ",
  ["N0 IN5T 0R BANK FILE5 "] = "NO INST OR BANK FILES ",
  ["FILE 1  TUT0RIAL BNK  "] = "FILE 1  TUTORIAL BNK  ",
  ["FILE 2  JM DIGI 5YN   "] = "FILE 2  JM DIGI SYN   ",
}

local function read_output(index)
  local root = manager.machine.devices[":"]
  local vfd = root:output(string.format("vfd%u", index))
  if vfd:exists() then return vfd:get() end
  return root:output(string.format("digit%u", index)):get()
end

function M.read_raw()
  local values = M.read_values()
  local chars = {}
  for index = 0, 21 do
    local value = values[index + 1]
    chars[#chars + 1] = M.GLYPH[value] or "?"
  end
  return table.concat(chars)
end

function M.read_values()
  local values = {}
  for index = 0, 21 do
    values[#values + 1] = read_output(index)
  end
  return values
end

function M.normalize(text)
  return M.NORMALIZED_TEXT[text] or text
end

return M
