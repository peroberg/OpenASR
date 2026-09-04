# FM+FX serialized effect control — artifact qualification

## Question

Can the supplied `FMFX` artifact provide a safely aligned serialized effect
object window at `+$62..+$6A`, and is its rate class independently known before
the observed `+$66` value is considered?  This is an artifact/format pass only:
it does not revisit loader, FDC, IDMA, firmware mode consumers, pitch, or any
clock model.

## Artifact identity

The path named in the task, `floppies/fx/FMFX.efe`, is not present.  The actual
read-only local artifact is an HFE disk image:

| path | bytes | SHA-256 |
|---|---:|---|
| `floppies/fx/FMFX.hfe` | 2,008,064 | `a0311829fc700d5573b26261d582bc788eb25acdbe97b54825917ca25ce404db` |

It begins `HXCPICFE`; `floptool identify` reports HxC HFE.  Its header gives
HFE revision 0, 80 cylinders, two heads, ISO IBM MFM and 250 kbit/s.  It was
decoded read-only with existing MAME tooling:

```text
./floptool flopconvert hfe esq16 floppies/fx/FMFX.hfe /tmp/FMFX.esq16.img
```

The temporary 819,200-byte derivative has SHA-256
`e5a34c941b16f241ca311c97afbbc356ac1264ac86c860f8cc0e161741ca6f6d` and
starts `EPS Disk`.  It is a reproducible analysis derivative, not a committed
artifact.

## Standalone effect classification and object base

The decoded disk directory at `$061A` has one nonempty 26-byte record:

```text
$061A: 00 18  00 20 20 20 46 4D 2B 46 58 20 20 20  00 0A
        type=$0018       name="   FM+FX   "         size=10 blocks
        start block=$0000000F
```

The published EPS/EPS-16 disk-format file-type table identifies `$18` as an
**EPS-16 Plus Effect File** ([Ensoniq Disk Formats](https://www.youngmonkey.ca/nose/audio_tech/synth/Ensoniq-DiskFormats.html)).
The EPS-16 Plus manual independently describes an independent effect file as
one not attached to an instrument or bank ([Musician's Manual excerpt](https://manualzz.com/doc/23302413/ensoniq-eps-16-plus-musician-s-manual)).  Thus this is a standalone EPS-16
effect file, not the unverified embedded-effect situation encountered in the
two previous WaveBoy HFE images.

The directory-established payload/object base is block `$000F`, decoded image
offset `$1E00`; its ten-block contiguous payload hashes to
`aa1e97583d4d2760e7cdc0234f9909cfce7908003af5d6fec58c9abcedc0a52f`.

## Structural correspondence

This file is EPS-16 Plus type `$0018`, whereas the established ASR-10 V3.50
baseline uses type `$0021`; they must not be called byte-identical formats.
However, their payloads share local structural landmarks: a leading effect
header, a spaced 12-character name region, parameter-name regions, and a
bounded `+$62..+$6A` window.  The direct comparison is deliberately modest:

| object-relative window | FM+FX (`$1E00`) | V3.50 `44LUSH PLATE` (`$17DC00`) |
|---|---|---|
| `+$3A..+$3F` | `00 74 00 BE 00 74` | `00 74 00 A2 00 74` |
| `+$62..+$6A` | `00 00 72 73 00 00 00 00 00` | `00 00 82 83 01 00 00 00 00` |

This is sufficient to identify the FM+FX value below as its standalone
effect-payload `+$66`, not as `container file +$66`.  It does **not** establish
that the EPS `$0018` and ASR `$0021` bytes at that offset have identical field
semantics.  The different type codes, header values, and `+$64/$65` contents
remain a format-variant boundary.  Thus the FM+FX byte is retained as a
standalone serialized-effect observation, not folded into the ASR 30k/44.1k
differential dataset.

## FM+FX control window

```text
FM+FX effect object base = decoded image $1E00
+$62..+$6A              = 00 00 72 73 00 00 00 00 00
+$66                     = $00
```

The prior ASR V3.50 baseline is twelve independently manual-classified 44.1
kHz type-`$0021` effects, all with `+$62..+$6A = 00 00 82 83 01 00 00 00 00`
and `+$66=$01`.  The window is useful structural context, but it is not a
two-class differential: both the independent-class gate and full
cross-generation field equivalence remain unestablished.

## Independent identity and rate evidence

The disk name `FM+FX` and its standalone type-$0018 record identify the local
artifact as FM+FX.  External WaveBoy material describes FM+FX as a WaveBoy
effect for the EPS-16 Plus/ASR family ([Waveboy effects overview](https://chickensys.com/products2/sounds/waveboy/),
[FM+FX description](https://synthblog.de/ensoniq-asr-10-fx-und-dsp/)).

No local or external source found in this bounded pass explicitly labels FM+FX
as 30 kHz, 44 kHz, or 44.1 kHz.  Compatibility, the absence of `44K` in the
name, the file type, and the observed `$00` are all invalid substitutes for
that missing evidence.

```text
FM+FX rate class: UNKNOWN
```

Therefore FM+FX is not a 30 kHz control and does not alter the existing
30k/44.1k classifier differential.

## Optional embedded comparison

Not attempted.  The standalone object is a useful byte-pattern anchor, but
matching it to an embedded representation in `WBFX12.hfe` or `WBFX38.hfe`
would require an Instrument-format boundary analysis, which is outside this
pass.

## Hypothesis verdicts

| hypothesis | verdict | reason |
|---|---|---|
| H1 serialized `+$66` is persistent effect-dependent metadata | PARTIALLY SUPPORTED, unchanged | a structurally aligned standalone EPS effect has a persisted value, but this pass does not prove cross-generation field semantics |
| H2 serialized 30 kHz effects use `+$66=$00` | OPEN | FM+FX has no independent 30 kHz classification |
| H3 serialized 44.1 kHz effects use `+$66=$01` | SUPPORTED, unchanged | prior 12/12 ASR V3.50 cohort; FM+FX is not a new 44.1 kHz control |
| H4 `+$66` is a binary 30k/44.1k classifier | OPEN | no qualified serialized 30 kHz control was acquired |
| H5 `+$66` is explicit sample-rate metadata | OPEN | no authored field name or independent rate evidence was found |
| H6 `+$66` is a broader serialized operating-mode field | PARTIALLY SUPPORTED, unchanged | the current artifact does not distinguish this from a narrower rate-class interpretation |

The safe term remains **serialized effect operating-mode byte** or
**candidate rate-class field**.  `FM+FX +$66=$00` is a reproducible serialized
observation with **unknown rate semantics**, not evidence that `$00` means
30 kHz.  No audio-model change is justified.

## Final bounded EPS type-`$0018` differential

This final local pass tested the only available direct discriminator for the
format-only alternative: an artifact-named `44K` EPS type-`$0018` standalone
effect.  The local artifact inventory has exactly this split:

| source HFE | artifact name | directory type | blocks | start block | rate evidence | eligible `$0018` control? |
|---|---|---:|---:|---:|---|---|
| `FMFX.hfe` | `FM+FX` | `$0018` | 10 | `$000F` | no explicit 30k/44k label | no |
| `WBFX38.hfe` | `44K DELAYS+X` | `$0003` Instrument | 13 | `$0129` | explicit artifact name `44K` | no: container, not standalone effect |
| `WBFX38.hfe` | `44K REVERB+X` | `$0003` Instrument | 14 | `$0136` | explicit artifact name `44K` | no: container, not standalone effect |
| `WBFX38.hfe` | `44K-COMPRS+X` | `$0003` Instrument | 22 | `$0144` | explicit artifact name `44K` | no: container, not standalone effect |
| `WBFX38.hfe` | `44K-COMPRSSR` | `$0003` Instrument | 22 | `$015A` | explicit artifact name `44K` | no: container, not standalone effect |

The four `44K` names are a positive control that the bounded artifact-name
search observes the expected labels.  Their `$0003` type is the previously
established Instrument-container boundary.  Reading any container `+$66` as an
effect byte would violate the object-boundary rule, so Gate 2 is not entered
for them.  No explicit-`44K` standalone `$0018` effect exists in the local
`floppies/fx/` set.

### One bounded 30k search

The same read-only decoded images (`FMFX`, `WBFX12`, and `WBFX38`) were searched
once for explicit `30K`, `30KHZ`, and spaced `30 KHz` spellings.  It produced
zero hits.  There is no bundled local documentation that maps an exact local
artifact name to 30 kHz.  The positive `44K` results above make this a bounded
artifact-inventory result, not an untested search mechanism.

No absence of `44K`, no `$00` byte, and no ROM-like algorithm was promoted to a
30 kHz label.  Hence no explicit 30k `$0018` control exists in this local
dataset.

### Result and parked boundary

```text
EXPLICIT 44K EPS $0018 CONTROL: NOT FOUND
EXPLICIT 30K EPS $0018 CONTROL: NOT FOUND
EPS $0018 FORMAT-ONLY EXPLANATION: STILL POSSIBLE
SERIALIZED +$66 BRANCH: PARKED
MODEL CHANGE JUSTIFIED: NO
```

The `FM+FX +$66=$00` observation remains valid only in its EPS `$0018`,
rate-unknown domain.  Because no explicit 44k `$0018` control was available,
this pass cannot test whether EPS `$0018` itself can produce `$01`; because no
explicit 30k control was available, it cannot test `$00 -> 30k`.  The binary
differential and explicit field semantics therefore remain open.

## Final hypothesis status

| hypothesis | verdict | reason |
|---|---|---|
| H1 `+$66` is persistent effect-dependent operating-mode metadata | PARTIALLY SUPPORTED, unchanged | serialized persistence remains established in its respective ASR/EPS representations |
| H2 30k-class uses `$00` | OPEN | no independently classified serialized 30k control exists locally |
| H3 44.1k-class uses `$01` | SUPPORTED, unchanged | prior 12/12 ASR V3.50 cohort; no EPS `$0018` 44k control was found |
| H4 `+$66` distinguishes 30k/44.1k operating classes | OPEN | neither required local negative control nor an EPS `$0018` positive comparison was available |
| H5 `+$66` is explicitly a sample-rate field | OPEN | no direct semantic evidence was added |
| H6 `+$66` is a broader operating-mode field | PARTIALLY SUPPORTED, unchanged | this final bounded pass does not distinguish it from narrower alternatives |

No additional `+$66` artifact search, embedded-Effect reverse engineering,
filesystem work, or firmware provenance is recommended for this branch.  The
next work should return to the project’s current functional priority —
controlled sequencer-to-voice audio correctness — rather than extend this
parked metadata correlation.
