# Pitch-mode D2 source — ASR-10 V3.50

## Question and scope

This is the single local producer step above the established `$0D66` pitch
input.  It asks why `$F8CCD6: move.w D2,$0D66.w` writes `$8DF5` in mode 0 and
`$94C3` in mode 1.  It does not investigate clocks, ACTV, ES5701, ES5506, or
the producer of the first mode condition beyond the point reached below.

## Established boundary

The prior two-note control established:

```text
$0D66 $8DF5 (A) -> $94C3 (B) -> $8DF5 (A2)
  -> $007980 subtract
  -> voice `$86(A4)` offset -1742
  -> `$0D80` -> FC
```

`$0D66` is pre-note reconfiguration state, so its A/B/A setup is the relevant
oracle.  The previous 3C/3D note witnesses independently show the same
`$0D66` values for both notes.

## Local static producer

The bounded ROM control block is:

```
$F8CC9A  tst.b   $0CE3.w
$F8CC9E  bne.s   $F8CCB6

; zero path (with local helper calls omitted here, not analysed)
$F8CCAA  moveq   #$1E,D0
$F8CCAC  move.w  #$8DF5,D2
$F8CCB0  move.w  #$0165,D3
$F8CCB4  bra.s   $F8CCCA

; nonzero path
$F8CCB6  jsr     $F8CC90
$F8CCBC  beq.s   $F8CCC0
$F8CCBE  bsr.s   $F8CC76
$F8CCC0  moveq   #$16,D0
$F8CCC2  move.w  #$94C3,D2
$F8CCC6  move.w  #$01FF,D3

; common tail
$F8CCD2  move.w  D0,$0D5E.w
$F8CCD6  move.w  D2,$0D66.w
```

The branch at `$F8CC9E` is the first local mode-dependent condition.  The
additional nonzero-path helper calls are not assigned a pitch or clock
semantic in this investigation; both paths reach the same D2 store.

## Runtime path and D2 reconstruction

A retained narrow write tap on `$0D66` accepted only post-store PC
`$F8CCDA`, the PC-correlated witness for static `$F8CCD6`.  It remained live
through the committed effect transitions:

| state | `$0CE3` at store | D0 | D2 | D3 | `$0D66` |
|---|---:|---:|---:|---:|---:|
| B, 44LUSH commit | `$01` | `$0016` | `$94C3` | `$01FF` | `$94C3` |
| A2, HALL restore | `$00` | `$001E` | `$8DF5` | `$0165` | `$8DF5` |

These are exact immediate-value loads in the static branches: no arithmetic
or width conversion occurs between the selected D2 literal and `$F8CCD6`.
The prior boot/mode-0 and two-note A/B/A captures supply the matching first-A
`$8DF5` witness.  The B/A2 event pairs are positive live checks that the tap
survived the full transition window.

## Direct input differential

| stage | PC | A / A2 | B | classification |
|---|---|---:|---:|---|
| mode byte | `$F8CC9A`, `$0CE3` | `$00` | `$01` | MODE-DEPENDENT |
| branch | `$F8CC9E` | fall through | branch to `$F8CCB6` | MODE-DEPENDENT control flow |
| D2 load | `$F8CCAC` / `$F8CCC2` | `#$8DF5` | `#$94C3` | MODE-DEPENDENT immediate result |
| D2 store | `$F8CCD6` | `$8DF5` | `$94C3` | MODE-DEPENDENT persistent state |

The direct source is therefore a **persistent current-effect mode byte**,
`$0CE3`, used directly as a branch condition.  It is already a verified
descriptor-derived class byte from the ACTV investigation; this run reaches it
naturally through D2 dataflow, rather than by a global consumer search.

## Two-note control

`$0D66` is written during effect reconfiguration before note setup.  The prior
3C and 3D A/B/A note fixtures both read `$8DF5 -> $94C3 -> $8DF5`, so the
selected branch/literal is note-independent for the two-note control.  No LFO
or note-dependent operand appears in this producer block.

## Hypothesis verdicts

| hypothesis | verdict | evidence |
|---|---|---|
| H1 D2 is locally produced from few inputs | SUPPORTED | `$0CE3` test selects immediate D2 loads and a common store |
| H2 one direct input/branch explains D2 difference | SUPPORTED | `$0CE3` controls `$8DF5` versus `$94C3` exactly |
| H3 first source is note-independent | SUPPORTED | same control applies before 3C and 3D setup |
| H4 first source is persistent effect/audio state | SUPPORTED | `$0CE3` is descriptor-derived current-effect state |
| H5 chain directly binds to `$0CE3` | SUPPORTED | static test/branch plus runtime reversible D2-store witnesses |
| H6 `$0D66` is a precomputed global pitch/rate compensation value | PARTIALLY SUPPORTED | it is global pre-note state with the established pitch arithmetic; semantic intent/physical rate remain open |

## Architectural implication

The bounded verified firmware chain is now:

```text
current effect descriptor +$66
  -> `$0CE3`
  -> `$F8CC9A/$F8CC9E` test/branch
  -> D2 `#$8DF5` / `#$94C3`
  -> `$F8CCD6` -> `$0D66`
  -> note-time `$007980` subtract
  -> voice `$86(A4)` -> `$0D80` -> FC
```

This verifies firmware provenance to the current-effect mode state.  It does
not show whether the byte denotes physical sample rate, does not connect ACTV
causally to pitch, and does not establish any physical clock, PB3, or ES5701
path.  **No model change is justified.**

## Remaining OPEN and single next experiment

The descriptor field's full semantic label remains conservative
(`effect-selected mode/ACTV class`); the physical sample/frame clock and the
firmware's intent for the pitch literals remain open.  If one further task is
warranted, compare only the descriptor `+$66` setting path and its immediate
effect-object source for A/B/A, then stop at the first object metadata field.
Do not make a clock-model task from this evidence alone.
