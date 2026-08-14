# ASR-10 Storage Completion Dispatch

## Scope

This note documents the V3.50 firmware-side async storage/device
completion architecture that is currently verified from the OS image,
ROM service routines, MC68302 register maps, and static callgraph tables.

It does not document board wiring. In particular, it does not prove that
uPD72069 `INTRQ`, SCSI interrupt output, or any PAL/GAL glue signal is
physically wired to MC68302 external IRQ1.

## Evidence levels

- [Verified static] Directly reconstructed from disassembly, vector table,
  binding table, or documented register maps.
- [Verified runtime/doc] Confirmed by existing runtime documentation in
  this repository.
- [Likely] Best current interpretation, but not independently proven.
- [OPEN] Not established by current evidence.
- [DISPROVEN] Contradicted by current evidence.

## Continuation Pointer `$0402`

[Verified static] V3.50 contains 14 real immediate writes to `$0402.w`:

```text
F01A96  $0402 <- $24AC
F01AD0  $0402 <- $B2CC
F01AF2  $0402 <- $B2CC
F10B44  $0402 <- $B1A4
F10C46  $0402 <- $B2CC
F10C60  $0402 <- $B2CC
F10C80  $0402 <- $B2CC
F10C92  $0402 <- $B2CC
F10EDA  $0402 <- $AA48
F10F0C  $0402 <- $A9BE
F10F2C  $0402 <- $A924
F10F5E  $0402 <- $A878
F1144A  $0402 <- $BA5E
F1145E  $0402 <- $B1A4
```

[Verified static] Only two read-and-dispatch sites are currently
identified:

```asm
F01B4E  movea.l $0402.w,A0
F01B52  jmp     (A0)

F114E2  movea.l $0402.w,A0
F114E6  jmp     (A0)
```

Therefore `$0402` is not an FDC-only callback. The current classification
is:

```text
$0402 = general async device/storage-I/O continuation pointer
```

Do not infer the dispatcher's source from the callback value alone. For
example, `$B2CC` is a common finish/error path and can be installed from
multiple places.

## Dispatcher A - Vector `$4B` / IDMA

### Entry Chain

[Verified static] The live V3.50 vector table contains:

```text
vector $4B -> $FFFF87E8
```

[Verified static] `docs/asr10/static/os-binding-table.csv` and
`docs/asr10/static/call-graph-edges.csv` identify the V3.50 binding slot:

```asm
$87E8.w  jmp.w $251A
```

Using the V3.50 OS high-view address convention, `$251A` corresponds to
`$F01B1A`.

Entry chain:

```text
MC68302 vector $4B
  -> $FFFF87E8
  -> binding slot $87E8.w
  -> jmp.w $251A
  -> high-view $F01B1A
```

### Register/Status Handling

[Verified static] Dispatcher A:

```asm
F01B1A  movem.l D0-D3/A0-A5,-(A7)
F01B1E  move.w  #$0000,$14EE.w
F01B24  move.b  $FFFC680E.l,D0
F01B2A  btst    #0,D0
F01B2E  beq     $F01B36
F01B30  clr.b   $049D.w
F01B34  bra     $F01B3C
F01B36  move.b  #$0A,$049D.w
F01B3C  movea.l #$00FC6000,A0
F01B42  andi.w  #$F7FF,($0816,A0)
F01B48  move.w  #$0800,($0818,A0)
F01B4E  movea.l $0402.w,A0
F01B52  jmp     (A0)
```

[Verified static] From `docs/mc68302/sib-register-map.md`:

```text
$FC680E = IDMA CSR
$FC6816 = IMR
$FC6818 = ISR
```

[Verified static] From `docs/mc68302/vector-origin-map.md` and
`docs/mc68302/interrupt-source-map.md`:

```text
vector $4B = MC68302 IDMA
IDMA bit = 11
$0800 = IDMA interrupt bit
```

The dispatcher samples IDMA CSR, converts CSR bit 0 into `$049D` status,
masks/clears the IDMA interrupt state, and then continues through `$0402`.

### Continuation Dispatch

[Verified static] The known callback family associated with this dispatcher
includes:

```text
$24AC  IDMA/SIB completion follow-up
$B2CC  common finish/error path
```

### Classification

```text
$F01B1A/$F01B4E = MC68302/SIB/IDMA completion dispatcher
```

[DISPROVEN] This dispatcher is not an FDC status dispatcher. It reads
MC68302 IDMA/SIB status and does not read `$FC4000-$FC4003` or FDC result
storage before the `$0402` jump.

## Dispatcher B - Vector `$51` / Shared Storage

### Entry Chain

[Verified static] The live V3.50 vector table contains:

```text
vector $51 -> $FFFF87CE
```

[Verified static] `docs/asr10/static/os-binding-table.csv` and
`docs/asr10/static/call-graph-edges.csv` identify the V3.50 binding slot:

```asm
$87CE.w  jmp.l $00BAB6
```

Using the V3.50 OS high-view address convention, `$00BAB6` corresponds to
`$F114B6`.

Entry chain:

```text
MC68302 external IRQ1 / vector $51
  -> $FFFF87CE
  -> binding slot $87CE.w
  -> jmp.l $00BAB6
  -> high-view $F114B6
```

### Shared Prelude

[Verified static] Dispatcher B:

```asm
F114B6  movem.l D0-D3/A0-A5,-(A7)
F114BA  move.w  #$0000,$14EE.w
F114C0  jsr     $FFFB7F0A
F114C6  tst.b   $04B0.w
F114CA  bne     $F114DC
F114CC  jsr     $FFFBA626
F114D2  beq     $F114DC
F114D4  jsr     $FFFB7E8E
F114DA  bra     $F114E2
F114DC  jsr     $FFFBB370
F114E2  movea.l $0402.w,A0
F114E6  jmp     (A0)
```

[Verified static] `$FB7F0A` reads IDMA CSR (`$FC680E`) into `$04A6`, calls
`$FB7F30`, and may report an error through `$049D`.

[Verified static] `$FBA626` tests the operation state `$049A`:

```asm
FBA626  cmpi.b  #$0B,$049A.w
FBA62C  beq     $FBA634
FBA62E  cmpi.b  #$0C,$049A.w
FBA634  rts
```

Together, `$04B0` and `$049A` select whether the dispatcher takes the FDC
status branch or the SCSI/storage branch.

### FDC Branch

[Verified static] The FDC branch is:

```text
F114D4 -> FFFB7E8E
```

Relevant `$FB7E8E` behavior:

```asm
FB7E8E  move.b  $04AD.w,D0
FB7E92  bne     $FB7EB2
FB7E94  bsr     $FB7C5A
...
FB7EF8  move.b  $04C7.w,$0300.w
FB7EFE  move.b  $04C8.w,$0301.w
FB7F04  move.b  D0,$049D.w
```

[Verified static] `$FB7C5A` builds and sends FDC command `08`:

```asm
FB7C5A  movea.w #$04D6,A0
FB7C5E  move.b  #$08,(A0)
FB7C62  move.b  #$01,$04E6.w
FB7C68  move.l  #$00001F40,$0476.w
FB7C70  bsr     $FB8CDA
FB7C74  bsr     $FB8D78
```

This is the SENSE INTERRUPT STATUS path when `$04AD == 0`.

### SCSI Branch

[Verified static] The SCSI/status branch is:

```text
F114DC -> FFFBB370
```

[Verified static] `$FBB5C0` establishes the SCSI register pair:

```asm
FBB5C0  movea.l #$FFFC5001,A4
FBB5C6  movea.l #$FFFC5003,A3
```

[Verified static] `$FBB370` calls `$FBB5C0`, reads/writes through A4/A3,
and updates storage/SCSI status bytes such as `$04B5`, `$04B6`, and `$04B7`
before returning to the shared dispatcher.

[Verified runtime/doc] `docs/asr10/reference/hardware-map.md` identifies
`$FC5001/$FC5003` as the ASR-10 SCSI controller register window in CS3.

### Continuation Dispatch

[Verified static] Both FDC and SCSI/status branches converge at:

```asm
F114E2  movea.l $0402.w,A0
F114E6  jmp     (A0)
```

Known callback families that can be used with this dispatcher include:

```text
$BA5E  FDC post-RECALIBRATE continuation
$B1A4  FDC post-SEEK / storage state-machine continuation
$A878  storage/device operation continuation
$A924  storage/device operation continuation
$A9BE  storage/device operation continuation
$AA48  MC68302/SIB setup continuation used by storage operation state
$B2CC  common finish/error path
```

### Classification

```text
$F114B6/$F114E2 = shared storage/device completion dispatcher
```

This dispatcher has device-specific prelude logic for at least:

```text
FDC
SCSI
MC68302/SIB status side checks
```

Therefore vector `$51` should be described as the firmware-side shared
storage/device completion entry, not simply as "the FDC interrupt".

## FDC RECALIBRATE To SEEK Example

[Verified static] The active V3.50 path around RECALIBRATE:

```asm
F11444  jsr     $FFFB7CF6
F1144A  move.l  #$0000BA5E,$0402.w
F11452  clr.b   $04AD.w
F11456  jsr     $FFFB7D70
F1145C  rts
```

[Verified static] `$FB7D70` builds:

```text
07 00 = RECALIBRATE
```

After the firmware receives the vector `$51` completion entry:

```text
vector $51
  -> $FFFF87CE
  -> $87CE.w
  -> $F114B6
  -> FDC branch
  -> $FB7E8E
  -> SENSE INTERRUPT STATUS path when $04AD == 0
  -> jmp [$0402]
  -> $BA5E / high-view $F1145E
```

[Verified static] `$F1145E` then installs the next callback and sends SEEK:

```asm
F1145E  move.l  #$0000B1A4,$0402.w
F11466  clr.b   $04AD.w
F1146A  jsr     $FFFB7D86
F11470  movem.l (A7)+,D0-D3/A0-A5
F11474  rte
```

[Verified static] `$FB7D86` builds:

```text
0F 00 01 = SEEK
```

[Verified firmware semantics] Vector `$51` is the completion entry used by
the active FDC state machine after RECALIBRATE and before the SEEK
continuation.

[OPEN] The physical source or routing that causes vector `$51` is not
established.

## Relationship To IDMA

[Verified static] Vectors `$4B` and `$51` are separate completion paths:

```text
$4B  MC68302 internal IDMA source -> $F01B1A/$F01B4E
$51  MC68302 external IRQ1 source -> $F114B6/$F114E2
```

Both paths use `$0402`, but they perform different status/prelude work
before dispatch.

[DISPROVEN] The FDC byte-transfer path documented in
`docs/asr10/investigations/instrument-load-v350.md` is not programmed
through MC68302 IDMA. The `$4B` dispatcher may still be a valid firmware
IDMA completion path, but it is not the proven FDC data-transfer mechanism.

## Dispatcher Matrix

| dispatcher | interrupt/vector | status source | callback families | subsystem |
|---|---|---|---|---|
| `$F01B1A/$F01B4E` | `$4B -> $FFFF87E8 -> $87E8.w -> $251A` | `$FC680E` IDMA CSR, `$FC6816/$FC6818` IMR/ISR | `$24AC`, `$B2CC` | MC68302/SIB/IDMA completion |
| `$F114B6/$F114E2` | `$51 -> $FFFF87CE -> $87CE.w -> $00BAB6` | `$FC680E` side check, FDC result/status via `$FB7E8E`, SCSI status via `$FBB370` | `$BA5E`, `$B1A4`, `$A878`, `$A924`, `$A9BE`, `$AA48`, `$B2CC` | shared storage/device completion |

## What Is Verified

- [Verified static] `$0402` is a general async device/storage-I/O
  continuation pointer with two identified dispatchers.
- [Verified static] Vector `$4B` enters the IDMA/SIB dispatcher at
  `$F01B1A`.
- [Verified static] Vector `$51` enters the shared storage/device dispatcher
  at `$F114B6`.
- [Verified static] `$F114B6` has an FDC branch and a SCSI branch before the
  final `$0402` dispatch.
- [Verified static] The FDC RECALIBRATE example installs `$BA5E`, sends
  `07 00`, later enters the vector `$51` dispatcher, runs the FDC status
  prelude, jumps to `$BA5E`, installs `$B1A4`, and sends `0F 00 01`.
- [Verified runtime/doc] `$FC5001/$FC5003` are the documented ASR-10 SCSI
  register window.

## What Remains OPEN

- [OPEN] Physical FDC `INTRQ` routing.
- [OPEN] Physical SCSI interrupt routing.
- [OPEN] Any PAL/GAL/glue logic between storage devices and MC68302 external
  IRQ1.
- [OPEN] Whether multiple storage sources electrically share IRQ1.
- [OPEN] Exact board-level acknowledge, polarity, and clearing behavior for
  the shared storage/device completion path.
- [OPEN] Full callback semantics for `$A878`, `$A924`, `$A9BE`, and `$AA48`;
  they are storage/device continuations by context, but should not be
  overclassified from the callback address alone.
