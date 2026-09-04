-- Phase 4B right-channel control: source mode 1 gates SCC1 off and accepts
-- SCC2. The payload distinguishes this path from the SCC1 LEFT run.

_G.SCC_RX_RECORD_CONFIG = {
  name = "scc2_rx_payload_format",
  source_steps = 1,
  expected_mode = 1,
  feeds = {
    { channel = 2, pattern = "aa_trigger" },
  },
}

dofile("docs/asr10/lua/lib/scc_rx_record_probe.lua")
