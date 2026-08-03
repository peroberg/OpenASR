# ASR-10 troubleshooting and service-reference notes

This file structures relevant ASR-10 service-manual troubleshooting information.

Purpose:

```text
- understand the ASR-10 module architecture
- map user-visible errors to likely subsystems
- preserve service-manual clues useful for MAME emulation
- separate hardware module knowledge from current emulator hypotheses
```

Do not treat this file as emulator proof by itself. Use it as external service/manual context to guide logging and hardware-model decisions.

## System overview

The ASR-10 is a self-contained computer system capable of:

```text
- sampling audio from external sources
- resampling its own audio output
- adding digital effects to samples
- sequencing
- digital audio track recording
- disk storage and retrieval
```

The service manual describes the ASR-10 as a modular system. Major modules can be replaced independently, and many faults can be narrowed by understanding the communication path between boards.

## Main modules

### 1. Disk drive

Purpose:

```text
Data storage and retrieval device.
```

Emulation relevance:

```text
Floppy behavior, disk geometry, ready/index/motor/density behavior, and uPD72069 result semantics matter for boot and file operations.
```

### 2. Power supply board

Purpose:

```text
Converts AC voltages to regulated DC and unregulated AC/DC distributed throughout the system.
```

Troubleshooting relevance:

```text
Bad power can cause broad, misleading failures across digital, analog, display, and disk subsystems.
```

### 3. Digital jack board

Purpose:

```text
Provides external connections:
- MIDI jacks
- CV pedal jack
- footswitch jacks
- connections for optional digital I/O board
```

### 4. Digital I/O board

Purpose:

```text
Optional board allowing direct digital audio input/output, such as DAT input/output.
```

### 5. Digital board

Purpose:

```text
Main engine of the ASR-10.
```

Contains:

```text
- microprocessor
- sound processor
- operating system memory
- sound memory
- effects processor
- floppy disk controller
- circuitry to control inputs and outputs
```

Emulation relevance:

```text
This is the primary board represented by the MAME driver:
- main CPU / MC68302-compatible behavior
- ROM/RAM map
- FDC
- SCSI interface path
- sound chips / DSP / memory
- panel/keyboard communication
- interrupt/timer/control-plane behavior
```

### 6. Keyboard / KPC board / keyboard adapter

Keyboard unit:

```text
Sends performance information to the digital board:
- note on
- note off
- pressure
```

Rack unit:

```text
Uses KPC simulator board instead of Poly-Key keyboard assembly.
```

ASR-88:

```text
Uses keyboard adapter board instead of KPC board.
```

Important manual convention:

```text
Where the manual says "keyboard", substitute:
- KPC simulator for ASR-10 Rack
- keyboard adapter board for ASR-88
unless otherwise noted.
```

### 7. Analog jack board

Purpose:

```text
Contains input preamp circuitry and audio inputs/outputs, except OEX-6 outputs.
```

### 8. Analog board

Purpose:

```text
- converts analog audio to digital audio and passes it to the digital board
- converts digital audio to analog audio
- supplies audio outputs and headphone amplifier
```

Emulation relevance:

```text
Audio input/output path is separate from CPU control logic.
PCM sample data should not be assumed to flow through MC68302 registers.
```

### 9. Keypad/display board

Purpose:

```text
- transmits button presses to the digital board through the keyboard/KPC path
- receives display data from the digital board through the keyboard/KPC path
```

Emulation relevance:

```text
Panel input/display is not likely a direct CPU-to-LCD connection.
There is an intermediate keyboard/KPC/panel communication path.
```

### 10. SCSI board

Purpose:

```text
Allows access to SCSI devices for storage/retrieval and disk-track digital recording.
```

Notes:

```text
- optional board on ASR-10 keyboard
- built into ASR Rack
- ASR-88 has SCSI
```

### 11. OEX-6 / OEX-6sr board

Purpose:

```text
Optional output expander adding three additional stereo D/A converters for six additional analog outputs.
```

Notes:

```text
- OEX-6sr built into ASR Rack
- optional for keyboard unit
```

### 12. Pitch/mod wheel assembly

Purpose:

```text
Transmits pitch and modulation wheel movements to the digital board.
```

Note:

```text
Not included on ASR-10 Rack.
```

### 13. Patch select button board

Purpose:

```text
Transmits patch select button presses to the digital board.
```

Note:

```text
Not included on ASR-10 Rack.
```

## Digital/analog board separation

The ASR-10 separates digital and analog sections for several reasons:

```text
- only the digital board requires a four-layer PCB
- analog section can use simpler two-layer board
- easier to break analog/digital connections when installing optically isolated SCSI
- helps prevent digital/SCSI noise reaching analog audio section
- jack boards keep external ESD exposure away from sensitive circuitry
```

Emulation relevance:

```text
Digital control problems, analog audio problems, and jack-board problems should be treated as separate subsystem classes.
```

## Keyboard and Rack similarities

The keyboard and rack use the same operating system disk.

Important differences:

```text
- ASR Rack released with 1.50B EPROMs and requires at least 1.50 disk OS.
- ASR Rack has KPC simulator board instead of keyboard assembly.
- ASR Rack has OEX-6sr output expander and SCSI interface built in.
- Digital I/O is optional for all ASR models.
- ASR-88 uses different EPROMs.
- ASR-88 has 16 MB RAM standard and SCSI.
- ASR-88 has keyboard adapter board instead of KPC board.
```

## Communication path

The service manual emphasizes that the communication path must be understood.

The following are described as complete computer systems:

```text
- digital board
- keypad/display board
- keyboard / KPC path
```

Each has:

```text
- its own microprocessor
- its own operating software
```

They communicate using serial communication ports.

### Keyboard to digital board

When a key is played:

```text
keyboard assembly microprocessor
-> transmits note/pressure data
-> digital board microprocessor
```

Communication:

```text
digital board <-> keyboard
two-line asynchronous interface
20-pin keyboard ribbon cable
```

### Keypad/display to digital board

Display output path:

```text
digital board
-> keyboard/KPC path
-> keypad/display board
-> display
```

Button input path:

```text
keypad/display board
-> keyboard/KPC path
-> digital board
```

Communication:

```text
keyboard <-> keypad/display board
three-line synchronous interface
carried through:
- 20-pin ribbon cable
- then 24-pin ribbon cable to keypad/display board
```

Important troubleshooting warning:

```text
Because several modules and serial links are involved, communication problems can be hard to isolate.
```

Emulation relevance:

```text
The panel/display path is likely a protocol/device path, not simply a raw LCD register.
The current candidate window $FC4800-$FC481F may represent a DUART/glue/frontpanel communication interface.
```

## Error messages

The service manual says occasional error messages are not unusual.

Important warning:

```text
These messages are diagnostics and do not necessarily indicate a hardware problem.
They were designed to help software engineers during development, not as complete hardware diagnostics.
```

## Software/system error messages

The following error messages could be caused by software:

| ID | Description |
|---:|---|
| 013 | software error in voice assignments |
| 016 | poly or mono pressure events sent to VC |
| 020 | unknown button event |
| 048 | parameter error |
| 049 | layer error |
| 080 | bad buffer to MIDI |
| 128 | bus error |
| 129 | odd address error |
| 130 | divide by zero |
| 131 | illegal instruction |
| 132 | CHK instruction register out of bounds |
| 133 | TRAPV instruction overflow error |
| 134 | privilege violation |
| 135 | trace |
| 137 | line 1111 emulator |
| 138 | spurious interrupt |
| 139 | unused vector |
| 192 | load all data error from MIDI or card |
| 193 | keyup playback error |
| 194 | out of SDBs error |

Emulation relevance:

```text
ERROR 129, 137, 138, 139 are especially useful while reconstructing CPU/vector/runtime behavior.
```

Current emulator relevance:

```text
ERROR 139 unused vector confirms wrong interrupt vector/autovector path.
ERROR 129 odd address error confirms a vector/path can reach real code but wrong state/context.
```

## Digital board problem messages

The following errors could be caused by a problem on the digital board:

| ID | Description |
|---:|---|
| 005 | could not synchronize audio input |
| 006 | could not synchronize audio input |
| 019 | bad OTTO interrupt |
| 032 | bad download |
| 033 | bad ESP chip |
| 034 | bad ESP FWM |
| 040 | bad ESP error |
| 145 | unknown DUART interrupt error |

Important distinction:

```text
Error 040 is ESP-related.
Error 40 is disk-controller/disk-drive related.
They are separate errors.
```

Emulation relevance:

```text
Error 145 may be important if DUART/panel/serial interrupt modeling is wrong.
```

## Analog or digital board clock problem

Error:

| ID | Description |
|---:|---|
| 009 | No LRCLK input to 68302 |

Manual note:

```text
Clock comes from analog board.
Check analog fuses and analog power supplies.
```

Emulation relevance:

```text
The digital board / 68302 side expects LRCLK or audio-clock status.
This supports the need for an LRCLK/status path in the emulator.
```

Current emulator relation:

```text
ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3 is a path-opening clock/status experiment.
```

Important caution:

```text
LRCLK status does not imply PCM sample data itself flows through MC68302 registers.
```

## MIDI/main board problem

Error:

| ID | Description |
|---:|---|
| 144 | out of buffers |

Likely causes:

```text
- too much incoming MIDI data
- possible keyboard problem
```

Emulation relevance:

```text
MIDI/keyboard event buffering may matter after panel/keyboard communication is modeled.
```

## Disk file operation errors

The following disk file operation errors could be caused by software:

| ID | Description |
|---:|---|
| 00 | illegal block number during read/write |
| 01 | missing End Of File marker |
| 02 | premature End Of File marker |
| 03 | file linked list points to unused block |
| 04 | file linked list points to bad block |
| 05 | current info in DIR buffer is not valid data |
| 06 | current info in FAT buffer is not valid data |
| 07 | current DIR and FAT info in buffer is not valid |
| 08 | not enough space to save file |
| 10 | file size greater than free blocks on device |
| 15 | no free blocks found on diskette |
| 16 | illegal FAT block number load was attempted |
| 17 | file size greater than 33 MB limit |
| 32-35 | NEC PD72069 errors |
| 40-44 | NEC PD72069 errors |

Important note:

```text
Error 40 is disk controller or disk drive related.
Error 040 is ESP related.
```

For error 40:

```text
First replace/check disk drive with known good drive.
If error persists, suspect the digital board / disk controller.
```

Emulation relevance:

```text
Errors 32-35 and 40-44 are likely useful when validating uPD72069/FDC behavior.
```

## Disk or disk-drive related errors

The following errors could be caused by a bad disk or disk drive:

| ID | Description |
|---:|---|
| 09 | block write attempt failed |
| 11 | device ID PCB load/save error |
| 12 | operating system PCB load/save error |
| 13 | directory save verify error |
| 14 | FAT save verify error |
| 22 | possible disk drive index pulse problem |

Emulation relevance:

```text
Error 22 may indicate missing or incorrect index pulse behavior.
```

## SCSI-related errors

The following errors could be caused by bad external SCSI cable or termination problem:

| ID | Description |
|---:|---|
| 18 | SCSI command not complete / no disconnect received |
| 20 | SCSI last command ignored |
| 21 | SCSI check condition error |

Special message:

```text
Not an EPS device
```

Note:

```text
"Not an EPS device" is not an error message.
See SCSI section of manual for explanation.
```

## Display self-test mode

When the keypad/display receives power but is not in proper communication with the digital board, the keypad/display board enters self-test mode.

In self-test mode:

```text
- display remains blank until control panel buttons are pressed
- pressing buttons causes the display to print characters, home cursor, etc.
```

Troubleshooting interpretation:

```text
If display responds in self-test mode, keypad/display board may be working.
Problem may be digital board or communication link through keyboard/KPC.
```

## Using self-test mode to diagnose keypad/display board

### Case 1: blank display but self-test works

If the unit has blank display but is in self-test mode, and buttons produce expected characters:

Likely problem:

```text
- digital board
- communication link between digital board, keyboard/KPC, and keypad/display board
```

Before replacing boards:

```text
Check all connections, especially 20-pin cable to keyboard/KPC.
```

Specific symptom:

```text
If pressing buttons only changes the leftmost character,
this usually indicates defective 20-pin ribbon cable connection
between digital board and keyboard, or possibly bad keyboard.
```

### Case 2: self-test active but display does not respond correctly

Likely problem:

```text
keypad/display board
```

If some buttons fail in normal operation:

```text
Test them in display self-test mode.
```

## Forcing display self-test mode

Manual procedure:

```text
With power off, face the front of the unit.
Jumper the minus (-) side of C83 to pin 1 of U64.
U64 is a 74F74 near J5.
C83 is located below J5, the digital jack board connector.
On power-up, the display stays in self-test as long as jumper is connected.
```

Emulation relevance:

```text
Self-test behavior confirms keypad/display board has local intelligence and firmware.
The panel should be modeled as a communicating device rather than a passive LCD only.
```

## Display self-test button mapping

The manual provides button-to-display-character behavior in self-test mode.

Partial structured mapping from provided text:

| Button | Display reads |
|---|---|
| LOAD | `8` |
| COMMAND | `$` |
| EDIT | `1.` |
| INSTRUMENT | `3` |
| SEQ/SONG | `9` |
| SYSTEM/MIDI | `I` |
| EFFECTS | `+` |
| 1/ENV 1 | `0` |
| 2/ENV 2 | `1` |
| 3/ENV 3 | `6` |
| 4/PITCH | `7` |
| 5/FILTER | `<` |
| 6/AMP | `=` |
| 7/LFO | Home Cursor |
| 8/WAVE | Home Cursor |
| 9/LAYER | `*` |
| 0/TRACK | `3.` |
| Down Arrow | `4.` |
| Left Arrow | `;` |
| Right Arrow | Home Cursor |
| CANCEL/NO | Space |
| ENTER/YES | `&` |
| Instrument Track 1 | `;` |
| Instrument Track 2 | Home Cursor |
| Instrument Track 3 | `5.` |
| Instrument Track 4 | `4` |
| Instrument Track 5 | `2.` |
| Instrument Track 6 | `>` |
| Instrument Track 7 | `0.` |
| Instrument Track 8 | `5` |
| EFFECT SELECT/BYPASS | `6.` |

Caution:

```text
The OCR/source text around this table is noisy.
Verify against original service manual pages before using exact key mappings.
```

Emulation relevance:

```text
Useful later for frontpanel input-port naming and possible keycode correlation.
Not needed for the current dispatcher/service blocker unless runtime proves it waits for panel input.
```

## Disk drive troubleshooting

### Transporting a unit

Manual warning:

```text
Do not transport the unit with a disk inserted.
Transport with drive empty.
Do not ship ASR or replacement drive in foam peanuts unless unit is wrapped in plastic.
Foam peanuts may damage disk drive or keyboard.
```

### Disk types

Manual recommends:

```text
3.5" double-sided double-density (DD) or high-density (HD) micro-floppy disks.
```

Important note:

```text
The ASR writes information to every track on a disk.
The entire disk must be good.
```

Avoid:

```text
MS-DOS preformatted disks are not always reliable and should not be used.
```

### Testing disk drive

Best test:

```text
format a disk in the ASR
```

Reason:

```text
Formatting reads/writes every track.
If formatting fails, disk may be bad.
Try another disk before determining drive is faulty.
```

Important behavior:

```text
Unlike some systems, ASR does not automatically discard bad sectors when formatting.
The entire disk must be good.
```

### Known disk-drive variants

Manual mentions:

```text
- two types of Panasonic drive
- one Sony drive
```

Important service notes:

```text
- Panasonic drive select should be set to 0.
- Some Panasonic drives have two-position drive select instead of four.
- When replacing Panasonic with Sony, a new 34-pin cable may be needed if the cable is too short.
- Sony 420-1 drive has a jumper block with specific settings shown in the manual.
```

Emulation relevance:

```text
Drive select should likely be drive 0.
Index/ready/media/density behavior matters.
```

### HD/DD compatibility warning

Manual warning:

```text
HD disks formatted as DD on a DD drive, such as EPS, EPS-16 PLUS, or Macintosh Plus,
may not be recognized in machines with HD drives.
This includes ASR and IBM PC/clone style HD drives.
```

Emulation relevance:

```text
Be careful with assumptions around DD/HD media type, density, and drive behavior.
```

## ASR-10 / ASR-88 software notes

### Checking ASR-10 software version

Procedure:

```text
Press Command, then Env 1.
Display shows NO COMMANDS ON PAGE.
Press left/right arrow until SOFTWARE INFORMATION.
Press Enter/Yes.
Display shows RAM VERSION = X.XX.
Press Enter/Yes.
Display shows ROM VERSION = Y.YY.
Press Enter/Yes.
Display shows KEYBOARD VERSION ZZZ.
For Rack, keyboard version always shows 1.
```

Emulation relevance:

```text
Useful future smoke test when panel input and OS UI work.
```

## ROM/disk compatibility

Important notes:

```text
- Keyboard and Rack use same OS disk.
- Rack released with 1.50B EPROMs and requires at least 1.50 disk OS.
- ASR-88 uses different EPROMs.
- Version 3.50 ROMs are for ASR-88 only.
- From OS version 2.00 onward, OS uses more RAM space due to Disk Tracks feature.
```

Emulation relevance:

```text
ROM and disk OS version pairing matters.
When testing V161/V350 or other images, confirm ROM compatibility.
```

## Copying operating system disks

General rule:

```text
If the OS being copied is the same version as currently running, fewer problems occur.
Copying a different OS version has restrictions.
```

Rules from manual:

| Running OS | OS to copy | Result / rule |
|---|---|---|
| 1.00-1.25 | 1.00-1.25 | OK |
| 1.00-1.25 | 1.50 or higher | destination disk must be empty |
| 1.50 | 1.00-1.25 | destination disk must be empty and same density as source |
| 1.50 | 1.50 or higher | OK |
| 1.60+ | 1.00-1.25 | destination disk must be same density as source |
| 1.60+ | 1.50 or higher | OK |

Note:

```text
Version compatibility is not an issue when using COPY DISK command.
```

Emulation relevance:

```text
OS disk layout and density assumptions may differ between ROM/OS versions.
```

## Selected OS release notes relevant to emulation

This section keeps only service-note points that may matter for emulator bring-up, stability, or regression testing.

### Keyboard software 2.41

Released:

```text
16 March 1993
```

Fixes problems in keyboard software 2.40:

```text
- rapid button presses could garble display
- missed button-up events
- parameter scrolling could run to extreme high/low
- display/button lock-up in extreme cases
```

Emulation relevance:

```text
Keyboard/panel communication has timing/queueing behavior.
Button up/down events matter.
```

### Disk OS 1.25

For:

```text
ASR-10 keyboards only.
Compatible with 1.00 ROMs.
```

Not a typical OS release; intended for customers with 1.00 ROMs not upgrading to SCSI/1.50 ROMs.

Relevant fixes:

```text
- sequence load corruption
- quantize/track-cleaner erasing track
- CHANGE SEQUENCE LENGTH on song track causing ERROR 129
- stereo wavesample/layer-map corruption
- low-level system data corruption
```

Known remaining problems:

```text
- MERGE TRACK range issue
- intermittent audio glitches due to OTTO losing sync
- TIME COMPRESS/EXPAND unreliable
- macro load/save problems
- 60-second timeout waiting for Enter/Cancel could lock display
```

### Disk OS 1.50

Released:

```text
28 Jan 1993
```

Requires:

```text
1.50B OS ROMs
```

Original release for:

```text
ASR Rack
```

New feature:

```text
ENTER PLAYS KEY
```

Relevant fixes:

```text
- sequence load corruption
- quantize/track-cleaner behavior
- CHANGE SEQUENCE LENGTH on song track / ERROR 129
- MERGE TRACK range issue
- OTTO sync/audio glitches
- stereo layer/wavesample map issue
- macro file load/save
- 60-second Enter/Cancel timeout crash changed to redisplay message
```

Known omissions/problems:

```text
- COPY SCSI DEVICE disabled
- SCSI BACKUP/RESTORE disabled
- TIME COMPRESS/EXPAND still not fully reliable
```

### Disk OS 1.60

Released:

```text
25 Feb 1993
```

Requires:

```text
1.50B OS ROMs
```

Relevant changes:

```text
- COPY SCSI DEVICE enabled
- SCSI BACKUP/RESTORE enabled
- COPY OS behavior fixed for older OS versions with density rules
- LOAD BANK / LOAD SONG crash with EPS/EPS-16 PLUS songs fixed
- sequence-load timing corruption fixed
- TIME COMPRESS/EXPAND splice-point behavior improved
```

### Disk OS 1.61

Released:

```text
16 Apr 1993
```

Requires:

```text
1.50B OS ROMs
```

Fix:

```text
COPY FLOPPY DISK could erase SCSI hard drive under specific conditions.
```

Conditions:

```text
- ASR-10 with SCSI interface
- hard drive SCSI ID 0
- destination floppy not formatted or wrong format/capacity
- user confirms ERASE AND FORMAT DISK
```

### Disk OS 2.51

Relevant fixes/features:

```text
- ENTER PLAYS KEY default/range corrected
- ERROR 129 on ASMPLNAME=UNDEFINED fixed
- COPY PITCH TABLE fixed
- invalid sequencer locate/GOTO hangs fixed
- memory shuffle messages added before sampling/create new instrument
- floppy disk change/FAT-cache issues fixed
- backup/restore directory creation hang fixed
- magneto-optical write-protect problem fixed
- Texel CD-ROM startup issue fixed
- memory management fixes for large RAM/audio samples
- sampling/RAMTrack fixes
- sequencer/display locate-page crash fixed
- faster SAVE SONG AND ALL SEQS
```

Emulation relevance:

```text
Many issues involve timing, memory management, display update, SCSI, and disk cache behavior.
Useful later for regression tests once OS UI and storage work.
```

### Disk OS 3.00

Production release for:

```text
all ASR-10 models:
- keyboard
- rack
- keyboard with SCSI
```

Requires:

```text
1.50 ROMs
```

OS size note:

```text
OS grew by 160 blocks compared with 2.51.
Free system blocks lower after boot.
Banks/instruments/project files near memory limit may no longer fit.
```

New features:

```text
- auto configure of DiskTracks
- auto prepare of audio tracks when song file is loaded
- import AKAI and Roland instruments
```

Relevant fixes:

```text
- audio track recording memory and KEEP NEW crashes
- DiskTrack overdub System Error 57
- song file load compatibility
- large song file / unreleased sequencer memory
- Delete Wavesample layer issue
- Restrike Time on stereo layers
- pops on muted sounds with sustain pedal and polyphony limit
- COPY FLOPPY DISK wrong-size/file-operation/format issues
- memory release behavior for ERASE SONG + ALL SEQS and other commands
- invalid instrument slot after failed load
- sequencer memory no longer erased automatically by oversized instrument load
- SysEx wavesample overview and recorder memory behavior
```

### Disk OS 3.08

Production release.

Requires:

```text
1.50 ROMs
```

No new features.

Relevant fixes:

```text
- MINI glide mode no-sound bug
- TRIGGER/LEGATO mode ERROR 129 on boot with multiple notes
- Roland import directory/import issues
- AKAI import program visibility
- AKAI import ticks/pops due to sample-start offset
- AKAI import ERROR 133
- aftertouch routing in AKAI import
- AKAI wavesample fine tuning
- no-free-instruments message after successful import into last bin
- PHASER+REVERB algorithm replaced due to ticks/pops
```

### Disk OS 3.53

Relevant note:

```text
Addresses global parameters not loading properly on bootup.
```

## Current emulator relevance summary

The service manual supports these current emulator assumptions:

```text
- ASR-10 is a multi-processor modular system.
- Digital board is the main emulation focus.
- Keypad/display is intelligent and communicates through keyboard/KPC path.
- Panel/display should not be treated as passive LCD only.
- LRCLK input to 68302 is a real expected status/clock dependency.
- Error 139 means unused vector.
- Error 129 means odd address error.
- Error 040 and Error 40 are distinct.
- uPD72069/FDC errors are in the 32-35 and 40-44 disk-error ranges.
- Rack with 1.50B ROMs requires at least 1.50 disk OS.
```

Current project-specific status:

```text
The emulator already reaches LOADING SYSTEM.
The current blocker is dispatcher idle after the MC68302/FC68xx 0x2400 service sequence,
not basic display text, not raw panel key input, and not proven FDC/media yet.
```

## Current one-line troubleshooting takeaway

For MAME bring-up, this service text mostly reinforces that ASR-10 boot/runtime behavior depends on a digital-board control plane plus serial communication with intelligent keyboard/panel modules; the most relevant current clues are `ERROR 009 No LRCLK input to 68302`, `ERROR 139 unused vector`, `ERROR 129 odd address error`, keypad/display self-test behavior, and the fact that disk/FDC, SCSI, panel, audio, and keyboard problems are separate subsystem classes.
