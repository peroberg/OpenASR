-- Del 2/3 follow-up to memory-size-alias-fix.md: now that stereo RECORD
-- reaches WAITING instead of Error 57, verify round-trip data integrity
-- with two genuinely different channel patterns -- not the same content
-- in both channels, which could pass even if L/R got crossed or merged.
--
-- LEFT (SCC1) = counter_trigger: a 0-255 ramp, carries the $7FFF-style
--   trigger bytes at offset $40/$41 that cross the threshold-scan
--   ($FFD54A) firmware already uses to leave WAITING.
-- RIGHT (SCC2) = aa_trigger: constant $AA, same trigger bytes -- clearly
--   distinct from the ramp at the byte level and as a waveform.
--
-- Reuses the existing, already-verified shared runner
-- (scc_rx_record_probe.lua, unmodified) that mono's own reference test
-- uses -- same technique, same byte-for-byte verification, different
-- content per channel instead of the same content in both.

_G.SCC_RX_RECORD_CONFIG = {
  name = "stereo_known_pattern",
  source_steps = 2,           -- cycle Sample-Source Select to L+R (mode 2)
  expected_mode = 2,
  expected_instrument_error = false,  -- was true before the memory-size fix
  finish_sample = true,
  trace_completion = false,
  feeds = {
    { channel = 1, pattern = "counter_trigger" },  -- LEFT
    { channel = 2, pattern = "aa_trigger" },        -- RIGHT
  },
}

dofile("docs/asr10/lua/lib/scc_rx_record_probe.lua")
