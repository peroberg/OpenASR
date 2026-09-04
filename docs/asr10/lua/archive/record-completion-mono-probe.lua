-- Phase 5A: trace the accepted LEFT recording through stop, sample metadata,
-- root-key selection, and the first direct ES5506 reads.

_G.SCC_RX_RECORD_CONFIG = {
  name = "record_completion_mono",
  source_steps = 0,
  expected_mode = 0,
  finish_sample = true,
  trace_completion = true,
  feeds = {
    { channel = 1, pattern = "counter_trigger" },
    { channel = 1, pattern = "aa55" },
    { channel = 1, pattern = "signed_triplet" },
  },
}

dofile("docs/asr10/lua/lib/scc_rx_record_probe.lua")
