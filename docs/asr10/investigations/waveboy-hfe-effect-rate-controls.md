# WaveBoy HFE effect-rate controls — artifact qualification

## Question

Can the two supplied WaveBoy HFE images provide an independently documented,
serialized 30 kHz/44.1 kHz effect matched pair for the `+$66` differential?
The intended strong control was the WaveBoy **Tempo Sync'd Delays** product,
which external product documentation says includes both 30 kHz and 44 kHz
versions for ASR-10 and EPS-16 Plus.

This pass is artifact-first.  It does not use a filename or `+$66` to infer a
rate class, and it does not enter firmware, loader, clock, or DSP analysis.

## Gate 1 — source artifacts

| path | bytes | SHA-256 | status |
|---|---:|---|---|
| `floppies/fx/WBFX12.hfe` | 2,049,024 | `bcbb2a69a62c82345c9d7966b20803fac9466774734c2c7be78e68d3bfb5631d` | PASS: read-only local research artifact |
| `floppies/fx/WBFX38.hfe` | 2,049,024 | `22d6a8237127939ac350a7d57a55bbad9b5ee4297fc37c606ea5ea9955718788` | PASS: read-only local research artifact |

Both begin with `HXCPICFE`, HFE revision 0.  Their headers specify 80
cylinders, two heads, ISO IBM MFM encoding, and 250 kbit/s sampling.  The HFE
containers differ; they must not be treated as duplicate source artifacts.

## Gate 2 — HFE to floppy contents

MAME's existing read-only format support was used; no HFE decoder or emulator
behaviour was added:

```text
./floptool identify floppies/fx/WBFX12.hfe floppies/fx/WBFX38.hfe
  -> hfe HxC Floppy Emulator HFE File format

./floptool flopconvert hfe esq16 <input.hfe> <temporary.raw.img>
```

`esq16`, rather than `asr10`, is the compatible raw-image target: the decoded
images are 819,200 bytes, matching 80 x 2 x 10 x 512 EPS-16/ESQ16 MFM sectors.
This also agrees with the decoded disk header (`EPS Disk`) and the valid
directory records below.  The temporary derivatives were:

| derivative | SHA-256 |
|---|---|
| `WBFX12.esq16.img` | `7fdcac613cc17bc630da49de62ed6cbcdad01c5edba46fd4aa877192bd7dc599` |
| `WBFX38.esq16.img` | `6c1d56e0e95a5ed0734891e8debb4db3fead8c3cfd45ba15fc39011a0acdcc48` |

The raw images differ, so they are not duplicate decoded media.  They are
temporary analysis derivatives, not committed artifacts.

## Gate 3 — bounded filesystem inventory

The valid directory starts at decoded raw offset `$061A`; its 26-byte records
match the established Ensoniq directory shape used by the earlier ASR work.
All relevant records are type `$0003` **Instrument**, not type `$0021`
standalone effect.  Thus any effect byte would require a verified embedded
effect-object base before it could be called `effect +$66`.

| container | relevant type-`$0003` records | file names |
|---|---:|---|
| `WBFX12.hfe` | 17 | `PAR-FX GTR+X`, `PARALLEL EFX`, `PHASER+REV+X`, `PLATE-REVERB`, `PLATE-VERB+X`, `RESON FILTR1..3`, `ROOM RVERB+X`, `TIME DICER+X`, `WA+DST+REV+X`, `-THE VODER-`, `BASS VOX`, `REZ MINIMOOG`, `SOPRANO VOX`, `STOLEN GROOV`, `VODER CHOIR` |
| `WBFX38.hfe` | 38 | `1 CHO +3 DST` through `4 REVERBS`; `44K DELAYS+X`, `44K REVERB+X`, `44K-COMPRS+X`, `44K-COMPRSSR`; `FM+FX`, `PITCH-WARP`, `BASSO VODER`, `CHO+...`, `DUAL DELAY+X`, `GRAIN-STORM`, `HALL RVERB+X`, `LO-FIDELITY`, `NONLIN REV+X` |

The two lists have no shared directory name.  They therefore describe 55
distinct *instrument containers*, but not yet 55 independently identified
serialized effect objects.  No broad Instrument parser was attempted.

## Gate 4 — Tempo Sync'd Delay matched pair

The external WaveBoy advertisement is independent evidence that the **Tempo
Sync'd Delays** product supplied both 30 kHz and 44 kHz versions for ASR-10 and
EPS-16 Plus ([Transoniq Hacker issue 144](https://www.synthmanuals.com/manuals/ensoniq/transoniq_hacker_archive/issue_144/th_144.pdf)).

That statement does not map either version to a local directory record.  The
decoded directory contains no explicit `TEMPO` or `SYNC` name.  In particular:

| local name | result |
|---|---|
| `TIME DICER+X` | identified externally as the Audio-In/Time Dicer disk's effect, not as the Tempo Sync'd Delay pair ([Matrixsynth description](https://www.matrixsynth.com/2017/05/waveboy-ensoniq-eps-16-external-audio.html)) |
| `44K DELAYS+X` | a plausible 44 kHz-named lead only; no independent source maps it to Tempo Sync'd Delays |
| `DUAL DELAY+X` | a plausible delay-named lead only; absence of `44K` is not 30 kHz evidence |

Therefore the matched pair is **NOT FOUND / not qualified**.  No other local
record has explicit independent 30 kHz rate evidence.  The primary gate fails
before any serialized effect-object field is read.

## Gates 5–6 — intentionally not entered

Because no candidate has both a proven algorithm identity and an independently
proven rate class, no embedded effect-object base was inferred from these
instrument containers.  Consequently this pass makes **no** read of container
`+$66`, effect-object `+$66`, or `+$62..+$6A`.

```text
container file +$66 != effect object +$66
until an embedded object boundary is independently established.
```

## Hypothesis status

| hypothesis | verdict | reason |
|---|---|---|
| H1 serialized `+$66` is persistent effect-dependent metadata | PARTIALLY SUPPORTED, unchanged | no qualifying WaveBoy effect object was reached |
| H2 serialized 30 kHz effects use `+$66=$00` | OPEN | zero qualified 30 kHz objects |
| H3 serialized 44.1 kHz effects use `+$66=$01` | SUPPORTED, unchanged | prior V3.50 12/12 evidence; no WaveBoy object read |
| H4 `+$66` is a binary 30 kHz/44.1 kHz classifier | OPEN | required matched pair/control remains unqualified |
| H5 `+$66` is explicit sample-rate metadata | OPEN | no semantic evidence added |
| H6 `+$66` is a broader serialized operating-mode field | PARTIALLY SUPPORTED, unchanged | no new discriminating evidence |

Safe terminology remains **serialized effect operating-mode byte** or
**candidate rate-class field**.  Physical rate/clock routing and ES5701 remain
open; no model change is justified.

## Single next experiment

Acquire a provenance-verified WaveBoy **Tempo Sync'd Delays** disk image (or
its original readme/manual that maps its actual serialized records to the 30
kHz and 44 kHz variants).  First identify the two container records by that
mapping; only then perform the separate, bounded embedded effect-object
boundary analysis needed before reading `+$62..+$6A`.

```text
RESULT: INCOMPLETE / PAIR IDENTITY AND EMBEDDED OBJECT BOUNDARY MISSING
MODEL CHANGE JUSTIFIED: NO
```
