# Voice pitch-mode source — ASR-10 V3.50

## Question and bounded target

This investigation moves the known reversible pitch offset exactly one step
upstream.  `$F8D33C` adds voice-record `$86(A4)` immediately before
`$F8D33E -> $0D80`; the question is which direct local input produces
`$86(A4) = $B5B0 / $AEE2 / $B5B0`.

No clock, ES5506, ACTV, ES5510, ES5701, PB3, routing, or C++ behaviour was
changed.  The backtrack stops at the first indirect reversible source.

## Established control

The prior successful-effect A/B/A control was retained for two note fixtures:

| state | effect | `$0CE3` | ACTV |
|---|---|---:|---:|
| A | ROM HALL REVERB | `$00` | `$1F` |
| B | 44LUSH PLATE | `$01` | `$17` |
| A2 | ROM HALL REVERB | `$00` | `$1F` |

The same selected instrument/sample note path was used for 3C and 3D.  A
retained write tap on `$815E` only accepted post-store PC `$007988`, which
identifies the static store `$007984: move.w D0,$86(A4)`.  That is an
execution-correlated writer witness, not a memory-fetch claim.

## Local static block and exact arithmetic

The straight-line block that creates `$86(A4)` is:

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

The final correlated store witnesses this uninterrupted local path.  Operand
snapshots are taken at that real store; they classify values, not individual
memory reads as instruction execution.

All observed values reconstruct exactly as 16-bit arithmetic:

```text
entry D0 = $0000
after ext/double = $0000
`$AA(A3)` = $4801; `sub.b D1,D1` leaves word D1 = $4800
$0000 - $4800 + $8BA5 = $43A5

mode A/A2: $43A5 - $8DF5 = $B5B0
mode B:    $43A5 - $94C3 = $AEE2
```

There is no signed/unsigned ambiguity in the reconstruction: each result is
the observed stored 16-bit word modulo `$10000`.

## Runtime operand differential

| stage | PC | A | B | A2 | classification |
|---|---|---:|---:|---:|---|
| entry D0 | `$007962` input, reconstructed | `$0000` | `$0000` | `$0000` | COMMON |
| doubled D0 | `$007964` | `$0000` | `$0000` | `$0000` | COMMON |
| `$AA(A3)` | `$007966` | `$4801` | `$4801` | `$4801` | COMMON |
| byte-cleared D1 word | `$00796A` | `$4800` | `$4800` | `$4800` | COMMON |
| `$110(A3)` | `$007970` | `$18` | `$18` | `$18` | COMMON |
| table index | `$007974` | `$0030` | `$0030` | `$0030` | COMMON |
| selected ROM word | `$00797C`, `$FFF87426` | `$8BA5` | `$8BA5` | `$8BA5` | COMMON |
| RAM subtrahend | `$007980`, `$0D66` | `$8DF5` | `$94C3` | `$8DF5` | MODE-DEPENDENT |
| final D0 / `$86(A4)` | `$007984` | `$B5B0` | `$AEE2` | `$B5B0` | MODE-DEPENDENT |

The source is the first and only mode-dependent direct input in this local
block.  Its B-minus-A change is `$94C3 - $8DF5 = +$06CE = +1742`; because the
block subtracts it, the stored output changes by exactly `-1742` pitch units.

## Two-note control

| field | 3C A | 3C B | 3C A2 | 3D A | 3D B | 3D A2 |
|---|---:|---:|---:|---:|---:|---:|
| `$0D66` | `$8DF5` | `$94C3` | `$8DF5` | `$8DF5` | `$94C3` | `$8DF5` |
| final `$86(A4)` | `$B5B0` | `$AEE2` | `$B5B0` | `$B5B0` | `$AEE2` | `$B5B0` |

`$0D66` has the same `+$06CE` mode delta for both notes.  Thus the source is
note-independent in this two-note control; no LFO-shaped variation was
observed in this note-setup value.

## First indirect source and immediate writer

`$0D66` is persistent RAM, not a note-local literal.  A retained narrow write
tap captured its writer with post-store PC `$F8CCDA`, identifying static
`$F8CCD6: move.w D2,$0D66.w`:

| reconfiguration phase | writer PC | value |
|---|---:|---:|
| A / boot mode 0 | `$F8CCD6` | `$8DF5` |
| B commit | `$F8CCD6` | `$94C3` |
| A2 commit | `$F8CCD6` | `$8DF5` |
| second B / A2 control | `$F8CCD6` | `$94C3 -> $8DF5` |

The local static window contains a B-path `move.w #$94C3,D2` before the
store, but this pass does not trace the corresponding mode-0 D2 producer or
the branch/control source.  Therefore `$0D66` is classified conservatively as
the **first indirect, reversible mode-dependent persistent RAM source**; the
writer's upstream D2 provenance is intentionally open.

## Hypothesis verdicts

| hypothesis | verdict | evidence |
|---|---|---|
| H1 `$86(A4)` uses a small local set of inputs | SUPPORTED | complete straight-line operand reconstruction |
| H2 one early input accounts for the offset | SUPPORTED | only `$0D66` differs; subtraction yields exact -1742 |
| H3 first divergent input is note-independent | SUPPORTED | same `$0D66` values/delta for 3C and 3D |
| H4 first source is persistent/precomputed state | SUPPORTED | `$0D66` is written during reconfiguration before notes |
| H5 direct `$0CE3` edge in this chain | OPEN | no such operand/read was reached in this bounded path |
| H6 direct ACTV edge in this chain | OPEN | no ACTV-related read was reached |
| H7 `$86` stores rate-dependent pitch compensation | PARTIALLY SUPPORTED | behaviour matches the pitch offset; semantic intent/rate provenance remain open |

## Architectural implication and stop

The verified firmware path has moved one bounded step:

```text
effect-mode-correlated reconfiguration [upstream OPEN]
  -> `$0D66` `$8DF5/$94C3` [Verified runtime]
  -> `$007980` subtract
  -> voice `$86(A4)` `$B5B0/$AEE2`
  -> `$F8D33C` add
  -> `$0D80` -> FC
```

This does not identify `$0D66` as a sample-rate coefficient, prove a direct
`$0CE3` or ACTV relation, or justify any MAME model change.  Physical clock
routing and ES5701 remain `[OPEN]`.

## Single next experiment

If another provenance step is warranted, capture only the branch and D2
producer immediately feeding `$F8CCD6` during the same A/B/A control, then
stop at the first mode-dependent condition or input.  Do not resume a broad
`$0CE3` search or any clock investigation.
