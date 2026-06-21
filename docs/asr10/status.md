
# status.md

# ASR-10 current status

## Goal

Bring up a working Ensoniq ASR-10 in MAME, eventually comparable in usability to the existing VFX/Ensoniq-family emulation.

Short-term goal:

```text
Boot ROM -> mount ASR OS disk -> read OS sectors -> reach LOADING SYSTEM / OS execution
```

Long-term goal:

```text
Usable ASR-10 emulation:
- OS boots from floppy/SCSI
- display and panel input work
- memory map and sample RAM work
- ES5506/ES5510 audio path works
- sequencer and UI work through original OS
```

## Current source file

Current experimental harness:

```text
src/mame/ensoniq/asr10_boot.cpp
```

Current target:

```text
asr10booth
```

This is a research/diagnostic harness, not the final clean driver.

Future clean target should likely be:

```text
src/mame/ensoniq/asr10.cpp
```

with `asr10booth` retained as debug/boot-research target while useful.

## Current ROMs

ROM directory:

```text
roms/asr10booth/
```

Known ROM files:

```text
asr-648c-lo-1.5b.bin
asr-65e0-hi-1.5b.bin
```

Current load pattern in harness:

```cpp
ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, ...)
ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, ...)
```

Note: original MAME skeleton may have used hi/lo differently. This should remain on the verification list.

## Current floppy images

Local, not committed:

```text
floppies/asr10booth/V161.img
floppies/asr10booth/V350.img
```

Both are raw 1.6 MB ASR images:

```text
1,638,400 bytes
80 tracks * 2 sides * 20 sectors * 512 bytes
```

Do not commit disk images.

Recommended `.gitignore` entry:

```gitignore
/floppies/
```

## Current blocker

Raw `.img` images are found but not identified by MAME:

```text
Fatal error: Device 3.5" double density floppy drive load failed:
Unable to identify image file format
```

This means:

```text
MAME did not mount the image.
The ASR ROM did not yet reject the disk.
The current blocker is floppy image format support, not ROM/FDC logic.
```

Need to add ASR-10 raw 1.6 MB `.img` floppy format:

```text
80 cylinders
2 heads
20 sectors per track
512 bytes per sector
1,638,400 bytes total
MFM
3.5" DSDD-like container from MAME perspective
```

## Current known-good baseline

No-media run works and is useful as regression baseline.

Without media:

```text
drive_attached=1
media_mounted=0
ready=0
motor=1
density=dd
FDC Recalibrate/Sense result = 68,00
Panel eventually reaches PLEASE INSERT DISK path
No ASR10HANG
```

This proves:

```text
- boot harness builds
- ROM executes
- panel text path works
- FDC is visible
- fdc:0 connector is attached
- no-media behavior is consistent
```

## Immediate next step

1. Commit current harness checkpoint if not already committed.
2. Add raw ASR-10 1.6 MB `.img` floppy format.
3. Register it before `FLOPPY_ESQIMG_FORMAT`.
4. Run `V161.img`.
5. Check for:

```text
media_mounted=1
ready=1
format=asr10_img or similar
```

6. Then inspect first `0x46 Read Data` command.


---

