# CDR-16 `MOVE ME*8MB` Load Hang

## Executive Result

[VERIFIED — firmware execution] `MOVE ME*8MB` is a 3-block (`$600` byte)
type-`$001E` **Bank**.  Its first heap request is the `$600`-byte Bank container.
The firmware then reads directory-record `size.w == 3`, subtracts the fixed
three-block container, and obtains zero.  When Bank byte `+$15F == $04`, the
executed payload path shifts that zero by nine and calls the heap allocator with
`D1 = $00000000`.  The allocator writes a zero-length header and its header scan
does not advance at `$F8A474-$F8A494`.

[OBSERVED — synthetic downstream test] Clearing only the loaded Bank's `+$15F`
byte bypasses that zero-size request and reaches volume-label validation.  The
firmware then displays `WRONG DISK INSERTED`: the mounted CDR-16 header contains
`ENSONIQID` at logical-byte offset `$21F`, while the first Bank slot's encoded
expected label has character bytes `CDR-016`.  This is a separate downstream
media-identity mismatch, not evidence about the heap loop.

[VERIFIED — V3.50 Bank load contract] Serialized Bank byte `+$15F == $04`
selects the appended Bank-effect payload path.  The path treats the bytes after
the fixed three-block Bank container as its payload.  Healthy `TUTORIAL BNK`
and `ATRK TUT BNK` records both have `$04`, an extent larger than three blocks,
and named serialized effect data in that appended interval.

[OPEN] The formal Bank-file-format name for `$15F`, its authoring provenance,
and the semantics of non-`$04` on-media values have not been established by an
ASR file-format specification.  The Bank refers to four Instrument records in
sibling directory `DEMO INST 1-`, totaling `$6EE000` (~7.27 MB).  The `*8MB`
suffix likely rounds that aggregate; it is not evidence of an 8 MiB WaveSample
or PCM owner.

[RULED OUT — for the observed zero-size request] No current OpenASR production
component originated `D1 = 0`; the value is calculated by executed V3.50 code
from the loaded directory record.  No emulator change is supported by this
evidence.  [OPEN] Whether a physical ASR-10 presented with this exact medium
would exhibit precisely the same failure requires physical-media evidence.

## Reproducer

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -cdrom cds/CDR-16.chd \
  -video none -sound none -nothrottle
```

From a cold boot: `CHANGE STORAGE DEVICE -> SCSI #4 -> FILE 6 SOUND DEMOS-
-> FILE 1 SOUNDBANKS- -> LOAD -> INST -> FILE 1 MOVE ME*8MB -> ENTER ->
ENTER -> Instrument/Track 1`.

[OBSERVED] Deterministic hang across all attempts: the display stays on the
selected Bank while the CPU executes the heap-header scan loop at
`$F8A474-$F8A494`.  The machine remains fully scheduled and responsive to
interrupts; the PC loops tightly inside the non-advancing header scanner.

## Fixture Identity

- `CDR-16.chd` SHA-256:
  `ee2097e7f998cfae5c29f052eb9b55be7f887a0c6cc0600810703f2fbb06b944`
  (`260,965,443` bytes).
- `V350.img` SHA-256:
  `2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b`
  (`1,638,400` bytes).

[VERIFIED] The CDR directory record is type `$001E`, name `MOVE ME*8MB`,
flags `$0003`, start logical block `$0A4225`, and length `$0003` blocks.  The
ASR CD target uses 512-byte logical blocks; its media extent is therefore
`3 * 512 = $600 = 1,536` bytes.  Parent directory structure:

| level | type | name | start block |
|---|---:|---|---:|
| root | `$0002` | `SOUND DEMOS-` | `$0A4221` |
| `SOUND DEMOS-` | `$0002` | `SOUNDBANKS-` | `$0A4223` |
| `SOUNDBANKS-` | `$001E` | `MOVE ME*8MB` | `$0A4225` |
| `SOUND DEMOS-` | `$0002` | `DEMO INST 1-` | `$0A4240` |

[VERIFIED] Sector 0 of `CDR-16.chd` contains:
`Formatted with Translator(tm) 2009, Version 2.9, Build 58. For more information, contact support@chickensys.com`.
[VERIFIED] The sector identifies the Translator build that formatted this image.
[OPEN] This string alone does not establish the provenance or intent of every
Bank record, nor whether the resulting labels conform to all ASR firmware media
semantics.

## Second Allocation Callsite

[VERIFIED dynamically] Across execution of `MOVE ME*8MB`, the allocator at ROM
`$F8A246` is entered twice:

1. **First Call (Bank Container):**
   - Timestamp: `t=28.6848s`
   - Caller: OS `$FFAA8C`, `RET = $FFAA8E`
   - Input: `D1 = $00000600` (3 blocks = 1,536 bytes)
   - Purpose: Heap space for the 3-block Bank file container.
   - Result: Allocated buffer at `$00F70A00` (stored in pointer `$34C.w`).
     SCSI READ(10) reads 3 blocks from LBA `$0A4225` into this buffer.

2. **Second Call (`+$15F == $04` Appended-Effect Path):**
   - Timestamp: `t=28.7339s`
   - Caller: OS `$00CF78: bsr.b $cf9a` (inside subroutine `$00CF66`),
     `RET = $00CF7A`
   - Context registers at `$00CF78`:
     - `D0 = $00000600`
     - `D1 = $00000000` (the failing zero allocation)
     - `D2 = $00F45C00` (total free heap)
     - `D3 = $00000000` (calculated payload byte size)
     - `A0 = $0002A400` (current heap scan pointer)
     - `A1 = $00000E96`
     - `A2 = $00FF888C`
     - `SP = $00001E12` (User Stack Pointer)
   - Purpose: Allocate heap memory for the appended Bank-effect payload selected
     by `+$15F == $04`, calculated from blocks 3..end of the Bank file.
   - Failure: Allocator receives `D1 = 0`, writes encoded header `$00000000` at
     heap address `$0002A400`, and loops infinitely at `$F8A474-$F8A494` because
     a block size of zero cannot advance the heap scan cursor.

## D1 Provenance (Instruction-Level Trace)

[VERIFIED dynamically] The exact instruction sequence leading to `D1 = $00000000`
executes in OS RAM at `$00CFB2-$00CFEC`:

```assembly
$00CFB2:  movea.l  $34c.w, a0          ; a0 = loaded Bank buffer ($F70A00)
$00CFB6:  cmpi.b   #$fe, $21(a0)       ; check Bank status/format byte (+0x21 == $FE)
$00CFBC:  bcs.b    $cffa               ; branch if not valid Bank format
$00CFBE:  jsr      $a4cc.w             ; subroutine: a1 = pointer to directory record ($00055E)
$00CFC2:  moveq    #$3, d4             ; d4 = 3 (Bank container size in 512-byte blocks)
$00CFC4:  move.w   $e(a1), d3          ; d3 = DirectoryRecord.size.w (+0x0E). For MOVE ME*8MB: d3 = 3
$00CFC8:  sub.w    d4, d3              ; d3 = size.w - 3 = 3 - 3 = 0 (remaining payload blocks!)
$00CFCA:  move.w   d3, $498.w          ; store remaining blocks ($498.w = 0)
$00CFCE:  move.w   d4, $496.w          ; store container blocks ($496.w = 3)
$00CFD2:  movea.l  $34c.w, a0          ; a0 = Bank buffer ($F70A00)
$00CFD6:  move.b   $15f(a0), $183.w    ; read Bank byte +$15F into global $183.w (value is $04)
$00CFDC:  move.b   $183.w, d0          ; d0 = 4
$00CFE0:  cmp.b    #$4, d0             ; test `$04` appended-effect selector
$00CFE4:  bne.b    $cffa               ; if != 4, skip effect allocation (rts)
$00CFE6:  jsr      $fff89798.l         ; ROM helper: lsl.l #9, d3 (0 << 9 = 0 bytes!)
$00CFEC:  jsr      $aa16.w             ; call payload routine -> calls $00CF66
...
$00CF66:  move.b   $183.w, -(sp)       ; save Bank-effect selector
$00CF6A:  move.b   #$02, $183.w        ; temporary payload-load state
$00CF70:  moveq    #$0, d1
$00CF72:  jsr      $8972.w
$00CF76:  move.l   d3, d1              ; D1 = D3 = $00000000
$00CF78:  bsr.b    $cf9a               ; jumps to binding that calls ROM allocator $F8A246
$00CF7A:  ...                          ; [RETURN ADDRESS - NEVER REACHED]
...
$00CF94:  move.b   (sp)+, $183.w       ; normal return restores selector
```

### Semantic Origin of D1:
- **Source Object:** Directory Record `A1 = $00055E`
- **Source Field:** `+$0E` (`size.w`, logical block count of file = 3)
- **Transformation:** `(DirectoryRecord.size.w - 3) << 9`
- **Condition:** Bank container byte `+$15F == 0x04` selects the appended
  Bank-effect payload path.
- **Observed firmware behavior:** the executed path has no local nonzero-size
  guard before shifting and calling the allocator.

## Bank Byte `+$15F`: Meaning, Provenance, and Consumers

### Serialized Provenance

[VERIFIED] `$15F` is a serialized Bank-container byte, not a value calculated
by the allocator.  In a clean V3.50 `TUTORIAL BNK` load, the allocated container
base was `$00F70A00`; its runtime byte `$00F70B5F` was `$04`, matching source
image `V350.img` at `TUTORIAL BNK +$15F`.  A targeted write tap observed no
firmware write to that field after the Bank container had been loaded.
`TUTORIAL BNK` begins at image offset `$032C00`, so its serialized selector is
source byte `$032D5F`; `ATRK TUT BNK` begins at `$153000`, so its selector is
source byte `$15315F`.

The Bank loader's fixed container is three 512-byte blocks (`$600`).  The
post-container interval is therefore exactly:

```text
[Bank + $600, Bank + size.w * $200)
```

| Bank | source extent `size.w` | `+$15F` | remaining blocks | second request | file-relative appended interval | observed appended content |
|---|---:|---:|---:|---:|---|---|
| `MOVE ME*8MB` | 3 | `$04` | 0 | `$00000000` | empty: extent ends at `+$600` | none within this file extent |
| `NIGHTMRE*2MB` | 3 | `$04` | 0 | `$00000000` | empty: extent ends at `+$600` | none within this file extent |
| `TUTORIAL BNK` | 7 | `$04` | 4 | `$00000800` | `+$600..+$DFF` | serialized `TIGHT REVERB` effect payload |
| `ATRK TUT BNK` | 18 | `$04` | 15 | `$00001E00` (from the verified formula) | `+$600..+$23FF` | serialized `44 DDL+CH+REV` effect payload |

[VERIFIED — bounded corpus] The V3.50 floppy directory contains two `$001E`
Banks: `TUTORIAL BNK` and `ATRK TUT BNK`.  Both are working Bank controls and
both have `$15F == $04`, `size.w > 3`, and nonempty appended effect payloads.
This is consistency evidence, not a claim that every valid ASR Bank has `$04`.

### Executed Consumers

[VERIFIED — V3.50 load path] The direct field reader is:

```assembly
$00CFD6:  move.b  $15f(a0), $183.w    ; copy serialized Bank byte
$00CFDC:  move.b  $183.w, d0
$00CFE0:  cmpi.b  #$04, d0
$00CFE4:  bne.b   $CFFA               ; all non-$04 values skip this payload path
$00CFE6:  jsr     $FFF89798           ; convert remaining blocks to bytes
$00CFEC:  jsr     $AA16               ; appended-effect payload load
$00CFFA:  rts
```

The healthy runtime witness saw the direct `$00F70B5F` read with value `$04`,
the copy to `$0183`, and the `$04` comparison.  In `$00CF66`, the payload
routine saves `$0183`, writes `$02` while it handles the appended effect, then
restores `$04` at `$00CF94`.  The general state dispatcher at `$012FCA` observed
both temporary `$02` and restored `$04`.  That dispatcher is a consumer of the
reused global `$0183`, not an additional direct Bank `+$15F` reader; its cases
for `$00`–`$03` must not be promoted into Bank-file-format meanings.

[VERIFIED — for this loader] `$04` is the only value selecting the appended
effect path.  `$00`, and every other non-`$04` byte value, take the same
`bne.b $CFFA` return and perform no appended-effect allocation in this routine.
[OPEN] No authentic `$001E` Bank with a non-`$04` value was inspected here, so
the authoring meaning of such values is not established beyond this branch.

### Consistency Verdict

[VERIFIED — internally inconsistent under V3.50's executed Bank-load contract]
`size.w == 3` leaves no file-relative bytes after the fixed three-block
container, while `$15F == $04` unconditionally selects a loader that allocates
and consumes that appended effect interval.  `MOVE ME*8MB` and `NIGHTMRE*2MB`
therefore assert an appended-effect path with an empty corresponding extent;
the resulting zero-byte allocation is the first observable consequence.

This verdict is deliberately bounded.  It establishes an inconsistency with
the V3.50 code path and the two healthy serialized controls; it does not prove
a formal ASR file-format validity rule, identify who authored either CDR-16
record, or establish physical-unit behavior.

## Bank Reference Resolution & MOVE ME*8MB References

[VERIFIED] The Bank file structure in RAM at `$F70A00` contains eight fixed
28-byte Instrument slot descriptors starting at offset `+$0024`:

```text
+$0000..$0007: Bank magic/header (60 00 00 00 34 c0 00 00)
+$0008..$001F: Bank name ("MOVE ME*8MB ")
+$0020..$0023: Bank status / version flags
+$0024..$003F: Slot 1 Descriptor (28 bytes)
+$0040..$005B: Slot 2 Descriptor (28 bytes)
+$005C..$0077: Slot 3 Descriptor (28 bytes)
+$0078..$0093: Slot 4 Descriptor (28 bytes)
+$0094..$0103: Slots 5..8 Descriptors (empty / zeroed)
+$0104..$011F: Song/Sequence Reference (28 bytes, empty)
+$015F:        appended Bank-effect selector byte ($04)
```

Each populated 28-byte slot descriptor contains:
- `+$00..$07`: an encoded expected volume-label field.  Its character bytes
  read `CDR-016`; the precise field encoding is [OPEN].
- `+$08..$09`: File index number (1-based word)
- `+$0A..$1B`: Instrument metadata / key range / MIDI parameters

### Populated Slot Mappings:

| Slot | Offset | Volume ID | File # | Matched Target in `DEMO INST 1-` | Blocks | Byte Size |
|---|---|---|---:|---|---:|---:|
| 1 | `+$0024` | `CDR-016` | 1 | `MOVE DRUMS` | 807 | 413,184 B (`$064E00`) |
| 2 | `+$0040` | `CDR-016` | 2 | `VRIOS RESAMP` | 8242 | 4,219,904 B (`$406400`) |
| 3 | `+$005C` | `CDR-016` | 3 | `DEMO STAB` | 1267 | 648,704 B (`$09E600`) |
| 4 | `+$0078` | `CDR-016` | 4 | `MOVE VOCALS` | 3876 | 1,984,512 B (`$1E4800`) |
| 5–8 | `+$0094..` | — | 0 | [Unused / Empty] | — | — |

**Total referenced Instrument extents:** 14,192 blocks = **7,266,304 bytes
(`$6EE000`) $\approx 7.27$ MB $\approx 8$ MB**.
[LIKELY] The Bank name rounds this aggregate to `*8MB`; it does not establish
the size of any one WaveSample, PCM owner, or allocation.

## Directory Context & Bypass Experiment

### The Sibling Directory Hypothesis:
The four referenced instruments reside in `DEMO INST 1-` (LBA `$0A4240`), whereas
the Bank resides in sibling directory `SOUNDBANKS-` (LBA `$0A4223`).
Does the hang occur because firmware searches `SOUNDBANKS-` for files 1..4?

[DISPROVEN — for the hang mechanism]:
The execution never even attempts to resolve slot descriptors 1..4 before hanging.
The hang occurs entirely within the `$15F == $04` payload path in
`$00CFB2-$00CFEC`, which runs before any slot descriptor resolution.

### Bypass Experiment (Clearing Byte `+$15F`):
In a targeted Lua experiment, byte `+$15F` of the loaded Bank container in RAM
was cleared from `0x04` to `0x00` at `$00CFB2`:
- The comparison `cmp.b #4, d0` at `$00CFE0` evaluated false and branched to
  `$CFFA` (`rts`), completely skipping the second allocator call.
- The firmware smoothly advanced past the payload path directly into **Instrument
  slot validation**.
- It read Slot 1's expected volume-label field before resolving its referenced
  Instrument, then displayed `WRONG DISK INSERTED`.

[OBSERVED — synthetic]: This establishes that:
1. The second allocation belongs exclusively to the `$15F == $04` payload path.
2. Slot volume-label validation occurs strictly downstream of that path.
3. Sibling directory search is not the cause of the allocator hang.

### Downstream Volume-Identity Result

[SYNTHETIC TEST] The temporary bypass changes only the already-classified
`+$15F` gate, allowing the firmware to reach its next refusal condition.  It is
not evidence that an unmodified load reaches this point.

[VERIFIED — media] The CDR-16 logical volume header has bytes `ENSONIQID` at
offset `$21F`.  [VERIFIED — runtime] The mounted-label buffer at `$F7101A`
contains a word-strided runtime representation of `ENSONIQ...`, while the first
Bank slot descriptor at `$F70A24` supplies character bytes `CDR-016` to the
comparison.  ROM `$F89852-$F89856` executes `move.b (a1),d0; cmp.b (a0),d0;
bne`, and the bypassed run reached that comparator before showing `WRONG DISK
INSERTED`.

[VERIFIED — functional model] OpenASR exposes the header identity stored in the
mounted CHD; it is not silently transformed into the Bank's expected label.
[OPEN] The exact physical-disc label encoding, why these two fields differ, and
whether Translator intended this Bank to be portable to a separately labelled
volume require external media/file-format evidence.  No MAME-side identity
rewrite is justified.

## Three-Way Differential

| Attribute | `MOVE ME*8MB` (CDR-16) | `NIGHTMRE*2MB` (CDR-16) | `TUTORIAL BNK` (Floppy) |
|---|---|---|---|
| Directory Record Type | `$001E` (Bank) | `$001E` (Bank) | `$001E` (Bank) |
| Directory Size `+$0E` | **3 blocks** (`$600` B) | **3 blocks** (`$600` B) | **7 blocks** (`$E00` B) |
| Allocation #1 (Container) | `$600` B (`RET=$FFAA8E`) | `$600` B (`RET=$FFAA8E`) | `$600` B (`RET=$FFAA8E`) |
| Bank Container Byte `+$15F` | `0x04` (appended-effect selector) | `0x04` (appended-effect selector) | `0x04` (appended-effect selector) |
| Container Blocks (`$496.w`) | 3 | 3 | 3 |
| Remaining Blocks (`$498.w`) | **0** (`3 - 3`) | **0** (`3 - 3`) | **4** (`7 - 3`) |
| Allocation #2 Request (`D1`) | **`$00000000`** (`0 << 9`) | **`$00000000`** (`0 << 9`) | **`$00000800`** (`4 << 9`) |
| Callsite for Allocation #2 | `$00CF78` (`RET=$00CF7A`) | `$00CF78` (`RET=$00CF7A`) | `$00CF78` (`RET=$00CF7A`) |
| Payload Content at Block 3+ | *None* (File ends at block 2) | *None* (File ends at block 2) | `TIGHT REVERB` (observed payload identity) |
| Allocator Return at `$02A400` | Header written: `$00000000` | Header written: `$00000000` | Header written: `$80000000` |
| Execution Result | **Infinite Loop at `$F8A474`** | **Infinite Loop at `$F8A474`** | **Success** (loads effect & `OB-8`) |

### Last Equivalent Semantic State:
At OS `$00CFBE`, both paths read their respective directory records and initialize
`d4 = 3` (container size).

### First Semantic Divergence:
At OS `$00CFC8`, `sub.w d4, d3` produces `d3 = 0` for `MOVE ME*8MB` (because
`size.w == 3`), whereas for `TUTORIAL BNK` it produces `d3 = 4` (because `size.w == 7`).
Because byte `+$15F` is `0x04` in both files, `MOVE ME*8MB` proceeds to calculate
`0 << 9 = 0` and calls the allocator with `D1 = 0`.

## Resolved Hang Boundary

[VERIFIED — firmware/emulator execution] For this loaded Bank in the current
functional model, `size.w == 3` and `+$15F == $04` reach a zero-byte allocation;
the resulting zero header is the first non-progressing state.

[OBSERVED] `NIGHTMRE*2MB` on this same CDR-16 has the same measured
three-block/`+$15F == $04` combination and reaches the same zero-request path.

[INFERRED] These two records may reflect a common media-authoring or conversion
condition.  The Translator identification in sector 0 is compatible with that
interpretation but does not prove it, and this investigation did not establish
the behavior of native hardware with this exact disc.

## Production Fix Status

**NO FIX.**
Under the OpenASR architectural rules:
- No production MAME patch is justified by the demonstrated firmware calculation.
- Adding a zero-size guard in the emulator would change the observed V3.50 path,
  not model a demonstrated hardware requirement.
- Rewriting the mounted volume identity to satisfy one Bank descriptor would be
  a media-specific workaround, not an established ASR board behavior.

## Evidence Table

| Claim | Evidence | Status |
|---|---|---|
| `MOVE ME*8MB` is a 3-block Bank | Directory record at LBA `$0A4225`: type `$001E`, size 3 blocks | [VERIFIED] |
| Call #1 allocates `$600` B | Dynamic tap at `$F8A246`: `RET=$FFAA8E`, `D1=$00000600` | [VERIFIED] |
| Call #2 allocates `$00000000` B | Dynamic tap at `$F8A246`: `RET=$00CF7A`, `D1=$00000000` | [VERIFIED] |
| Caller is `$15F == $04` appended-effect path | OS disassembly at `$00CF66-$00CF7A` called from `$00CFEC`; healthy controls contain serialized appended effects | [VERIFIED] |
| D1 origin is `(size - 3) << 9` | Disassembly at `$00CFC4-$00CFE6`: `move.w $e(a1), d3; sub.w d4, d3; jsr $fff89798` | [VERIFIED] |
| Byte `+$15F == $04` gates appended effect | `cmp.b #$4, d0; bne.b $cffa` at `$00CFE0`; healthy controls have appended effect payloads | [VERIFIED] |
| Populated slots reference 4 file numbers | Descriptors at `+$0024, +$0040, +$005C, +$0078` contain expected label character bytes `CDR-016`, files 1..4 | [VERIFIED] |
| Referenced files total ~8 MB | `DEMO INST 1-` files 1..4 total 14,192 blocks = 7,266,304 bytes (`$6EE000`) | [VERIFIED] |
| Clearing `+$15F` bypasses hang | Synthetic Lua test reaches volume validation and `WRONG DISK INSERTED` | [OBSERVED — synthetic] |
| Mounted and expected labels differ | CHD `$21F` is `ENSONIQID`; runtime comparator sees Bank descriptor character bytes `CDR-016` | [VERIFIED — media/runtime] |
| Sibling directory not hang cause | Hang occurs during `$15F == $04` payload path before slot resolution starts | [DISPROVEN — for the hang mechanism] |
| Executed path has no local zero-size guard | Firmware logic reaches allocator with zero when `size == 3` and flag is `0x04` | [VERIFIED — firmware execution] |
| Physical-unit outcome | No physical ASR-10 run with this medium | [OPEN] |
