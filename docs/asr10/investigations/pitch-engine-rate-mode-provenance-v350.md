# Pitch-engine rate-mode provenance — ASR-10 V3.50

## Question and scope

This investigation follows the verified ASR-10 V3.50 voice-frequency-control
(FC) differential backward from the final ES5506 write.  It does **not** alter
the clock model, ACTV semantics, ES5510, ES5701, serial framing, or audio
routing.  The question is narrower: where is the first reproducible
mode-dependent value on the observed pitch path for the same musical note?

The previous clock experiment is deliberately not retried here.  It was
reverted after the known-note oracle disproved ACTV as a sufficient standalone
clock proxy; see `audio-rate-model-implementation-v350.md`.

## Established control

The existing media workflow selected the same `JM DIGI SYN` musical object and
note-on path while committing the two already verified effect modes:

| state | effect | `$0CE3` | ACTV | slots |
|---|---|---:|---:|---:|
| A | ROM HALL REVERB | `$00` | `$1F` | 32 |
| B | Bank 44LUSH PLATE | `$01` | `$17` | 24 |
| A2 | ROM HALL REVERB | `$00` | `$1F` | 32 |

The effect-upload/readback contract remained successful in the established
control.  Voice 1 was captured; its CR, bank, START, END, and ACCUM were equal
in the prior A/B/A comparison.  Thus this is not a comparison of different
sample geometry.  FC has an already measured LFO-phase contribution, so raw
late endpoints are not used as the sole pitch oracle.

The original known-note control (note `3C`) measured FC intervals
`$369..$397`, `$24D..$26C`, and `$369..$396` for A/B/A2: respective midpoints
about 896, 604.5, and 895.5.  Current unmodified MAME stream rates were
59,523.789, 79,365.052, and 59,523.789 Hz; its measured audio was about 262.3,
235.3, and 260.9 Hz.  Those measurements are reproduced from
`voice-fc-rate-mode-differential-v350.md`, not reinterpreted as physical clock
measurements here.

## Final FC writer: corrected exact path

The final ES5506 boundary is the known `$FC602E` MOVEP.  The bounded dynamic
and static backtrack establishes this base-programming chain:

```
$007DD6  move.w  $0D80,D0
$007DDA  jsr     $F8D490
          ; D0 returned as the base FC value
$007DEE  move.l  D0,$AE(A4)       ; voice-record persistent field
...
$FC619A  move.l  $AE(A4),D0
$FC619E  movep.l D0,($08,A0)      ; ES5506 FC at $FC602E
```

The Lua callbacks saw PC `$007DF0` for the `$AE(A4)` store and `$FC61A0` for
the final MOVEP.  Both are post-instruction PCs; the static instructions above
are the actual writers.  The base path is distinct from the later periodic FC
updates that carry the known LFO variation.

`$F8D490` is a bounded pitch-conversion routine.  It clamps/normalises its D0
input, reads fixed ROM tables rooted at `$F846CE` and `$F852CE`, shifts, and
returns D0 to its caller.  Its observed lookup addresses varied between A and
A2 with LFO phase.  Therefore neither a single lookup address nor either ROM
table is classified as the mode/rate coefficient.

## First persistent mode-dependent boundary reached

`$0D80` is the immediate persistent input to the base-FC path.  Its actual
writer is `$F8D33E: move.w D0,$0D80.w`; the write callback reports post-write
PC `$F8D342`.  A PC-correlated RAM-write witness confirms execution.

That writer is the end of arithmetic at `$F8D2C0..$F8D33E`, including signed
products, shifts, additions, a `$0176` contribution, and voice-record fields
around `$A8(A4)`/`$AA(A4)`/`$AC(A4)`.  The bounded investigation does not name
those upstream terms or continue through their wider pitch-engine producers.

| state | `$0D80` signed range | midpoint | write count | `$AE(A4)` first base FC |
|---|---:|---:|---:|---:|
| A, note 3C | `$F13F..$F221` | -3,664 | 39 | `$0000038C` |
| B, note 3C | `$EA70..$EB53` | -5,406.5 | 38 | `$0000026B` |
| A2, note 3C | `$F13E..$F221` | -3,664.5 | 38 | `$0000037F` |

The ranges have essentially identical LFO-shaped width (226/227/227) but
separate, reversible DC locations.  This proves a mode-dependent pitch value
has been prepared before `$F8D490`; it does **not** prove that `$0D80` is a
linear rate coefficient or that its writer directly reads `$0CE3`.

## Second-note control

One nearby second-note fixture (`3D`) was run with the same A/B/A effect
control.  It reproduced the same property without a keyboard sweep:

| state | `$0D80` signed range | midpoint | `$AE(A4)` first base FC |
|---|---:|---:|---:|
| A, note 3D | `$F23F..$F321` | -3,248 | `$000003C2` |
| B, note 3D | `$EB70..$EC53` | -5,022.5 | `$00000290` |
| A2, note 3D | `$F23E..$F321` | -3,248.5 | `$000003B4` |

The mode offset remains about -1,774.5 pitch-domain units, while the
within-state range remains about 226/227 units.  This supports a global or
at least non-coincidental mode contribution across the two adjacent notes; it
does not yet establish its representation or ownership.

## Provenance differential

| stage | PC/address | A | B | A2 | status |
|---|---|---:|---:|---:|---|
| effect mode | `$0CE3` | `$00` | `$01` | `$00` | [Verified runtime] |
| OTTO configuration | ACTV | `$1F` | `$17` | `$1F` | [Verified runtime] |
| pitch intermediate | `$0D80`, note 3C midpoint | -3,664 | -5,406.5 | -3,664.5 | [Verified runtime] reversible divergence |
| persistent FC field | `$AE(A4)`, first value | `$38C` | `$26B` | `$37F` | [Verified runtime]; phase-sensitive setup value |
| final FC load | `$FC619A` D0 | `$AE(A4)` | `$AE(A4)` | `$AE(A4)` | [Verified firmware static/runtime] |
| ES5506 FC write | `$FC619E` -> `$FC602E` | D0 | D0 | D0 | [Verified firmware static/runtime] |

The first *persistent* reversible mode-dependent state reached by this
limited, one-producer-at-a-time backtrack is `$0D80`.  Its final arithmetic
writer is a clean unresolved boundary: inputs to that arithmetic are not yet
shown equal before the writer or traced to an effect-mode state.  Stopping here
avoids turning this task into a full pitch-engine reconstruction.

## Rate mathematics

| quantity | value | status |
|---|---:|---|
| FC midpoint B/A, previous note-3C capture | 0.674665 | [Verified measurement] |
| documented 29,761.9 / 44,100 | 0.674873 | [Derived arithmetic] |
| current MAME stream B/A | 4/3 | [Verified MAME runtime] |
| predicted output B/A: FC ratio x stream ratio | 0.89955 | [Derived arithmetic] |
| measured output B/A | about 0.8971 | [Verified measurement] |

The close FC/documented-rate numerical match remains a strong discriminant,
not a provenance proof.  `$0D80` is signed pitch-domain state and its A/B
difference must not be divided as though it were a linear FC scalar.

## ECOUNT and K2

The preceding voice differential independently found ECOUNT and K2
mode-dependent.  This investigation did not trace their producers.  No
verified fan-out from the `$0D80` producer to either field was found, so a
single shared rate-mode state remains `[OPEN]`.

## Rejected clock-model result

The rejected effective-clock trial made the known note about 262.3 -> 196.7
Hz (about 0.750).  The new evidence explains why ACTV alone was an incomplete
proxy: the firmware also changes pitch-domain state and therefore FC across
the two operating modes.  It does **not** fully derive the 0.750 result,
because that trial did not establish this exact effect A/B transition or a
physical clock path.  No compensating factor is inferred.

## Architectural reading

The evidence currently best fits **Model C**:

```
effect-selected mode
   |- verified: ACTV/resource configuration
   `- [OPEN producer] -> mode-distinct $0D80 pitch state
                         -> $F8D490 -> voice +$AE -> FC
physical sample/frame clock: [OPEN]
```

This is not evidence that ACTV physically selects the rate, that `$0CE3`
directly feeds pitch, or that ES5701 selects a clock.  It does establish that
firmware voice programming and current MAME ES5506 timing jointly explain the
observed A/B output-pitch ratio for the controlled note.

## Hypothesis verdicts

| hypothesis | verdict | reason |
|---|---|---|
| H1: mode-dependent FC scaling | PARTIALLY SUPPORTED | FC and the pre-FC `$0D80` state differ reversibly; the representation is not yet proven to be a scalar. |
| H2: scaling matches documented rate ratio | PARTIALLY SUPPORTED | FC midpoint ratio closely matches, but provenance/intent is not established. |
| H3: direct/indirect `$0CE3` provenance | OPEN | no bounded dataflow edge from `$0CE3` to the `$0D80` producer was observed. |
| H4: precomputed rate/pitch state | PARTIALLY SUPPORTED | `$0D80` is persistent before note FC conversion and is mode-distinct; writer inputs remain unresolved. |
| H5: common state for FC/ECOUNT/K2 | OPEN | no shared producer was measured. |
| H6: compensation preserves real-hardware pitch | LIKELY | numerical contract is consistent, but firmware intent and physical timing are unverified. |
| H7: current MAME A/B pitch error follows timing mismatch | PARTIALLY SUPPORTED | measured output closely follows programmed-FC ratio times current stream-rate ratio; full physical timing is open. |

## Negative findings and open boundaries

- No direct `$0CE3` read, effect-descriptor read, ES5510 access, or board
  register write was established on the bounded FC path.
- `$F8D490` table observations are LFO-phase sensitive and are not a verified
  rate table selection.
- No physical clock routing, ES5701 role, PB3/LRCLK relationship, or allocator
  explanation for 31/23 versus 32/24 follows from this evidence.

## Single best next experiment

At `$F8D338..$F8D33E`, capture the final D0 and its immediate additive inputs
in the same two-note A/B/A window, then backtrack only the first input whose
value is reversible mode-dependent.  If that input is indirect or opens a
broader pitch subsystem, stop at that boundary.  Do not alter clocks or FC.

## Required compact conclusion

```
FIRST MODE-DEPENDENT DIVERGENCE REACHED:
    persistent global pitch intermediate $0D80
    writer $F8D33E (post-write probe PC $F8D342)

DIVERGENCE TYPE:
    derived pitch state; upstream source [OPEN]

FINAL FC WRITER:
    $FC619E MOVEP through $FC602E

$0CE3 CONNECTION:
    [OPEN]

RATE-RATIO CONNECTION:
    FC ratio is numerically consistent; provenance/intent [OPEN]

ARCHITECTURE MODEL:
    C, partially supported

PHYSICAL CLOCK ROUTING / ES5701:
    [OPEN]
```
