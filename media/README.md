# OpenASR — Media Directory

This directory is intended for user-provided storage media, including SCSI hard disk images and CD-ROM images.
**No copyrighted software, factory disks, OS images, or sound libraries are distributed with OpenASR.**

## Supported Formats

### SCSI Hard Disk (`-hard1`, SCSI ID 0)
- **MAME CHD format** (recommended): `asr10_hdd.chd`
  - Created using `chdman createhd -i asr10_hdd.raw -o asr10_hdd.chd`
  - Can be formatted directly within the ASR-10 firmware using the `SYSTEM/MIDI -> FORMAT SCSI DRIVE` menu.
- **Raw hard disk image**: `asr10_hdd.hd` or `asr10_hdd.raw`

### SCSI CD-ROM (`-cdrom`, SCSI ID 4)
- **MAME CHD format** (recommended): `cdr1_sound_library.chd`
  - Created using `chdman createcd -i cdr1.cue -o cdr1_sound_library.chd`
- **ISO / CUE+BIN / CDR**: `cdr1.iso`, `cdr1.cue`
- Authentic Ensoniq CD-ROMs (such as the Ensoniq CDR-series sound libraries) can be browsed and loaded via SCSI ID 4.

## Example Invocations
```sh
# SCSI HDD boot
./mame asr10booth -hard1 media/asr10_hdd.chd

# CD-ROM browsing with floppy OS
./mame asr10booth -flop1 floppies/asr10booth/V350.img -cdrom media/cdr1_sound_library.chd

# Combined SCSI HDD + CD-ROM
./mame asr10booth -hard1 media/asr10_hdd.chd -cdrom media/cdr1_sound_library.chd
```
