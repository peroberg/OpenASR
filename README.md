# OpenASR

**OpenASR** is an open-source emulator and reverse-engineering research platform for the **Ensoniq ASR-10** (Advanced Sampling Recorder), developed on top of the [MAME](https://github.com/mamedev/mame) emulation framework.

The project aims to achieve cycle-accurate, hardware-faithful emulation of the ASR-10 architecture, including its Motorola MC68302 processor, Ensoniq OTTO (ES5506) wavetable synthesis engine, Ensoniq ESP (ES5510) digital signal processor, SCSI controller (WD33C93A), floppy disk controller (uPD72069), and front-panel VFD interface.

> **Development Status**: OpenASR is in **active development / research preview**. Core system architecture, booting, storage, and synthesis pipelines are functioning, while higher-level sequence validation, UI layout refinement, and edge-case timing fidelity remain under active investigation.

---

## Current Status

### Known Working
- **Firmware Boot**: Boots authentic Ensoniq boot ROMs (`asr-648c-lo-1.5b.bin` / `asr-65e0-hi-1.5b.bin`) through MC68302 initialization, power-on diagnostics, and disk readiness checks.
- **Floppy Subsystem**: High-density 3.5" disk image support (OS Version 3.50 and 1.61) with uPD72069 floppy controller, IDMA data transfer, and standby auxiliary command support.
- **SCSI Storage Subsystem**: WD33C93A SCSI controller supporting:
  - Direct SCSI hard disk booting (`-hard1`, SCSI ID 0)
  - Authentic low-level disk formatting via firmware (`SYSTEM/MIDI -> FORMAT SCSI DRIVE`)
  - Persistent read/write operations and file system mounting
  - SCSI CD-ROM browsing and bank loading (`-cdrom`, SCSI ID 4, supporting authentic Ensoniq CDR-series libraries)
  - Simultaneous multi-device configurations (HDD + CD-ROM)
- **Memory & System Architecture**: 2 MB – 16 MB sample RAM aliasing, MC68302 SIM (BAR/SCR, chip selects, Port A GPIO, Port B GPIO, interrupt controller with authentic IACK autovectored handling).
- **Sound Synthesis (ES5506 / OTTO)**:
  - 6-channel output with authentic bus routing (`BUS1/2/3` to ESP serial ports `SER0/2/3`)
  - Specification-correct clock domains: Mode 0 ($Y_2/2 = 15.238090\text{ MHz} \to F_s = 29,761.895\text{ Hz}$) and Mode 1 ($Y_3/2 = 16.934400\text{ MHz} \to F_s = 44,100.000\text{ Hz}$) matching authentic firmware reference `$0D66` traversal math 1:1.
- **Effects DSP (ES5510 / ESP)**: Firmware-driven execution lifecycle controlled via MC68302 Port A pin PA4 (halted during microcode/GPR upload, running during audio processing).
- **Sequencer**: Real-time event dispatch, transport controls (Play/Stop/Continue), and clocking operational.
- **Front Panel & Display**: 22-character vacuum fluorescent display (VFD) output, softkey navigation, and parameter editing.
- **Automated Regression Harness**: 12-stage non-interactive headless test suite verifying boot, UI, storage, audio capture, and operating modes.

### Open Areas & Known Limitations
- **Multi-Voice / Dense Demo Playback**: Certain dense multi-instrument sequences exhibit intermittent audio artifacts (rhythmic crackle). Cause has not yet been localized.
- **Sequencer Verification**: While sequence playback runs and responds to transport controls, comprehensive timing and event-mask verification against real hardware recordings remains ongoing.
- **Front-Panel Geometry**: Current MAME layout provides functional debug access to all front-panel buttons, but does not yet visually mirror the physical ASR-10 chassis panel layout.
- **Board-Level Clock Routing**: The $Y_2/2$ and $Y_3/2$ clock domains are verified numerically and acoustically, but physical PCB multiplexer/divider topology between crystals and OTTO pins remains unverified by physical schematics.

---

## Building OpenASR

OpenASR is built using MAME's standard build system.

### Prerequisites
- **Compiler**: Modern C++17 compiler (Clang 12+ or GCC 10+)
- **Build Tools**: GNU Make, Python 3
- **Libraries**: SDL2 (including development headers)

### Compilation Commands

To build only the OpenASR driver and its immediate dependencies (recommended for development):
```sh
# On Linux / macOS (adjust -j to your CPU core count)
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

To build the entire MAME suite:
```sh
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

The resulting binary is named `mame` (or `mame.exe` on Windows) in the repository root.

---

## Running OpenASR

The driver name is **`asr10booth`**.

### 1. No-Media Boot (Diagnostics)
Tests firmware boot and front-panel prompt without loading an OS disk:
```sh
./mame asr10booth
```
*Expected display*: `PLEASE INSERT DISK`

### 2. Standard Floppy Boot (OS V3.50)
Boots the ASR-10 operating system from a floppy disk image:
```sh
./mame asr10booth -flop1 floppies/asr10booth/V350.img
```
*Expected display*: `FILE 1  TUTORIAL BNK` (or default loaded bank)

### 3. SCSI Hard Disk Boot
Boots directly from an Ensoniq-formatted SCSI hard disk image mounted at SCSI ID 0:
```sh
./mame asr10booth -hard1 media/asr10_hdd.chd
```

### 4. SCSI CD-ROM Browsing
Loads the OS from floppy and attaches an Ensoniq CDR sound library at SCSI ID 4:
```sh
./mame asr10booth -flop1 floppies/asr10booth/V350.img -cdrom media/cdr1_sound_library.chd
```

### 5. Simultaneous Hard Disk + CD-ROM
Full production studio configuration:
```sh
./mame asr10booth -hard1 media/asr10_hdd.chd -cdrom media/cdr1_sound_library.chd
```

### Headless / Benchmark Mode
For fast, non-GUI execution (e.g. for batch testing or tracing):
```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30
```

---

## Automated Regression Testing

The test suite validates the complete software stack headlessly using Lua integration scripts:
```sh
docs/asr10/regression-test.sh floppies/asr10booth/V350.img
```

### Test Suite Scope
1. **Boot**: Cold boot to `FILE 1  TUTORIAL BNK` prompt.
2. **Display**: Character ring buffer and VFD output.
3. **Button**: Keypad scan and channel navigation.
4. **No-disk**: Cold boot without media to `PLEASE INSERT DISK`.
5. **File Loaded**: Floppy DMA transfer of 172 KB bank.
6. **MC68302 Guards**: Bus error and unhandled interrupt assertion guards.
7. **Note Audio**: MIDI Note-On transmission, ES5506 voice register programming, and audio WAV capture.
8. **Audio Pitch**: Autocorrelation pitch detection validating 130.8 Hz fundamental frequency for JM DIGI SYN Note 60 under 29.76 kHz Mode-0 clock.
9. **Audio Rate Mode**: A/B/A switching between 29.76 kHz (Mode 0) and 44.1 kHz (Mode 1).
10. **Interrupt Controller**: MC68302 IACK autovectoring (`$4B`/`$4D`).
11. **Memory Size**: Low-memory allocator boundary check (2 MB / 16 MB).
12. **Display Protocol & Field Rewrite**: Cursor navigation, underline tracking, and tempo field edits.

*Prerequisite*: Requires a local, legally obtained `V350.img` floppy image.

---

## Project Layout

```text
├── src/
│   ├── mame/ensoniq/
│   │   └── asr10_boot.cpp       # Main ASR-10 system driver & hardware bus wiring
│   └── devices/
│       ├── machine/mc68302*     # Motorola MC68302 Integrated Multiprotocol Processor
│       ├── machine/wd33c9x*     # Western Digital WD33C93A SCSI Controller
│       ├── machine/upd765*      # NEC uPD72069 Floppy Disk Controller
│       ├── sound/es5506*        # Ensoniq ES5506 (OTTO) Wavetable Synthesizer
│       └── sound/es5510*        # Ensoniq ES5510 (ESP) Signal Processor
├── docs/
│   ├── asr10/
│   │   ├── README.md            # Comprehensive research documentation index
│   │   ├── current-status.md    # Active engineering baseline and verified findings
│   │   ├── regression-test.sh   # Full acceptance test suite
│   │   ├── reference/           # Architectural specifications and subsystem guides
│   │   ├── investigations/      # Detailed engineering investigation reports
│   │   └── lua/                 # Automated testing probes and headless harnesses
│   └── ensoniq/
│       └── README.md            # Bibliographic index and SHA-256 hashes of silicon manuals
├── roms/
│   └── README.md                # ROM placement instructions and checksums
├── media/
│   └── README.md                # Hard disk and CD-ROM placement instructions
└── floppies/
    └── README.md                # Floppy image instructions
```

---

## Legal & Media Notice

OpenASR is an independent reverse-engineering and emulation research project. It is **not** affiliated with, endorsed by, or sponsored by Ensoniq Corp. or its successors.

- **No ROMs or Firmware Included**: System ROMs (`asr-648c-lo-1.5b.bin`, `asr-65e0-hi-1.5b.bin`) and OS images (`V350.img`, `V161.img`) are the intellectual property of their respective copyright holders and are **not** distributed with OpenASR.
- **No Commercial Media Included**: Factory sound libraries, sample disks, CD-ROMs, and demo sequences must be provided by the user from legally obtained physical media or authentic backups.
- **MAME Ancestry**: OpenASR includes MAME core and device emulation code subject to the MAME licensing terms (BSD-3-Clause and GNU GPL-2.0+). See `LICENSE` for details.
