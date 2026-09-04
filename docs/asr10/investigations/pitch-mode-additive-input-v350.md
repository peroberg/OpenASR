# Pitch-mode additive input — ASR-10 V3.50

## Question and boundary

The preceding investigation established `$0D80` as the persistent
pitch-domain input to base FC conversion, but stopped at its writer:

```
$F8D33E  move.w D0,$0D80.w
```

This bounded follow-up asks only which *direct* input first makes that D0
mode-dependent.  It does not model a clock, change ES5506 semantics, or infer
physical Y2/Y3/ES5701 routing.

## Control and method

The established successful-effect control was repeated with the same JM DIGI
SYN object, voice path, velocity and note fixtures.  The committed states were
ROM HALL REVERB (A), 44LUSH PLATE (B), then ROM HALL REVERB again (A2):

| state | `$0CE3` | ACTV |
|---|---:|---:|
| A | `$00` | `$1F` |
| B | `$01` | `$17` |
| A2 | `$00` | `$1F` |

A retained Lua write tap at `$0D80` required post-store PC `$F8D342`; static
disassembly identifies the actual store as `$F8D33E`.  Thus the capture is a
PC-correlated execution witness, not an instruction-fetch inference.  A second
narrow retained tap at `$815E` witnessed the selected direct source's writer.
The probe was removed after capture.

## Executed local producer block

The final executed D0-producing instructions are:

```
$F8D338  add.w  D1,D0
$F8D33A  move.w D0,(A1)+
$F8D33C  add.w  (A1)+,D0
$F8D33E  move.w D0,$0D80.w
```

At the post-store callback A1 has advanced twice, so `A1-2` is exactly the
memory word read by `$F8D33C`.  All arithmetic below is 16-bit modulo-word
arithmetic; signed values are used only when comparing pitch offsets.

The mode-independent note term was D1=`$3C00` for note 3C and `$3D00` for
note 3D in every A/B/A2 state.  The preceding D0 component carries the known
phase variation, but the final source term is stable for an entire note fixture
and supplies the reversible DC offset.

### Compact raw witness

| note/state | D0 before `$F8D338` | D1 | D0 after `$F8D338` | source at `$815E` | final D0 / `$0D80` |
|---|---:|---:|---:|---:|---:|
| 3C A | `$003D` | `$3C00` | `$3C3D` | `$B5B0` | `$F1ED` |
| 3C B | `$FFFF` | `$3C00` | `$3BFF` | `$AEE2` | `$EAE1` |
| 3C A2 | `$FFA3` | `$3C00` | `$3BA3` | `$B5B0` | `$F153` |
| 3D A | `$003D` | `$3D00` | `$3D3D` | `$B5B0` | `$F2ED` |
| 3D B | `$FFFF` | `$3D00` | `$3CFF` | `$AEE2` | `$EBE1` |
| 3D A2 | `$FFA3` | `$3D00` | `$3CA3` | `$B5B0` | `$F253` |

For example, `$3C3D + $B5B0 = $F1ED`, and `$3BFF + $AEE2 = $EAE1`.
The source delta is `$AEE2 - $B5B0 = -1742` signed units.  Because it is
added directly at `$F8D33C`, it exactly accounts for the corresponding
mode-to-mode displacement of the `$0D80` interval.

## A/B/A and two-note differential

| operand/stage | 3C A | 3C B | 3C A2 | 3D A | 3D B | classification |
|---|---:|---:|---:|---:|---:|---|
| D1 low word | `$3C00` | `$3C00` | `$3C00` | `$3D00` | `$3D00` | note-dependent, not mode-dependent |
| final `$F8D33C` source | `$B5B0` | `$AEE2` | `$B5B0` | `$B5B0` | `$AEE2` | mode-dependent, reversible |
| `$0D80` signed interval | -3779..-3550 | -5520..-5293 | -3777..-3550 | -3523..-3294 | -5264..-5037 | phase variation plus reversible mode DC offset |
| `$0D80` midpoint | -3664.5 | -5406.5 | -3663.5 | -3408.5 | -5150.5 | mode + note dependent |

For both notes, `B - A = -1742` pitch units.  A2 returns to the A range
within the established phase variation.  The one-semitone 3C-to-3D delta is
`+256` units in both modes (and remains +256 under B).  This independently
measures a local pitch scale of 256 units/semitone, or 3072 units/octave;
it was not chosen from the sample-rate hypothesis.

## First mode-dependent direct input

The selected word is a persistent voice-record field addressed as `$86(A4)`;
in this control it is `$815E`.  Its PC-correlated writer is:

```
$007962  ext.w  D0
$007964  add.w  D0,D0
$007966  move.w $AA(A3),D1
$00796A  sub.b  D1,D1
$00796C  sub.w  D1,D0
$00796E  moveq  #0,D1
$007970  move.b $110(A3),D1
$007974  add.w  D1,D1
$007976  movea.l #$FFF873F6,A0
$00797C  add.w  (A0,D1.w),D0
$007980  sub.w  $0D66,D0
$007984  move.w D0,$86(A4)
```

The write tap observes post-store PC `$007988` and captured:

| phase | writer PC | stored word |
|---|---:|---:|
| A | `$007984` | `$B5B0` |
| B | `$007984` | `$AEE2` |
| A2 | `$007984` | `$B5B0` |

This classifies the first direct divergent input as **persistent RAM generated
at note setup**, not as a value shown to have been precomputed during effect
commit.  The table at `$FFF873F6`, `$AA(A3)`, `$110(A3)`, and `$0D66` are
only immediate inputs to that writer.  Their semantics and their provenance to
`$0CE3` are deliberately left open: tracing them would be the next task, not
this one.

## Pitch-scale and rate arithmetic

| quantity | value | status |
|---|---:|---|
| independently measured pitch scale | 256 units/semitone; 3072 units/octave | [Verified runtime/derived from two notes] |
| mode offset | -1742 units = -6.8046875 semitones = -0.5670573 octaves | [Derived arithmetic] |
| implied linear ratio | `2^(-1742/3072) = 0.6749922` | [Derived arithmetic] |
| documented 29.7619/44.1 ratio | `29761.8945 / 44100 = 0.6748729` | [Documented/derived arithmetic] |
| relative difference | +0.0177% | [Derived arithmetic] |

The independent semitone scale makes the numerical relation substantially
stronger than the earlier FC-midpoint coincidence.  It nevertheless does not
prove firmware intent or physical clock routing: quantisation-compatible
arithmetic is evidence for, not proof of, sample-rate compensation.

## Architectural result

The evidence currently best fits **Model C**:

```
effect-selected mode  -> ACTV                 [Verified separately]
effect-selected mode  -> note-time `$86(A4)`  [relation upstream OPEN]
`$86(A4)`             -> `$F8D33C` -> `$0D80` -> FC [Verified]
physical rate clock                              [OPEN]
```

There is no direct dataflow from `$0CE3` or ACTV to `$86(A4)` in this bounded
investigation.  The established reversible effect correlation is not promoted
to a causal firmware edge.

## Hypothesis verdicts

| hypothesis | verdict | reason |
|---|---|---|
| H1 `$0D80` is pitch-domain | SUPPORTED | one semitone is +256 units in both modes |
| H2 mode adds a pitch offset | SUPPORTED | `$F8D33C` adds the reversible -1742 source delta directly |
| H3 offset is note-independent | SUPPORTED | exact -1742 delta for 3C and 3D |
| H4 offset corresponds to documented rate ratio | PARTIALLY SUPPORTED | independently scaled implied ratio is within 0.0177% |
| H5 first input is precomputed rate/pitch state | UNSUPPORTED | `$86(A4)` is written at note setup, not demonstrated effect commit |
| H6 provenance to effect/audio mode | OPEN | effect correlation exists; no upstream edge was followed |
| H7 firmware compensates real rate modes | PARTIALLY SUPPORTED | numerical and pitch-domain evidence is strong; intent and physical rate remain open |

## Negative findings and stop

- No direct `$0CE3` read, ACTV read, ES5506 control write, ES5510 access, or
  board-facing access was established in this producer window.
- No physical clock or ES5701 conclusion follows from the pitch offset.
- ECOUNT/K2 shared provenance remains open.

The main backtrack stops at `$86(A4)`, the first secure, reversible direct
mode-dependent input reproduced on two notes.

## Single next experiment

If further provenance is needed, capture only the immediate inputs and
post-operation D0 of `$007962..$007984` for the same A/B/A two-note control,
then stop at the first indirect source.  In particular distinguish a changed
`$AA(A3)`/`$110(A3)` field, selected `$FFF873F6` table entry, or `$0D66` from
an already divergent incoming D0.  Do not resume a broad `$0CE3` consumer or
clock search.
