-- Shared helpers for ASR-10 Lua regression tests.

local display = dofile("docs/asr10/lua/lib/asr10_display.lua")
local taps = dofile("docs/asr10/lua/lib/asr10_taps.lua")

local M = {
  display = display,
  taps = taps,
}

function M.pass(name, detail)
  if detail and detail ~= "" then
    print(string.format("PASS %s %s", name, detail))
  else
    print(string.format("PASS %s", name))
  end
  manager.machine:exit()
end

function M.fail(name, reason)
  print(string.format("FAIL %s %s", name, reason))
  manager.machine:exit()
end

function M.wait_for_text(expected, timeout_seconds)
  local deadline = emu.time() + timeout_seconds
  while emu.time() < deadline do
    local text = display.read_raw()
    if text == expected then
      return true, text
    end
    emu.wait(emu.attotime.from_msec(100))
  end
  return false, display.read_raw()
end

function M.wait_until_changed(from_text, timeout_seconds)
  local deadline = emu.time() + timeout_seconds
  while emu.time() < deadline do
    local text = display.read_raw()
    if text ~= from_text then
      return true, text
    end
    emu.wait(emu.attotime.from_msec(50))
  end
  return false, display.read_raw()
end

return M
