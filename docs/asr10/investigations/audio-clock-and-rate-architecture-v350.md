# ASR-10 V3.50 — audio clock and rate architecture

> **Historical architecture boundary.** The later functional two-rate policy
> is committed in `a8481df1f72` and the bounded ESP frame adapter in
> `7bfd2f3722d`. This document's direct-speaker/current-clock description is
> provenance for the pre-adapter model, not current MAME behaviour. The
> physical ASR clock route remains `[OPEN]`; see `../HANDOFF.md`.

## Executive summary

This pass does **not** establish a firmware-controlled 30 kHz ↔ 44.1 kHz
transition, so it makes no audio implementation change.  It corrects a more
basic premise: `clock / (16 * (ACTV + 1))` is the ES5506/OTTO's documented
per-voice **sample rate**, not a generic MAME-only timer or a stereo-pair
rate.  In MAME it is also exactly the oscillator sound-stream rate: each
generated frame advances every active voice once and produces all L/R channel
outputs for that frame.

With the current ASR-10 configuration, Y2 directly clocks MAME's ES5506:

```
30,476,180 / (16 * 32) = 59,523.789 Hz
```

There is no factor-of-two in either the reviewed ES5506 source or the OTTO
specification that turns that value into 29,761.895 Hz.  A physical `/2`
before OTTO would yield 29,761.895 Hz, but the existing dry-note runtime
measurement needs the current full-Y2 MAME configuration for approximately
correct pitch.  The difference remains `[OPEN]`; it is not evidence for an
ACTV workaround.

The firmware writes `ACT=$1F` and `MODE=$0D` only during boot in the examined
paths.  ACTV therefore cannot presently be the observed runtime 30/44.1
selector.  MODE is a serial-interface configuration register, not a sample
rate register.  In particular, `$0D` configures OTTO as single/master/normal
address mode while the BCLK and LRCLK enable bits leave those pins as inputs;
the board can therefore supply serial framing independently of the host
register write.  This establishes an interface boundary, not its board wiring.

The new targeted CS1 observation proves that the firmware uses the otherwise
unidentified write-only `$FF6000-$FF7FFF` window.  The observed `$FF7Fxx`
traffic is associated with the existing 31-entry per-voice helper structure
and a note-on producer, not with a discriminated rate transition.  CS1 is now
a concrete board-control candidate, but assigning it a clock function would
be invention.

## Evidence labels

* `[Verified source]` — local Ensoniq specification PDF, page cited below.
* `[Verified code]` — current MAME source or decoded V3.50 code.
* `[Verified runtime]` — witnessed V3.50 execution in the current harness.
* `[Derived arithmetic]` — exact calculation from a verified input.
* `[Likely]` and `[OPEN]` retain the meanings in
  `reference/methods-hypothesis-management.md`.

## 1. ES5506 rate semantics

### 1.1 What the formula means

`docs/ensoniq/ES5506.pdf`, rev. 2.3 p. 6, states all of the following
explicitly `[Verified source]`:

* ACTV selects the number of active voices; the register value plus one is the
  number of voices, from one through 32.
* Each active voice takes one microsecond at a 16 MHz master clock.
* The number of active voices determines OTTO's **output sample rate**.
* `Sample rate = Master Clock / (16 * Number of Voices)`.

Thus the value denotes OTTO's per-voice update/output frame rate.  It is not
the master oscillator itself, not a voice-round-robin frequency distinct from
sample updates, and not a left/right-pair divisor.  The chip processes each
voice in a time slot; one complete active-voice round produces the next sample
for that voice's assigned channel output.

The same source distinguishes this from the serial transport: OTTO has BCLK,
WCLK and LRCLK pins and programmable W_ST/W_END/LR_END registers (pp. 6–8).
Those describe serial word/frame formatting; the cited sample-rate equation
does not contain a stereo or serial-clock factor.

### 1.2 MAME implementation cross-check

`src/devices/sound/es5506.cpp` implements the same relation `[Verified code]`:

```cpp
m_sample_rate = m_master_clock / (16 * (m_active_voices + 1));
m_stream->set_sample_rate(m_sample_rate);
```

It recomputes this only when the device clock changes or an ACTV write occurs.
In `generate_samples()`, one `sampindex` iteration loops from voice zero
through `m_active_voices`, calls `generate_pcm()`/`generate_ulaw()` once per
voice, and emits the complete set of channel L/R samples.  There is no divide
by two for stereo in this code `[Verified code]`.  MODE, W_ST, W_END and
LR_END are retained as register state but do not alter that scheduler or
stream rate in the current implementation `[Verified code]`.

| ACTV field | Active slots in spec/MAME | Master clock | Formula result | Status |
|---:|---:|---:|---:|---|
| `$1F` | 32 | Y2 = 30,476,180 Hz | 59,523.789 Hz | `[Derived arithmetic]` |
| `$1F` | 32 | Y2 / 2 = 15,238,090 Hz | 29,761.895 Hz | `[Derived arithmetic]` |
| `$17` | 24 | Y3 / 2 = 16,934,400 Hz | 44,100 Hz | `[Derived arithmetic]` |

This also exposes an unresolved terminology mismatch.  The often repeated
“31 voices / 23 voices” pair cannot be silently equated with ACTV `$1F`/
`$17`: the documented ACTV encoding makes those values 32 and 24 slots.
If “23 voices” literally means 23 active slots, its master clock for exactly
44.1 kHz would instead be 16,228,800 Hz.  No V3.50 trace has shown either a
`$17` or `$16` ACTV write. `[OPEN]`

### 1.3 Current firmware use

`$F8CF06-$F8D00E` initializes OTTO with `ACT=$1F` and `MODE=$0D` during boot
and initializes its voices `[Verified code]`, as separately recorded in
`reference/subroutine-index.md`.  Existing V3.50 boot/load/select/playback
traces have not observed a later ACTV or MODE write `[Verified runtime]`.

`MODE=$0D` is binary `01101`: MODE1:MODE0 is `01` (single/master/normal
address); BCLK_EN and LRCLK_EN are high and therefore leave BCLK/LRCLK as
inputs; WCLK_EN is low and drives WCLK as output.  This bit interpretation is
from ES5506 rev. 2.3 pp. 6–7 `[Verified source]`.  It is positive evidence
that serial framing is a board-level concern, but not proof that an external
LRCLK changes OTTO's sample-update equation. `[OPEN]`

## 2. Clock inventory: current MAME versus board evidence

| Oscillator / clock | Established consumer | MAME ASR-10 consumer | Status |
|---|---|---|---|
| Y1 = 16.000 MHz | MC68302/CPU board oscillator | `M68000` stand-in at 16 MHz | `[OBSERVED — owner-supplied physical inspection]` (canonical inventory); MAME `[Verified code]` |
| Y2 = 30.47618 MHz | audio-side candidate; exact physical pin path untraced | ES5506 direct at 30,476,180 Hz | `[OBSERVED — owner-supplied physical inspection]`; MAME `[Verified code]`; board path `[OPEN]` |
| Y3 = 33.8688 MHz | audio-side candidate; exact physical pin path untraced | no audio device consumes it | `[OBSERVED — owner-supplied physical inspection]`; MAME `[Verified code]`; board path `[OPEN]` |
| synthetic 44.1 kHz | none | timer drives MC68302 PB3/LRCLK model | MAME `[Verified code]`; physical derivation `[OPEN]` |
| 10 MHz | no ASR-10 wiring evidence | disabled ES5510 placeholder | MAME `[Verified code]`; board value `[OPEN]` |

`asr10_boot.cpp` directly routes ES5506 outputs to `speaker` and contains no
ASR-10 ES5701 device, serial-audio wiring, clock mux, divider, or
ES5506 `set_clock()` callback.  Therefore it hard-codes Y2 as OTTO's master
clock and loses any board-side serial framing/ESP path `[Verified code]`.

The PB3 44.1 kHz timer is an explicitly provisional firmware-facing LRCLK
source.  It is not wired to the ES5506 device's serial pins (which the current
generic device does not simulate as audio timing) `[Verified code]`.  It is
not evidence that Y3 is the source of a real 44.1 kHz mode.

## 3. Y2/Y3 arithmetic

| Source | Divider | Result | Architectural reading |
|---|---:|---:|---|
| Y2 | 2 | 15,238,090 Hz | a simple flip-flop division; family precedent exists, ASR wiring `[OPEN]` |
| Y2 | 512 = `2 * 16 * 32` | 29,761.895 Hz | exact 29.76 kHz-class OTTO result for 32 slots `[Derived arithmetic]` |
| Y2 | 384 = `16 * 24` | 79,365.052 Hz | not 44.1 kHz |
| Y3 | 2 | 16,934,400 Hz | simple flip-flop division `[Derived arithmetic]` |
| Y3 | 384 = `2 * 16 * 24` | 44,100 Hz | exact 44.1 kHz result if 24 slots and `/2` are real `[Derived arithmetic]` |
| Y3 | 512 = `16 * 32` | 66,150 Hz | not 30/44.1 kHz |
| Y3 | 768 = `2 * 16 * 24` | 44,100 Hz | same relationship written as one divider `[Derived arithmetic]` |

The two crystals make a two-domain model numerically plausible:
`Y2/2` with 32 slots reaches 29.7619 kHz and `Y3/2` with 24 slots reaches
44.1 kHz.  That is only arithmetic.  No ASR-10 source yet proves a `/2`, a
Y2/Y3 mux, ACTV `$17`, or an association between those values and effect
metadata. `[OPEN]`

The physically observed Main Board U47 SN74F161 and U50/U64 SN74F74
(`[OBSERVED — owner-supplied physical inspection]`; see `docs/asr10/reference/physical-component-inventory.md`)
can in principle implement counter/divider/flip-flop functions; Main Board U65 SN74F02 and U67 MC74HC27
can provide combinational glue. Component function alone does not establish their nets,
select inputs, or consumers. `[OPEN]`

## 4. ES5701 / SuperGLU

The local ES5701 specification is concrete about the **silicon**, but not
about ASR-10 U41 wiring `[Verified source]`:

* CPU-to-ESP and CPU-to-OTIS bus translation;
* OTIS multiplexed address/data to static sound-memory glue;
* a 20 MHz crystal oscillator divided by two to buffered 10 MHz; and
* a separate 16 MHz oscillator divided by two to buffered 8 MHz
  (ES5701 rev. 2, pp. 1, 3–4).

The bundled `sources/es5701.vhd` reconstructs exactly those two fixed `/2`
paths, `20→10` and `16→8`.  It contains no clock mux and no 30/44.1 rate
control `[Verified code/provenance]`.  The part's documented function is
therefore compatible with clock generation in general, but it does **not**
document Y2/Y3 selection, a 15.23809 MHz OTTO clock, or a 16.9344 MHz clock.

ASR-10 facts remain narrower: U41 is reported as an ES5701-family device and
the firmware reaches ES5506 `$FC2000` and ES5510 `$FC3000-$FC31FF` host
windows `[Verified firmware/runtime]`.  `reference/es5701-wiring.md` correctly
keeps the exact U41 variant, pins and FC-window placement open.  It would be
incorrect to call U41 the rate switch or to invent registers for it.

## 5. Firmware-facing board-control candidates

### 5.1 Rejected as observed rate producers

* **ES5506 ACTV/MODE.** Boot-only `$1F/$0D`; no observed V3.50 transition.
  ACTV directly affects sample rate but is not the observed runtime selector.
  `[Verified runtime]`
* **MC68302 PBDAT.** Its observed low bits select the analog PAR mux;
  `reference/memory-map.md` documents the 0/2/3/4/5/7 scan.  No evidence ties
  those writes to audio clocking. `[Verified firmware/runtime]`
* **ES5510 host upload.** Firmware performs a real host download, but the
  disabled current model cannot establish ESP execution or rate selection.
  Effect-download traffic is not a rate transition. `[Verified firmware]`,
  rate implication `[OPEN]`.

### 5.2 CS1: a real, still-unidentified board path

CS1 is ROM-configured as write-only `$FF6000-$FF7FFF` with external DTACK.
Earlier static scans found no direct absolute/immediate-base reference, but
that never excluded register-indirect access.

A temporary Lua write tap over all CS1, retained for the complete run and
paired with a live low-RAM write witness, was run once with V3.50 through:

```
FILE 1 TUTORIAL BNK → FILE LOADED → Instrument 1 → MIDI note-on
```

Result `[Verified runtime]`:

```
CLOCK_CS1 witness=1490908 writes=133
```

The boot portion repeatedly wrote `$0007` at `$FF7F06`, then every eight
bytes through `$FF7FF6`.  At the note onset, the relevant writes were:

```
$F8E278 -> $FF7F00 = $0006
$F8E27C -> $FF7F02 = $0007
$F8E280 -> $FF7F04 = $0008
```

Static V3.50 code at `$F8E270` loads `A0` from the current voice record's
`+$2A`; `$F8E274/$F8E278/$F8E27C` write three consecutive words through that
pointer.  The existing voice initialization maps `+$2A` from `$FF7F00` in an
eight-byte-per-voice structure.  The runtime sequence therefore corroborates
the pre-existing per-voice-helper interpretation `[Verified code/runtime]`.

This disproves neither a different, unexecuted CS1 clock function nor a
board-side relation between its write-only hardware and voice service.  It
does show that the previous “no observed CS1 use” state was too strong, and
that the measured path is not a two-state 30/44.1 selector.

## 6. ES5510 timing relationship

The ESP data sheet says it executes a microprogram around 10–50 kHz and that
the `HALT` pin can synchronize it to an external sample-rate clock or a
begin-sample pulse (ES5510 rev. 2.4 pp. 1, 39) `[Verified source]`.  Its serial
interface has BCLK, WCLK and LRCLK and can be Sony-master or Sony-slave
(pp. 36, 43) `[Verified source]`.

Consequences:

* ESP has a separate `CLK_IN` system clock, so it is not proven to share
  OTTO's master clock.
* It can nevertheless be synchronized to the audio frame domain through
  HALT/begin-sample/serial clocks.
* A correct future ASR-10 rate model must preserve both possibilities rather
  than special-casing ES5506 from an effect name.

Current MAME configures an ES5510 at 10 MHz and disables it.  This is a
datasheet-valid speed and family precedent, but not an ASR-10 board
measurement.  No ESP serial or HALT/begin-sample connection is modeled.
`[Verified code]` for the present state; actual ASR timing `[OPEN]`.

## 7. Current MAME model versus evidence-supported target

```
CURRENT MAME
  Y2 fixed -> ES5506 generic internal sample stream -> speaker
  synthetic 44.1 kHz -> MC68302 PB3 only
  ES5510 @ 10 MHz, disabled
  no ES5701, serial frame wiring, clock mux/divider or ESP synchronizer

EVIDENCE-SUPPORTED TARGET MODEL (not yet an implementation)
  real board clock/framing source(s)
       -> OTTO master/serial clocks and, if wired, ESP synchronization
       -> generic ES5506/ES5510 behavior
       -> board-specific serial/ESP routing
```

The target deliberately leaves the actual source and select policy open.
MAME's direct speaker route and unmodeled serial interface are genuine
architectural gaps, but neither tells us to set a particular 30 or 44.1 kHz
number today.

## 8. Tutorial Song boundary

A global mismatch in OTTO's sample rate **can explain** a global pitch shift,
sample duration/loop-time change, envelope-time change and voice-frame timing
change.  It does **not by itself explain** wrong sample selection,
instrument/track association, or apparently random blips.  The observed
Tutorial Song high-pitched/blippy/musically wrong result remains a symptom,
not a rate diagnosis. `[OPEN]`

## 9. Minimal next experiment

First obtain two reproducible firmware states that independently identify
their own audio/voice mode (for example a true installed algorithm/property,
not merely the Effects file browser).  Then, for A → B → A, retain witnessed
Lua taps on:

1. all CS1 writes, including PC and phase;
2. ES5506 ACTV/MODE and serial-format register writes;
3. ES5510 host-control/HALT-relevant accesses; and
4. any board GPIO/latch write already statically tied to the transition.

Only a state that changes A → B and restores B → A may be promoted to a clock
select candidate.  If it is an existing firmware write to an identified board
endpoint, model that endpoint at the ASR-10 board boundary; do not encode an
effect name or an arbitrary rate in ES5506.

## Compact status

**VERIFIED**

* OTTO sample rate is `master / (16 * active voices)`; ACTV field is
  zero-based.
* Current MAME maps that rate to its generated L/R audio frame; no stereo `/2`.
* V3.50 boot writes ACTV `$1F` and MODE `$0D`; observed later paths do not.
* MODE `$0D` leaves BCLK/LRCLK as inputs.
* ES5701 silicon has fixed 20→10 and 16→8 clock dividers.
* CS1 is used at runtime; observed `$FF7Fxx` traffic belongs to the
  per-voice-helper path.

**LIKELY**

* A complete ASR-10 audio model needs a board serial/ESP boundary, rather
  than the current direct ES5506-to-speaker shortcut.

**DISPROVEN**

* “`clock/(16*(ACTV+1))` is merely a MAME voice-cycle rate that must be
  divided by stereo two.”
* “The observed CS1 activity is already a 30/44.1 select.”

**OPEN**

* physical Y2/Y3 consumers and any `/2` or mux network;
* actual ASR-10 U41 variant/pin wiring and whether it participates in timing;
* a firmware-visible 30/44.1/voice-mode state and its producer;
* the real ESP system clock and its HALT/begin-sample/serial synchronization;
* the reason current full-Y2 MAME pitch matches dry-note measurement despite
  the unresolved physical factor-of-two question.
