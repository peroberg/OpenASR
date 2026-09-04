# Effect-mode construction semantics — ASR-10 V3.50

## Question

The prior destination-only probe correctly established that installing loaded
`44LUSH PLATE` makes `$0062B666=$01`, but it stopped because its two PC
snapshots did not identify a source operand.  This pass asks the narrower
structural question left open by that limitation: is the byte copied directly
from serialized effect data, transformed by ROM/OS, or derived from runtime
context?

The target remains only the loaded current-effect object field:

```text
current-effect object `$0062B600` + `$66` = `$01`
  -> `$0CE3`
```

No ACTV, pitch, ES5510, clock, or file-system semantics are reconsidered.

## Established boundary and control

The committed-state witnesses remain:

| state | effect | current object | `+$66` | `$0CE3` |
|---|---|---:|---:|---:|
| A | ROM HALL REVERB | `$FFF9B626` | `$00` | `$00` |
| B | loaded 44LUSH PLATE | `$0062B600` | `$01` | `$01` |
| A2 | ROM HALL REVERB | `$FFF9B626` | `$00` | `$00` |

This run used B as the primary construction witness.  It followed the normal
physical panel path `Load -> Effects -> Enter/Yes`; the selection display was
`FILE 16 44LUSH PLATE`, and the final non-display state was
`$0E92=$0062B600`, `$0CE3=$01`, `$0062B666=$01`.

## Construction observation

A temporary Lua probe retained a write tap over only
`$0062B600-$0062C3FF`, plus a live tap over the standard MC68302 IDMA register
window `$FC6800-$FC681F`.  It captured 3,596 writes during the B installation.
The object starts with a short CPU-visible allocation/setup phase, then is
populated as a monotonic byte-lane stream.  The relevant local sequence was:

| object offset | high-lane value | low-lane value | final word |
|---:|---:|---:|---:|
| `$60` | `$20` | `$20` | `$2020` |
| `$62` | `$00` | `$00` | `$0000` |
| `$64` | `$82` | `$83` | `$8283` |
| `$66` | `$01` | `$00` | `$0100` |
| `$68` | `$00` | `$00` | `$0000` |
| `$6A` | `$00` | `$00` | `$0000` |

Thus `$0062B666` receives the target byte as the high byte of the sequential
object population, not as a later isolated field patch.  The immediately
preceding header/name bytes are likewise streamed into their matching offsets.

The probe's CPU PC samples rotate through scheduler/interrupt-return addresses
(`$F87F98`, `$F87F9C`, `$F87FCA`, and neighbours) while adjacent bytes arrive.
Existing scheduler evidence identifies this region as saved-context/return
machinery.  Therefore those samples are live context but **not** PC-correlated
evidence of a CPU copy instruction.  In particular, this corrects the prior
temptation to treat `$F87F9C/$F87FCA` as writer PCs.

The standard `$FC6800-$FC681F` IDMA-arm tap had a live zero count during this
effect-installation window.  That negative result is narrow: it rules out only
an observed *standard MC68302 IDMA arm at that register window* in this run.
It does not identify the bus-master/source responsible for the streamed RAM
writes, and it does not rule out another loader transfer mechanism.

## Source/destination evidence

```text
unknown construction transfer/source
  -> byte-lane stream over current-effect RAM object
  -> `$0062B666` high lane = `$01`
  -> current-effect `+$66` = `$01`
  -> `$0CE3` = `$01`                 [established earlier]
```

The byte stream proves that the B object is populated during installation, but
it exposes neither a source effective address nor arithmetic/branch logic that
sets the `$01`.  A streamed destination is compatible with all three live
possibilities:

* serialized data copied directly;
* serialized data first transformed by ROM/OS and then streamed; or
* a runtime-derived object serialized into the destination stream.

Consequently construction semantics cannot honestly be promoted beyond
`UNKNOWN` yet.

## ROM versus loaded effects

ROM HALL and loaded 44LUSH are not forced into a false symmetric construction
claim.  The former is selected in place from ROM (`$FFF9B626`); the latter is
installed into RAM (`$0062B600`).  They converge at the already verified
current-object read contract `*(A1+$66) -> $0CE3`, but this run neither proves
nor requires identical upstream construction paths.

## Classification and hypotheses

| item | result |
|---|---|
| B `+$66` population | verified streamed object write |
| source representation | `UNKNOWN` |
| construction class | `UNKNOWN` |
| CPU writer PC | not established; scheduler PC samples are not writer evidence |
| standard IDMA arm at `$FC6800` | not observed in this installation window |

| hypothesis | verdict | reason |
|---|---|---|
| H1 `+$66` is direct serialized metadata | OPEN | the destination stream has no source address/value witness |
| H2 `+$66` is derived from serialized metadata | OPEN | no transformation or input was observed |
| H3 `+$66` is runtime/context-derived | OPEN | a populated RAM object alone does not distinguish derivation from copy |
| H4 loaded 30k/44.1k effects share one construction rule | OPEN | only B's loaded path was measured; ROM HALL is a different object class |
| H5 ROM and loaded effects converge to identical runtime object semantics | SUPPORTED | both supply the same current-object `+$66 -> $0CE3` contract, not necessarily the same loader path |
| H6 `+$66` is explicit sample-rate metadata | OPEN | object construction does not establish the field's original semantic name |

## Corrected boundary and handoff decision

The previous conclusion that the current object was necessarily the final safe
firmware boundary was too strong.  This pass safely establishes one additional
fact: B's `+$66` is part of an installation-time object stream.  It does **not**
yet establish a serialized source byte, a transformation, or runtime derivation.

```text
FILE-FORMAT HANDOFF JUSTIFIED: NOT YET
```

The smallest discriminating next experiment is not a file-format parser or a
general loader audit: identify the producer/bus-master of this one bounded RAM
stream and capture its source-side address/value at the `$66` iteration.  If
that source is a persistent loaded representation, the result can then classify
`SERIALIZED_DIRECT`; if ROM/OS logic supplies it, the local transformation can
be classified instead.  Until that producer identity is observed, continuing
into file sectors or inventing a file field would be speculation.

## What remains open

* whether the `$01` originates in serialized effect data;
* whether ROM/OS transforms effect metadata into this field;
* whether contextual runtime state contributes;
* the semantic name beyond effect-selected operating/ACTV class;
* physical rate/clock routing, PB3 meaning and ES5701 involvement.

**MODEL CHANGE JUSTIFIED: NO.**
