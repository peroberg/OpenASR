# ES5506 -> ES5510 pump comparison — exact-chip references

> **Superseded as the current implementation boundary.** This source comparison
> selected the generic mechanism. The bounded ASR functional adapter landed
> later in `7bfd2f3722d`; it does not upgrade the physical routing/HALT claim.
> See `es5510-upload-executable-contract-v350.md` and `../HANDOFF.md`.

## Question

What is the smallest reusable MAME mechanism for the missing ASR-10 audio
path:

```text
ES5506 serial outputs -> ES5510 processing -> stereo output
```

This is a source comparison only.  It does not claim ASR board wiring, enable
ESP execution in ASR, select an ES5506 clock, or alter production code.

## Result

**KT-76/KT-88 and TS are the closest MAME configuration references, not
VFX/SD-1.**  `src/mame/ensoniq/esqkt.cpp` instantiates the exact chip pair:
one (TS) or two (KT) `ES5506`, plus `ES5510`, plus the shared
`ESQ_5505_5510_PUMP` device.  The generic pump is explicitly written for
both ES5505 and ES5506 in its device type and source heading.

VFX/SD-1 remains valuable for a different, narrower reason: it is the
reference that actually drives the pump's ESP halt gate from a firmware-visible
output.  It uses the predecessor ES5505, not ES5506.

Neither precedent establishes ASR's physical serial wiring, output-pair count,
or HALT source.  `MODEL CHANGE JUSTIFIED: NO`.

## Established ASR boundary

The current ASR harness has a real ES5510 host adapter and V3.50 completes its
program upload/readback through `$FC3000-$FC31FF`.  The generic device therefore
contains the firmware-downloaded GPR/instruction state.  It is constructed with
`set_disable()` and no pump is instantiated; ES5506 is routed directly to the
speakers.  Thus host upload is `[Verified]`, while audio execution and the
ES5506 -> ES5510 serial boundary remain `[OPEN]`.

The previously completed rate-control census found no reversible ASR SIB PIO,
CS1, unresolved CS2 or CS3 write beyond known ACTV/pitch/effect traffic.  This
comparison does not reopen that negative result or infer a clock control from
any pump configuration.

## The reusable generic pump contract

`src/devices/sound/esqpump.cpp` creates a synchronous stream with eight inputs
and four outputs.  Its fixed MAME-side mapping is:

| Pump inputs | Generic action | ESP serial ports |
|---|---|---|
| 0/1 | Auxiliary stereo bypass | outputs 2/3 directly |
| 2/3 | first processed stereo pair | `SER0L/R` |
| 4/5 | second processed stereo pair | `SER1L/R` |
| 6/7 | dry stereo pair | `SER2L/R` |
| — | processed main stereo | reads `SER3L/R` from ports 6/7 into outputs 0/1 |

When its private `m_esp_halted` flag is clear, it calls
`es5510_device::run_once()` once per pump sample.  `run_once()` deliberately
deasserts the ESP HALT input, runs a microprogram to its halt/end state, then
reasserts HALT.  This call is independent of `set_disable()`, which only keeps
the ESP out of MAME's ordinary CPU scheduler.

The pump starts halted.  Inserting routes alone therefore does not execute an
effect program.

This is `[Verified MAME code]`, not a reconstruction of physical BCLK/WCLK/
LRCLK nets.

## Reference systems

| Reference | Generator | ESP | Pump routing / clock relationship | HALT behaviour | Reuse value for ASR |
|---|---|---|---|---|---|
| VFX / VFX-SD / SD-1 | ES5505 | ES5510 | all eight ES5505 stream outputs route 1:1 to pump inputs 0..7; generator rate callback updates pump rate | DUART output bit 6 (`ESPHALT`) calls `set_esp_halted()` | HALT-control precedent only; chip generation differs |
| KT-76/KT-88 | ES5506 (two) | ES5510 | first ES5506's outputs 0..7 route 1:1; `sample_rate_changed()` updates pump rate | no `set_esp_halted(false)` call in this driver | closest exact-chip wiring/configuration shape |
| TS | ES5506 (one) | ES5510 | outputs 0..7 route 1:1; same sample-rate callback | no `set_esp_halted(false)` call | closest one-OTTO configuration shape |
| old `esqasr.cpp` skeleton | ES5506 | ES5510 | same one-OTTO, outputs-0..7 pattern as TS/KT | no HALT release | historical configuration precedent only |
| current `asr10_boot.cpp` | ES5506 | ES5510 host adapter | direct outputs 0/1 to speaker; no pump | no pump | identifies the missing implementation boundary |

The source search finds pump HALT release only in the ES5505 VFX-family path
and Taito's ES5505 board model.  Taito documents that signal from a Gun Buster
schematic; it is not ASR evidence.  KT/TS prove that the generic pump accepts
ES5506 stream outputs, but their configured pumps remain halted and therefore
are not proof of a working ES5510-execution path.

## Why ES5505 versus ES5506 matters

The two chips share the generic MAME active-slot sample-rate formula, but they
are not interchangeable.  ES5505 has two sample-memory banks and at most four
stereo stream pairs; ES5506 has four banks and the device implementation
supports up to six stereo pairs.  The generic pump has only four stereo input
pairs.

That does **not** show that ASR needs twelve inputs: KT/TS and the historical
ASR skeleton explicitly configure ES5506 with four pairs and route outputs
0..7.  It does show that any ASR reuse must establish or deliberately scope the
actual ASR output-pair routing before assuming the generic pump covers every
OTTO output.  The current ASR driver leaves ES5506 at its default one pair,
which is only sufficient for its current dry-speaker witness.

## Practical conclusion

The shortest non-speculative implementation direction is to **reuse**
`ESQ_5505_5510_PUMP`, not invent an ASR-specific audio mixer or rewrite
ES5510.  KT/TS is the code template for connecting an ES5506 to it; VFX/SD-1
is the code template for treating ESP execution as a separate HALT-controlled
state.

Before such a wiring change can be called ASR-correct, two narrowly defined
questions remain:

1. Which four (or other subset of) ASR ES5506 serial output pairs feed the ESP
   and which, if any, are Aux bypass?
2. What releases or frame-synchronizes the ASR ESP HALT path after program
   load?

Neither question requires solving the 30/44.1-kHz clock source.  Conversely,
adding the pump cannot solve that rate problem: its own sample rate only
resamples the stream after ES5506 has generated it; it does not alter ES5506
FC-to-pitch timing.

## Evidence status

| Claim | Status | Evidence domain |
|---|---|---|
| Generic pump supports ES5506 as well as ES5505 stream inputs. | `[Verified]` | MAME device type/source and KT/TS configs |
| KT/TS are exact ES5506 + ES5510 + pump MAME references. | `[Verified]` | MAME source |
| VFX/SD-1 use predecessor ES5505, not ES5506. | `[Verified]` | MAME source |
| VFX DUART bit 6 controls the MAME pump HALT gate. | `[Verified]` | MAME source |
| ASR uses the same HALT signal or routing. | `[OPEN]` | ASR board/firmware |
| Generic pump solves ASR's physical 30/44.1-kHz clock selection. | `[DISPROVEN]` | device architecture; it is downstream of generator rate |

## Single next step

Make a small, reversible **pump-integration spike** based on the KT/TS
one-ES5506 configuration, while preserving the current ES5506 clock and all
rate policy.  Its acceptance question is limited to whether the already
verified ASR ES5510 program can execute and return non-zero serial output with
an explicitly documented temporary HALT policy.  Do not claim that policy is
physical ASR wiring; if the spike cannot be made without inventing HALT or
output-pair semantics, stop and retain this comparison as the implementation
boundary.
