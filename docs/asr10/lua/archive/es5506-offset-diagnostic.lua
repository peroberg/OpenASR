-- Throwaway diagnostic: dump raw (offset,data,mask) for the first N
-- ES5506 host writes to determine MAME's actual offset convention for
-- write(offs_t offset, u8 data) wired via .rw(...).umask16(0x00ff) over a
-- word-addressed range, before trusting any register decode built on an
-- assumption about it.

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local taps = {}
local count = 0
local LIMIT = 60

taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "diag_w", function(offset, data, mask)
  if count < LIMIT then
    count = count + 1
    print(string.format("DIAG t=%.6f offset=%08X data=%08X mask=%08X", emu.time(), offset, data, mask))
  end
  return nil
end)

emu.wait(emu.attotime.from_msec(20))
print(string.format("DIAG_DONE count=%u", count))
reg.pass("es5506_offset_diagnostic", string.format("count=%u", count))
