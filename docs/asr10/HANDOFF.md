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
                                      |-- SCSI candidate window (partial)
                                      `-- ES5506 -> functional ESP frame adapter -> ES5510 -> audio
```

The firmware-visible CS regions and their coarse use are established; secondary
board decode remains incomplete. In particular, CS1 is an unresolved,
write-only external region, while CS2 contains the ES5506/ES5510 host regions
and CS3 contains FDC, DUART and SCSI candidates. CS selection is not proof of
the physical receiver behind every address. See `reference/hardware-map.md`
and `investigations/audio-rate-control-write-v350.md`.

### Subsystem status

| Subsystem | Current status | Important boundary |
|---|---|---|
| Boot, V3.50 load, FDC/IDMA | **[Verified runtime/current model]** | V3.50 reaches `FILE 1`; real MAME FDC/IDMA is used for bounded load cases. Physical IRQ/glue routing is not fully known. |
| Storage | **[Partly functional]** | FDC boot/load works. SCSI is a partial candidate/stub; ASR filesystem `SAVE`/write semantics are not modelled merely by MAME save states. |
| DUART/panel/display | **[Verified bounded runtime]** | Normal boot/navigation, display fields and selected workflows work; full UI semantics and every DUART output pin are not claimed. |
| ES5506 dry voice path | **[Verified runtime]** | Note allocation, sample fetch and audible pitched output are established for bounded controls. |
| ES5510 upload/execution | Good enough / functionally established | Host upload/readback is verified. Safe execution is admitted only through the explicit adapter lifecycle below. |
| ES5506 -> ES5510 frame path | **[Likely functional, bounded]** | ROM HALL and 44LUSH work through the functional adapter; physical serial wiring and HALT are open. |
| 30 kHz / 44.1 kHz policy | **[Likely functional]** | A firmware-mode-driven MAME policy produces the intended two functional modes; physical clock route is open. |
| Sequencer/music correctness | **[OPEN]** | Transport runs and is audible, but original-musical playback remains unvalidated. |
| Bank 11 history-dependent symptom | **[OPEN]** | A real BAD/GOOD user symptom survives; several simpler hardware/state explanations are falsified. |

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

## Bank 11: active symptom, rejected shortcuts

The reproducible user flows are:

```text
BAD:   boot -> File 11 -> Play
GOOD:  boot -> File 1 / Tutorial Bank -> File 11 -> Play
```

This remains a real **[OPEN]** history-dependent symptom. The following have
already failed as explanations in the controlled census: ignored/partial MMIO,
SIB/PIO, CS1/CS2/CS3, the SCSI stub, FDC state and initial DUART state. The
specific sample-RAM-prefix hypothesis is **[DISPROVEN]**: the differing
`$100000-$11DFFF` contents are not referenced by observed Bank 11 ES5506
voice/sample setup. Do not reopen any of those candidates without new evidence.
A future investigation starts at the musical/runtime chain and finds the first
BAD/GOOD divergence (sequence/track/instrument/voice/result), not at another
broad hardware census.

Primary record: `investigations/bank11-history-mmio-state-census.md`.

## Important remaining boundaries

- **Physical audio rate/clock:** no verified ASR CLKIN source, divider/mux,
  Y2/Y3 connection or ES5701 routing. The functional policy is retained as
  `[Likely functional]`; physical implementation is `[OPEN]`.
- **Resampling:** MAME stream resampling and the ASR board/sample-rate
  architecture are separate questions. A working frame adapter does not
  establish original board resampling or serial timing.
- **Storage future:** SCSI device behaviour and physical completion wiring are
  open. An ASR filesystem `SAVE`/write path is a separate future capability;
  it must not be conflated with MAME save-state serialization.
- **Sequencer:** transport is working, but audio-correct musical playback is
  not demonstrated. This and the Bank 11 first-divergence question are the
  most useful next reviewer areas.
- **Board decode:** CS regions are known at coarse level; secondary decode,
  particularly CS1 and unresolved portions of CS2, remains open.

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
- Bank 11's prefix, MMIO and init candidates are not live leads anymore.

## Recommended next review areas

1. The first controlled BAD/GOOD divergence in Bank 11's musical runtime chain.
2. Sequencer/track/instrument/voice evidence needed to assess musical
   correctness, independently of transport start/stop.
3. Storage write/SCSI work as a separately scoped subsystem, with explicit
   separation of ASR filesystem operations from MAME state saves.
4. Physical audio-clock evidence only when a concrete board observation can
   discriminate remaining models; do not treat it as a prerequisite for using
   the current functional policy.
