-- SS8.6-proof tap helpers. A raw install_read_tap()/install_write_tap()
-- call whose return value isn't kept in a variable reachable for the
-- rest of the script gets garbage-collected almost immediately, and the
-- tap dies silently -- no error, just no further callbacks. This has
-- now bitten this project's own scripts twice (see
-- investigations/tempo-clock-consumer-chain.md's "relapse" section).
-- A rule that fails twice is a tooling problem, not a discipline
-- problem: these wrappers hold every handle in a module-level upvalue,
-- so a script cannot forget to persist it -- the only thing a caller
-- must do is keep holding the module table returned by dofile(), which
-- it already needs to call these functions at all.

-- SS8.10: a hit on an instruction's read-tap is not execution. A CPU can
-- prefetch the word after an adjacent unconditional branch (bra/jmp)
-- without ever decoding it -- see $782A, which fired at a steady 83Hz
-- purely from prefetch while its own instruction's extension word
-- ($782C) fired only 0/1 times per note. PC-correlation catches the
-- common case (data reads from unrelated code never show PC == tapped
-- address) but is NOT independently sufficient for this specific
-- adjacent-branch pattern: $782A showed PC == $782A even on the
-- prefetch-only hits. The only way to tell them apart is to also check
-- that the full instruction -- including any extension words -- actually
-- completed, which pc_correlated_read_tap cannot do generically because
-- that requires knowing the instruction's own encoding. Callers with a
-- multi-word instruction at the tapped address must add that check
-- themselves; pc_matches alone is necessary, not sufficient, there.

local M = {}
local _taps = {}

function M.read_tap(space, start, stop, name, callback)
  local handle = space:install_read_tap(start, stop, name, callback)
  _taps[#_taps + 1] = handle
  return handle
end

function M.write_tap(space, start, stop, name, callback)
  local handle = space:install_write_tap(start, stop, name, callback)
  _taps[#_taps + 1] = handle
  return handle
end

function M.tap_count()
  return #_taps
end

-- Current PC, masked to the CPU's 24-bit address bus. Tries the state
-- names known to exist in this build's register namespace in order;
-- "A7" does not exist here (use "SP") and neither does a bare "PC" on
-- every core, hence the fallback chain.
function M.pc(cpu)
  cpu = cpu or manager.machine.devices[":maincpu"]
  local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
  return pc_state and (pc_state.value & 0x00ffffff) or nil
end

-- Read tap whose callback also receives the CPU's PC at the instant of
-- the hit, and a pre-computed pc_matches boolean (PC == start or
-- PC == start - 1, to tolerate odd/even word-alignment offsets seen in
-- prior probes). This makes PC-correlation the default, easy path
-- instead of something each script has to hand-roll -- see SS8.10 above
-- for why pc_matches alone is still not sufficient for adjacent-branch
-- prefetch on multi-word instructions.
--
-- callback signature: function(offset, data, mask, pc, pc_matches)
function M.pc_correlated_read_tap(space, start, stop, name, callback, cpu)
  return M.read_tap(space, start, stop, name, function(offset, data, mask)
    local current_pc = M.pc(cpu)
    local matched = current_pc ~= nil and (current_pc == start or current_pc == (start - 1))
    return callback(offset, data, mask, current_pc, matched)
  end)
end

return M
