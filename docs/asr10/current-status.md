# ASR-10 current status

Current truth for the ASR-10 MAME bring-up. This file is deliberately short:
verified reference facts belong in `reference/`, and experiment history belongs
in `investigations/` or `archive/`.

## Works

- `asr10booth` boots `floppies/asr10booth/V350.img` with no `ASR10_*`
  environment variables to:

  ```text
  ENSONIQ ASR-10 -> LOADING SYSTEM -> FILE 1  TUTORIAL BNK
  ```

- The acceptance test is `docs/asr10/regression-test.sh`. It runs `./mess
  asr10booth -flop1 floppies/asr10booth/V350.img` and passes only when
  `TUTORIAL BNK` appears in `error.log`.
- Tag `asr10-file1-2026-08-03` marks the first documented `FILE 1
  TUTORIAL BNK` milestone. That tag records the historical flag set that was
  required at the time; those flags have since been removed from the driver.
- Current boot uses real MAME devices for the DUART host path (`mc68681`),
  ES5506 host registers, ES5510 host registers, and the FDC path used by this
  boot. MC68302 modelling is split between the new device files and remaining
  ASR-10 driver glue.
- Channel B panel RX is owned by `mc68681_device`. Removed code includes
  `m_panel_c_srb`, `m_panel_c_rx_byte`, `m_panel_c_rx_valid`, `m_panel_c_isr`,
  `m_panel_c_imr`, `panel_c_update_irq6`, and the hand-rolled channel-B RX
  path.

## Does not work

- Button navigation is not implemented far enough to prove `FILE 2` or normal
  file-browser interaction.
- Audio output, sampling, sequencer behavior, and complete ES5506/ES5510 sound
  integration are not working end-to-end.
- DUART channel A RX is not wired to a real external source. It is MIDI-side
  behavior and must not be inferred from the panel channel-B path.
- The fixed PAR value is plumbing only. It is not a measured analog value and
  not a real ADC model.

## Next blocker

Implement the smallest real input path that moves the file browser beyond
`FILE 1  TUTORIAL BNK` without reintroducing stubs:

1. Identify the host-visible panel input path needed for browser navigation.
2. Route that input through real DUART/channel or panel-device plumbing.
3. Extend `docs/asr10/regression-test.sh` only after a new visible milestone is
   reproduced.

Do not restart scheduler, DUART timer, PAR-channel, or SRA-break investigations
unless a new failing run contradicts the facts below.

## Open questions

- PAR value: the current value is selected to let the boot proceed, not
  measured from hardware.
- ADC channel identity: firmware channel numbers are verified, but the
  physical source behind each channel is unknown.
- PB3 LRCLK: the driver supplies a plausible LRCLK observation, but the exact
  board frequency at PB3 is unmeasured.
- Lua passthrough taps: `$0067EC` executes and writes `$0DD6`, but Lua taps saw
  no `$FC6000-$FC6FFF` accesses while `$FC20xx`, `$FC40xx`, and `$FC48xx` were
  visible. Treat this as a Lua/tap-layer question, not hardware evidence.
- Channel A: the physical MIDI input and break/null handling need real wiring.

## Disproved hypotheses

- DUART OPR selects ADC/PAR channel. Disproved by OPR/PAR captures and the
  verified channel-select code: MC68302 PBDAT bit 2:0 selects channels.
- SRA bit 7 caused the browser regression. Forcing Received Break high on the
  bad DUART revision did not produce `TUTORIAL BNK`; the real first regression
  was missing DUART `irq_cb` wiring.
- AN414-style implicit DUART timer start was a MAME defect. `mc68681_device`
  already starts the counter on the relevant ACR transition; the missing
  behavior was IRQ wiring, not timer start.
- `$0DD6` is the divisor at `$006800`. The divisor is register D2. `$0DD6`
  stores the measured channel-7 sum that feeds later calculations.
- PAR is read at `$FC2069`. That byte is structurally the top byte of the
  32-bit latch and is expected to be zero. The meaningful PAR bytes are
  visible at `$FC206D`/`$FC206F`; see `reference/subroutine-index.md`.
- `filesystem-browser-map.md` sections 4.19 and 4.28 described the final
  current state. They were historical. Reproduction on `da1b4c38525` reached
  `FILE 1  TUTORIAL BNK`, and current HEAD also reaches it flaglessly.

## Current documents

- `reference/subroutine-index.md`: verified address/routine facts.
- `investigations/duart.md`: DUART/tick/IRQ/OPR investigation history.
- `investigations/par-adc.md`: PAR/ADC/channel-select investigation history.
- `filesystem-browser-map.md`: filesystem/browser research notes; historical
  claims are marked as such where they differ from current boot behavior.
