-- Calibration only for sync-exception-handler-probe.lua's technique
-- (docs/asr10/investigations/mc68302-consolidation-2.md Del 1). Run only
-- against the temporarily-reintroduced naive IRQ1 wiring (ready-line fix
-- commented out) -- the known case that produces ERROR 129, a genuine
-- vector-3 Address Error, BEFORE FILE 1 is ever reached (the crash is in
-- the OS's own polled boot-time disk load, pre-FILE1 -- this is why the
-- main probe script's FILE-1 precondition cannot be reused for
-- calibration; a fixed short wait is used instead).
--
-- Success criterion: the read tap on vector 3's own handler address
-- fires at least once before the run ends.

local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}

local function now() return emu.time() end

local hits = 0
local first_t = nil

emu.wait(emu.attotime.from_msec(500))
-- Mask to the CPU's real 24-bit address bus -- MAME's own error caught
-- this: "start address is outside of the global address mask ffffff,
-- did you mean f882ae?" when a first draft used the raw 32-bit read_u32
-- result unmasked (the vector table's top byte is not always zero).
local handler_addr = prog:read_u32(3 * 4) & 0x00ffffff
print(string.format("CALIB_HANDLER vector=3 addr=%08X t=%.6f", handler_addr, now()))

taps[#taps + 1] = prog:install_read_tap(handler_addr, handler_addr + 1, "calib_vec3", function(offset, data, mask)
  hits = hits + 1
  if not first_t then first_t = now() end
  return nil
end)

local deadline = now() + 15
while now() < deadline do
  emu.wait(emu.attotime.from_msec(200))
end

print(string.format("CALIB_RESULT hits=%u first_t=%s", hits, first_t and string.format("%.6f", first_t) or "never"))
if hits > 0 then
  print("CALIB_VERDICT technique_confirmed")
else
  print("CALIB_VERDICT technique_failed_or_handler_address_wrong")
end
manager.machine:exit()
