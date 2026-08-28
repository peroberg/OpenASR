# Effect-mode object construction boundary — ASR-10 V3.50

## Question and final firmware scope

The preceding pass established direct current-object metadata
`*(A1+$66) -> $0CE3`.  This deliberately final firmware-side pass asks only
whether the construction of B/44LUSH's RAM object can expose the immediately
preceding source byte for `$0062B666=$01` without entering loader or file-format
analysis.

It does not revisit the verified `$0CE3` consumers, ES5510 upload, pitch,
ACTV, clocks, or hardware routing.

## Stable boundary before the experiment

```text
current effect object B base `$0062B600`
  + `$66` = `$01`
  -> byte copy -> `$0CE3`
```

The corresponding ROM HALL object is `$FFF9B626+$66=$00`; the established
A/B/A2 object-state witness returns to that value after B.  Thus the target is
only the construction of B's loaded RAM byte.

## Narrow construction witness

A retained, aligned write tap covered exactly `$0062B666-$0062B667` while the
standard 44LUSH effect installation committed.  It had a live positive result:

| event | destination lane | data | mask | PC snapshot | result |
|---|---|---:|---:|---:|---|
| 1 | `$0062B666` high byte | `$0100` | `$FF00` | `$F87FCA` | establishes `+$66=$01` |
| 2 | `$0062B667` low byte | `$0000` | `$00FF` | `$F87F9C` | companion-byte write; does not alter `+$66` |

After the successful mode-1 commit, `$0E92=$0062B600`,
`$0062B666=$01`, and `$0CE3=$01`.  This verifies that the relevant B object
byte is established during the installation window, rather than inferred from
later display text.

## Why the source is intentionally not named

The two captured PC snapshots cannot be safely promoted to construction-writer
PCs.  The small static window around `$F87F9C/$F87FCA` does not itself expose
an unambiguous byte/block-copy instruction to `$0062B666`; it includes return,
condition and exception-adjacent control flow.  The retained destination tap
therefore proves the write and byte lane, but not a source effective address
or source register.  Treating A0--A3 register snapshots from that point as a
copy source would be speculation.

Recovering the source would require following the surrounding loader/trap or
object-construction machinery, precisely the broader file/parser domain that
this experiment excludes.  No broad trace was installed.

## Classification and verdicts

| item | result |
|---|---|
| current object `+$66` | `OBJECT_METADATA`, effect-dependent `[Verified]` |
| construction source before RAM object | `UNKNOWN` within this bounded firmware pass |
| source-to-destination copy mapping | not safely isolated |
| earliest safe firmware boundary | current-effect object `$0062B600+$66=$01` |
| next provenance domain | ASR effect-file/object format |

| hypothesis | verdict | reason |
|---|---|---|
| H1 B `+$66` comes from earlier stable object/header state | OPEN | the destination write is live, but source representation is not isolated |
| H2 `$01` is copied/extracted directly to B `+$66` | OPEN | lane write is observed; source operand is not safely known |
| H3 source is persistent metadata rather than runtime audio calculation | OPEN | resulting field is persistent, but upstream construction is outside the boundary |
| H4 A/B/A2 reproduces `$00/$01/$00` | SUPPORTED | established committed-object witness; B construction adds the live write |
| H5 source is explicit sample-rate metadata | OPEN | no source/header field was reached |
| H6 current effect object is the earliest safe representation before loader/file analysis | SUPPORTED | the next needed source is embedded in excluded construction machinery |

## Handoff and conclusion

The completed firmware provenance is:

```text
effect object `+$66` `$00/$01`
  -> `$0CE3`
  -> verified ACTV class and pitch-offset consumers
```

The earliest safe handoff for the B object is:

```text
ASR effect-file/object-format project
  target: serialized/header/object representation corresponding to
          loaded RAM `$0062B600+$66=$01`
```

**MODEL CHANGE JUSTIFIED: NO.**  This boundary provides no physical rate,
clock, Y2/Y3, PB3, ACTV-causality, or ES5701 evidence.

**Should this firmware provenance branch continue: NO.**  Another upstream
step would be loader/file-format reverse engineering rather than a bounded
firmware dataflow experiment.  The next single experiment belongs in the
file-format project: locate the serialized source byte/field that maps to a
loaded 44LUSH object at `+$66`, then compare it with a ROM-HALL `$00` object.
