# ASR-10 DUART investigation

Consolidates the former DUART/tick documents. Current reference facts that are
no longer investigative live in `../reference/subroutine-index.md`.

## Verified

- The ASR-10 DUART path now uses MAME's `mc68681_device`; the old handwritten
  SCN2681 shadow, skugg-ISR and channel-B RX byte shadow are gone.
- The DUART interrupt reaches CPU IRQ6 through the real device `irq_cb()`.
  MC68302 interrupt acknowledge returns external vector `$56`, which enters
  the firmware IRQ6 handler at `$F88300`.
- Firmware dispatch at `$F884BE` reads the real DUART ISR:
  bit 5 = channel B RxRDY, bits 2|1 = channel A, bit 0 = channel A TxRDY,
  bit 3 = counter ready. If none match, firmware raises ERROR 145.
- DUART counter/timer tick is 1.000 ms in the verified boot path. With
  `CTUR:CTLR = $07D0`, this implies effective DUART clock 4.000 MHz.
- Firmware does not need an explicit Start Counter Command read from
  `$FC481D` in the path that now boots. The previously suspected MAME defect
  was not a missing implicit timer start.
- `mc68681_device` already starts the counter on the relevant ACR transition;
  AN414/implicit-start speculation is closed.
- Channel B is the panel channel. Panel RX bytes are injected into the real
  channel-B receive FIFO, so RxRDYB/ISR/IMR/IRQ ownership is in the DUART.
- Channel A is the MIDI-side channel. Its physical RX source is still not
  wired.

## OPR

- The table-driven OPR writer at `$F8E1DA` uses table bytes
  `$2E,$57,$36,$00`; physical output is the inverse of the internal OPR latch.
- OPR was observed constant around the PAR loop and does not select the ADC
  channel in the verified path.
- Firmware's analog channel selection is MC68302 PBDAT bit 2:0, not DUART
  OPR. U55 as the physical mux is `[Likely]`; pin routing is `[OPEN]`.

## SRA bit 7

- ROM routine `$F8845A` reads SRA and tests bit 7 (Received Break). If set, it
  performs the expected break/null recovery path.
- The first bad `mc68681_device` regression showed SRA value differences, but
  forcing bit 7 high on the bad revision did not restore `TUTORIAL BNK`.
- SRA bit 7 is therefore an observed difference, not the root cause of the
  browser regression.

## Regression history

- Good revision `80ee114be1a` reached `FILE 1 TUTORIAL BNK`.
- Bad revision `7bc57b8ab45` replaced the handwritten SCN2681 path with
  `mc68681_device` but lacked a complete IRQ6 path for the boot chain.
- The later IRQ6 wiring fixed that regression class.
- Current HEAD boots flaglessly to `FILE 1 TUTORIAL BNK`.

## Open

- Channel A RX must be connected to the correct MIDI-side source.
- Exact external circuitry that holds or releases MIDI IN break/null state is
  not modelled.
- DUART/panel timing may still need refinement once real panel input is added.
