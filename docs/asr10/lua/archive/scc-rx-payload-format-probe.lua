-- Phase 4B mono payload run: threshold-bearing counter followed by two full
-- descriptors with alternating and signed-boundary word patterns.

_G.SCC_RX_RECORD_CONFIG = {
  name = "scc_rx_payload_format",
  source_steps = 0,
  expected_mode = 0,
  finish_sample = true,
  feeds = {
    { channel = 1, pattern = "counter_trigger" },
    { channel = 1, pattern = "aa55" },
    { channel = 1, pattern = "signed_triplet" },
  },
}

dofile("docs/asr10/lua/lib/scc_rx_record_probe.lua")
