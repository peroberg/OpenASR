
# boot-flow.md

# ASR-10 boot flow findings

## Service/manual boot expectation

Boot ROM contains disk loader.

Expected user-visible sequence:

```text
ENSONIQ ASR-10
SCSI INSTALLED, SEARCHING FOR SCSI DEVICE    [on SCSI-equipped units]
PLEASE INSERT DISK
LOADING SYSTEM
TUNING KBD - HANDS OFF
```

ASR-10 Rack reportedly released with 1.50B EPROMs and requires at least 1.50 disk OS.

## Current observed panel text

Observed through `$FC4817` text path:

```text
ENSONIQ ASR-10
PLEASE INSERT DISK
```

`LOADING SYSTEM` has not yet been reached with raw `.img`, because images currently fail to mount.

## Display print path

Current evidence:

```text
ROM routine at $F89CB0 writes printable ASCII-like bytes to $FC4817.
```

Harness reconstructs text when:

```text
PC == $F89CB0
data byte in range 0x20..0x7E
write address == $FC4817
```

This creates logs like:

```text
ASR10PANEL text="   ENSONIQ  ASR-10    "
ASR10PANEL text="  PLEASE INSERT DISK  "
```

## `$049D` prompt/status decision

`$049D` is a key ROM decision/status field.

Known observed values:

```text
05 -> PLEASE INSERT DISK path
0D -> disk/controller/media error path
```

## Input gate path to `$049D=05`

Relevant ROM path:

```asm
FB7C7A move.b $04EE,D0
FB7C7E tst.b  D0
FB7C80 bpl    FB7C9C
FB7C82 clr.b  D0
FB7C84 btst   #4,$FFFC4809
FB7C8C beq    FB7C92
FB7C8E move.b #1,D0
FB7C92 move.b D0,-(A7)
FB7C94 bsr    FB8D78
FB7C98 move.b (A7)+,$04EE
FB7C9C bne    FB7CA4
FB7C9E move.b #$05,$049D
```

Interpretation:

```text
$04EE=FF means a pending input/panel status check.
ROM tests $FC4809 bit 4.
If bit 4 is clear, ROM writes $049D=05.
If bit 4 is set, ROM avoids this $049D=05 write and proceeds further.
```

This is a panel/input/status gate, not an FDC error.

## FDC/media path to `$049D=0D`

Relevant path:

```asm
FB8C46 tst.b  $049D
FB8C4A bne    FB8C6C
FB8C4E move.b $04C6,D2
FB8C52 andi.b #$C0,D2
FB8C56 bne    FB8C5E

FB8C5E move.b #$FF,$04AC
FB8C64 move.b #$2B,D3
FB8C68 bsr    FB81AE

FB81AE moveq  #$0D,D2
FB81B0 move.b D3,$04AE
FB81B4 move.b D2,$049D
```

Interpretation:

```text
ROM requires ($04C6 & 0xC0) == 0.
If top bits are set, it writes:
  $04AE = 2B
  $049D = 0D
```

With no media:

```text
$04C6=68
$04C6 & C0 = 40
=> rejected
```

## Retry/countdown path to `$049D=05`

Known path:

```asm
FB91AC move.b #$08,$04B0
FB91CE subq.b #1,$04B0
FB91D2 bne    FB91B2
FB91D4 move.b #$05,$049D
```

Interpretation:

```text
After repeated retry countdown, ROM writes $049D=05 and panel goes to PLEASE INSERT DISK.
```

## Important branch facts

Known branch PCs in media retry routine:

```text
FB9182
FB9192
FB91A6
FB91BA
FB91C8
FB91D2
```

Escape from one loop appears to require `$049D == 0` at the relevant test, especially around `FB91A6`.

Do not treat one prompt as a single cause. There are multiple ways to reach `$049D=05` or `$049D=0D`.


---
