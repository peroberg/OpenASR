# Effect-mode metadata source — ASR-10 V3.50

## Question and stop boundary

The verified pitch and ACTV paths both consume `$0CE3`.  This investigation
follows only the immediate upstream boundary:

```text
effect object byte +$66 -> `$0CE3`
```

It asks whether the byte is direct object metadata or a derived descriptor
value.  It stops at the first stable object field; it does not reverse the
loader, file format, ES5510 upload, clock path, or downstream consumers.

## Established writer/read contract

The prior ACTV provenance established the runtime RAM operation at `$013060`
as the current-effect pointer/read boundary:

```text
move.l A1,$0E92.w
move.b ($66,A1),$0CE3.w
```

The important correction of terminology is that `$0E92` is a **current-effect
object pointer**.  At this boundary there is no separate write that fills a
new descriptor `+$66`; the firmware stores/uses the object base and reads the
byte already at `A1+$66` directly into the runtime mode byte.

The retained write tap on `$0CE2-$0CE3` observed post-write PC `$013066`.
At each selected committed-object event it captured A1, `$0E92`, `A1+$66`,
the raw byte at that address, and the byte being stored.  This is a
PC-correlated sink witness; the source read is identified from the established
static instruction rather than promoted from a memory tap alone.

## A/B/A2 object-field witness

| state | effect | A1 / `$0E92` object base | source address `+ $66` | source byte | `$0CE3` result |
|---|---|---:|---:|---:|---:|
| A | ROM HALL REVERB | `$FFF9B626` | `$FFF9B68C` | `$00` | `$00` |
| B | 44LUSH PLATE | `$0062B600` | `$0062B666` | `$01` | `$01` |
| A2 | ROM HALL REVERB | `$FFF9B626` | `$FFF9B68C` | `$00` | `$00` |

The newly captured B event had A1=`$0062B600`, `$0E92=$0062B600`, source
`$0062B666=$01`, and store result `$01`.  The A2 event had
A1=`$FFF9B626`, `$0E92=$FFF9B626`, source `$FFF9B68C=$00`, and store result
`$00`.  The established initial-A and prior A/B/A2 pointer witness supplies
the matching first-A row.  Transient object reads during loading were not
treated as the selected committed object; only events whose A1 and `$0E92`
matched the A/B/A2 current-effect identities enter the table.

## Exact mapping and classification

```text
source object base A1 == current-effect pointer `$0E92`
source byte       = *(A1 + $66)
transformation    = byte copy
destination        = `$0CE3`
```

Classification of the first source is:

| property | result |
|---|---|
| source type | `DIRECT_METADATA` |
| source behaviour | `EFFECT_DEPENDENT`, reversible A/B/A2 |
| structural field | effect object `+$66` |
| verified semantic minimum | effect-selected operating/ACTV class metadata |
| not established | explicit physical sample-rate, clock selector, or ES5701 field |

## Hypothesis verdicts

| hypothesis | verdict | reason |
|---|---|---|
| H1 `+$66` comes directly from persistent effect-object metadata | SUPPORTED | A1/$0E92 points to the object and its raw `+$66` byte is copied to `$0CE3` |
| H2 same field gives A=`00`, B=`01`, A2=`00` | SUPPORTED | committed-object table above |
| H3 field is effect-dependent, not note-dependent | SUPPORTED | selected during effect-object resolution before note setup; A/B/A2 tracks effect identity |
| H4 field represents effect/audio operating mode | PARTIALLY SUPPORTED | it drives verified ACTV and pitch-class branches; full semantic label remains conservative |
| H5 field is explicit 30k/44.1k sample-rate metadata | OPEN | class correlation does not establish exact field meaning |
| H6 field is dynamically calculated rather than stored | UNSUPPORTED at this boundary | runtime directly copies stored object byte; earlier object construction is outside scope |

## What this establishes

The verified bounded provenance is now:

```text
ROM HALL object `$FFF9B626` + `$66` = `$00`
44LUSH object   `$0062B600` + `$66` = `$01`
                  | direct byte copy
                  v
                `$0CE3`
                  | already verified consumers
                  +-> ACTV class
                  `-> global pitch-offset class
```

This is direct object metadata provenance to `$0CE3`; no firmware-RAM polling
or effect-name special case is involved.

## What this does not establish

- The semantic name of object `+$66` beyond effect-selected operating/ACTV
  class.
- The object format's complete header/parser or the source of its loaded RAM
  representation.
- A physical rate/frame clock, ACTV-to-clock causality, PB3 identity, or any
  ES5701 responsibility.

**MODEL CHANGE JUSTIFIED: NO.**  The metadata path is stronger, but physical
timing remains a separate evidence domain.

## Single next experiment

If another provenance step is needed, compare only the immediate effect-object
construction/copy that makes `+$66` available for the B RAM object and stop
at the first object-format/header field.  Do not expand into loader-wide or
clock analysis.
