-- Del 1: empirical button-to-routine map. For every raw button code
-- (0x00-0x3F), press it and record: the wire frame set_button() sends
-- (0x80|code then 0x00 -- NOT the raw code alone), the PC trace until
-- the machine returns to the scheduler idle loop ($F87F92-$F87FD0,
-- subroutine-index.md), the $0B6A "current task" pointer before/after,
-- and the display text before/after. Run in two contexts: idle after
-- FILE LOADED, and the REC SRC screen.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end
local function in_idle(p) return p >= 0xF87F80 and p <= 0xF87FD0 end

local function press_button(code, settle_ms)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return false end
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(settle_ms or 200))
  return true
end

-- Trace PCs from just before a press until the machine settles back
-- into the idle loop (or a timeout), returning the set of distinct
-- non-idle PCs visited and whether it actually returned to idle.
local function press_and_trace(code)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return nil end

  local visited = {}
  local order = {}
  local last_pc = nil
  local function sample()
    local p = pc()
    if not in_idle(p) and p ~= last_pc then
      if not visited[p] then
        visited[p] = true
        order[#order + 1] = p
      end
    end
    last_pc = p
    return p
  end

  field:set_value(1)
  local deadline1 = now() + 0.08
  while now() < deadline1 do
    sample()
    emu.wait(emu.attotime.from_usec(150))
  end
  field:clear_value()

  local returned_idle = false
  local idle_streak = 0
  local deadline2 = now() + 0.05
  while now() < deadline2 do
    local p = sample()
    if in_idle(p) then
      idle_streak = idle_streak + 1
      if idle_streak >= 5 then returned_idle = true break end
    else
      idle_streak = 0
    end
    emu.wait(emu.attotime.from_usec(150))
  end

  return { order = order, returned_idle = returned_idle }
end

local task_ptr_writes = 0
local task_ptr_last = 0
local prog_taps = {}
prog_taps[#prog_taps + 1] = prog:install_write_tap(0x000b6a, 0x000b6b, "task_ptr_w", function(offset, data, mask)
  task_ptr_writes = task_ptr_writes + 1
  task_ptr_last = data
  return nil
end)

local function sweep(context_label, enter_fn)
  print(string.format("BRS_CONTEXT %s", context_label))
  for code = 0, 0x3f do
    if enter_fn then enter_fn() end
    local before_text = display.read_raw()
    local before_task_writes = task_ptr_writes
    local trace = press_and_trace(code)
    if trace then
      local after_text = display.read_raw()
      local changed = after_text ~= before_text
      local task_delta = task_ptr_writes - before_task_writes
      if changed or task_delta > 0 or #trace.order > 0 then
        local pcs = {}
        for i = 1, math.min(#trace.order, 8) do
          pcs[#pcs + 1] = string.format("%06X", trace.order[i])
        end
        print(string.format("BRS code=%02X frame=%02X,00 display_changed=%s task_writes=%u returned_idle=%s pcs=%s after=\"%s\"",
          code, 0x80 | code, tostring(changed), task_delta, tostring(trace.returned_idle),
          table.concat(pcs, ">"), after_text))
      end
    end
  end
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("button_routine_sweep", string.format("boot_timeout display=\"%s\"", text))
  return
end

press_button(0x0a, 250)
press_button(0x23, 250)
press_button(0x02, 250)
reg.wait_for_text("FILE L0ADED           ", 20)
emu.wait(emu.attotime.from_msec(500))

sweep("idle_after_file_loaded", nil)
sweep("rec_src", function() press_button(0x20, 300) end)

print(string.format("BRS_SUMMARY total_task_writes=%u", task_ptr_writes))
manager.machine:exit()
