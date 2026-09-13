# OpenASR Normative Project Handoff (2026-09-13)

**Authoritative Project State and Technical Baseline**
**Target Machine:** `asr10booth` (source: `src/mame/ensoniq/asr10_boot.cpp`)
**Pre-Handoff Baseline Commit:** `5f5e76f80bb6a1137327acb7a9e24ab3f431f5fe` (`docs/asr10: document floppy media-change semantics`)

---

## 1. Purpose and Scope

This document is the **normative, authoritative technical handoff** for the OpenASR project in MAME. It supersedes all prior chronological handoffs (`docs/asr10/HANDOFF.md`, `docs/asr10/archive/asr10-handoff-*`) and establishes current project truth as verified against authentic hardware firmware (ASR-10 Boot ROM 1.50B and OS V3.50), MAME source code, and empirical discriminator experiments.

This document serves as the sole required entry point for technical reviewers and incoming autonomous coding agents. Older investigation documents preserve historical provenance; when older reports conflict with this document, the statements here take precedence.

---

## 2. Epistemic Rules and Nomenclature

OpenASR operates under a strict observation-first, evidence-driven methodology defined in report `docs/asr10/reference/methods-hypothesis-management.md` and repository guide `AGENTS.md`.

### The Three Architectural Layers
To prevent false claims of physical hardware parity from functional emulation code, every subsystem is analyzed across three distinct epistemic domains:
1. **Firmware-Visible Functional Model:**
   What authentic Ensoniq ROM and OS code demonstrably configures, reads, calculates, and depends upon at runtime.
2. **Current MAME Implementation:**
   How OpenASR models that behavior in MAME C++ (sources: `asr10_boot.cpp`, `mc68302.cpp`, `es5506.cpp`, `es5510.cpp`).
3. **Physical Hardware Model:**
   What is definitively established regarding the physical 4-layer ASR-10 Digital Board PCB, discrete logic, custom ASICs, and pin interconnections.

### Epistemic Qualifiers
- **`[VERIFIED]`**: Directly established through reproducible execution, mathematical identity, or unambiguous hardware specification.
- **`[OBSERVED]`**: Directly witnessed via static disassembly or runtime bus/trace instrumentation.
- **`[INFERRED]`**: Deductive or inductive conclusion strongly supported by evidence, but lacking direct schematic or silicon proof.
- **`[LIKELY]`**: Converging circumstantial evidence without definitive falsification.
- **`[DISPROVEN]`**: Falsified under the stated model or abstraction.
- **`[SUPERSEDED]`**: Historical project assumption replaced by later evidence.
- **`[RULED OUT]`**: Negated within a specifically defined operational scope.
- **`[OPEN]`**: Unresolved; requires discriminating empirical evidence or authentic documentation.
- **`DOCUMENTATION FRONTIER REACHED`**: Physical attribution or schematics unavailable in service literature; functional emulation is complete, and physical IC attribution is not an active blocker.

---

## 3. Repository Baseline and Git State

- **Pre-Handoff Git HEAD:** `5f5e76f80bb6a1137327acb7a9e24ab3f431f5fe`
- **Pre-Handoff Subject:** `docs/asr10: document floppy media-change semantics`
- **Handoff Subject:** `docs/asr10: add project state handoff`
- **Working Tree State:** Clean (0 tracked modifications). Two untracked external artifacts (`3rdparty/portaudio/bindings/java/jportaudio/bin/` and `docs/ensoniq/ASR-10_Cheat_Sheet_Recovered.docx`) are intentionally excluded from git.
- **Process Audit:** 0 orphaned or active `mame` background processes (`pgrep -fl mame` must return exit code 1).
- **Regression Suite:** `docs/asr10/regression-test.sh` passing: 16 test targets executing 20 distinct acceptance checkpoints, producing 21 lines matching `^PASS ` because `PASS regression` is the final aggregate script verdict (exit 0).

---

## 4. Current Functional Architecture and Subsystems

```text
======================================================================================================
                                   ENSONIQ ASR-10 SYSTEM TOPOLOGY
======================================================================================================

               +--------------------------------------------------------------+
               |                    Motorola MC68302 IMP                      |
               |                                                              |
               |   +-----------------------+     +------------------------+   |
               |   |   68000 Core 16 MHz   |     |  System Integration    |   |
               |   |  (Y1 = 16.000000 MHz) |     |  Block (SIB) Registers |   |
               |   +-----------+-----------+     +-----------+------------+   |
               |               |                             |                |
               |   +-----------+-----------+     +-----------+------------+   |
               |   |  2 KB Internal DPRAM  |     |   Chip Selects CS0-3   |   |
               |   |   ($FC6000-$FC67FF)   |     |    (Decode Windows)    |   |
               |   +-----------------------+     +----+---+---+---+-------+   |
               +--------------------------------------|---|---|---|-----------+
                                                      |   |   |   |
         +--------------------------------------------+   |   |   +--------------------------+
         | CS0                                            |   |                              | CS3
         v                                                |   v CS2                          v
+------------------+                                      | +--------------------+   +-------------------+
|  Boot / OS ROM   |                                      | |  Audio Subsystem   |   | Peripheral Glue   |
| 256 KB ($F80000) |                                      | |                    |   |                   |
+------------------+                                      | |  ES5506 OTTO Sound |   | NEC uPD72069 FDC  |
                                                          | |  ($FC2000-$FC207F) |   | ($FC4000-$FC4003) |
         +------------------------------------------------+ |                    |   |                   |
         | CS1 (Write-qualified MC68302 decode, Ext DTACK)  |  ES5510 ESP DSP    |   | SCN2681 DUART     |
         v                                                  |  ($FC3000-$FC31FF) |   | ($FC4800-$FC481F) |
+------------------+                                        +--------------------+   |                   |
| Per-Voice Sample |                                                                 | WD33C93A SCSI     |
| Banking ($FF7F00)|                                                                 | ($FC5000-$FC5003) |
+--------+---------+                                                                 +-------------------+
         |
         | [Dynamic translation: 32 voices x 4 words]
         v
+--------------------------------------------------------------------------------------------------------+
|                                16 MiB DRAM Sound & System Memory                                       |
|                                                                                                        |
|  $000000-$0FFFFF : 1 MiB Lowmem / System Variables / Buffers (Writes tracked for $0CE3 rate mode)      |
|  $100000-$1FFFFF : 1 MiB Sample-RAM region                                                             |
|  $200000-$F7FFFF : 14 MiB Expanded Sample-RAM / Recording Buffers                                      |
+--------------------------------------------------------------------------------------------------------+
```

### 4.1 CPU and MC68302 IMP (source: `src/devices/machine/mc68302.cpp`)
- **Central Core:** Motorola 68000 core clocked at 16.0 MHz via `Y1` (`XTAL(16'000'000)`).
- **Internal DPRAM:** `$FC6000-$FC67FF` (2 KB), relocated by `BAR` initialization during cold boot.
- **Interrupt Routing:** Priority-arbitrated Level 4 interrupt controller:
  - Vector `$47` (PB9): ES5506 audio event / IRQB.
  - Vector `$4B`: MC68302 internal IDMA completion (used in recording).
  - Vector `$4D`: SCC1 receive byte interrupt.
  - Vector `$51` (External IRQ1): Shared storage completion (FDC and SCSI).
- **IDMA Engine:** Transfers sector data between FDC/SCSI FIFO and system memory during disk I/O, and between SCC and sample RAM during audio sampling.

### 4.2 Memory Subsystem (source: `src/mame/ensoniq/asr10_boot.cpp`)
- **Unified 16 MiB Canonical Backing (`m_ram`):** CPU-visible system RAM and ES5506 wavetable fetches access a single save-stated 16 MiB DRAM buffer.
- **ROM Overlay & Lowmem Tracking:** ROM overlays `$000000-$000007` at reset. Writes to `$000000-$0FFFFF` pass through `lowmem_w()`, maintaining tracking for `$000CE2/$000CE3` (rate mode commit) and system vector tables.
- **Memory Probe:** V3.50 ROM's native memory sizing probe (`$F8A166-$F8A244`) distinguishes `$008000/$408000/$808000/$C08000` and establishes `base = $00000000`, `size = $00F80000` (~15.5 MiB usable heap/sample RAM) without emulator patches.

### 4.3 CS1 Per-Voice Sample Banking (`$FF7F00-$FF7FFF`)
- **Table Structure:** 32 descriptors (8 bytes each, 4 words per voice) mapped across `$FF7F00-$FF7FFF`.
- **Address Translation:** ES5506 21-bit word addresses contain logical page bits 20:19, selecting one of the 4 words in the voice's table entry. Bits 18:0 select the 512K-word offset within that megabyte.
- **Physical Address Formula:**
  $$\text{phys\_byte\_address} = (\text{table\_word}[\text{voice}][\text{logical\_page}] \ll 20) \mid (\text{sub\_offset} \ll 1)$$
- **Digital Board Boundary:** The physical receiver IC(s) storing this table remain open (`DOCUMENTATION FRONTIER REACHED`). Functional translation is completely verified in emulation.

### 4.4 ES5506 OTTO Sound Engine (source: `src/devices/sound/es5506.cpp`)
- **Specification-Correct Clocks (commit `70cc09eed38c2686822b4821d10a4cbbaf84779e`):**
  - **Mode 0 (32 active voices, ACTV = 31):** Input CLK = `Y2 / 2 = 15.238090 MHz`.
    $$Fs = \frac{15,238,090}{16 \times 32} = 29,761.895\text{ Hz}$$
  - **Mode 1 (24 active voices, ACTV = 23):** Input CLK = `Y3 / 2 = 16.934400 MHz`.
    $$Fs = \frac{16,934,400}{16 \times 24} = 44,100.000\text{ Hz}$$
- **Authentic WaveSample Traversal:** Middle C (MIDI Note 60) traversal of `JM DIGI SYN` under specification-correct Mode 0 clock produces approximately **130.8 Hz** (reflecting the sample's C5 root key and ~99.65 word period).
- **Zero-Length Loop Support (commit `9ce3f000024d9bfa0aae845cd3e62e2d151b0e6f`):** Generic MAME `es5506.cpp` allows zero-length loops (`start == end`) to run, enabling authentic firmware BLE/IRQE boundary handling for samples >4 MiB.

### 4.5 ES5510 ESP DSP & Audio Routing (source: `src/devices/cpu/es5510/es5510.cpp`)
- **Host Interface:** Host upload/readback functional via `$FC3000-$FC31FF`.
- **Frame Adapter:** Functional ASR-specific frame adapter routes ES5506 serial lanes 0/1 to ES5510 SER0/SER2/SER3, runs ESP once per frame, and captures SER1 for post-effects audio.
- **Lifecycle & Quiescence:** 10 ms post-upload quiescence and PA4 muting enforce safe execution during effect program switches.

### 4.6 Storage Subsystem (Floppy, SCSI HDD, SCSI CD-ROM)
- **Floppy (NEC uPD72069, source: `src/devices/machine/upd765.cpp`):** Mapped at `$FC4000-$FC4003`. Spindle motor INDEX pulses drive DUART IP0. Standby auxiliary commands `$35/$34` delegated to `upd72065_device::auxcmd_w`. Floppy directory is cached in RAM; unprompted media swaps do not auto-invalidate cache.
- **SCSI (WD33C93A):** Mapped at `$FC5000-$FC5003`. Supports basic SCSI FORMAT, WRITE, READ, and cold boot from formatted media (report: `docs/asr10/investigations/writable-scsi-hdd-and-format.md`). Supports cold HDD boot (ID 0), runtime device switching, and CD-ROM browsing/loading (ID 4).

### 4.7 DUART & Front Panel Interface (sources: `src/devices/machine/mc68681.cpp`, `asr10panel_device`)
- **DUART:** SCN2681 mapped at `$FC4800-$FC481F`. Channel B communicates with panel at 62,500 baud 8N2.
- **Panel State Machine:** `asr10panel_device` maintains ASR cursor position, selected-field anchor (`$62`), field-relative rewrite (`$63`), and renders to generic 1x22 VFD.

### 4.8 Sequencer Subsystem
- **Established Execution Pipeline:**
  $$\text{Track Stream Decoder} \to \text{Track Event Queue} \to \text{Stepper (\$F902D2)} \to \text{Opcode \$0538 (\texttt{BTST D2, \$0CDE.w})} \to \text{TRAP \#3 Outbound Node} \to \text{TRAP \#C/\#12 to Queue \$00DC} \to \text{Sound Scheduler} \to \text{Voice Allocator} \to \text{ES5506}$$
- **Event Filter Mask (`$0CDE`):** Dynamic event filter mask. NOTE events (Type `$01`, $D2 = 0$) test bit 0 of `$0CDE`. `$0CDE` transitions from `$00` (idle) to `$3C` (Bank load) to `$3F` (Bank + Instrument load), enabling note dispatch.

---

## 5. Canonical Authentic Acceptance Fixtures

The fixtures below are reconciled against report `docs/asr10/investigations/authentic-factory-floppy-bank-load.md` and related investigations.

### A. Final Established Acceptance Fixtures (Factory BANKs)
1. **Fixture A — Single-Disk Clean BANK:**
   - **Medium:** `floppies/essential/AD-003.img` -> `CLN GTR BANK` (7 blocks).
   - **Scope:** 1 stereo instrument (`CLEAN GUITAR`, 845 blocks), 1 song (`GUIT ON BUY`, 9 blocks), 1 appended effect (`PHASER+DDL`, 4 blocks).
   - **Verdict:** **PASSED** (14.0s clean load, stereo voice allocation, audio peak 32,714 / 99.84%).
2. **Fixture B — Multi-Disk Media-Swap BANK:**
   - **Media:** `floppies/essential/ASR10Demo-Disk1.eda` & `ASR10Demo-Disk2.eda` -> `REZO ASR-BNK` (14 blocks).
   - **Scope:** 8 instruments spanning `ASR-001` and `ASR-002`, 1 song (`ASR-10`), 1 appended effect (`REVERB+EQ`).
   - **Verdict:** **PASSED** (Halt at Slot 5, prompt `INSERT ASR-002 - ENTER`, volume Block 2 validation at `$F89852`, completed load, polyphonic playback).
3. **Fixture C — Comprehensive 8-Instrument Showcase:**
   - **Medium:** `floppies/essential/AD-008.img` -> `AD-008 BANK` (7 blocks).
   - **Scope:** All 8 instrument slots populated (3,100 blocks = 1.58 MB across 9 objects), 1 song (`TIME BOMB`), 1 appended effect (`DIST+CHO+REV`).
   - **Verdict:** **PASSED** (50.0s load, 8-track instrument restoration, audio peak 32,739 / 99.91%).
4. **Baseline Local Control Fixture:**
   - **Medium:** `floppies/asr10booth/V350.img` -> `ATRK TUT BNK` (18 blocks).
   - **Scope:** 3 instruments (`BLUES DRUMS`, `BLUES BASS`, `BLUES ORGAN`), 1 song (`ATRK TUT SNG`), 1 appended effect (`44DDL+CH+REV`).
   - **Verdict:** **PASSED** (Single-disk local baseline, peak 32,768 / 100.0%).

### B. Exploratory and Corpus BANK Fixtures
- **Exploratory Single-Disk:** `floppies/essential/AD-001.img` (`SLOW MOVES`, `PIANO BANK 1/2`), `AD-002.img` (`SNDTRACKBANK`), `AD-004..007`.
- **EPS-16+ Factory Disks:** `floppies/EPS16plusFactory/` (`ED-001..015`, DD geometry, Type `$0017` Banks).
- **Waveboy Third-Party Disks:** `floppies/transwaves/` (`WB101..105`), `floppies/fx/` (`FMFX`, `WBFX12/38`).
- **Non-Canonical / Authoring Anomalies:** `CDR-16.chd` (`MOVE ME*8MB`, `NIGHTMRE*2MB`).

### C. Other Canonical Acceptance Fixtures
- **Large-WaveSample Continuation (>4 MiB):** `CDR-03.chd` -> `AUDIO DEMOS` -> `ICY TACO` WS1 (~6.4 MiB PCM owner, START=14612, END=6403388). **PASSED** (28.87 s continuous audio, 3 dynamic transitions).
- **16 MiB Sample RAM Allocation:** `CDR-04.chd` -> `9FT-BALDWIN` (3.68 MiB allocation, 26 PCM owners spanning MB 0..3). **PASSED** (Distinct 16 MiB backing, save/restore bit-identical).
- **SCSI CD-ROM & Device Switch:** `CDR-1.chd` -> `ORCH STRNGS1` (963 blocks, `$C1C9..$C58B`). **PASSED** (Cold HDD boot -> switch to SCSI 4 -> load -> audible Middle C).
- **Baseline Boot & Audio Check:** `floppies/asr10booth/V350.img` -> `JM DIGI SYN` (File 2). **PASSED** (20 acceptance checkpoints, ~130.8 Hz note audio).

---

## 6. CLOSED / FROZEN Milestones

1. **Large-WaveSample Continuation (>4 MiB) [CLOSED / FROZEN]**
   - commit: `9ce3f000024d9bfa0aae845cd3e62e2d151b0e6f` (`sound/es5506: allow zero-length loops to run`)
   - report: `docs/asr10/investigations/authentic-large-wavesample-continuation.md` (commit `2ffe91c34f0c4cf1b9cf3e573e35181b5ffdb5d1`)
   - source: `src/devices/sound/es5506.cpp`
   - Firmware sets temporary boundary `START == END == $80000000` with BLE+IRQE (`CR=$4330`). ES5506 IRQB asserts -> MC68302 PB9 -> Level 4 Vector `$47` -> ISR `$F8D072` -> IRQV read -> continuation callback (`$FFFF8EE2`) -> CS1 remapping -> continued playback.
2. **16 MiB Canonical Memory Backing [CLOSED / FROZEN]**
   - commit: `cca8be1ae339398461e53bc8ebaf303072edcca7` (`asr10: add 16 MiB sample memory backing`)
   - report: `docs/asr10/investigations/cdr04-16m-baldwin-experiment.md`
   - source: `src/mame/ensoniq/asr10_boot.cpp`
   - Replaced former modulo-2-MiB mirror with unified 16 MiB store; verified with CDR-04 `9FT-BALDWIN`.
3. **Dynamic CS1 Per-Voice Sample Banking [CLOSED / FROZEN]**
   - commit: `b7cd112199dd99f4b133e9d126d33ec18487780f` (`asr10: implement CS1 per-voice sample banking`)
   - report: `docs/asr10/investigations/cs1-voice-banking-and-sample-addressing.md`
   - source: `src/mame/ensoniq/asr10_boot.cpp`
   - Eliminates Bank 11 / `BLUES DRUMS` silent click / history-dependent failure (99.89% cross-correlation between fresh boot and chained paths; 8/8 consecutive loads identical hash `E1F17C2E`).
4. **Storage Subsystem (Floppy, SCSI HDD, SCSI CD-ROM) [CLOSED / FROZEN]**
   - commit: `0799328176b6d85ebbeafbfa1e0b5104d44086ad` (`docs/asr10: document scsi storage bringup, upd72069 fix and freeze storage milestone`)
   - reports: `docs/asr10/investigations/upd72069-standby-auxcmd-fix.md`, `docs/asr10/investigations/writable-scsi-hdd-and-format.md`
   - sources: `src/devices/machine/upd765.cpp`, `src/mame/ensoniq/asr10_boot.cpp`
   - Cold HDD boot, SCSI device switch, CD-ROM browse and load, and basic SCSI FORMAT/WRITE/read verified end-to-end.
5. **Authentic Factory Floppy / BANK Acceptance [CLOSED / FROZEN]**
   - commit: `e2e837248bdb33a89823ac356abc6ec9a88b2b8f` (`docs/asr10: document authentic factory BANK acceptance`)
   - report: `docs/asr10/investigations/authentic-factory-floppy-bank-load.md`
   - Single-disk (`CLN GTR BANK`, `AD-008 BANK`) and multi-disk (`REZO ASR-BNK`) factory BANK workflows fully verified under unmodified V3.50.
6. **Floppy Media-Change Semantics [CLOSED / FROZEN]**
   - commit: `5f5e76f80bb6a1137327acb7a9e24ab3f431f5fe` (`docs/asr10: document floppy media-change semantics`)
   - report: `docs/asr10/investigations/floppy-media-change-semantics.md`
   - Error code `$049D = 09` (`DISK HAS BEEN CHANGED`) verified as SCSI-only. V3.50 retains in-RAM directory cache across unprompted floppy swaps; explicit rescan (`CHANGE STORAGE DEVICE -> FLOPPY`) flushes and repopulates directory. Current emulation matches firmware policy.
7. **Specification-Correct ES5506 Clocks [CLOSED / FROZEN]**
   - commit: `70cc09eed38c2686822b4821d10a4cbbaf84779e` (`asr10: use specification-correct ES5506 clocks`)
   - source: `src/mame/ensoniq/asr10_boot.cpp`, `docs/asr10/lua/check_note_audio.py`
   - Mode 0 = Y2/2 (15.24 MHz, 32 voices, 29.76 kHz); Mode 1 = Y3/2 (16.93 MHz, 24 voices, 44.1 kHz). Note 60 audio verified at ~130.8 Hz.
8. **Audio Frame Adapter & Mode-Selected Rate Policy [CLOSED / FROZEN]**
   - commits: `7bfd2f3722d36fc39bb09bb9ffea829037c62c3e` (`asr10: add functional ESP frame adapter`), `a8481df1f72d5b62b10292f7e53f191fc6825c34` (`asr10: add likely audio rate policy`)
   - reports: `docs/asr10/investigations/es5510-upload-executable-contract-v350.md`, `docs/asr10/investigations/audio-rate-y2-y3-synthetic-policy-v350.md`
   - Functional frame adapter for ROM HALL and 44LUSH; mode-selected rate policy verified via A/B/A note test (~130.8..131.1 Hz).
9. **Driver Consolidation and Hygiene [CLOSED / FROZEN]**
   - commit: `5870769ee6f1df858ac9a055bd67037b3a29b013` (`asr10: consolidate driver structure and documentation`)
   - source: `src/mame/ensoniq/asr10_boot.cpp`

---

## 7. DISPROVEN and SUPERSEDED Hypotheses

| Hypothesis | Epistemic Verdict | Historical Context & Causal Finding |
|---|---|---|
| **ES5506 `START == END` requires immediate `STOP0`** | **[DISPROVEN]** | Falsified by authentic CDR-03 ICY TACO (>4 MiB) forward one-shot continuation. Firmware deliberately programs `START == END` with BLE+IRQE to trigger a boundary interrupt for cross-megabyte paging. |
| **NOTE sequencer playback requires `$0CDE` bit 5 (`$20`)** | **[DISPROVEN]** | Falsified by opcode disassembly at `$FFF90324` (`BTST D2, $0CDE.w`). NOTE is Event Type 1, which tests bit 0 ($D2 = 0$). `$0CDE` transitions `$00 \to $3C \to $3F` upon loading Bank + Instrument. |
| **Bank 11 failure caused by stale Sample-RAM** | **[DISPROVEN]** | Falsified by CS1 dynamic banking discovery. Failure was caused by missing CS1 per-voice banking translation ($FF7F00-$FF7FFF), which forced all voices to fetch from Chunk 0 instead of physical MB 7. |
| **H0/H2 timing drift was a BAD-audio reproducer** | **[DISPROVEN]** | Falsified: Timing drift was traced to `$0B6E` DUART counter/timer phase variation, not audio corruption. |
| **ESP upload-race hypothesis as cause of effect failure** | **[DISPROVEN]** | Falsified: PRE/POST execution comparison showed the audio pump was already halted during effect upload. |
| **Direct `Y2/2 <-> Y3/2` clock toggle without voice count change** | **[DISPROVEN]** | Falsified: ES5506 sample rate is governed by active voice slot count (`ACTV+1`). 29.76 kHz requires 32 slots; 44.1 kHz requires 24 slots. |
| **Modulo-2-MiB sample RAM backing model** | **[SUPERSEDED]** | Replaced in commit `cca8be1ae339398461e53bc8ebaf303072edcca7` by unified 16 MiB canonical store. |
| **Historical ~262 Hz Note 60 audio pitch** | **[SUPERSEDED / ARTIFACT]** | Artifact of running ES5506 at unscaled 30.47 MHz. Corrected to specification clock `Y2/2 = 15.24 MHz` in commit `70cc09eed38c2686822b4821d10a4cbbaf84779e`, yielding verified ~130.8 Hz. |
| **Slot 5 `$00780C` missing-sequencer-link** | **[SUPERSEDED]** | Falsified as execution entry point. Sequencer dispatch runs via Stepper (`$F902D2`), `$0CDE` event filter, and Queue `$00DC`. |
| **Naive FDC INTRQ -> MC68302 IRQ1 wiring** | **[SUPERSEDED]** | Caused `ERROR 129` Address Error due to spontaneous ready-line interrupts (`ST0=$C8`). Resolved by disconnecting ready line in MAME. |
| **CDR-16 `MOVE ME*8MB` load hang** | **[EXPLAINED / NOT EMULATOR DEFECT]** | Internal inconsistency: Directory `size.w == 3` with `+$15F == $04` (appended effect payload) derives a zero-length second heap allocation, causing a firmware allocator loop at `$F8A474-$F8A494`. |
| **CDR-16 `WRONG DISK INSERTED` error** | **[EXPLAINED / NOT EMULATOR DEFECT]** | Volume mismatch: Bank expects volume label `CDR-016`, while media header contains `ENSONIQID` at byte `$21F`. |

---

## 8. Current Open Frontier

The remaining open areas are categorized by engineering domain. Do not treat uninvestigated areas as bugs unless an authentic workload fails.

### A. Workload Acceptance Gaps
1. **Sequencer Multi-Track Musical Correctness (`[OPEN — workload-level musical correctness]`):**
   - Transport and event dispatch are verified and audible sound is produced.
   - However, complex multi-track songs/sequences (e.g. from factory demo disks or tutorial songs) have not been verified for musical arrangement fidelity, voice-stealing behavior, or rapid note-polyphony envelope scaling.
2. **EPS-16+ Factory Floppy Compatibility (`[OPEN]`):**
   - DD (800 KB) disk geometry and EPS-16+ Type `$0017` Banks remain unexercised under the current driver.
3. **Waveboy Commercial Disks (`[OPEN]`):**
   - Third-party transwave and effect disks (`floppies/transwaves/`, `floppies/fx/`) contain custom algorithms and external volume references that have not been validated.
4. **SCSI Object-Level Persistence (`[OPEN — object-level persistence acceptance]`):**
   - Save authentic ASR objects such as Instruments, Sequences, or Banks through V3.50 to writable SCSI media, reload them, and verify semantic and/or byte-level round-trip correctness. (Basic SCSI FORMAT, WRITE, READ, and cold boot from formatted media are already verified; this item is scoped strictly to high-level ASR filesystem object persistence).

### B. Physical Documentation Gaps (`DOCUMENTATION FRONTIER REACHED`)
1. **Physical CS1 Receiver IC(s):**
   - The discrete logic, PAL, or ASIC receiving `$FF7F00-$FF7FFF` on the 4-layer Digital Board remains unestablished due to absence of schematics in service literature. (Functional emulation is complete).
2. **Physical Audio Clock & Divider Routing:**
   - Physical ES5506 CLKIN generation, divider/mux circuitry, Y2/Y3 hardware switching, and ES5701 interaction remain open.
3. **Physical Storage Completion IRQ Glue:**
   - Vector `$51` is delivered via external IRQ1 and IDMA works, but physical PAL/GAL line sharing between FDC and SCSI remains open.
4. **Physical Floppy Pin 34 (DSKCHG):**
   - Exact mainboard wiring for floppy pin 34 remains unmeasured (V3.50 evaluates no consumer for it in floppy paths).
5. **Expansion Audio & Bus Routing:**
   - AUX1-AUX3 and OEX-6 expansion bus lines remain open.

### C. Technical Debt & Front Panel Edge Cases
1. **Display Protocol Edge Cases:**
   - `$67`, `$74-$76` one-operand panel commands, display blinking, and complete annunciator state coverage remain unmodeled, though standard menu navigation is stable.
2. **MC68302 Unimplemented Peripherals:**
   - Do not implement idle or unaccessed MC68302 registers unless required by an authentic workload.

---

## 9. Regression Suite and Acceptance Infrastructure

### Test Harness: `docs/asr10/regression-test.sh`
The test harness runs headless under `SDL_VIDEODRIVER=dummy` with `-video none -sound none -nothrottle`.

```sh
# Full regression run:
docs/asr10/regression-test.sh
```

### Checkpoint Structure (16 Targets, 20 Checkpoints, 21 PASS Lines)
1. `boot`: V3.50 boots to `FILE 1  TUTORIAL BNK` (1 PASS line).
2. `display`: VFD renders complete 22-character line (1 PASS line).
3. `button`: BTN_0A moves `FILE 1` -> `FILE 2` (1 PASS line).
4. `button_upper`: BTN_23 reaches firmware as two RHRB bytes (1 PASS line).
5. `nodisk`: Boot without disk produces `PLEASE INSERT DISK` (1 PASS line).
6. `file_loaded`: BTN_0A/23/02 loads `JM DIGI SYN`, moves 172,544 bytes via IDMA across 21 arms (1 PASS line).
7. `mc68302_guards`: Clean boot and load produces 0 unexpected exception vectors or SIB alarms (1 PASS line).
8. `note_audio`: Plays MIDI note, verifies voice writes (2,400) and WAV pitch at **~130.8 Hz** (2 PASS lines: MAME + WAV check).
9. `audio_rate_mode`: A/B/A effect rate mode switch (`ROM HALL` -> `44LUSH` -> `ROM HALL`), checks WAV pitch at ~130.8..131.1 Hz (4 PASS lines: MAME + A + B + A2 WAV checks).
10. `interrupt_controller`: Recording chain asserts priority-arbitrated Level 4 interrupts: vector `$4D` (SCC1), vector `$4B` (IDMA), and IMR `$E480 -> $EC80 -> $E480` (1 PASS line).
11. `memory_size`: ROM memory-size probe naturally concludes `base = $00000000`, `size = $00F80000` from 16 MiB store (1 PASS line).
12. `stereo_round_trip`: Injects interleaved byte patterns into SCC1/SCC2, checks exact readback and channel separation (1 PASS line).
13. `display_protocol`: Cursor/underline rendering and instrument-select lamp toggle (1 PASS line).
14. `panel_input`: Simultaneous button holds generate distinct wire events; BTN_0A modifies REC SRC (1 PASS line).
15. `panel_navigation`: Left Arrow (`$10`) and Right Arrow (`$11`) move underline between fields (1 PASS line).
16. `display_field_rewrite`: Loads `TUTORIAL SEQ`, rewrites TEMPO `90 -> 91 -> 92 -> 91 -> 90` via `$62/$63` anchor (1 PASS line).
17. **Line 174 Summary:** `echo "PASS regression"` emits the **21st PASS line**.

---

## 10. Git and Process Hygiene

1. **Two Execution Modes — Never Mix:**
   - **No-Media Boot:** `SDL_VIDEODRIVER=dummy ./mame asr10booth -video none -sound none -nothrottle -seconds_to_run 30` (Expects `PLEASE INSERT DISK`).
   - **With-Media Boot:** `SDL_VIDEODRIVER=dummy ./mame asr10booth -flop1 floppies/asr10booth/V350.img -video none -sound none -nothrottle -seconds_to_run 30` (Expects `FILE 1  TUTORIAL BNK`).
2. **Zero Orphaned MAME Processes:**
   - Always run `pgrep -fl mame` before and after tasks. Kill any stuck instances immediately.
3. **No Unrelated File Staging:**
   - Never stage `3rdparty/portaudio/bindings/java/jportaudio/bin/` or `docs/ensoniq/ASR-10_Cheat_Sheet_Recovered.docx`.
4. **C++ vs Lua Discipline:**
   - C++ edits are reserved exclusively for modifying emulated machine behavior.
   - All observation, probing, and tracing must be done via Lua scripts using `-autoboot_script`.
5. **No Instrumentation Left Behind:**
   - Delete temporary Lua scripts and scratch artifacts when investigations complete.

---

## 11. Mandatory "DO NOT REOPEN" Invariants

The following issues are conclusively resolved or disproven. Do NOT reopen them without new, contradictory authentic physical evidence:

1. **DO NOT reopen stale Sample-RAM as the BANK-load root cause.** (Resolved: CS1 per-voice sample banking).
2. **DO NOT reopen H0/H2 as a BAD-audio reproducer.** (Resolved: DUART timer phase).
3. **DO NOT search for a missing sequencer Slot 5 link.** (Superceded: Sequencer dispatches through `$F902D2` / `$0CDE` / Queue `$00DC`).
4. **DO NOT claim NOTE sequencer playback requires `$0CDE` bit 5.** (Disproven: NOTE is Event Type 1, tests bit 0).
5. **DO NOT restore ES5506 `START == END` immediate `STOP0`.** (Disproven: Firmware uses `START == END` for cross-megabyte WaveSample continuation interrupts).
6. **DO NOT use CDR-16 as canonical BANK media.** (Non-canonical: Contains authoring format anomalies).
7. **DO NOT implement floppy DSKCHG merely because media can be swapped.** (Closed: V3.50 does not evaluate DSKCHG for floppy; uses in-RAM directory cache).
8. **DO NOT model ES5701 as storage for the CS1 per-voice table.** (Disproven: ES5701 register model lacks 256-byte storage and LA20+ addressing).
9. **DO NOT return to the modulo-2-MiB sample backing model.** (Superceded: 16 MiB canonical backing is verified).
10. **DO NOT infer physical ASR wiring from functional MAME behavior.** (Epistemic separation must be preserved).
11. **DO NOT mass-implement incomplete MC68302 peripherals without an authentic workload requiring them.**
12. **DO NOT present the historical ~262 Hz Note 60 audio pitch as current acceptance truth.** (Superceded: Unscaled clock artifact; current verified pitch is ~130.8 Hz).
13. **DO NOT implement an explicit `es5701_device` without an authentic workload failure.** (Closed / Frozen: Generic ES5701 behavior is documented in `docs/asr10/investigations/es5701-super-glu-architecture.md`; current collapsed model passes all 50 internal ROM effects with 0 retries; exact board routing remains partially open; reopen only on new evidence or workload divergence).

---

## 12. Clean-Agent Bootstrap and Investigation Method

### 60-Second Orientation for Fresh Agents
1. Verify baseline status:
   ```sh
   git log -10 --oneline
   git status --short
   pgrep -fl mame || true
   docs/asr10/regression-test.sh
   ```
2. Read this document (`docs/asr10/OPENASR-HANDOFF-2026-09-13.md`).
3. Check the "DO NOT REOPEN" list (Section 11) before proposing any hypothesis.
4. Align on the proposed workload with the user before editing code.

### Canonical Investigation Cycle
```text
Authentic Workload
       ↓
First Firmware-Visible Semantic Divergence
       ↓
Minimal Causal Discriminator (Lua Tap / Probe)
       ↓
Targeted Minimal Correction (C++ only if behavior must change)
       ↓
Regression Suite Verification (docs/asr10/regression-test.sh)
```

---

## 13. Recommended Next Workload

**Candidate 1: Sequencer Multi-Track Musical Correctness (`[OPEN — workload-level musical correctness]`)**
- **Rationale:** Transport and single-note dispatch are frozen and verified. However, multi-track song playback from authentic factory disks (e.g. `ASR-001` demo song, `AD-001` sequences, or `TUTORIAL SEQ`) exhibits audible clicks or timing anomalies under polyphony.
- **Scope:** Trace sequencer tick countdown, event queue traversal, track-to-instrument voice allocation, and envelope/pitch updates without modifying production C++ code.

**Alternative Candidate: SCSI Object-Level Persistence (`[OPEN — object-level persistence acceptance]`)**
- Save authentic ASR objects such as Instruments, Sequences, or Banks through V3.50 to writable SCSI media, reload them, and verify semantic and/or byte-level round-trip correctness.

---

## 14. Key Commit and Landmark Index

| Commit SHA | Date | Description / Architectural Significance |
|---|---|---|
| `5f5e76f80bb6a1137327acb7a9e24ab3f431f5fe` | 2026-09-13 | `docs/asr10: document floppy media-change semantics` |
| `e2e837248bdb33a89823ac356abc6ec9a88b2b8f` | 2026-09-13 | `docs/asr10: document authentic factory BANK acceptance` |
| `5870769ee6f1df858ac9a055bd67037b3a29b013` | 2026-09-13 | `asr10: consolidate driver structure and documentation` |
| `2ffe91c34f0c4cf1b9cf3e573e35181b5ffdb5d1` | 2026-09-13 | `docs/asr10: document authentic large-WaveSample continuation and ES5506 resolution` |
| `9ce3f000024d9bfa0aae845cd3e62e2d151b0e6f` | 2026-09-13 | `sound/es5506: allow zero-length loops to run` (Generic MAME fix for WaveSamples >4 MiB) |
| `148952b3bb2e1f6e2106cb05eb3eb59cb1286c07` | 2026-09-13 | `docs/asr10: document CDR-16 bank load failures` |
| `791f57791834241680d294870020f5c9354086ad` | 2026-09-13 | `docs/asr10: consolidate current functional architecture` |
| `cca8be1ae339398461e53bc8ebaf303072edcca7` | 2026-09-12 | `asr10: add 16 MiB sample memory backing` |
| `b7cd112199dd99f4b133e9d126d33ec18487780f` | 2026-09-11 | `asr10: implement CS1 per-voice sample banking` |
| `70cc09eed38c2686822b4821d10a4cbbaf84779e` | 2026-09-04 | `asr10: use specification-correct ES5506 clocks` (Y2/2 Mode 0, Y3/2 Mode 1, ~130.8 Hz) |
| `0799328176b6d85ebbeafbfa1e0b5104d44086ad` | 2026-09-03 | `docs/asr10: document scsi storage bringup, upd72069 fix and freeze storage milestone` |
| `7bfd2f3722d36fc39bb09bb9ffea829037c62c3e` | 2026-08-30 | `asr10: add functional ESP frame adapter` |
| `a8481df1f72d5b62b10292f7e53f191fc6825c34` | 2026-08-30 | `asr10: add likely audio rate policy` |
