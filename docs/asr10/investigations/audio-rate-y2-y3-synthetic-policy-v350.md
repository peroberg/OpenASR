# ASR-10 V3.50: bounded synthetic Y2/Y3 rate-policy spike

## Question

Can the already verified V3.50 mode transaction select two **current-MAME
ES5506 device-domain** rates that preserve the controlled C4 pitch across
ROM-01 HALL (mode 0) and 44LUSH PLATE (mode 1), without treating the already
disproven ACTV write as the clock-select event?

This was a temporary behaviour spike, not a board-clock implementation.  It
does not establish Y2/Y3 wiring, a mux, divider topology, ES5701 involvement,
PB3/LRCLK semantics, or an ASR physical sample rate.

## Established inputs

The prior A/B/A witness fixes the mode contract:

```text
ROM-01 HALL (A) -> $0CE3=00 -> ACTV=$1F -> FC midpoint about 896
44LUSH (B)      -> $0CE3=01 -> ACTV=$17 -> FC midpoint about 604.5
ROM-01 HALL (A2)-> $0CE3=00 -> ACTV=$1F -> FC midpoint about 895.5
```

`$0CE3` is copied from current-effect object `+$66` before the ACTV and voice
configuration branches.  The old temporary policy instead reacted to each
ACTV host write and used half-frequency values.  Its controlled note changed
from 262.3 to 196.7 Hz and is retained as `[DISPROVEN]` in
`audio-rate-model-implementation-v350.md`.

Unmodified current MAME uses Y2 directly for both modes:

| state | clock | ACTV slots | generic ES5506 stream rate |
|---|---:|---:|---:|
| A | 30,476,180 Hz | 32 | 59,523.789 Hz |
| B | 30,476,180 Hz | 24 | 79,365.052 Hz |

The existing note witness was 262.3 / 235.3 / 260.9 Hz (A/B/A2), so this
generic ratio does not preserve a fixed note across the firmware's reversible
FC compensation.

## Temporary policy and prediction

The spike observed the verified low-RAM byte write at `$0CE3` and, only for
the experiment, selected:

| `$0CE3` | temporary ES5506 clock | slots | generic stream rate |
|---:|---:|---:|---:|
| `00` | Y2 = 30,476,180 Hz | 32 | 59,523.789 Hz |
| `01` | Y3 = 33,868,800 Hz | 24 | 88,200 Hz |

This is deliberately a **device-domain synthetic policy**.  The relevant
relative relation is:

```text
stream B/A = 88,200 / 59,523.789 = 1.48176
FC B/A     = 604.5 / 896.0       = 0.674665
product    = 0.9997
```

Thus it predicts that the measured note should remain in the same pitch range.
Unlike the rejected `/2` trial, it preserves the already working mode-0 clock
value and tests only the mode-1 relative rate required by the observed FC
compensation.

## Controlled runtime witness

Each run used the established panel/MIDI sequence:

```text
boot -> load/select JM DIGI SYN
     -> ROM-01 HALL -> MIDI $90 $3C $64       (A)
     -> 44LUSH PLATE -> same MIDI fixture     (B)
     -> ROM-01 HALL -> same MIDI fixture      (A2)
```

The temporary Lua driver printed a live completion witness and mode at every
onset.  Both independent runs reached:

```text
A:  t=25.860000  $0CE3=00
B:  t=31.100000  $0CE3=01
A2: t=35.340000  $0CE3=00
RATE_Y2Y3_DONE live=1
```

`-wavwrite` plus the established autocorrelation checker produced the same
numbers in both runs:

| phase | peak | dominant frequency |
|---|---:|---:|
| A | 3824 | 262.3 Hz |
| B | 3848 | 260.9 Hz |
| A2 | 3819 | 260.9 Hz |

The B/A relation is `0.9947`; the A-to-A2 difference is also in the already
observed small LFO/window range.  B therefore returns to the controlled C4
band rather than the unmodified 235.3-Hz result.

Raw transient evidence is retained outside the repository at:

```text
/private/tmp/asr10-rate-y2-y3-aba.log
/private/tmp/asr10-rate-y2-y3-aba-r2.log
/private/tmp/asr10-rate-y2-y3-aba.wav
/private/tmp/asr10-rate-y2-y3-aba-r2.wav
```

## Result and boundary

| claim | status | reason |
|---|---|---|
| Firmware mode and FC compensation can be paired with a two-rate generic ES5506 policy that preserves the controlled note. | `[Verified current-MAME synthetic spike]` | repeated A/B/A WAV witness |
| ACTV alone is a sufficient clock-policy boundary. | `[DISPROVEN]` | prior `/2` ACTV-post-write experiment |
| The temporary Y2/Y3 values are ASR board-clock nets or a physical selector. | `[OPEN]` | this spike supplies no board routing evidence |
| The documented 29.7619/44.1-kHz labels equal MAME's generic stream rates. | `[OPEN]` | this successful current-MAME policy uses 59,523.789/88,200 Hz |

## Falsification: datasheet-rate half-clock alternative

The OTTO specification gives the physical-chip examples 16.0 MHz / 32 slots
and 16.9 MHz / 24 slots.  Those values are numerically Y2/2 and Y3/2, so the
most important nearby alternative was tested again at the **same `$0CE3`
mode-commit boundary**, not through the already rejected ACTV callback:

| phase | temporary clock policy | measured dominant frequency |
|---|---|---:|
| A | Y2/2 = 15,238,090 Hz | 196.7 Hz |
| B | Y3/2 = 16,934,400 Hz | 196.7 Hz |
| A2 | Y2/2 = 15,238,090 Hz | 197.5 Hz |

It preserves A/B pitch *relation* but moves the known C4 about 25% low.  This
is `[DISPROVEN]` as the current-MAME functional policy.  It does **not**
disprove a physical divider on an ASR board: an unmodelled downstream timing or
audio-domain relation could still exist.

## Retained functional model

The temporary tests are now a small ASR board policy in production code:

```text
current effect +$66 -> $0CE3
  $00 -> ES5506 current-MAME device clock 30,476,180 Hz
  $01 -> ES5506 current-MAME device clock 33,868,800 Hz
```

This is `[Likely functional model]`, not `[Verified physical]`.  It is the
simplest surviving implementation because it uses the two documented board
oscillator values, the verified firmware operating-mode byte, and no invented
register, divider, mux control, FC compensation, or effect-name special case.
It is backed by the permanent `audio_rate_mode` A/B/A WAV acceptance:

```text
ROM HALL C4   262.3 Hz
44LUSH C4     260.9 Hz
ROM HALL C4   260.9 Hz
```

The production model must be revised if any of the following occurs:

1. a second independently controlled note/effect A/B/A does not preserve its
   pitch under this policy;
2. firmware-visible evidence identifies a different rate-selection state that
   contradicts `$0CE3` mode selection; or
3. a simple physical measurement, schematic or net trace identifies an
   incompatible ES5506 clock relation.

The original temporary C++ hooks and Lua probes were deleted.  The retained
implementation is covered by the reusable acceptance, not by diagnostic code.

## Next single experiment

Use one additional independently controlled A/B/A note or effect as a
functional falsifier.  A physical board measurement is worthwhile only when a
single accessible clock/CLKIN point can distinguish the surviving clock-route
alternatives; it is not a prerequisite for this `[Likely]` model.  Separately,
the parked ES5510 serial-routing question must not be reopened merely to
explain this direct-ES5506 pitch result.
