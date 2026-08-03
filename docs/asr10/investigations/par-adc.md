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
- MC68302 PBDAT bit 2:0 selects ADC channel. Firmware uses at least channels
  0, 2, 3, 4, 5 and 7; the boot-critical calibration path uses 7, 5, 0.
- The current driver binds a fixed PAR value so V350 proceeds to the file
  browser. That is plumbing, not a physical ADC model.

## Reproduction history

- With unbound PAR, channel 7 reads as zero, D2 becomes zero, and `$006800`
  raises ERROR 130.
- Lua `answer` mode proved that correctly channelised synthetic PAR was
  sufficient to pass ERROR 130 with the ES5510 path enabled.
- Later C++ cleanup made the fixed PAR path unconditional; the boot now reaches
  `FILE 1 TUTORIAL BNK` without `ASR10_EXPERIMENT_PAR_DIAGNOSTIC` or
  `ASR10_DIAG_PAR_VALUE`.

## Disproved

- DUART OPR selects PAR channel. It does not; PBDAT bit 2:0 does.
- `$FC2069 == 0` proves PAR is zero. It does not; that byte is structurally
  expected to be zero.
- `$0DD6` is the divisor instruction's operand. It is a stored measurement;
  D2 is the live divisor.
- The scheduler/slot-5 idle loop was the cause of ERROR 130. It was normal
  background polling; ERROR 130 came from the PAR/D2 path.

## Open

- The fixed PAR value is not measured from hardware.
- Physical identity of each ADC channel is unknown.
- `$FC6000-$FC6FFF` accesses were invisible to Lua taps in one observe run
  despite the channel-select path executing; this remains a low-priority
  tap-layer mystery, not hardware evidence.
- A real ADC/front-end model still needs board evidence.
