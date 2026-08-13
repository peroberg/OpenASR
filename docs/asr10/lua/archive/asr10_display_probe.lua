-- ASR-10 display probe.
--
-- Observation only: reads the 22 VFD output values and prints the display text
-- whenever it changes.

local display_lib = dofile("docs/asr10/lua/lib/asr10_display.lua")

local display = {
  last = nil,
  changes = 0,
}

display.frame_notifier = emu.add_machine_frame_notifier(function()
  local text = display_lib.read_raw()
  if text ~= display.last then
    display.changes = display.changes + 1
    display.last = text
    print(string.format(
      "DISPLAY_CHANGE seq=%u text=\"%s\" normalized=\"%s\"",
      display.changes, text, display_lib.normalize(text)))
  end
end)

display.stop_notifier = emu.add_machine_stop_notifier(function()
  local final = display.last or ""
  print(string.format(
    "DISPLAY_SUMMARY changes=%u final=\"%s\" normalized=\"%s\"",
    display.changes, final, display_lib.normalize(final)))
end)
