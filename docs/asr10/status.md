# ASR-10 current status

This file tracks the current working truth for the ASR-10 MAME research harness.

Do not treat hypotheses as confirmed hardware behavior. The current `asr10booth` target is an instrumented boot/runtime harness, not the final clean production driver.

## Goal

Bring up a working Ensoniq ASR-10 in MAME, eventually comparable in usability to the existing VFX/Ensoniq-family emulation.

Short-term current goal:

```text id="b3mzrh"
Boot ROM / loaded runtime reaches LOADING SYSTEM,
then progresses beyond the current dispatcher idle state.
```

Current immediate goal:

```text id="q41sjr"
Understand why the slot 2 callback/service path completes through the finalizer and does not re-arm or produce the next dispatcher event after LOADING SYSTEM.
```

Long-term goal:

```text id="xbb1cs"
Usable ASR-10 emulation:
- OS boots from floppy/SCSI
- display and panel input work
- memory map and sample RAM work
- MC68302/control-plane behavior is modeled cleanly enough
- ES5506/ES5510 audio path works
- sequencer and UI work through original OS
```

## Current source file

Current experimental harness:

```text id="9kdhxq"
src/mame/ensoniq/asr10_boot.cpp
```

Current target:

```text id="x6klwg"
asr10booth
```

This is a research/diagnostic harness, not the final clean driver.

Future clean target should likely be:

```text id="f74g6u"
src/mame/ensoniq/asr10.cpp
```

with `asr10booth` retained as debug/boot-research target while useful.

## Current branch

Current working branch:

```text id="o2shs2"
asr10-vfx-experiments
```

## Current ROMs

ROM directory:

```text id="ovhd99"
roms/asr10booth/
```

Known ROM files:

```text id="ypibmu"
asr-648c-lo-1.5b.bin
asr-65e0-hi-1.5b.bin
```

Current load pattern in harness:

```cpp id="r88rtm"
ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, ...)
ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, ...)
```

Note: original MAME skeleton may have used hi/lo differently. This should remain on the verification list.

## Current floppy images

Local, not committed:

```text id="kdcod1"
floppies/asr10booth/V161.img
floppies/asr10booth/V350.img
```

Both are raw 1.6 MB ASR images:

```text id="on5l8p"
1,638,400 bytes
80 tracks * 2 sides * 20 sectors * 512 bytes
```

Do not commit disk images.

Recommended `.gitignore` entry:

```gitignore id="tg4rap"
/floppies/
```

## Current boot/runtime progress

The emulator reaches panel text:

```text id="l9v5mx"
ENSONIQ ASR-10
LOADING SYSTEM
```

Then loaded runtime returns to dispatcher idle around:

```text id="l9l3oo"
f87f96 / f87f9a / f87fca
```

This is not treated as a crash. It is a firmware dispatcher queue scan / idle state.

Current final hang signature often looks like:

```text id="ypv1rc"
ASR10HANG pc=f87f9a previous_pc=f87f9e opcode=122a
```

The dispatcher scan is understood roughly as:

```asm id="f7lnhv"
f87f92: movea.w $00c6.w,A2
f87f96: move.b  $0002(A2),D0
f87f9a: move.b  $0003(A2),D1
f87f9e: cmp.b   D0,D1
...
f87fc2: adda.w  #$0016,A2
f87fc6: cmpa.w  $00c8.w,A2
f87fca: bcs     f87f96
```

Current interpretation:

```text id="tac8mt"
Queue slot stride is 0x16.
A slot appears pending when byte2 != byte3.
The final state means the dispatcher sees no next event to process.
```

## Current blocker

The current blocker is no longer primarily:

```text id="sllf63"
raw ASR .img mounting
```

nor:

```text id="nm8jdq"
simple interrupt vectoring
```

nor:

```text id="lg9abh"
FC6816 0x2400 merely staying set
```

Current blocker:

```text id="w2k7c3"
Boot reaches LOADING SYSTEM, dispatcher takes slot 2, slot 2 callback executes from 007308 in baseline, the service chain reaches 00bf1a/00bf22, post-service/finalizer code writes slot 2 to 8080, and the dispatcher then finds no stable new work.
```

Current best hypothesis:

```text id="coz8hc"
The missing piece is likely callback-chain / scheduler re-arm / producer-path after slot 2 completion.
```

Not current primary blockers without new evidence:

```text id="jz89nr"
A000 / Line-A
blind FC6816 clear
FDC/raw image
broad MC68302 refactor
```

## Current MC68302 / FC68xx findings

Current candidate window:

```text id="oqmlbf"
$FC6800-$FC68FF  MC68302 internal / board-control candidate
```

Important current-phase registers:

```text id="idnsj8"
$FC6814  pending/status candidate
$FC6816  service/in-service candidate
$FC6818  control/ack/EOI-ish candidate
$FC6884  timer/control/reload candidate
$FC6894  timer/control/reload candidate
```

Accepted-looking service source:

```text id="atj080"
0x2400
```

Observed service lifecycle:

```text id="y994wl"
FC6814 000b -> 240b   source/pending injected
IACK vector 0x4e or 0x4f
FC6818 = 4000 or 8000 from vector handler
FC6814 240b -> 000b   clears naturally
runtime 00bf1a sets FC6816 c080 -> e480
runtime 00bf22 sets $0d06
optional gated experiment clears FC6816 e480 -> c080
dispatcher still returns to f87f9a idle
```

Vector findings:

```text id="gkip1x"
0x4e -> f88f06 -> writes FC6818=4000
0x4f -> f88f22 -> writes FC6818=8000
```

Both `0x4e` and `0x4f` are accepted-looking and converge.

Wrong/deprioritized vectors:

```text id="f480a8"
autovectors 0x19..0x1f -> ERROR 139 unused vector
0x40 -> ERROR 139 unused vector
0x46 -> ERROR 129 odd address error
```

## Confirmed runtime service setter

Runtime code around `00bf1a`:

```asm id="v6f9v2"
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

Confirmed context:

```text id="idxulh"
D0=00000004
$0e82=0004
$0d06=0000 before 00bf22
FC6884=703b
FC6894=703b
```

Interpretation:

```text id="a85v88"
The IACK handler does not directly set FC6816 0x2400.
Runtime code sets it later at 00bf1a.
This looks like a real service/handshake routine.
```

Additional proven facts:

```text id="rtef9a"
$0e82 later changes 0004 -> 0010.
No proven post-set firmware read/test of FC6816 0x2400 is known in the current sequence.
No proven consumption/clear of the new $0d06=ff00 before idle is known in the current sequence.
```

## Line-A / A000 finding

A000 is now explained and should not be treated as the missing service-payload producer.

Line-A vector #10 points to ROM `f882ca`:

```asm id="linea10"
f882ca: move.w D0,(A7)
f882cc: addq.l #2,2(A7)
f882d0: rte
```

Effect:

```text id="lineaeffect"
writes D0.w to the stacked SR/CCR
skips the A000 opcode
returns to the instruction after A000
```

In the `00bf18` service-setter sequence, `D0=0004`, so the post-RTE SR/CCR becomes `0004`. This does not mutate the dispatcher queue, lowmem service state, or FC681x registers directly.

## Dispatcher RTE frame and slot fields

The dispatcher builds a normal 68000 short RTE frame:

```asm id="dispatcherrte"
f87fa2  move.l $0006(A2),-(A7)   ; callback PC
f87fa6  move.w $000a(A2),-(A7)   ; stacked SR
f87fb4  move.w $000c(A2),D5
f87fb8  clr.w  $000c(A2)
f87fbc  move.w A2,$0b6a.w
f87fc0  rte
```

Slot field candidates:

```text id="slotfields"
+0x02/+0x03  pending/equalized bytes
+0x06        callback / RTE PC
+0x0a        stacked SR
+0x0c        dispatch context / continuation, moved to D5 and cleared
+0x0e        extra context / USP-ish candidate
+0x10        secondary continuation/list pointer candidate
+0x12        companion state to +0x10
```

Baseline slot 2 evidence:

```text id="slot2rte"
slot base=002400
pending word=002402
frame SR=0000
frame PC=007308
dispatcher SR before RTE=2700
first-PC diagnostics showed actual_pc=007308
no immediate IACK was observed in baseline
007308 executes and starts with jsr $fff8d01a
```

Unknown for 4e/4f profiles:

```text id="rteunknown4e4f"
whether first-PC is also 007308
whether SR restore from 2700 to 0000 causes immediate IACK/service entry
where f8d020/f8d05a sit in the callback chain
```

## Slot continuation and finalizer model

Continuation logic around `f880e0..f88100`:

```asm id="contlogic"
f880e0: move.w  $0010(A2),D0
f880e4: beq.s   f880fc
f880e6: movea.w D0,A0
f880e8: move.w  A0,$000c(A2)
f880ec: move.w  (A0),$0010(A2)
f880f0: bne.s   f880f6
f880f2: clr.w   $0012(A2)
f880f6: bclr    D2,$0002(A2)
f880fa: bra.s   f88100
f880fc: bset    D2,$0002(A2)
f88100: bset    D2,$0003(A2)
```

With `D2=7`:

```text id="contmeaning"
+0x10 == 0 gives byte2 |= 0x80 and byte3 |= 0x80, so equalized/not pending.
+0x10 != 0 can give byte2 != byte3, so pending.
```

Current logs do not prove a nonzero continuation path through `f880e6/f880ec/f880f6`.

Finalizer/list-drain/completion candidate:

```asm id="f8cefinalizer"
f8ce00: move.w $0b6c.w,D1
f8ce04: move.w $000c(A2),D0
f8ce08: beq.s  f8ce1c
f8ce0a: move.w D0,$0b6c.w
f8ce0e: subq.b #1,$0b7f.w
f8ce12: move.w D0,D5
f8ce14: move.w D1,(A5)
f8ce16: move.w D0,D1
f8ce18: clr.w  $000c(A2)
f8ce1c: move.w $0010(A2),D0
f8ce20: beq.s  f8ce3a
f8ce22: move.w D0,D5
f8ce24: move.w D0,$0b6c.w
f8ce28: subq.b #1,$0b7f.w
f8ce2c: move.w (A5),D0
f8ce2e: beq.s  f8ce32
f8ce30: move.w D0,D5
f8ce32: bne.s  f8ce28
f8ce34: move.w D1,(A5)
f8ce36: clr.l  $0010(A2)
f8ce3a: move.w #$8080,$0002(A2)
f8ce40: move.w #$0000,D0
f8ce44: A000
f8ce46: rts
```

For slot 2:

```text id="slot2finalizer"
A2=002400
f8ce36 clears 002410/002412 if that path runs
f8ce3a writes 002402=8080
8080 is equalized/not pending because byte2 == byte3
This looks like completion/finalizer output, not a failed enqueue.
```

Healthy pending production is distinct:

```asm id="healthypending"
f87f28: clr.b  $0002(A1)
f87f2c: move.b #$01,$0003(A1)
```

This creates `0001`, so byte2 != byte3 and the slot is pending. Code around `f88120/f88124` also appears capable of direct pending/equalize transitions for slot 4/5-like paths.

## Latest negative result

Experiment:

```text id="ro8b2a"
ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER
```

When enabled, this clears FC6816 `0x2400` only after:

```text id="qhp6c6"
- runtime setter at/near 00bf1a has set FC6816 0x2400
- $0d06 has been set by 00bf22
- FC6814 0x2400 is already clear
- at least one later dispatcher RTE has occurred
- FC6816 still has 0x2400 set
```

Result:

```text id="rsamfk"
FC6816 e480 -> c080 works mechanically.
No panel advance beyond LOADING SYSTEM.
No post-clear FDC activity.
No new error.
Final hang remains f87f9a dispatcher idle.
```

Conclusion:

```text id="o0lq5s"
FC6816 0x2400 may be part of an in-service/EOI/service-active lifecycle,
but FC6816 0x2400 staying set is not the sole blocker.
```

## Current known-good baseline

No-media and earlier FDC/media baselines were useful for proving earlier boot phases.

Earlier no-media behavior:

```text id="j4grze"
drive_attached=1
media_mounted=0
ready=0
motor=1
density=dd
FDC Recalibrate/Sense result = 68,00
Panel eventually reaches PLEASE INSERT DISK path
```

This proved:

```text id="v4psn9"
- boot harness builds
- ROM executes
- panel text path works
- FDC is visible
- fdc:0 connector is attached
- no-media behavior is consistent
```

Current baseline should now mean:

```text id="hzv4jy"
experiments disabled/default
boot reaches LOADING SYSTEM
final state is dispatcher idle around f87f96/f87f9a
no unexpected ERROR prompt
```

## FDC/media status

FDC/media findings remain valid and should stay documented in `fdc.md`.

Earlier raw `.img` issue:

```text id="pv26fe"
Fatal error: Device 3.5" double density floppy drive load failed:
Unable to identify image file format
```

This implied that ASR-10 raw 1.6 MB `.img` format support may be needed:

```text id="ihdyo7"
80 cylinders
2 heads
20 sectors per track
512 bytes per sector
1,638,400 bytes total
MFM
3.5" DSDD-like container from MAME perspective
```

But current status after reaching `LOADING SYSTEM`:

```text id="cwa6c7"
FDC/media is not the immediate current blocker unless logs show post-service FDC activity.
```

Do not add FDC success stubs just to escape the current dispatcher idle state.

## Safe established boot aids

These are currently considered established path-openers:

```cpp id="pmi2va"
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 = true;
ASR10_EXPERIMENT_CMD88_RATE_500K = true;
ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE = true;
ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3 = true;
```

Keep documenting these as path-openers until their exact hardware semantics are confirmed.

## Experimental flags should default false

These should be restored to disabled/default state before committing unless explicitly testing:

```cpp id="us9w4m"
ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B = false;
ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480 = false;

ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ = false;
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR = false;
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT = false;
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR = false;

ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER = false;
```

Default placeholder vector:

```cpp id="hf1sgl"
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE = 0x40;
```

`0x40` is known wrong/placeholder. It maps to unused vector / ERROR 139.

## Immediate next step

Do not add a broad new behavior stub first.

Next task:

```text id="p6fd03"
Follow the actual slot 2 callback chain and identify where it chooses producer/re-arm, wait-for-status, or finalizer.
```

Focus:

```text id="cku34j"
007308
f8d01a
f8d020/f8d05a
00bef2/00bf00/00bf1a/00bf22
f8ce00..f8ce46
first branch choosing producer/re-arm, wait-for-status, or finalizer
```

Current key question:

```text id="lox769"
Why does the slot 2 callback/service path finish in the f8ce00..f8ce46 finalizer and leave slot 2 equalized at 8080 instead of producing or re-arming the next pending dispatcher work?
```

## Suggested next analysis prompt

```text id="lqejnq"
Analyze callback-chain / scheduler re-arm after the 0x4e/0x4f service sequence.

Do not edit files and do not commit.

Known facts:
- 0x4e and 0x4f synthetic IACK runs are accepted and converge.
- FC6814 0x2400 clears naturally.
- FC6816 0x2400 is set at 00bf1a:
    ori.w #$2400,$00fc6816.l
- 00bf22 sets $0d06.
- Optional experiment clearing FC6816 e480 -> c080 after 00bf1a + $0d06 + dispatcher RTE works mechanically.
- Even after FC6816 is cleared, there is:
  - no panel advance beyond LOADING SYSTEM
  - no post-clear FDC activity
  - no new error
  - final idle/hang remains f87f9a dispatcher scan.
- Therefore FC6816 service clear is not sufficient.
- A000 is explained as Line-A vector #10 to f882ca; it writes D0.w to stacked SR, skips A000, and returns after the opcode.
- Baseline dispatcher RTE for slot 2 has frame PC=007308, frame SR=0000, dispatcher SR=2700, and first-PC diagnostics showed actual_pc=007308 with no immediate IACK.
- 007308 executes and starts with jsr $fff8d01a.
- Post-service/finalizer writes slot 2 pending word 002402=8080, which is equalized/not pending.

Goal:
Determine where the callback chain should produce/re-arm next dispatcher work, or whether it is correctly waiting for an external completion.

Tasks:
1. Disassemble/follow baseline from 007308 to f8d01a, f8d020/f8d05a if reached, 00bef2, 00bf1a, 00bf22, and f8ce3a.
2. Run the same first-PC diagnostics on 0x4e/0x4f profiles:
   - prove f87fc0 -> 007308, or
   - prove immediate IACK/service entry after SR restore.
3. Identify the first branch where callback-chain chooses:
   - producer/re-arm
   - wait-for-status
   - finalizer
4. Lift FC68xx/MC68302 again only if firmware clearly tests external status/timer/completion and that test controls producer/re-arm.
5. End with one best next diagnostics-only breakpoint set.
Do not implement it yet.
```

## What not to do next

Do not immediately:

```text id="bqx3ew"
- fake panel input
- fake FDC activity
- force FDC command 0x46 success
- implement full MC68302
- chase audio/PCM/A-D path
- add broad new behavior stubs
- refactor the whole harness before the next blocker is understood
```

## Validation

Before committing harness/source changes:

```sh id="zqed3a"
git diff --check
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

Before committing docs-only changes:

```sh id="z8fwqx"
git diff --check
git status --short
```

## Current one-line summary

The ASR-10 boot harness reaches `LOADING SYSTEM`, dispatcher takes slot 2, baseline first-PC after dispatcher RTE is `007308`, `007308` starts with `jsr $fff8d01a`, the service chain reaches `00bf1a/00bf22`, and post-service/finalizer code writes slot 2 to equalized `8080`; the next blocker is therefore the callback-chain/scheduler re-arm/producer path after slot 2 completion, not A000, blind FC6816 clear, FDC/raw image handling, or a broad MC68302 refactor.
