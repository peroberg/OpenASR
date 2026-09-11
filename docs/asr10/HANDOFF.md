# OpenASR Part I — current technical handoff

**Current checked-in baseline:** `7bfd2f3722d` (`asr10: add functional ESP
frame adapter`). This is the entry point for a technical reviewer. It describes
the running `asr10booth` model as it is now, rather than replaying the
investigation chronology. Read `current-status.md` and
`reference/methods-hypothesis-management.md` next; investigations supply the
reproducible evidence and revisions behind the statements here.

## Model philosophy and scope

MAME is the active executable reference for this project. Work is
observation-first: a firmware access, a MAME-device behaviour, a chip-datasheet
fact, and a physical ASR board connection are different evidence domains.
Consequently a useful compatibility abstraction may be **[Likely functional]**
without being a claim about Ensoniq's actual board wiring. Do not upgrade one
domain from evidence in another.

The model deliberately favours existing MAME devices and small board policies
over replacement implementations. Lua observations require retained tap
references and a live witness; a memory tap or sampled CPU PC alone is not an
instruction-execution witness. Negative results are evidence only with stated
coverage and positive controls. The normative rules and labels are in
`reference/methods-hypothesis-management.md`.

## Architecture at a glance

```text
68000 <-> MC68302 wrapper/SIB <-> ASR board decode
                                      |-- ROM/RAM and sample RAM
                                      |-- uPD72069 FDC -> external IDMA -> RAM
                                      |-- DUART -> panel/display path
                                      |-- WD33C93A SCSI (ID 0 HDD, ID 4 CD-ROM)
                                      `-- ES5506 -> functional ESP frame adapter -> ES5510 -> audio
```

The firmware-visible CS regions are established: CS1 is verified as per-voice
sample banking ($FF7F00-$FF7FFF), CS2 contains the ES5506/ES5510 host regions,
and CS3 contains FDC, DUART and SCSI candidates. CS selection is not proof of
the physical board receiver behind every address. For CS1, functional emulation
is complete, while the physical receiver IC(s) on the 4-layer Digital Board
remain at the documentation frontier. See `reference/hardware-map.md`,
`investigations/cs1-voice-banking-and-sample-addressing.md`, and
`investigations/cs1-board-level-implementation-frontier.md`.

### Subsystem status

| Subsystem | Current status | Important boundary |
|---|---|---|
| Boot, V3.50 load, FDC/IDMA | **[Verified runtime/current model]** | V3.50 reaches `FILE 1`; real MAME FDC/IDMA is used for bounded load cases. Physical IRQ/glue routing is not fully known. |
| Storage | **[Verified / Functionally Closed / Frozen]** | Cold HDD boot, authentic Ensoniq HDD filesystem, OS V3.50 boot, SCSI device switch, authentic CDR-1 browse, type-3 Bank load, Sample-RAM transfer, ES5506 voice programming and audible output are fully verified. |
| DUART/panel/display | **[Verified bounded runtime]** | Normal boot/navigation, display fields and selected workflows work; full UI semantics and every DUART output pin are not claimed. |
| ES5506 dry voice path | **[Verified runtime]** | Note allocation, sample fetch and audible pitched output are established for bounded controls. |
| ES5510 upload/execution | Good enough / functionally established | Host upload/readback is verified. Safe execution is admitted only through the explicit adapter lifecycle below. |
| ES5506 -> ES5510 frame path | **[Likely functional, bounded]** | ROM HALL and 44LUSH work through the functional adapter; physical serial wiring and HALT are open. |
| 30 kHz / 44.1 kHz policy | **[Likely functional]** | A firmware-mode-driven MAME policy produces the intended two functional modes; physical clock route is open. |
| Sequencer/music correctness | **[OPEN]** | Transport runs and is audible, but original-musical playback remains unvalidated. |
| Bank 11 history-dependent playability | **[VERIFIED / RESOLVED]** | Resolved by dynamic CS1 per-voice sample banking (commit `b7cd112199d`). Path 1 vs Path 2 waveform correlation 99.89%. |

## What works end-to-end today

**[Verified runtime/current model]** V3.50 boots with
`floppies/asr10booth/V350.img`, loads the regression instrument, allocates
voices, fetches sample RAM through ES5506 and produces non-silent,
pitch-checked audio. The FDC, external-IDMA, panel/DUART, display and bounded
recording paths have individual acceptance coverage.

**[Likely functional, bounded]** The post-effect audio path has a controlled
known-note acceptance: C4 under
`ROM HALL -> 44LUSH -> ROM HALL` gives approximately
`262.3 -> 260.9 -> 260.9 Hz`, with non-silent WAV output. The stronger
acceptance is the same A/B/A firmware mode transition, active ES5510 program,
and audible output together—not the small final frequency variation by itself.
It covers the two tested effect programs, not every effect or the physical
board.

## ES5510 and rate freeze: four-point plan

This branch is **parked** at the following useful milestone. Do not reopen it
without a concrete new discriminating observation.

| Point | Status | Evidence / precise limit |
|---|---|---|
| 1. ES5510 execute and receive data | Good enough; functionally established | V3.50 host upload/readback and bounded executable images are captured. |
| 2. ES5506 -> ES5510 frame/SER/HALT contract | **[Likely functional]**, bounded | `7bfd2f3722d`; ROM HALL and 44LUSH use an ASR-specific generic-pump policy. |
| 3. Reproducible 30 kHz / 44.1 kHz modes | **[Likely functional]** | `a8481df1f72`; mode-selected MAME board policy survives A/B/A acceptance. |
| 4. Known note/effect end-to-end | **[Likely functional]**, bounded | C4, ROM HALL -> 44LUSH -> ROM HALL; it is not a general effect or physical-board verification. |

The explicit non-physical remarks are part of the model contract:

- The 10 ms post-upload quiescence is a functional safety rule, **not** a
  verified physical ES5510 HALT signal.
- ES5506 lane 0/1 fanned to ES5510 SER0/SER2/SER3 is a functional board
  abstraction, **not** verified ASR wiring.
- SER1 is a verified useful output for the tested ROM HALL and 44LUSH programs,
  **not** a universal effect-output proof.
- The mode-selected Y2/Y3 `set_clock()` choice is a MAME board policy. Actual
  ES5506 CLKIN, divider, mux and ES5701 routing remain **[OPEN]**.
- `Y2/2 <-> Y3/2` is **[DISPROVEN] only as a direct MAME-device-clock policy**;
  it is not disproven as possible physical electronics.
- Unknown or unapproved ESP images take the dry fallback because their safe
  lifecycle is unestablished. That is a compatibility policy, not a hardware
  statement.

The primary records are
`investigations/es5510-upload-executable-contract-v350.md` and
`investigations/audio-rate-y2-y3-synthetic-policy-v350.md`.

## Closed and parked hypotheses

- **[DISPROVEN, scoped]** An ACTV-only clock change gave the wrong known-note
  result; do not recreate the old `set_unscaled_clock()` experiment.
- **[DISPROVEN, scoped]** A direct `Y2/2 <-> Y3/2` MAME clock policy does not
  satisfy rate acceptance. This is not a PCB conclusion.
- **[Verified negative, bounded]** An A/B/A census found no additional
  reversible firmware-visible rate-control write in SIB PIO, BR/OR, CS1, CS2
  or observed partial-MMIO holes beyond established effect/ACTV/pitch and
  ESP-upload traffic. It does not prove that a physical selector is absent.
- **PARKED** The serialised effect `+$66` artifact branch: direct provenance is
  known for the tested 44LUSH window, but no independently classified
  serialised 30 kHz control exists locally. It must not be reintroduced as a
  binary sample-rate fact.

## Bank 11: resolved playability defect (CS1 dynamic voice banking) [VERIFIED / RESOLVED]

The historical playability defect across alternating bank loads:

```text
BAD:   boot -> File 11 (ATRK TUT BNK) -> BLUES DRUMS -> click / near-silent (Peak 486)
GOOD:  boot -> File 1 (TUTORIAL BNK) -> File 11 -> phantom audible audio (Peak 5065)
```

has been completely resolved by dynamic CS1 per-voice sample banking (commit `b7cd112199d`):
- **Causal Defect:** The previous driver hardcoded ES5506 Bank 1 wavetable reads to DRAM Chunk 0 (`m_lowmem_shadow`). `BLUES DRUMS` resides in physical megabyte 7 (DRAM Chunk 1 / `m_sample_ram`). On fresh boot, Chunk 0 held uninitialized silence. After loading `TUTORIAL BNK`, Chunk 0 retained leftover PCM from `JM DRUMS`, playing phantom audio.
- **Resolution:** Implementing dynamic per-voice translation from the `$FF7F00-$FF7FFF` table enables both paths to correctly read Chunk 1, producing authentic drum audio (Peak ~14550, RMS ~521, 99.89% waveform cross-correlation).
- **Physical Boundary:** The physical IC(s) storing the table on the 4-layer Digital Board remain at the documentation frontier (`DOCUMENTATION FRONTIER REACHED — BOARD OWNERSHIP`).

Primary records: `investigations/cs1-voice-banking-and-sample-addressing.md` and `investigations/cs1-board-level-implementation-frontier.md`.

## Storage subsystem freeze: [VERIFIED / FUNCTIONALLY CLOSED / FROZEN]

The storage subsystem is functionally complete and frozen:

```text
cold HDD boot
  ↓
authentic Ensoniq HDD filesystem
  ↓
OS V3.50 boot
  ↓
SCSI device switch (to SCSI ID 4)
  ↓
authentic Ensoniq CDR-1 browse
  ↓
type-3 Bank load (ORCH STRNGS1: 963 blocks, $C1C9..$C58B)
  ↓
Sample-RAM transfer
  ↓
ES5506 voice programming
  ↓
audible output (Middle C, 262.3 Hz)
```

### uPD72069 Standby Auxcmd Fix (Generic MAME)

- **Symptom:** HDD boot → CHANGE STORAGE DEVICE to SCSI 4 → browse CDR-1 OK → load file (`ORCH STRNGS1`) → `FILE OPERATION ERROR`. 0 SCSI READ commands sent to WD33C93.
- **Root cause:** In upstream MAME `src/devices/machine/upd765.cpp`, `upd72069_device::auxcmd_w()` erroneously grouped `case 0x35:` (*set standby*) and `case 0x34:` (*reset standby*) under `PHASE_RESULT` with `ST0_UNK`. This caused FDC MSR to report `$D0` (`MSR_RQM | MSR_DIO | MSR_CB`).
- **First causal divergence:** Firmware's `prepare_device_for_io` (`$013398`) issues auxcmd `$35` before SCSI operations, then polls FDC MSR bit 4 (`MSR_CB`) at `$FFFB8D1E`. Because `MSR_CB` never cleared, it timed out after 8,000 loops, set `$049D=$0D` (`FILE OPERATION ERROR`) and `$04AE=$20` (FDC busy timeout), and aborted before issuing any SCSI commands.
- **Fix:** In `src/devices/machine/upd765.cpp`, `case 0x35:` and `case 0x34:` are delegated to base class `upd72065_device::auxcmd_w(data)` (`break;`). FDC remains idle with MSR `$80` (`MSR_CB` = 0).
- **Epistemic status:**
  - **[VERIFIED]** `FILE OPERATION ERROR` was caused by uPD72069 incorrect `PHASE_RESULT` after auxcmd `$35/$34`.
  - **[VERIFIED]** Defect was in generic `upd765.cpp`, not in ASR driver. Zero ASR-specific workarounds.
  - **[VERIFIED]** CD file-load after HDD boot and SCSI switch works completely. First authentic READ is at `LBA $0000C1C9`, extent reads to `LBA $0000C58B` (963 blocks), followed by `FILE LOADED`.
  - **[VERIFIED]** ORCH STRNGS1 plays with audible Middle C note (~262.3 Hz, peak 11057) via MIDI.
  - **[DISPROVEN]** SCSI/IDMA/CD-filesystem as the cause of this error.
  - **Note:** The `ORCH STRNGS1` file extent is exactly 963 × 512 bytes. Sample-RAM writes observed during load do not represent sample payload size without independent verification.

Primary record: `investigations/upd72069-standby-auxcmd-fix.md`.

## Important remaining boundaries

- **Physical audio rate/clock:** no verified ASR CLKIN source, divider/mux,
  Y2/Y3 connection or ES5701 routing. The functional policy is retained as
  `[Likely functional]`; physical implementation is `[OPEN]`.
- **Resampling:** MAME stream resampling and the ASR board/sample-rate
  architecture are separate questions. A working frame adapter does not
  establish original board resampling or serial timing.
- **Storage:** **[VERIFIED / FUNCTIONALLY CLOSED / FROZEN]**. Both floppy boot/load,
  SCSI HDD boot/format/remount, and SCSI CD-ROM browse/load are functionally closed.
  Physical completion IRQ line glue details remain secondary/open.
- **Sequencer:** transport is working, but audio-correct musical playback is
  not demonstrated.
- **Board decode:** CS regions are known; CS1 per-voice banking ($FF7F00-$FF7FFF)
  is functionally verified and resolved in emulation, with physical board receiver
  IC(s) parked at the documentation frontier. Portions of secondary decode for
  discrete board control remain open.

## Regression and review procedure

Run from repository root:

```sh
git status --short
git diff --check
docs/asr10/regression-test.sh
```

The suite uses `./mame`, `asr10booth`, V3.50 and
`floppies/asr10booth/V350.img`; do not substitute historical `./mess`/V1.61
commands. Its current expected result is 16 named controls, their WAV
sub-check rows, and final `PASS regression` with exit 0 (normally 21 `PASS`
lines). The A/B/A audio control is `docs/asr10/lua/audio_rate_mode.lua`.
Use a no-media boot separately when touching boot/storage behaviour.

## Existing unrelated working-tree state at this handoff

Do not stage, revert or absorb these without separate authority:

```text
M  3rdparty/portaudio/bindings/java/jportaudio/.project
M  src/devices/sound/es5506.h
M  src/mame/layout/asr10_panel.lay
?? docs/ensoniq/*.pdf
```

## Reviewer checklist: do not assume

- A MAME functional board policy is not physical ASR wiring.
- Firmware register traffic is not a physical clock-select proof.
- A CS range is not a complete secondary-board decode.
- A Lua memory tap plus PC snapshot is not an execution witness.
- A serialised `+$66` value is not established as a literal `sample_rate`
  field.
- MAME save states are not ASR disk/filesystem SAVE.
- ES5510 success for ROM HALL and 44LUSH is not a claim for all ESP programs.
- Bank 11 playability is resolved; do not reopen sample-RAM-prefix or MMIO candidates.

## Recommended next review areas

1. Sequencer/track/instrument/voice evidence needed to assess musical
   correctness, independently of transport start/stop.
2. Verification of additional complex/multi-sample instruments under dynamic CS1 voice banking.
3. Storage write/SCSI work as a separately scoped subsystem, with explicit
   separation of ASR filesystem operations from MAME state saves.
4. Physical audio-clock and board-ownership evidence only when concrete board observation
   or authentic schematics become available; do not treat physical IC identification as
   a prerequisite for functional emulation progress.
