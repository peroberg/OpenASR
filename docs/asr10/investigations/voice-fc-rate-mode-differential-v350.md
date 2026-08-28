# ASR-10 V3.50: voice FC differential across committed effect modes

## 1. Question and boundary

This is a read-only A -> B -> A observation of one established MIDI-note
path.  It asks whether V3.50 programs the same selected instrument/sample
differently when the committed effect mode changes.  It does **not** change an
ES5506 clock, ACTV semantics, PB3, ES5510, ES5701, serial routing, or any
other emulated behavior.

The immediate reason is the rejected ACTV effective-clock experiment:
changing the clock from the ACTV value alone moved the known note from 262.3
to 196.7 Hz (x0.750).  That failure remains valid; this experiment tests a
separate possible contributor, firmware voice programming.

## 2. Preconditions and method

The temporary Lua probe retained two program-space taps throughout its live
window: ES5506 host writes at `$FC2000-$FC207F`, and the known ES5510 host
readback comparison window.  It used only physical panel press/release events
and the established `noteon.mid` fixture (`90 3C 64`).

The sequence was:

```text
boot -> load/select JM DIGI SYN Instrument 1 -> note A
     -> Load -> Effects -> Enter (44LUSH PLATE commit) -> note B
     -> FX Select -> Up -> Enter (ROM-01 HALL REVERB commit) -> note A2
```

The effect commits are positive non-display witnesses: B had 549 matching
ES5510 compare-window reads and A2 had 822, both with zero mismatch, error
`$00C0=00`, and retry `$0E8C=00`.

## 3. Committed state and musical-object identity

| field | A | B | A2 | status |
|---|---:|---:|---:|---|
| effect | ROM HALL REVERB | Bank 44LUSH PLATE | ROM HALL REVERB | `[Verified runtime]` |
| `$0CE3` | `00` | `01` | `00` | `[Verified runtime]` |
| ES5506 ACTV | `$1F` | `$17` | `$1F` | `[Verified runtime]` |
| slots | 32 | 24 | 32 | `[Verified ES5506 semantics]` |
| selected instrument state | `$1098/$01` | `$1098/$01` | `$1098/$01` | same firmware state observed |
| MIDI input | `$90 $3C $64` | `$90 $3C $64` | `$90 $3C $64` | same fixture |
| programmed voice | 1 | 1 | 1 | `[Verified runtime]` |
| CR | `$4018` | `$4018` | `$4018` | same |
| bank | 1 (from CR) | 1 | 1 | same |
| START | `$0FE8A800` | `$0FE8A800` | `$0FE8A800` | same |
| END | `$153FF980` | `$153FF980` | `$153FF980` | same |
| ACCUM | `$0B278000` | `$0B278000` | `$0B278000` | same |

Thus this is the same selected runtime instrument state and the same observed
voice/sample address geometry.  The wavesample-header root key and fine-tune
fields are not yet decoded anywhere in the established note path; they are
therefore `[OPEN]`, rather than silently inferred from the matching geometry.
No observation indicates a changed instrument, layer, sample bank, start,
end, or accumulator between the three notes.

## 4. Raw voice-state differential

The low-page words are raw host-register values.  `H4` through `H14` were all
zero in the 350-ms captured endpoint; they are omitted from the table because
they do not differ.  ES5506 has no separately named per-voice loop-start/end
host registers in this observed write set; loop-related semantics must not be
invented from the high-page zeros.

| Field | A mode 0 | B mode 1 | A2 mode 0 | classification |
|---|---:|---:|---:|---|
| FC endpoint | `$00385` | `$0024F` | `$00369` | phase-dependent endpoint; see FC range below |
| CR | `$4018` | `$4018` | `$4018` | SAME |
| LVOL / RVOL | `$D8F0 / $D8F0` | `$D8F0 / $D8F0` | `$D8F0 / $D8F0` | SAME |
| LVRAMP / RVRAMP | `$0000 / $0000` | `$0000 / $0000` | `$0000 / $0000` | SAME |
| ECOUNT | `$0165` | `$01FF` | `$0165` | MODE-DEPENDENT |
| K2 | `$F8E0` | `$FB20` | `$F8E0` | MODE-DEPENDENT |
| K2RAMP | `$0001` | `$0001` | `$0001` | SAME |
| K1 / K1RAMP | `$FFF0 / $0001` | `$FFF0 / $0001` | `$FFF0 / $0001` | SAME |
| START / END / ACCUM | see §3 | see §3 | see §3 | SAME |

The endpoint FC values are not valid fixed pitch constants.  FC updates about
every 11--12 ms after note-on (a pre-existing documented vibrato/LFO
behavior).  A and A2 began their MIDI delivery at different points in that
wave, so their endpoint snapshots differ without invalidating the repeated
mode-0 configuration.

| FC measure over the captured update window | A | B | A2 |
|---|---:|---:|---:|
| first observed nonzero FC | `$38C` (908) | `$262` (610) | `$382` (898) |
| minimum | `$369` (873) | `$24D` (589) | `$369` (873) |
| maximum | `$397` (919) | `$26C` (620) | `$396` (918) |
| range midpoint | 896.0 | 604.5 | 895.5 |
| captured arithmetic mean | 898.233 (30 writes) | 603.690 (29) | 893.966 (29) |

The extrema and shape return in A2; only phase/time alignment differs.  The
reversible mode-class result is therefore the FC **range/midpoint**, not one
late register snapshot.

## 5. Current MAME output measurement

The unmodified driver still configures ES5506 with 30,476,180 Hz.  Generic
ES5506 code derives `clock / (16 * (ACTV + 1))`; it therefore produces the
following current-model stream rates, not physical-ASR rate measurements:

| phase | ACTV | current MAME stream rate | `-wavwrite` dominant frequency |
|---|---:|---:|---:|
| A | `$1F` | 59,523.7890625 Hz | 262.3 Hz |
| B | `$17` | 79,365.0520833 Hz | 235.3 Hz |
| A2 | `$1F` | 59,523.7890625 Hz | 260.9 Hz |

The same established autocorrelation method as `note_audio_wav` was applied
to each printed note-on time.  A/A2 differ by 0.53%, consistent with the
short-window LFO phase; both remain in the established 230--290-Hz acceptance
band.

## 6. Ratios and consequence

Using FC-range midpoints avoids treating the LFO phase as a mode flag:

```text
FC B/A       = 604.5 / 896.0       = 0.674665
FC B/A2      = 604.5 / 895.5       = 0.675042
documented low/high rate ratio
             = 29,761.8945/44,100  = 0.674873

MAME stream B/A = 79,365.0521 / 59,523.7891 = 1.333333
measured Hz B/A = 235.3 / 262.3 = 0.897064
FC midpoint B/A * stream B/A = 0.899553
```

The FC midpoint relation is within about 0.03% of the documented low/high
rate ratio.  Its product with the current generic MAME stream-rate ratio is
within about 0.28% of the independently measured B/A output-frequency ratio.
This is strong evidence that V3.50 mode-dependent voice programming and the
generic current-MAME ACTV rate both contribute to the observed pitch.

It does **not** explain the earlier 262.3 -> 196.7 x0.750 failure completely:
that was a different experiment which changed mode-0's effective clock while
holding the effect path otherwise fixed.  This result does explain why ACTV
alone was never a semantically sufficient system-pitch proxy: in normal
effect-mode operation FC and at least two envelope/filter fields change too.

## 7. FC write provenance and stop boundary

The initial write is observed with PC `$FC61A0`; subsequent periodic writes
have PC `$FC6030`.  The latter lies in the verified DPRAM MOVEP thunk
`$FC602E: movep.l D0,($08,A0)` with `A0=$FC2001`, i.e. the final FC host
write.  At every captured final write, D0 equals the raw FC word.  This is a
verified final-writer/source-register boundary:

```text
runtime pitch producer [not yet decoded]
    -> D0
    -> $FC602E MOVEP long at ES5506 +$08
    -> selected low-page voice FC
```

No earlier mode-dependent pitch input was verified in this bounded pass.  The
CPU's exposed SP was `$00000300` at the MOVEP callback and yielded no usable
caller return address; a read/write tap cannot promote that absence into a
call-chain claim.  Following the periodically updated pitch producer now
would be a separate pitch-engine investigation, so this pass stops here.

## 8. Hypotheses

| Hypothesis | verdict | evidence |
|---|---|---|
| H1: firmware programs mode-dependent FC for this same note | **SUPPORTED** | FC range/midpoint B differs from A/A2 while sample geometry is unchanged |
| H2: FC change is reversible A -> B -> A | **SUPPORTED** | mode-0 FC extrema/midpoint return; endpoint phase is intentionally not used as the oracle |
| H3: FC accounts for all/part of prior clock-proxy failure | **PARTIALLY SUPPORTED** | it quantitatively explains current A/B output ratio with ACTV stream change, but not the separate x0.750 low-mode clock experiment |
| H4: verified firmware mode -> FC-calculation dataflow | **OPEN** | final D0/MOVEP writer is verified; first mode-dependent producer is not |
| H5: ACTV alone is sufficient system rate/pitch proxy | **DISPROVEN** | ACTV transition coexists with reversible FC/ECOUNT/K2 programming changes |

## 9. Status and next experiment

**[Verified firmware/current-MAME runtime]** Effect mode changes the
programmed FC trajectory, envelope count and K2 for the measured MIDI-note
voice.  **[Verified current-MAME runtime]** the unmodified generic stream
rate changes 4/3 with ACTV, and the note output changes by the corresponding
FC-times-stream relation.

**[OPEN physical hardware]** actual audio clock routing, Y2/Y3 division or
muxing, ES5701 participation, PB3/LRCLK identity, and ES5510 execution/frame
timing.  This investigation adds none of those claims.

**Single best next experiment:** capture the *same mode-0 note* before and
after a controlled scheduler/audio-time alignment, then backtrack only the
initial FC producer if a stable first-write base value can be isolated from
the LFO.  Do not change a clock before that producer/phase contract is known.
