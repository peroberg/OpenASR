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

## Single next experiment

Acquire or identify an original, independently rate-labelled 30 kHz standalone
EPS/ASR effect file with an aligned object base.  First establish its rate
class from the artifact/documentation, then read only `+$62..+$6A`; a verified
30 kHz `+$66=$01` would be a counterexample, while two independent `$00`
controls would complete the missing negative side of the differential.

```text
RESULT: STRUCTURAL OBSERVATION / RATE CLASS UNKNOWN
MODEL CHANGE JUSTIFIED: NO
```
