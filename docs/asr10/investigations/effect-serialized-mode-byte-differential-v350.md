# Serialized effect mode-byte differential — ASR-10 V3.50

## Question

Does the persistent byte at serialized effect-object/file offset `+$66`
robustly distinguish independently established 30 kHz- and 44.1 kHz-class
effects?  This is a correlation test only.  It neither names the field
`sampleRate` nor revisits loader, transport, ACTV, pitch, or clock behaviour.

The direct-provenance anchor from the preceding pass is:

```text
44LUSH PLATE file +$66 = $01
  -> FDC / external IDMA (previously verified bounded window)
  -> current-effect object +$66 = $01
  -> $0CE3 = $01
```

## Independent class evidence and candidate inventory

The local V3.50 image has twelve type-`$0021` effect directory entries
(entries 16–27).  The ASR-10 Musician's Manual Appendix, *44 kHz Effect
Descriptions and Variations*, independently describes Version 2 as containing
"12 additional 44 kHz effect algorithms" and enumerates this same group.  It
also explicitly describes the first three members as 44 kHz plate effects.
Thus the group classification comes from the manual, not from `+$66` or from
the effect names.

All twelve start at their directory-established file/object base; no wrapper
offset is applied.  The following direct raw-image reads use each file base,
not a common absolute disk offset.

| V3.50 entry | effect | file start block | independent class evidence | serialized `+$62..+$6A` | `+$66` |
|---:|---|---:|---|---|---:|
| 16 | `44LUSH PLATE` | `$0BEE` | manual 12-effect 44 kHz group; individually described | `00 00 82 83 01 00 00 00 00` | `$01` |
| 17 | `44LUSH PLAT2` | `$0BF6` | manual 12-effect 44 kHz group; individually described | `00 00 82 83 01 00 00 00 00` | `$01` |
| 18 | `44PERC PLATE` | `$0BFE` | manual 12-effect 44 kHz group; individually described | `00 00 82 83 01 00 00 00 00` | `$01` |
| 19 | `44EQ+DDL` | `$0C06` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 20 | `44DDL+CH+REV` | `$0C10` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 21 | `44DDL+CHORUS` | `$0C1F` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 22 | `44DLYLFO+REV` | `$0C26` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 23 | `44EQ+DDL+CHO` | `$0C30` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 24 | `44PARAM EQ` | `$0C3F` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 25 | `44EQ+REVERB` | `$0C45` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 26 | `44ROTO+REVRB` | `$0C51` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |
| 27 | `44EQ+ROT+DDL` | `$0C5C` | manual 12-effect 44 kHz group | `00 00 82 83 01 00 00 00 00` | `$01` |

The neighbour window is an alignment control only.  Its common contents show
that `+$66` is being compared at the same logical object offset in each file;
this pass assigns no semantics to the neighbouring bytes.

## Differential result

| independently established class | serialized cases available locally | observed `+$66` |
|---|---:|---|
| 44.1 kHz | 12 | all `$01` |
| 30 kHz | 0 serialized disk-effect objects | no control value available |
| ROM-only control | ROM HALL runtime object previously observed as `$00` | not included in serialized dataset |

There is no counterexample in the accessible serialized 44.1 kHz cohort.
However, the requested two-class differential cannot be completed: the
available V3.50 disk contains this documented 44 kHz addition set, but no
independently classifiable, loaded serialized 30 kHz effect object.  ROM HALL
REVERB remains useful runtime evidence but does not share this verified
serialized file/object representation, so using it as the missing negative
file control would be circularly over-broad.

## Hypotheses

| hypothesis | verdict | reason |
|---|---|---|
| H1 `+$66` is effect-dependent persistent metadata | PARTIALLY SUPPORTED | direct persistence is verified for 44LUSH and the same serialized position exists in twelve distinct files; this one-valued cohort does not test value variation across classes |
| H2 all verified 30 kHz-class serialized effects have `+$66=$00` | OPEN | no independently classified serialized 30 kHz control is locally available |
| H3 all verified 44.1 kHz-class serialized effects have `+$66=$01` | SUPPORTED | 12/12 independently manual-classified V3.50 44 kHz effects have `$01` |
| H4 `+$66` is a robust binary 30 kHz/44.1 kHz classifier | OPEN | positive class is strong; required serialized negative class is absent |
| H5 `+$66` has explicit sample-rate semantics | OPEN | even perfect future binary correlation would not by itself establish the field's authored semantic name |
| H6 `+$66` is a broader operating/voice/rate mode | PARTIALLY SUPPORTED, unchanged | previous firmware evidence shows its runtime copy feeds both ACTV-class and pitch setup; this file-only differential does not distinguish that from a narrower rate label |

## Safe status

The safe current name is **serialized effect operating-mode byte** or
**candidate rate-class field**.  The V3.50 44 kHz cohort makes `$01` a strong
positive association, but neither `$00 -> 30 kHz` nor an explicit
`sample-rate` field name is verified.

```text
serialized +$66 = $01
  for all 12 locally available, independently manual-classified
  V3.50 44 kHz effect files

serialized +$66 = $00 for an independently classified loaded 30 kHz
effect file: OPEN
```

No loader, FDC, IDMA, runtime-object, ACTV, pitch, ES5506, or physical-clock
claim changes in this pass.

## Single next experiment

Obtain or mount one independently documented **loaded serialized 30 kHz
effect file** that uses the same logical effect-object layout, then perform one
bounded file-base `+$62..+$6A` extraction.  A `$00` result would complete the
minimum two-class differential only after a second independent 30 kHz control;
a `$01` result would be a counterexample requiring verification and an
immediate stop.

```text
MODEL CHANGE JUSTIFIED: NO
PHYSICAL RATE/CLOCK: OPEN
```
