# Bank 11 history: MMIO and global-state census

## Question

Does the reproducible contrast

```
BOOT -> File 11 ATRK TUT BNK -> Play          (BAD)
BOOT -> File 1 TUTORIAL BNK -> File 11 -> Play (GOOD)
```

come from an unmodelled external hardware side effect or from a simple
global-device initialization difference?  This is deliberately before any new
sequencer/voice/audio investigation.

## Controlled paths

The panel flow itself was first verified in a temporary Lua calibration:

* the boot browser starts at `FILE 1  TUT0RIAL BNK`;
* Enter starts the File-1 Bank transaction and reaches `BANK L0AD C0MPLETED`;
* Load returns to File 1; and
* eight verified `UP` transitions reach `FILE 11 ATRK TUT BNK`.

The two runs then used the same File-11 selection, Enter, completed-Bank
witness, and a three-second Play window.  The census was live only from the
File-11 commit through that window.  This avoids comparing boot traffic or
the File-1 transaction itself with File-11 load traffic.

Raw, temporary evidence remains outside the tree:

* `/private/tmp/asr10-bank11-census-bad.log`
* `/private/tmp/asr10-bank11-census-good.log`

The temporary Lua probe was observation-only and removed after the run.

## Bounded external/MMIO census

The census covered the address-map regions currently represented as fallback
RAM or incomplete peripheral space, plus all existing global device windows:

| Region | BAD/GOOD result | Status |
|---|---|---|
| CS2 fallback (`$FC0000-$FC1FFF`, `$FC2080-$FC2FFF`, ES5510 gaps, `$FC3200-$FC3FFF`) | no writes | [Verified negative, bounded] |
| CS3 fallback (`$FC4004-$FC47FF`, `$FC4820-$FC4FFF`, `$FC5020-$FC5FFF`) | no writes | [Verified negative, bounded] |
| SIB (`$FC6000-$FC6FFF`) | no writes | [Verified negative, bounded] |
| SCSI stub (`$FC5000-$FC501F`) | no writes | [Verified negative, bounded] |
| FDC | same 35 normalized write tuples | [Verified non-discriminating] |
| CS1 (`$FF6000-$FF7FFF`) | same 87 normalized tuples, including existing per-voice helper PCs `$F8E278/$F8E27C/$F8E280`, `$F8CF00`, `$007C72` | [Verified non-discriminating] |
| DUART | same configuration/control writers; differing counts and characters only in panel-output traffic | [Verified non-discriminating for hardware-init] |

Known ES5506 and ES5510 host traffic was retained only as a control.  During
the File-11 loading phase it was respectively `11114` and `13787` (BAD),
`11114` and `13823` (GOOD); the small ES5510 difference is not an isolated
write or unexplained address.  In the three-second Play window ES5506 traffic
was high in both paths (`30979` / `31159`) and ES5510 host traffic was exactly
`3840` in both.  Neither count establishes musical correctness.

This falsifies a **bounded** class of explanations: no missing write is seen
to current fallback CS2/CS3 space, SIB/PIO state, SCSI, or a distinct CS1
receiver during the controlled File-11 transaction.  It does not prove that
the complete hardware model is correct.

## Global RAM state

The completed File-11 boundaries were also read-only fingerprinted.  The
previously noted effect-preset candidate was identical:

```
$02CA80-$02DA7F: FNV BF8E5B67, 3429 non-zero bytes  (BAD and GOOD)
```

The sample backing was not identical:

| Range | BAD after File 11 | GOOD after File 11 |
|---|---:|---:|
| `$100000-$1FFFFF` | FNV `80EF5C64`, 1,031,334 non-zero | FNV `BE2023EF`, 996,986 non-zero |
| `$100000-$11DFFF` | FNV `A3EF3DC5`, 122,368 non-zero | FNV `8F5E32C6`, 87,981 non-zero |

Thirty-one 4 KiB blocks differ: almost all of `$100000-$11DFFF`, plus
`$1F1000-$1F1FFF`.

The decisive preservation control is:

```
BAD  pre-File-11 $100000-$11DFFF = A3EF3DC5 / 122368 non-zero
BAD post-File-11 $100000-$11DFFF = A3EF3DC5 / 122368 non-zero

GOOD pre-File-11 $100000-$11DFFF = 8F5E32C6 /  87981 non-zero
GOOD post-File-11 $100000-$11DFFF = 8F5E32C6 /  87981 non-zero
```

Thus File 11 preserves the pre-existing prefix byte-for-byte at this
granularity.  This is not an artefact of missing observation: the source
address has to include the established `$200000-$F7FFFF` system-RAM alias,
which folds modulo 2 MiB into the shared sample backing.  An alias-aware
write census showed that File 11 writes mainly `$1C8000-$1Fxxxx`; it does not
write the differing `$100000-$11DFFF` prefix.  BAD and GOOD File-11 write
sets differ only at `$11D000` (eight writes only in GOOD) and minor `$1Fxxxx`
payload/count details.

## Direct ES5506 sample-reference test

The retained-prefix correlation was tested directly, without changing machine
behaviour.  A temporary, bounded Lua probe decoded the final ES5506 host
register commits after the same File-11 load followed by Play in each flow.
It retained a live panel/load witness and recorded each voice's final CR,
START, END, ACCUM, and volumes.  The raw evidence remains outside the tree:

* `/private/tmp/asr10-bank11-prefix-reference-bad.log`
* `/private/tmp/asr10-bank11-prefix-reference-good.log`

The address test deliberately used the driver's existing ES5506 wavetable
maps rather than an inferred sample-pointer convention:

* bank 0 word addresses `$000000-$07FFFF` share CPU sample RAM
  `$100000-$1FFFFF`; consequently the candidate CPU prefix maps to bank-0
  words `$000000-$00EFFF`;
* bank 1 word addresses `$000000-$07FFFF` use the existing CPU low-memory
  mapping, not that sample-RAM share; and
* CR bits 15:14 select the ES5506 bank.  START, END, and ACCUM are only
  compared with the prefix when that selection is bank 0.

Both completed File-11 Play windows produced the same normalized final
voice-register rows.  All 32 observed voices selected bank 1; this includes
every voice with non-zero final volume.  The probe therefore found:

| Flow | Final decoded voices | Bank-0 references into `$100000-$11DFFF` |
|---|---:|---:|
| BAD: Boot -> File 11 -> Play | 32 | 0 |
| GOOD: Boot -> File 1 -> File 11 -> Play | 32 | 0 |

For example, active voice 1 ended with `CR=$4300` (bank 1),
`START=$35624000`, `END=$372CDF80`, and `ACCUM=$34FB4000` in both flows.
The observation establishes the relevant final voice/sample setup after Play;
the memory-tap PC is intentionally not used as a CPU-writer claim.

Thus the differing prefix is **not directly dereferenced by the observed
ES5506 File-11 voices in the current model**.  This is a falsification of the
specific hypothesis that BAD versus GOOD reaches different prefix contents
through the File-11 ES5506 voice/sample reference.  It does not assign an
acoustic cause to the remaining BAD/GOOD symptom, and it does not claim that
no other subsystem can inspect that RAM.

## What this establishes

* [Verified] The strict File-1/File-11 panel paths can be reproduced.
* [Verified] The specified fallback-MMIO, SIB, SCSI, FDC, and CS1 census
  supplies no BAD/GOOD hardware-write discriminant.
* [Verified] The File-11 load inherits a materially different sample-RAM
  prefix in the two paths and does not initialize that prefix.
* [DISPROVEN, bounded] The observed File-11 ES5506 voice/sample setup does
  not use that prefix as a direct sample source: both paths select bank 1 and
  yield zero bank-0 references into `$100000-$11DFFF`.

## What this does not establish

* [OPEN] Whether the retained bytes are correct serialized/allocation state,
  harmless residual sample data, or an emulation-side allocation omission.
* [OPEN] The acoustic BAD/GOOD mechanism.  This experiment used ES5506 host
  traffic only as a live Play witness; it made no audio-quality claim.
* [OPEN] Any ES5510 pump/routing or physical audio-clock conclusion.

No production change is justified from a correlation between retained sample
RAM and the reported playback difference.

## Stop boundary

The requested direct-prefix hypothesis is falsified, so the Bank-11 branch
stops here.  No loader/allocation or sequencer follow-up is justified by this
prefix correlation alone, and no production change is justified.

The separate main-plan boundary remains unchanged: ES5510 execution is good
enough for now; the functional two-rate policy is [Likely]; the next selected
macro-area is the still-[OPEN] generic ASR ES5506-to-ES5510 frame/SER/HALT
contract.  This document makes no new audio/pump claim.

## Deletion accounting

Production C++: +0/-0.  Temporary Lua probes: +0/-0 in the tree (removed
from `/private/tmp` after evidence capture).  Documentation: +1 file, then
this bounded follow-up edit.
