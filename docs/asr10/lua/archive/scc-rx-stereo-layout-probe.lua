-- Phase 4B stereo allocation run: select source mode 2 through panel input,
-- then distinguish SCC1 and SCC2 with constant AA and 55 payloads.

_G.SCC_RX_RECORD_CONFIG = {
  name = "scc_rx_stereo_layout",
  source_steps = 2,
  expected_mode = 2,
  expected_instrument_error = true,
  feeds = {},
}

dofile("docs/asr10/lua/lib/scc_rx_record_probe.lua")
