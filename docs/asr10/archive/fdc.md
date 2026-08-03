# ASR-10 FDC findings

This file tracks ASR-10 floppy/FDC findings.

Important current-context note:

```text id="h8q0sr"
FDC/media findings are still valid, but FDC/media is not the immediate current blocker once the emulator reaches LOADING SYSTEM.
```

The current immediate blocker is:

```text id="kob7io"
runtime dispatcher idle around f87f96/f87f9a after the accepted-looking MC68302/FC68xx 0x2400 service sequence
```

Do not add new FDC behavior stubs unless logs show post-service FDC activity.

## Current relevance

The FDC work was essential for reaching later boot paths and should remain documented.

However, once the emulator reaches:

```text id="1r71pg"
ENSONIQ ASR-10
LOADING SYSTEM
```

and then returns to:

```text id="y3rj09"
f87f96 / f87f9a / f87fca dispatcher idle
```

the next blocker appears to be outside the immediate FDC/media layer.

Current likely blockers are:

```text id="gitrbf"
- dispatcher queue re-arm
- event payload
- timer tick/timebase side effect
- FC6884/FC6894 completion behavior
- lowmem service state around $0d06/$0e82
```

FDC should be revisited when logs show one of:

```text id="5e6dvb"
- post-service FDC activity
- a new 0x46 Read Data command
- media/status polling after LOADING SYSTEM
- dispatcher progress into disk/media code
```

## Hardware

FDC:

```text id="ha7ijb"
NEC uPD72069
```

Current candidate mapping:

```text id="pnrj6a"
$FC4000-$FC4003
```

Current MAME device:

```cpp id="i4ubdt"
UPD72069(config, m_fdc, XTAL(16'000'000));
FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35dd", asr10_boot_state::floppy_formats, true);
```

Connector tag:

```text id="4pu3sa"
fdc:0
```

The no-media baseline proves the FDC sees an attached drive:

```text id="2t4naz"
drive_attached=1
media_mounted=0
ready=0
```

## Important conclusion

The `fdc:0` child connector is likely working.

No explicit `m_fdc->set_floppy(...)` has been needed so far because the conventional child connector tag appears to be discovered by the uPD72069 device.

Do not add explicit connection hacks unless evidence shows the controller cannot see the drive.

## Observed command sequence

Early command sequence included:

```text id="b6os71"
36, 0B, 4F, 1E, 0E
```

After passing the input gate, later sequence includes:

```text id="a5lcxc"
88, F3, 03, 07, 00, 08
```

Interpretation of later sequence:

```text id="663p06"
0x88 -> uPD72069 data-rate command, selects 250 kbit/s
0xF3 -> uPD72069 precomp/precompensation auxiliary command
0x03 E1 09 -> Specify
0x07 00 -> Recalibrate drive 0
0x08 -> Sense Interrupt Status
```

Caution:

```text id="bf6g7h"
The FDC interpretation above belongs to the earlier boot/media phase.
It should not be used to explain the current f87f9a dispatcher idle unless new logs show post-service FDC access.
```

## No-media result

Without media:

```text id="c9wd7n"
CMD 88 result: 80
F3 transaction includes FIFO reads: 80,68,00
```

Important decoded meaning:

```text id="70l5p0"
68,00 is Sense Interrupt Status result:
ST0=0x68
PCN=0x00
```

`0x68` means approximately:

```text id="32e24b"
abnormal termination / failure
seek end
drive not ready
```

ROM stores:

```text id="l619f0"
$04C6 = 68
```

Then ROM checks:

```text id="34wx03"
$04C6 & 0xC0
```

Since bit 6 is set:

```text id="02mre1"
ROM writes $04AE=2B
ROM writes $049D=0D
```

## Earlier raw image problem / still open

Raw `.img` images:

```text id="eespj8"
V161.img = 1,638,400 bytes
V350.img = 1,638,400 bytes
```

Expected ASR geometry:

```text id="m8vm9d"
80 tracks
2 sides
20 sectors per track
512 bytes per sector
total = 1,638,400 bytes
```

Earlier/current image recognition error:

```text id="vnak5i"
Unable to identify image file format
```

Current format registration:

```cpp id="mgu3fs"
void asr10_boot_state::floppy_formats(format_registration &fr)
{
    fr.add_mfm_containers();
    fr.add(FLOPPY_ESQIMG_FORMAT);
    fr.add(FLOPPY_HFE_FORMAT);
}
```

Conclusion:

```text id="9y592v"
FLOPPY_ESQIMG_FORMAT does not recognize these ASR-10 1.6 MB raw .img files.
A raw ASR-10 .img format may still be needed.
```

Current relevance:

```text id="3mgxby"
This remains an open FDC/media task, but it is not the immediate current blocker if the emulator already reaches LOADING SYSTEM and then idles in the dispatcher.
```

## Required image format

Potential raw format:

```text id="0msyij"
name: asr10_img
description: Ensoniq ASR-10 1.6MB raw disk image
extension: img
media: 3.5"
encoding: MFM
tracks: 80
heads: 2
sectors/track: 20
sector size: 512
total size: 1,638,400 bytes
```

Important unknown:

```text id="0itme0"
Sector IDs may be 0..19 or 1..20.
```

After mounting, command `0x46 Read Data` should reveal what `R` value the ROM requests.

## First Read Data command

Command to trace:

```text id="euybkn"
0x46 Read Data
```

Trace should decode:

```text id="r9qluz"
command byte
drive/head select byte
C
H
R
N
EOT
GPL
DTL
result ST0
result ST1
result ST2
result C
result H
result R
result N
```

Relevant lowmem layout:

```text id="r09gfe"
$04C6 high = ST0
$04C6 low  = ST1
$04C8 high = ST2
$04C8 low  = C
$04CA high = H
$04CA low  = R
$04CC high = N
```

Potential failure:

```text id="ys8lxy"
ST0=40 ST1=01
```

Interpreted as:

```text id="8bapf1"
abnormal termination + missing address mark
```

Likely causes:

```text id="w0typp"
wrong sector ID numbering
wrong side/head
wrong track
wrong data rate/density
wrong image geometry
wrong HFE/raw decoding
```

## Interaction with current dispatcher/service blocker

The current accepted-looking MC68302/FC68xx service sequence is:

```text id="ylkepi"
FC6814 000b -> 240b
IACK vector 0x4e or 0x4f
FC6818 = 4000 or 8000
FC6814 240b -> 000b
runtime 00bf1a sets FC6816 c080 -> e480
runtime 00bf22 sets $0d06
optional gated experiment clears FC6816 e480 -> c080
dispatcher returns to f87f9a idle
```

In the latest service-clear runs:

```text id="y571h5"
No panel advance beyond LOADING SYSTEM.
No post-clear FDC activity.
No new error.
Final hang remains dispatcher idle at f87f9a.
```

Therefore, do not currently assume that FDC/media is the next missing behavior.

The next FDC-relevant evidence would be a log showing:

```text id="9ljkl4"
- reads/writes to $FC4000-$FC4003 after the service sequence
- new FDC command bytes after LOADING SYSTEM
- a queue event leading back into FDC/media code
- lowmem $04C6-$04CC updated after current service sequence
```

## Future FDC tasks

Revisit FDC when dispatcher/service progress resumes or when logs show post-service disk activity.

Future tasks:

```text id="d1mkyh"
- add or verify raw ASR-10 1.6MB .img format
- determine sector numbering 0..19 vs 1..20
- trace first real 0x46 Read Data after media is mounted
- verify uPD72069 clock and data rate handling
- verify motor/ready/density/side/drive-select behavior
- confirm whether FLOPPY_35_DD is sufficient
- test V161 and V350 images once format mounts
```

## Current one-line FDC takeaway

The FDC path is partially understood and the `fdc:0` connector appears to work, but the current boot blocker after `LOADING SYSTEM` is not presently proven to be FDC/media. Treat FDC as an important earlier and future subsystem, while the immediate focus remains dispatcher queue/event payload and MC68302/FC68xx service completion behavior.
