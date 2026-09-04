# ASR-10 V3.50 — firmware-visible audio-rate-control write census

## Question

Does the controlled `ROM HALL REVERB` (A, mode 0) -> `44LUSH PLATE`
(B, mode 1) -> ROM HALL REVERB (A2) transition perform a further
firmware-visible hardware write or MC68302/SIB state change that could be a
board-level audio-clock/frame control?

This is a **write census**, not an audio-clock implementation experiment.  It
does not identify a clock mux, oscillator, divider, ES5701 role, or physical
receiver behind a chip select.

## Evidence discipline

`[Verified runtime]` below means a retained Lua write tap was live through the
whole transition and the same run contained the independently established
effect-object and ACTV witnesses.  A CPU-PC sampled while a memory tap fires
is not, by itself, claimed as CPU writer provenance: another bus master can
write RAM while the CPU has that PC.  The only positive execution attribution
reused here is the already independently established ACTV store at `$00E826`
(tap post-PC `$00E82A`).

The temporary probe was kept outside the tree and removed before commit.  Its
raw logs remain outside the tree for this handoff:

```text
/private/tmp/asr10-rate-write-census-r{4,5}.log
```

They are reproducibility aids, not committed tooling.  The probe retained all
tap references and printed `RATE_LIVE_PROBE taps_installed=22` before either
transition.

## Firmware and media artifacts

The locally supplied research artifacts were read only.  They match the
MAME-loaded artifacts byte-for-byte (`cmp` exit 0):

| Artifact | Size | SHA-256 |
|---|---:|---|
| `resources/FW/asr-648c-lo-1.5b.bin` | 131,072 | `94312f142db5b307bf31fbcc9b46e54197460f691596465d24870aa579a892fc` |
| `resources/FW/asr-65e0-hi-1.5b.bin` | 131,072 | `d3f4863d74b44e85de69432e1618dc9b1df7dcdefe7c79f81af2b7990e0e16eb` |
| `resources/OS/V350.img` | 1,638,400 | `2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b` |

The resource directory is external to this worktree; its two ROM halves are
identical to `roms/asr10booth/` and `V350.img` is identical to
`floppies/asr10booth/V350.img`.

## Controlled workflow and live witnesses

The normal panel path first loaded B, selected and committed ROM-01 HALL for
A, then loaded B again and selected ROM-01 for A2.  Two independent fully
controlled runs had the same timing and state points; each reported:

| Phase | emulated time | current object | `$0CE3` | stable ACTV witness |
|---|---:|---:|---:|---:|
| A | 23.320000 s | `$FFF9B626` / ROM-01 HALL | `$00` | established `$1F` |
| B | 28.080000 s | `$0062B600` / 44LUSH loaded object | `$01` | `$17` at `$00E826` |
| A2 | 30.940000 s | `$FFF9B626` / ROM-01 HALL | `$00` | `$1F` at `$00E826` |

At each commit the known ACTV reset/reprogram sequence was observed:

```text
A -> B:  $F8D006: $1F,  $00E7FA: $00,  $00E82A: $17
B -> A2: $F8D006: $1F,  $00E7FA: $00,  $00E82A: $1F
```

The PC column is the tap's post-access sample; `$00E82A` is the existing
instruction-boundary witness for store `$00E826`.  This establishes that the
census observed the intended mode transition, rather than merely UI traffic.

## Scope of the census

The retained probe observed after A was established:

- SIB: `SCR`, `PACNT`, `PADDR`, `PADAT`, `PBCNT`, `PBDDR`, `PBDAT`, and
  `BR0/OR0` through `BR3/OR3`;
- all of CS1, `$FF6000-$FF7FFF`;
- all of CS2, `$FC2000-$FC3FFF`, separating mapped ES5506 and ES5510 host
  registers from static-map holes;
- static `.ram()` holes in CS3: `$FC4004-$FC47FF`, `$FC4820-$FC4FFF`, and
  `$FC5020-$FC5FFF`.

The census does **not** claim those holes are RAM physically.  It asks whether
the current firmware writes them while committing the mode switch.

## Reduction

| Address/signal | A | B | A2 | Transition writes | Current MAME classification | Evidence classification | Candidate? |
|---|---|---|---|---|---|---|---|
| `PACNT` | `E000` | `E000` | `E000` | none | SIB PIO control | observed unchanged | no |
| `PADDR` | `FBFF` | `FBFF` | `FBFF` | none | SIB PIO direction | observed unchanged | no |
| `PADAT` | `1878` | `1878` | `1878` | transient low-byte `60/68/78` in both windows | SIB Port A data | no stable reversible value | no |
| `PBCNT` | `0000` | `0000` | `0000` | none | SIB PIO control | observed unchanged | no |
| `PBDDR` | `0000` | `0000` | `0000` | none | SIB Port B direction | observed unchanged | no |
| `PBDAT` | `0015` | `0015` | `0010` | repeated active values `$10-$1F` in both windows | SIB Port B data | timing-dependent service traffic; no A/B/A persistent bit | no |
| `SCR` | `0002` | `0002` | `0002` | `$0D51` then `$0002` only on disk-loading A->B | SIB system control | non-reversible load traffic | no |
| `BR0/OR0` | `1F01/3F82` | same | same | none | CS0 ROM | observed unchanged after boot | no |
| `BR1/OR1` | `1FEF/FFFE` | same | same | none | CS1 selector | observed unchanged after boot | no |
| `BR2/OR2` | `1F85/FFFC` | same | same | none | CS2 selector | observed unchanged after boot | no |
| `BR3/OR3` | `1F89/7FFC` | same | same | none | CS3 selector | observed unchanged after boot | no |
| CS1 `$FF7F06..$FF7FB6` | `$0007`, 23 stride-8 addresses | — | `$0007`, same first 23 plus `$FF7FBE..$FF7FF6` | word writes, sampled PC `$F8CF00` | write-only external CS1 | slot-count-sized helper initialization, not a two-value control | no |
| ES5506 mapped registers | normal initialization; ACTV ends `$17` | `$17` | ACTV ends `$1F` | expected OTTO initialization and known ACTV | ES5506 | known traffic | no new candidate |
| ES5510 host/register windows | upload / verify traffic | upload / verify traffic | upload / verify traffic | variable payload and count | ES5510 | known effect program transfer | no |
| CS2 unresolved holes | — | — | — | **zero writes** in all three runs | static `.ram()` fallback | observed negative result | no |
| CS3 unresolved holes | — | — | — | **zero writes** in final full-range run | static `.ram()` fallback | observed negative result | no |

`PBDAT` cannot be called a stable register-state comparison: the tap observed
continuous values while service code executed, and the final A2 snapshot
landed at a different point in that activity.  Crucially, neither its sampled
stable values nor its write value set supplied `X -> Y -> X` mode behavior.

The CS1 extent is informative but not a rate-control candidate.  A->B writes
through `$FF7FB6` (23 stride-8 locations); B->A2 writes the same value to
those locations and nine more through `$FF7FF6` (32 locations).  Existing
firmware evidence maps this structure to the per-voice `+$2A` helper/output
pointer family.  It is therefore consistent with 24/32-slot reinitialisation,
not with a reversible board control value.  Its physical receiver remains
`[OPEN]`.

## Conclusion

**[Verified runtime / negative result]** In the observed V3.50 A->B->A
effect commit, no additional reversible firmware-visible hardware state was
found outside the already established ES5506 ACTV and firmware pitch/effect
programming paths.

Specifically, the post-boot MC68302 CS configuration is invariant; neither
Port A nor Port B supplies a stable mode-correlated output bit; CS1 supplies
slot-scaled helper writes rather than a two-state value; and the full CS2 plus
the audited unresolved CS3 holes contain no non-ES5506/non-ES5510 write
candidate.  This does **not** prove that physical ASR hardware lacks a rate
control.  It only rules out a firmware-visible write/state in these observed
domains and transition windows.

No production model change is justified.

## Status

| Claim | Status |
|---|---|
| A/B/A effect mode and ACTV transition | [Verified] |
| BR/OR as runtime rate selector | [DISPROVEN for the observed transition] |
| SIB PIO stable mode-control bit | [DISPROVEN for the observed transition] |
| CS1 reversible mode-control write | [DISPROVEN for the observed transition] |
| unresolved CS2/CS3 write candidate | [DISPROVEN for the observed transition] |
| physical rate/clock routing | [OPEN] |

## Next single discriminating experiment

Trace the physical ASR-10 audio-board secondary decode and clock/frame nets
from the ES5506 master-clock/control pins using local board schematics,
netlists, photographs, or service documentation.  This negative firmware
census is the boundary at which a physical control path is required; it does
not justify inventing a mux or modifying MAME timing.
