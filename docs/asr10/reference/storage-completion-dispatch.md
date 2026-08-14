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
- [Verified device] Confirmed by the current MAME device implementation.
- [Verified chip] Confirmed by chip documentation captured in `docs/mc68302`.
- [Verified firmware] Confirmed by V3.50 firmware/static control flow.
- [Verified board] Confirmed by ASR-10 board evidence.
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

[Verified firmware] For the RECALIBRATE path, `$FB7E8E` then tests the
SIS result byte:

```asm
FB7E98  move.b  $04C6.w,D0
FB7E9C  andi.b  #$E0,D0
FB7EA0  cmpi.b  #$20,D0
FB7EAA  moveq   #$08,D0     ; error if not $20
FB7EAE  clr.b   D0          ; success
FB7F04  move.b  D0,$049D.w
```

For `$04AD == 0`, the condition for the later `jmp [$0402]` to reach
`$BA5E` as the successful RECALIBRATE continuation is therefore:

```text
SENSE INTERRUPT STATUS result ST0 & $E0 == $20
```

[Verified device] MAME's `upd72069_device` inherits the
`upd765_family_device` RECALIBRATE implementation. On successful track-0
completion for drive 0, `command_end(..., false)` sets `irq`, sets
`st0_filled`, and drives `intrq_wr_callback()` through `check_irq()`. This
is INTRQ, not DRQ. A later `SENSE INTERRUPT STATUS 08` returns:

```text
ST0 = $20
PCN = $00
```

The SIS/result read path clears the device-side `irq`/INTRQ state,
clears the filled ST0 condition, and clears drive-busy state according to
the current device model.

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

[Verified static] The SCSI branch is selected when `$04B0 != 0`. It is
also selected when `$04B0 == 0` and `$FBA626` reports operation state
`$049A == $0B` or `$0C`. Therefore `$04B0` is not a generic completion
flag; in this dispatcher it is part of the firmware-side FDC/SCSI branch
selection state.

[Verified static] The SCSI-side status routine uses these RAM fields:

```text
$0402  async continuation pointer
$049A  storage/device operation state
$04B0  storage branch selector/state byte
$04B5  SCSI auxiliary/LUN/result scratch byte depending on phase
$04B6  SCSI command-phase/status byte
$04B7  SCSI status byte
```

[Verified static] `$FBB370` reads WD33C93-style status by selecting
register `$17` through `$FC5001` and reading `$FC5003`:

```asm
FBB38C  move.b  #$17,(A4)
FBB390  move.b  (A3),$04B7.w
```

[Likely] With the MAME `wd33c93_device` register model, `$17` is
`SCSI_STATUS`, `$18` is `COMMAND`, `$FC5001` is the indirect register
select/address port, and `$FC5003` is the indirect register/data port.
Reading `SCSI_STATUS` is the device-side completion acknowledge in that
model. This is separate from MC68302 interrupt acknowledge/vector
delivery.

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

## Storage IRQ1 Boundary

[Verified firmware] For the specific outstanding state:

```text
$0402 = $BA5E
$04AD = 0
FDC RECALIBRATE 07 00 outstanding
```

vector `$51` is the only identified firmware entry that performs the
needed FDC status prelude and reaches `$BA5E`:

```text
vector $51
  -> $FFFF87CE
  -> binding $87CE.w
  -> $00BAB6 / high-view $F114B6
  -> FDC branch $FB7E8E
  -> $FB7C5A sends SIS 08
  -> accepts ST0 & $E0 == $20
  -> $049D <- 0
  -> jmp [$0402]
  -> $BA5E / high-view $F1145E
```

[DISPROVEN] The matching entry is not vector `$4B`: `$4B` is the
MC68302 IDMA/SIB completion dispatcher and does not run the FDC
SENSE INTERRUPT STATUS prelude. The matching entry is also not the
PB9/PB10/PB11 handlers, IRQ6/DUART, SCC1/SCC2, or normal 68000
autovectors.

[Verified chip] MC68302 external IRQ1 is an EXRQ source. With the
ASR-10-observed `GIMR=$8040`, `IV1=0` and the vector prefix is `$40`;
external IRQ1's low vector bits are `$11`, so the MC68302-supplied IACK
vector is:

```text
$40 | $11 = $51
```

External IRQ1 asserts CPU interrupt level 1. It is not one of the
internal level-4 INRQ sources represented by the normal IPR/IMR/ISR bit
table. The external source must be cleared by device or board logic
outside that internal interrupt-controller pending/in-service model.

[OPEN] The board source remains unidentified. Current evidence does not
prove that uPD72069 `INTRQ`, SCSI interrupt output, a shared storage line,
or PAL/GAL glue physically drives MC68302 IRQ1.

[Verified firmware semantics] Vector `$51` is the completion entry used by
the active FDC state machine after RECALIBRATE and before the SEEK
continuation.

[OPEN] The physical source or routing that causes vector `$51` is not
established.

## SCSI RESET Example

This is one verified firmware example, not a complete model of the ASR-10
SCSI subsystem.

[Verified static] One bounded SCSI async start path installs `$B1A4`,
selects the controller command register, sends command `$00`, and returns:

```asm
F10B44  move.l  #$0000B1A4,$0402.w
F10B4C  move.b  #$18,$FFFC5001.l
F10B54  move.b  #$00,$FFFC5003.l
F10B5C  rts
```

[Likely] Under the WD33C93-compatible register model documented for the
ASR-10 SCSI window:

```text
$FC5001 <- $18  select COMMAND register
$FC5003 <- $00  issue RESET command
```

[Verified static] A clean SCSI branch into this start exists when
`$FBA626` identifies operation state `$049A == $0B` or `$0C`; the caller
sets `$04B0 = 4` before jumping to `$F10B44`:

```asm
F10ABA  jsr     $FFFBA626
...
F10AD8  move.b  #$04,$04B0.w
F10ADE  bra     $F10B44
```

[Verified static] The corresponding firmware-side completion chain is:

```text
$0402 <- $B1A4
write $18 -> $FC5001
write $00 -> $FC5003
return
...
vector $51
  -> $FFFF87CE
  -> $87CE.w
  -> $F114B6
  -> SCSI branch because $04B0 != 0
  -> $FBB370
  -> select/read WD33C93 status register $17
  -> jmp [$0402]
  -> $B1A4 / high-view $F10BA4
  -> dispatch by $049A
  -> possible next SCSI command
```

[Verified static] `$B1A4` is a storage state-machine continuation. For
operation states `$0B/$0C`, it dispatches through the table at `$BA76` and
reaches `$BA0E` / high-view `$F1140E`:

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

[Verified static] The `$0B/$0C` table entry reaches code that may start a
later SCSI command, for example command `$0C`:

```asm
FBA728  move.b  #$18,$FFFC5001.l
FBA730  move.b  #$0C,$FFFC5003.l
```

[Likely] In the WD33C93-compatible command set, command `$0C` is
`WAIT_SELECT_RECEIVE_DATA`. The exact higher-level ASR-10 SCSI operation
semantics remain outside this example.

## FDC And SCSI Comparison

| field | FDC | SCSI |
|---|---|---|
| async start | FDC command builder, e.g. `$FB7D70` | SCSI register write at `$F10B44` |
| continuation installation | `$0402 <- $BA5E`, later `$0402 <- $B1A4` | `$0402 <- $B1A4` |
| start MMIO | uPD72069 command bytes, e.g. `07 00` RECALIBRATE | `$FC5001 <- $18`, `$FC5003 <- $00` RESET |
| outstanding state | `$04AD`, `$049A`, FDC command/result state | `$04B0`, `$049A`, `$04B5-$04B7` |
| completion vector | `$51` firmware-side entry | `$51` firmware-side entry |
| dispatcher | `$F114B6/$F114E2` | `$F114B6/$F114E2` |
| status routine | `$FB7E8E` | `$FBB370` |
| device acknowledge | SENSE INTERRUPT STATUS path when `$04AD == 0` | select/read WD33C93 status register `$17` |
| callback | `jmp [$0402]` to `$BA5E`, then `$B1A4` | `jmp [$0402]` to `$B1A4` |
| state-machine continuation | FDC RECALIBRATE -> SEEK | SCSI reset/status -> dispatch by `$049A` |

[Verified static] Both storage paths use the same vector `$51` dispatcher
and the same `$0402` continuation mechanism, but they run different
device-specific status/acknowledge preludes first.

[Verified firmware] This is structural evidence that vector `$51` is a
shared storage/device completion ingress in the firmware. It is not
electrical evidence for how FDC or SCSI reach MC68302 external IRQ1.

[DISPROVEN] `$F114B6` should not be described as an FDC-only dispatcher.

## Relationship To IDMA

[Verified static] Vectors `$4B` and `$51` are separate completion paths:

```text
$4B  MC68302 internal IDMA source -> $F01B1A/$F01B4E
$51  MC68302 external IRQ1 source -> $F114B6/$F114E2
```

Both paths use `$0402`, but they perform different status/prelude work
before dispatch.

[Verified static] The firmware contains an async FDC READ DATA path that
programs MC68302 IDMA and uses the `$4B` completion dispatcher before storage
command completion proceeds through `$51`.

[Verified runtime] The currently observed `LOADING JM DIGI SYN` instrument-load
run has not reached that async IDMA READ path; it stalls after RECALIBRATE
start and before RECALIBRATE completion is delivered to the vector `$51`
firmware entry.

## Relationship To Current Instrument-Load Request

[Verified runtime/static] The current observed instrument-load request reaches
storage through the service-node path documented in
`runtime-service-model.md`:

```text
producer $FFA882-$FFA8AA
  -> trap #3
  -> node +2/+3 = $03/$02
  -> node +4 = $0002B600
  -> A1 = $14DA
  -> trap #12 immediate path
  -> storage callback $00B08C
  -> $0466 = $1504
  -> $046A = $0002B600
```

[Verified runtime] In that run, `$043E` remains zero. The accepted payload
`$046A=$02B600` has not been promoted to the current payload/runtime
descriptor pointer.

[Verified static] Class `$03` dispatch begins:

```text
$049A.b == $03
  -> dispatch table $BA76[$03]
  -> $B64C
  -> $FB7F9E
  -> $FBA5A2
  -> $FB9C5E
  -> $FB9FE2
  -> $FB84DA
  -> $FB85C0
  -> MC68302 IDMA setup
  -> $FB8672
  -> FDC READ DATA $46
```

[Verified static] The IDMA setup uses source `$FFFC5803`, destination `$040E`,
count derived from transfer/sector state, and MC68302 IDMA registers
`$FC6802/$FC6804/$FC6808/$FC680C/$FC6810`.

[Verified static/runtime] If this request reaches common storage exit, the same
saved node from `$0466` is returned. The observed node has positive
`node +2=$0302`, so common exit statically selects scheduler slot `$23F6`;
`$2438` is the negative-node return target. The `$23F6` post-completion
consumer is still [OPEN] because runtime does not yet reach completion.

[OPEN] The full class `$03` continuation through `$23F6`, later
callback/request-class transitions to class `$06/$0D`, and `$043E` activation.

[Verified runtime] The current runtime still stalls before RECALIBRATE
completion is delivered to the vector `$51` firmware entry. It has not reached
SEEK, async READ DATA, IDMA programming, vector `$4B` completion, or `$043E`
activation.

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
- [Verified device] MAME's uPD72069/uPD765 RECALIBRATE completion produces
  INTRQ and SIS `ST0=$20, PCN=$00` for successful drive-0 completion.
- [Verified chip] MC68302 external IRQ1 can deliver vector `$51` at
  `GIMR=$8040`; this is EXRQ level 1, not an internal IPR/IMR/ISR source.
- [Verified static] The SCSI RESET example installs `$B1A4`, writes
  `$18/$00` to `$FC5001/$FC5003`, later enters the vector `$51` dispatcher,
  runs the SCSI status prelude at `$FBB370`, reads SCSI status register
  `$17`, and jumps to `$B1A4`.
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
- [OPEN] Whether future board policy should connect FDC INTRQ, SCSI IRQ,
  or a separate storage-glue source to MC68302 external IRQ1.
- [OPEN] Full callback semantics for `$A878`, `$A924`, `$A9BE`, and `$AA48`;
  they are storage/device continuations by context, but should not be
  overclassified from the callback address alone.
