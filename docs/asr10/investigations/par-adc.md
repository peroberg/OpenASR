# ASR-10 PAR / ADC investigation

Consolidates the former PAR/ADC documents. Current address and routine facts
live in `../reference/subroutine-index.md`.

## Verified

- PAR is the ES5506 host read-port value used by firmware as a 10-bit analog
  measurement.
- The meaningful PAR bytes are in the low half of the 32-bit latch as observed
  through MOVEP at `$FC206D/$FC206F`. `$FC2069` is the top byte and is
  expected to be zero.
- The `$006864` loop performs eight PAR reads and accumulates a sum in D2.
- `$006800` faults only when D2 is zero. The failing exception was divide by
  zero vector 5, formatted as ERROR 130.
- `$0DD6` stores the channel-7 raw sum; it is not the CPU divisor operand.
  The divisor operand at `$006800` is register D2.
- MC68302 PBDAT bit 2:0 carries the firmware's analog channel selector.
  Firmware uses at least channels 0, 2, 3, 4, 5 and 7; the boot-critical
  calibration path uses 7, 5, 0. Runtime now pairs these writes with PAR reads,
  but physical U55 pin routing remains unverified.
- The current driver does not follow that selector. Its PAR callback indexes
  board-default values with `m_duart_io & 7`; this is boot plumbing, not a
  physical ADC model and not the firmware-selected channel path.
- V3.50 continuously scans 0,2,5,3,4 (plus periodic 7) at 500 aggregate PAR
  reads/s in both idle and the analog diagnostic. The diagnostic only selects
  a RAM-table entry for display. See `analog-control-acquisition-v350.md`.

## Reproduction history

- With unbound PAR, channel 7 reads as zero, D2 becomes zero, and `$006800`
  raises ERROR 130.
- Lua `answer` mode proved that correctly channelised synthetic PAR was
  sufficient to pass ERROR 130 with the ES5510 path enabled.
- Later C++ cleanup replaced the gated fixed-value workaround with
  `analog_r()` plus per-index defaults; the boot now reaches `FILE 1 TUTORIAL
  BNK` without `ASR10_EXPERIMENT_PAR_DIAGNOSTIC` or `ASR10_DIAG_PAR_VALUE`.

## Disproved

- DUART OPR selects PAR channel. It does not; PBDAT bit 2:0 does.
- `$FC2069 == 0` proves PAR is zero. It does not; that byte is structurally
  expected to be zero.
- `$0DD6` is the divisor instruction's operand. It is a stored measurement;
  D2 is the live divisor.
- The scheduler/slot-5 idle loop was the cause of ERROR 130. It was normal
  background polling; ERROR 130 came from the PAR/D2 path.

## Open

- The current default-derived `$200` PAR value is not measured from hardware.
- Firmware/diagnostic identities are now mapped: 0=PITCHWHL, 2=MODWHEEL,
  4=PEDAL, 3=VOLUME, 5=MR. KNOB and 7=REFRENCE. Their physical connector/U55
  input-pin routing remains unknown.
- `$FC6000-$FC6FFF` accesses were invisible to Lua taps in one observe run
  despite the channel-select path executing; this remains a low-priority
  tap-layer mystery, not hardware evidence.
- A real ADC/front-end model still needs board evidence.
