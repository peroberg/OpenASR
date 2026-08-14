# ASR-10 SCSI Operation Example

## Scope

This note documents one verified V3.50 firmware-side SCSI async operation.
It is not a complete description of the ASR-10 SCSI subsystem, target
protocol, option probing, or board wiring.

Evidence levels:

- [Verified static] Directly reconstructed from disassembly, binding/vector
  tables, or documented register maps.
- [Verified runtime/doc] Confirmed by existing runtime documentation in
  this repository.
- [Likely] Best current interpretation, not independently proven.
- [OPEN] Not established by current evidence.

## Register Window

[Verified runtime/doc] `hardware-map.md` identifies the ASR-10 SCSI
controller window in CS3:

```text
$FC5001  SCSI register select / index
$FC5003  SCSI register data
```

[Likely] The controller is AM33C93A / WD33C93A-compatible. The matching
MAME model is `wd33c93_device`/`wd33c93a_device`, whose indirect interface
uses an address/register-select port and a data/register port.

## Async Start

[Verified static] The selected example starts at `$F10B44`:

```asm
F10B44  move.l  #$0000B1A4,$0402.w
F10B4C  move.b  #$18,$FFFC5001.l
F10B54  move.b  #$00,$FFFC5003.l
F10B5C  rts
```

Firmware-side meaning:

```text
$0402 <- $B1A4
WD33C93 COMMAND register selected through $FC5001
RESET command $00 written through $FC5003
return while operation is outstanding
```

[Verified static] The operation can be reached from a SCSI branch where
operation state `$049A` is `$0B` or `$0C`; `$04B0` is set to `$04` before
the async start:

```asm
F10ABA  jsr     $FFFBA626
...
F10AD8  move.b  #$04,$04B0.w
F10ADE  bra     $F10B44
```

## Completion Entry

[Verified static] The shared storage/device completion dispatcher is vector
`$51`:

```text
vector $51
  -> $FFFF87CE
  -> binding slot $87CE.w
  -> jmp.l $00BAB6
  -> high-view $F114B6
```

[Verified static] `$F114B6` selects the SCSI branch when `$04B0 != 0`:

```asm
F114C6  tst.b   $04B0.w
F114CA  bne     $F114DC
...
F114DC  jsr     $FFFBB370
F114E2  movea.l $0402.w,A0
F114E6  jmp     (A0)
```

## SCSI Status/Acknowledge

[Verified static] `$FBB370` establishes the register pair, reads controller
status, then returns to the shared dispatcher:

```asm
FBB370  clr.b   $049D.w
FBB374  bsr     $FBB5C0
FBB378  move.b  (A4),$04B5.w
...
FBB38C  move.b  #$17,(A4)
FBB390  move.b  (A3),$04B7.w
FBB394  move.b  #$10,(A4)
FBB398  move.b  (A3),$04B6.w
FBB39C  move.b  #$0F,(A4)
FBB3A0  move.b  (A3),$04B5.w
...
FBB460  rts
```

[Likely] In the WD33C93-compatible register model, register `$17` is
`SCSI_STATUS`. Reading it is the device-side completion acknowledge/IRQ
clear in MAME's `wd33c9x` model. This is not the same thing as MC68302
interrupt acknowledge or vector delivery.

## Continuation

[Verified static] After `$FBB370`, the shared dispatcher jumps through the
continuation pointer:

```asm
F114E2  movea.l $0402.w,A0
F114E6  jmp     (A0)
```

For this example:

```text
jmp [$0402] -> $B1A4 / high-view $F10BA4
```

[Verified static] `$B1A4` dispatches by operation state `$049A`:

```asm
F10BA4  jsr     $FFFBA626
F10BAA  beq     $F10C02
...
F10C02  moveq   #$00,D1
F10C04  move.b  $049A.w,D1
F10C08  movea.l #$0000BA76,A0
F10C0E  asl.w   #2,D1
F10C10  movea.l (A0,D1.w),A0
F10C14  jmp     (A0)
```

[Verified static] For `$049A == $0B` or `$0C`, the table reaches `$BA0E`
/ high-view `$F1140E`. That path can start later SCSI work, including a
command `$0C` write:

```asm
FBA728  move.b  #$18,$FFFC5001.l
FBA730  move.b  #$0C,$FFFC5003.l
```

[Likely] In the WD33C93-compatible command set, command `$0C` is
`WAIT_SELECT_RECEIVE_DATA`.

## End-To-End Chain

[Verified static] The firmware-side chain for this example is:

```text
$0402 <- $B1A4
  -> WD33C93 COMMAND register selected
  -> RESET command written
  -> return
  ...
  -> vector $51
  -> $FFFF87CE
  -> $87CE.w
  -> $F114B6
  -> SCSI branch $FBB370
  -> SCSI_STATUS register $17 read
  -> jmp [$0402]
  -> $B1A4
  -> dispatch by $049A
  -> next state
```

## Open Items

- [OPEN] Physical SCSI interrupt routing into MC68302 external IRQ1.
- [OPEN] Any PAL/GAL/glue logic between SCSI and vector `$51`.
- [OPEN] Electrical sharing between FDC and SCSI completion sources.
- [OPEN] Interrupt polarity and board-level line clearing.
- [OPEN] Full semantics of the later SCSI operation state reached after
  `$B1A4`.
