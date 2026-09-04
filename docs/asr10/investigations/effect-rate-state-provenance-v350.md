# ASR-10 V3.50 — committed effect rate-state provenance

## Executive summary

This investigation begins at a **successful firmware commit**, rather than at
an oscillator or an OTTO register.  It establishes a reproducible path from
two distinct effect objects through the existing ES5510 host verifier to a
reversible ES5506 active-voice write:

```text
effect object selected/loaded
  -> $F973F0 uploader, source base in $0E8E
  -> ES5510 host upload and firmware readback verification
  -> runtime RAM reconfiguration code at $00E7B4
  -> D4 -> ES5506 ACTV write at $FC205E
```

The observed low/high values are `$1F` and `$17`, respectively.  The latter
is the first live V3.50 witness of the documented 24-slot ACTV value; it is
not inferred from oscillator arithmetic.  The immediate producer uses D4,
but this pass has not yet recovered the caller that assigns D4 from an effect
descriptor field.  Thus the **effect commit -> ACTV control** edge is
verified, whereas the named rate/polyphony metadata field remains open.

No C++ change is made.  In particular, this does not prove an ASR-10 board
clock mux, an ES5701 register role, or a complete 29.7619/44.1 kHz timing
model.

## Evidence labels

* `[Verified source]` — local Ensoniq manual or chip documentation.
* `[Verified firmware]` — V3.50 code/data with a bounded provenance.
* `[Verified runtime]` — reproduced V3.50 observation with retained Lua taps
  and a live boot witness.
* `[Derived]` — direct consequence of the preceding evidence.
* `[Likely]` and `[OPEN]` follow the hypothesis-management reference.

## 1. Effect identities and independent rate oracle

| Role | Runtime object | Local documentation | Rate/polyphony class |
|---|---|---|---|
| A | ROM `HALL REVERB`, selected after FX Select -> ROM | Musician's Manual lists `ROM-01 HALL REVERB`; it identifies the neighbouring `ROM-02 44KHZ REVERB` as the 44-kHz special case | 30-kHz / 31-voice class `[Likely source pairing]` |
| B | Bank `44LUSH PLATE`, V3.50 disk file 16 | Musician's Manual explicitly calls `44LUSH PLATE` a 44-kHz small-plate reverb; the Effects page gives 44 kHz = 23 voices | 44.1000 kHz / 23 voices `[Verified source]` |

The current display decoder renders some control bytes as `?`, so display
text was used only for navigation.  Non-display witnesses below identify the
committed objects: the source base/cursor, completed host verification, and
the reconfiguration write.  A direct committed `ROM-04 DUAL DELAYS` (the
manual explicitly calls it standard 30-kHz/31-voice) remains the better
future low-rate oracle; it was not reached by a new button sweep here.

## 2. Successful A -> B -> A commit witness

The temporary Lua probe retained host-window, effect-state, object, and OTTO
write taps, and first required `FILE 1  TUT0RIAL BNK`.  It used the existing
Load -> Effects -> Enter flow to load V3.50 disk file 16.  Firmware displayed
`DI5K C0MMAND C0MPLETED`; that alone was not accepted as commit proof.

| Stable point | `$0E8E` source base | `$0E7E` cursor | verify reads / mismatch | `$00C0` error / `$0E8C` retry |
|---|---:|---:|---:|---:|
| A, ROM HALL REVERB | `FFF9B626` | `FFF9B948` | 1644 / 0 | `00 / 00` |
| B, loaded 44LUSH PLATE | `0062B600` | `0062C342` | 549 / 0 | `00 / 00` |
| A again, ROM HALL REVERB | `FFF9B626` | `FFF9B948` | 822 / 0 | `00 / 00` |

The verifier compares the live ES5510 host-read byte to D2 across the known
`$F97574-$F9757A` compare window.  It observed zero mismatches in every
phase.  The larger first-A count includes the initial selection/setup; the
reversible pointers, zero retry/error and successful fresh uploads establish
the required A/B/A firmware state, independently of display naming.

## 3. Committed descriptor state and object candidates

`$F973F0` writes A3 to `$0E7E` and the loader later uses `$0E8E` as its
current transfer-object base `[Verified firmware/runtime]`.  The two values
are therefore **provenance pointers**, not a rate field:

| Field | A | B | A2 | Meaning/status |
|---|---:|---:|---:|---|
| `$0E8E` | `FFF9B626` | `0062B600` | `FFF9B626` | transfer object base `[Verified]` |
| `$0E7E` | `FFF9B948` | `0062C342` | `FFF9B948` | parser/upload cursor `[Verified]` |
| `$0E9D` | `02` | `02` | `02` | unchanged; unnamed `[OPEN]` |
| `$0E8C` | `00` | `00` | `00` | retry state `[Verified]` |

The first 128 object bytes also differ reversibly.  Both contain a spaced
ASCII effect name beginning at `+0x0c`; B is `44LUSH PLATE`.  Word `+0x08`
is `0002` for A and `0005` for B, but no consumer establishes its meaning.
It is an object-format candidate only, **not** a rate-class label.

The cursor-minus-base distances (`$322` A, `$d42` B) are derived parser
stream lengths and likewise do not name a mode.

## 4. Consumers reached after commit

The common loader consumer is static ROM code:

```text
$F973F0: A3 -> $0E7E
          record parser / ES5510 host transfer
          verification at $F97574-$F9757A
```

After B was selected, retained object-read taps observed code at
`$0062BA1C`, `$0062BA32`, `$0062BB16`, `$0062BB24`, and `$0062BB2C` reading
structured B-object offsets such as `+0x36`, `+0x38`, `+0x94`, `+0x96` and
`+0x76`.  This proves post-commit runtime object consumption, but the loaded
RAM code and fields are not yet semantically decoded.  Other readers at
`$007208/$007210/$007216` are likewise executable runtime RAM, not a basis
for naming a rate field.

The only consumer with a concrete audio control consequence in this pass is
the runtime routine at `$00E7B4`.  Its relevant instructions decode as:

```text
$00E810  movea.l #$00FC6829,A1
...       waits on the status bit at (A1)
$00E826  move.b D4,($005E,A0)       ; A0 = $00FC2001
```

The final store is `$FC205F`/the ACTV register byte lane.  The write tap sees
the aligned word address `$FC205E` and its post-write PC is `$00E82A`.
Earlier notes labeled the instruction `$00E824`; disassembly establishes that
the instruction starts at `$00E826`.  D4 is therefore the immediate firmware
rate/slot-control value.  The routine is copied/executed RAM; this evidence
does **not** identify the earlier assignment to D4.

## 5. Reversible hardware differential

| Commit state | Writer | ES5506 ACTV observed | Result |
|---|---:|---:|---|
| A, ROM HALL REVERB | `$00E82A` | `$1F` | 32 OTTO slots |
| B, 44LUSH PLATE | `$00E82A` | `$17` | 24 OTTO slots |
| A again, ROM HALL REVERB | `$00E82A` | `$1F` | 32 OTTO slots restored |

The same phases include the standard `$F8D006` write of `$1F` and an
intermediate `$00` during reinitialisation.  The post-wait `$00E82A` write is
the stable, reversible difference.  The B selection page itself creates no
new OTTO writes: the change occurred during the successful file load/commit,
not while merely displaying the effect name.

This is a concrete firmware -> chip-control witness.  It verifies an
`ACTV $1F <-> $17` transition, contrary to the earlier limited failed-upload
trace.  It does not by itself show a clock-source switch.

## 6. 31/23 voices, ES5506, and ES5510

ES5506 documentation defines ACTV zero-based, so `$1F` is 32 slots and `$17`
is 24 slots `[Verified source]`.  The manual-facing numbers are 31 and 23.
The A/B/A correlation makes the proposed one-reserved-slot relationship
**likely**, but allocation-state provenance has not yet been recovered:

```text
31 playable voices + 1 OTTO slot = 32   [Likely]
23 playable voices + 1 OTTO slot = 24   [Likely]
```

Both algorithms exercise the same verified ES5510 host upload/readback route.
B commits with 4,389 host writes and 549 matching verify reads; restored A
uses 4,773 writes and 822 matching reads.  This establishes ESP program/data
provenance for both states.  It does not establish ESP execution, serial
framing, or an ESP clock transition; those remain open and no DSP/audio code
was changed.

## 7. Rate path verdict and implementation boundary

The observed chain is:

```text
effect object A/B
  -> committed source base/cursor ($0E8E/$0E7E)
  -> $F973F0 ES5510 upload + verified readback
  -> unclassified runtime consumer(s)
  -> D4 at $00E826
  -> ES5506 ACTV $1F <-> $17
```

This is a **partial** rate-switch path: it reaches real OTTO hardware state,
but the rate/voice-class metadata field, D4 producer, board clock source and
ES5510 timing control are not yet proven.  Existing Y2/Y3 `/2` arithmetic
therefore remains arithmetic only.  No rate fix, clock divide, effect-name
special case, or ES5701 model is justified by this result.

The smallest next investigation is to capture the caller/assignment that
sets D4 before `$00E7B4`, then backtrack that value into a named object field.
In parallel, use a direct manual 30-kHz object such as `ROM-04 DUAL DELAYS`
as the low-class effect oracle; no UI sweep or clock experiment is required.

## 8. Remaining OPEN

* exact descriptor field that carries rate/voice class and its D4 dataflow;
* whether the likely 31/23-to-32/24 reserved-slot relation reaches the voice
  allocator;
* board clock selection/division and the roles of Y2, Y3, ES5701 and glue;
* ES5510 execution/frame-clock response to the same mode;
* physical routing of the control and clock nets.

EFFECT A:
    ROM HALL REVERB; successful A/A2 commit is verified, while its explicit
    30-kHz class is a likely manual pairing rather than a decoded field.

EFFECT B:
    Bank 44LUSH PLATE (V3.50 disk file 16), 44.1000 kHz / 23 voices
    [Verified source].

A/B/A COMMIT:
    [VERIFIED]

RATE/POLYPHONY FIELD:
    D4 at the final runtime control routine is verified; the upstream
    descriptor/metadata field that supplies it is [OPEN].

CONSUMER:
    $00E7B4/$00E826 writes D4 to ES5506 ACTV after the host-verified load.

HARDWARE CONTROL:
    ES5506 ACTV at $FC205E/$FC205F: $1F -> $17 -> $1F [Verified runtime].

31/23 -> 32/24:
    [LIKELY]

ES5506 ACTV:
    $1F -> $17 -> $1F [Verified runtime]; no clock-source transition shown.

ES5510 RELATION:
    both effect objects commit through verified host upload/readback; ESP
    execution and timing relation remain [OPEN].

RATE-SWITCH PATH:
    [PARTIAL]

NEXT MINIMAL STEP:
    backtrack the D4 assignment feeding $00E824 to a decoded effect field,
    using a directly documented 30-kHz committed object for the comparison.
