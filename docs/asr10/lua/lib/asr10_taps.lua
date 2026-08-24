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

return M
