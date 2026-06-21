
# memory-map.md

# ASR-10 memory map notes

## Current harness candidate map

The current boot harness has an experimental map including:

```text
$000000-$0FFFFF  low ROM/overlay/lowmem wrapper
$100000-$1FFFFF  sample RAM candidate
$200000          ES550x VFX reference candidate
$260000          ES5510 VFX reference candidate
$280000          DUART VFX reference candidate
$2C0000          FDC VFX reference candidate
$300000          ES5506 TS reference candidate
$380000          ES5510 TS reference candidate
$F00000-$F7FFFF  RAM
$F80000-$FBFFFF  high ROM/RAM alias candidate
$FC0000-$FC3FFF  RAM
$FC4000-$FC4003  uPD72069 FDC candidate
$FC4800-$FC481F  DUART/panel/frontpanel candidate
$FC5000-$FC501F  SCSI candidate
$FC6800-$FC68FF  MC68302 internal candidate
```

## Important caution

VFX/TS reference windows are research scaffolding, not verified ASR-10 addresses.

Eventually prune or isolate:

```text
ES550X_VFX_CANDIDATE
ES5510_VFX_CANDIDATE
DUART_VFX_CANDIDATE
FDC_VFX_CANDIDATE
*_vfx_shadow
```

Do not remove before current FDC/image work is stable unless done in a separate cleanup commit.

## Known lowmem fields

### `$049D`

ROM status/prompt code.

Observed values:

```text
$049D=05 -> PLEASE INSERT DISK path
$049D=0D -> disk/controller/media error path
```

Important writes:

```text
FB7C9E writes $049D=05 after input/status gate fails
FB81B4 writes $049D=0D after FDC/media result rejection
FB91D4 writes $049D=05 after retry countdown reaches zero
```

### `$04B0`

Countdown/retry counter.

Known behavior:

```text
FB91AC move.b #$08,$04B0
FB91CE subq.b #1,$04B0
FB91D2 bne back while nonzero
FB91D4 writes $049D=05 when countdown expires
```

### `$04C6-$04CC`

FDC result/status storage area.

For command 0x46 result interpretation:

```text
$04C6 high = ST0
$04C6 low  = ST1
$04C8 high = ST2
$04C8 low  = C
$04CA high = H
$04CA low  = R
$04CC high = N
```

### `$04AE`

Additional detail/status field.

Observed:

```text
$04AE=2B when FB81B4 writes $049D=0D from FDC/media rejection path
```

### `$04EE`

Input/panel state candidate.

Observed path:

```text
$04EE=FF triggers check at FB7C7A/FB7C84
ROM tests $FC4809 bit 4
if bit clear -> $04EE=00 -> $049D=05
if bit set   -> $04EE=01 -> input gate passed
```


---
