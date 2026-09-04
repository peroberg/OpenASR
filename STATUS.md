# OpenASR — System Status

This document provides a factual summary of what currently works, what is partially verified, known limitations, and active research questions in OpenASR.

Last updated: September 2026.

---

## 1. Working

### Processor & Bus Architecture
- **Motorola MC68302 SIM**:
  - Base Address Register (`BAR`) and System Control Register (`SCR`) decoding.
  - Chip select logic: `CS0` (boot ROM), `CS1` (system/sample RAM), `CS2` (DUART SCN2681), `CS3` (FDC uPD72069), and `CS4` (SCSI WD33C93A).
  - Port A GPIO: `PACNT`, `PADDR`, `PADAT` with external output notification callbacks. Pin PA4 drives the ESP HALT/RUN lifecycle.
  - Port B GPIO: `PBCNT`, `PBDDR`, `PBDAT` handling analog multiplexer channel selection (`PB0`-`PB2`).
  - Interrupt Controller: Authentic vector generation for internal channels and external IRQs (`$4B` level 4 autovector, `$4D` level 6 DUART autovector/IACK cycle).
  - Save-state support across all implemented SIM registers.

### Boot & Operating System Progression
- Cold boot from authentic ROM v1.50B (`asr-648c-lo-1.5b.bin` / `asr-65e0-hi-1.5b.bin`).
- Hardware self-test passing with 0 diagnostic error codes.
- Operating system loading from floppy (OS V3.50 and V1.61) and SCSI hard disk.
- Memory allocation support for 2 MB baseline through 16 MB expanded configurations.

### Storage Subsystems
- **Floppy Controller (NEC uPD72069)**:
  - Supports 3.5" HD floppy images (`.img`, `.mfi`, `.hfe`).
  - Correct execution of standby auxiliary commands (`$35` set standby, `$34` reset standby) without spurious FIFO result phases.
  - Floppy IDMA data transfer with terminal count handling.
- **SCSI Controller (Western Digital WD33C93A)**:
  - Direct SCSI hard disk boot (`-hard1`, SCSI ID 0).
  - Low-level disk formatting via authentic firmware routines (`FORMAT SCSI DRIVE`).
  - Partitioning, directory traversal, file loading, and writing back to disk.
  - SCSI CD-ROM browsing and bank loading (`-cdrom`, SCSI ID 4) with 512-byte sector support for authentic Ensoniq CDR-series sound discs.
  - Simultaneous dual-device configurations (HDD at ID 0, CD-ROM at ID 4).

### Audio Synthesis & Effects DSP
- **Ensoniq OTTO (ES5506)**:
  - 6 audio output channels (`set_channels(6)`).
  - Authentic serial bus routing: `BUS1` (outputs 0/1) to `SER0`, `BUS2` (outputs 2/3) to `SER2`, and `BUS3` (outputs 4/5) to `SER3`.
  - Specification-correct clock domains:
    - Mode 0 (30 kHz standard mode, 32 voices): $Y_2/2 = 15,238,090\text{ Hz} \implies F_s = 29,761.895\text{ Hz}$.
    - Mode 1 (44.1 kHz mode, 24 voices): $Y_3/2 = 16,934,400\text{ Hz} \implies F_s = 44,100.000\text{ Hz}$.
    - Firmware reference `$0D66` traversal math matches ES5506 rate stepping with a 1.000000 ratio.
  - Verified acoustic pitch accuracy: Middle C (MIDI note 60) for authentic factory samples generates nominal frequencies within 0.3% equal temperament tolerance.
- **Ensoniq ESP (ES5510)**:
  - Firmware-controlled lifecycle via MC68302 PA4: ESP is held in reset during microprogram and GPR write passes, and runs during audio processing.
  - Correct effects patch compilation, parameter updates, and audio routing through DSP algorithms (reverb, chorus, delay).

### Front Panel & User Interface
- 22-character vacuum fluorescent display (VFD) output with cursor underline tracking and flashing fields.
- Softkey keypad input handling and data entry slider emulation.
- Interactive bank selection, instrument loading, sample editing, and system parameter modifications.

### Testing & Automation
- 12-test automated regression suite covering cold boot, storage, sound generation, rate switching, interrupts, and display protocol.

---

## 2. Partially Verified

- **SCSI Initiator Timing**: While reading, writing, and formatting are robust across all tested workloads, bus disconnect/reselect edge cases under heavy asynchronous transfer warrant additional verification.
- **Sample Rate Conversion**: WaveSample sample rate editing and cross-rate interpolation work correctly in software tests, but extreme pitch transpositions (±4 octaves) have not been exhaustively mapped.
- **Save State Coverage**: MC68302, memory, and board glue support save states; sound devices (ES5506, ES5510) and SCSI controllers inherit upstream MAME state support with ongoing testing.

---

## 3. Known Limitations

- **Front-Panel Geometry**: The current MAME layout arranges buttons logically for functional access and debugging, rather than replicating the exact physical front-panel geometry of the rack/keyboard unit.
- **Generic ES5510 Execution Speed**: The ESP DSP emulation runs as a host interface adapter; real-time internal DSP execution timing does not model cycle-by-cycle pipeline delays.

---

## 4. Research & Open Questions

- `[USER OBSERVATION] / [OPEN]` **Dense Demo Audio Artifacts**:
  Some dense multi-instrument demo material exhibits intermittent, rhythmically correlated audio artifacts (crackle/noise bursts). Simple and moderate voice-density patches play cleanly with verified pitch. The root cause has not yet been localized.
- `[OPEN]` **Sequencer Validation**:
  Sequencer playback is functional, audible, and responds to all front-panel transport commands (Play, Stop, Continue). However, comprehensive cycle-accurate timing and voice-stealing comparison against real hardware recordings remains open.
- `[OPEN]` **Physical Board Clock Distribution**:
  The $Y_2/2$ and $Y_3/2$ clock rates fed to OTTO are mathematically and acoustically verified, matching the 16 MHz limit of the ES5506 specification and the firmware's rate tables. The exact physical divider and multiplexing circuitry on the ASR-10 PCB (whether via discrete logic or custom gate array) has not been traced from physical schematics.
