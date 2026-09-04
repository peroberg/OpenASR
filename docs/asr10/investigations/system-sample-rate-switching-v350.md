# System sample-rate switching, V3.50

## Result

No reproducible V3.50 `30 kHz <-> 44.1 kHz` firmware transition was reached.
Consequently there is no verified producer-to-hardware control chain and no
rate implementation in this change. This is a bounded negative result, not a
claim that physical ASR-10 hardware lacks a switch.

## Physical clock inventory and current model

The documented board oscillators are Y1 = 16 MHz, Y2 = 30.47618 MHz and Y3 =
33.8688 MHz. Their exact ASR-10 pin-level distribution remains `[OPEN]`.

Current MAME topology is:

| Consumer | Current input | Status |
|---|---:|---|
| MC68302 | 16 MHz | model configuration |
| ES5506 | undivided Y2, 30.47618 MHz | effective emulation value; pitch-calibrated, physical distribution `[OPEN]` |
| ES5510 | 10 MHz, disabled | family/databook-supported placeholder; ASR-10 board source `[OPEN]` |
| PB3 LRCLK input | synthetic 44.1 kHz | driver stimulus only; not evidence of a board rate selector |

The generic ES5506 model is runtime-mutable: a write to `ACTV` changes
`sample_rate = input_clock / (16 * (ACTV + 1))`. The relevant generic register
is `ACTV` (`$58` in the ES5506 32-bit host-register model); MAME loops voices
zero through `ACTV` inclusive. `MODE` controls serial/address mode, not a
clock divisor.

For the commonly quoted two ASR-10 rates, the arithmetic exposes what a real
board path would have to establish, but does not identify it:

| Target | One matching generic ES5506 state |
|---|---|
| 29,761.9 Hz | 15.23809 MHz input, `ACTV=31` |
| 44,100 Hz | 16.9344 MHz input, `ACTV=23` |

This is not a proposed implementation. It demonstrates that a 30/44 switch
cannot be represented by only the current fixed input clock or only the
observed `$1F` value.

## Firmware representation found

`$F8CF06-$F8D00E` (`es5506_boot_init`, existing static/runtime evidence) writes
`ACT=$1F` and `MODE=$0D` during boot, plus the 32-voice initialization. Existing
traces and the source review agree that these writes occur once at startup and
were not seen again in the prior boot/load/select/play measurements.

The effect upload producer is separately identified: firmware transfers ESP
objects through `$FC3000-$FC31FF`, including the real ES5510 host select/commit
paths. The upload/verify producer at `$F973F0-$F97776` is a real effect-object
path, but no static evidence in the existing effect-object work assigns a
30/44-rate field to its records. Such an assignment remains `[OPEN]`.

## Targeted runtime observation

The temporary `system_rate_switch_probe.lua` retained its write tap and used
display changes as a live firmware witness:

| Phase | Firmware-visible result | `$FC2000-$FC7FFF` writes after phase |
|---|---|---:|
| Effects category `$09` | `FILE 16  LUSH PLATE` | 0 |
| Enter `$23` on that file | `DISK COMMAND COMPLETED` | 0 |

The tap covered ES5506 (`$FC2000`), ES5510 (`$FC3000`), intervening board
windows and MC68302 internal state. The display results prove that the injected
panel path and wait window were alive. They do **not** prove that an effect
algorithm was installed: this UI path reached a file-browser/disk completion,
not a verified successful effect download. Therefore the absence of a rate
write applies only to this tested transition.

No reproducible A -> B -> A pair of firmware states with independently known
30 kHz and 44.1 kHz identities is currently available in this model. The
requested differential table cannot honestly be populated beyond the static
boot state.

## Hardware roles and emulator consequence

- **ES5701:** `[Verified silicon capability]` it provides OTIS/ESP bus and
  clock-generation glue; `[OPEN]` whether the ASR-10 instance selects Y2/Y3,
  divides either source, or carries the system-rate control.
- **ES5510:** `[Verified firmware]` receives host uploads; `[OPEN]` whether its
  runtime clock is coupled to the ES5506/system-rate choice. It remains
  disabled and has no audio route in the current model.
- **ES5506:** `[Verified device]` provides the generic `ACTV` mechanism. Current
  ASR-10 configuration supplies a fixed 30.47618 MHz input and the boot's
  fixed `$1F`; it consequently cannot model a firmware-driven system-rate
  transition even if the physical board has one.

The existing dry-note calibration makes the present fixed configuration sound
approximately correct for its tested note. That does not verify Tutorial Song
playback or establish a rate mismatch as its cause. A rate mismatch **can
explain** global pitch and sample-duration errors, but this investigation does
not explain the observed instrument association or establish it as the
Tutorial Song cause.

## Required next discriminator

Before code changes, obtain two successful algorithm states with an oracle for
their real rate class, then capture A -> B -> A while tapping the same board
windows and the ES5506 `ACTV` write sequence. A reversible board-facing write,
or a verified clock-control input, is required before placing any policy at the
ASR board boundary. Effect-name special cases and clock fudge factors are
explicitly ruled out.
