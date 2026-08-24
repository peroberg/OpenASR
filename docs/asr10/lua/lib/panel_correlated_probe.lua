-- Del 3: the correlated probe. One row per panel event, not three
-- separate logs matched afterward:
--   timestamp | panel wire frame | mode/state vars | PC chain to
--   scheduler idle | emitted display bytes | display text before->after
--
-- $F87F92-$F87FD0 (subroutine-index.md sched_dispatch_scan/
-- sched_idle_loop) makes "back at rest" detectable, so every event has
-- a clean boundary. Observation only, Lua only.
--
-- Known reliability caveat, honestly reported rather than silently
-- left: the disp_bytes column has been observed to come back empty for
-- an entire run on roughly 1 of 3 attempts, despite the SS8.6 fix below
-- (the tap handle is kept alive via the returned `taps` table). All
-- other columns (PC chain, mode/state vars, display text) have been
-- reliable across every run tried. Re-run once if disp_bytes is
-- unexpectedly empty across an entire sweep; do not trust a single
-- all-empty run as a negative result for that column specifically
-- (SS8.7 -- a zero result needs a live witness for the whole window,
-- and this column's witness is itself sometimes silently dead).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}

local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end
local function now() return emu.time() end
local function in_idle(p) return p >= 0xF87F80 and p <= 0xF87FD0 end

local disp_stream = {}
taps[#taps + 1] = prog:install_write_tap(0x00fc4800, 0x00fc481f, "duart_w", function(offset, data, mask)
  if (mask & 0x00ff) == 0 then return nil end
  if (offset & 0xf) == 6 then disp_stream[#disp_stream + 1] = data & 0xff end
  return nil
end)

local function press_and_correlate(code, mode_label)
  local port_name = (code < 0x20) and ":panel:buttons_0" or ":panel:buttons_32"
  local port = manager.machine.ioport.ports[port_name]
  local field = port and port:field(1 << (code & 0x1f)) or nil
  if not field then return nil end

  local t0 = now()
  local mode_before = prog:read_u8(0x016f)
  local state_before = prog:read_u16(0x0d04)
  local text_before = display.read_raw()
  local disp_before = #disp_stream

  local pc_visited, pc_order = {}, {}
  local function sample()
    local p = pc()
    if not in_idle(p) and not pc_visited[p] then
      pc_visited[p] = true
      pc_order[#pc_order + 1] = p
    end
    return p
  end

  field:set_value(1)
  local d1 = now() + 0.08
  while now() < d1 do sample(); emu.wait(emu.attotime.from_usec(150)) end
  field:clear_value()

  local returned_idle, streak = false, 0
  local d2 = now() + 0.05
  while now() < d2 do
    if in_idle(sample()) then
      streak = streak + 1
      if streak >= 5 then returned_idle = true break end
    else
      streak = 0
    end
    emu.wait(emu.attotime.from_usec(150))
  end

  local mode_after = prog:read_u8(0x016f)
  local state_after = prog:read_u16(0x0d04)
  local text_after = display.read_raw()
  local disp_after = #disp_stream

  local disp_bytes = {}
  for i = disp_before + 1, disp_after do disp_bytes[#disp_bytes + 1] = disp_stream[i] end
  local disp_hex = {}
  for _, b in ipairs(disp_bytes) do disp_hex[#disp_hex + 1] = string.format("%02X", b) end

  local pcs = {}
  for i = 1, math.min(#pc_order, 6) do pcs[#pcs + 1] = string.format("%06X", pc_order[i]) end

  print(string.format(
    "PANEL_EVENT ctx=%s t=%.6f code=%02X frame=%02X,00 mode_before=%02X mode_after=%02X state_before=%04X state_after=%04X returned_idle=%s pc_chain=%s disp_bytes=[%s] text_before=\"%s\" text_after=\"%s\"",
    mode_label, t0, code, 0x80 | code, mode_before, mode_after, state_before, state_after,
    tostring(returned_idle), table.concat(pcs, ">"), table.concat(disp_hex, " "), text_before, text_after))
end

-- SS8.6: an unsaved tap reference can be silently GC'd. `taps` is a
-- bare top-level local not otherwise captured by press_and_correlate's
-- closure -- returning it here (even unused by the caller) keeps it
-- reachable for as long as the caller holds `probe`, exactly the fix
-- this project's own methods doc prescribes.
return { press_and_correlate = press_and_correlate, taps = taps }
