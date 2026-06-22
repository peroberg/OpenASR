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
Understand what dispatcher event, queue payload, timer cadence, or hardware completion signal is missing after the accepted-looking MC68302/FC68xx 0x2400 service sequence.
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
After an accepted-looking MC68302/FC68xx 0x2400 service sequence, the runtime returns to dispatcher idle at f87f9a instead of generating/receiving the next event.
```

Current best hypothesis:

```text id="coz8hc"
The missing piece is likely dispatcher queue re-arm, event payload, timer cadence, FC6884/FC6894 completion behavior, or lowmem service state around $0d06/$0e82.
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
Analyze dispatcher queue/event payload after the accepted 0x4e/0x4f service sequence.
```

Focus:

```text id="cku34j"
queue base $00c6
queue end $00c8
active/current record $0b6a
slot byte2/byte3
slot 2 callback/context
last non-idle event before final f87f9a
whether any slot should become pending again but does not
readers/writers of $0d06 and $0e82
FC6884/FC6894 behavior after service sequence
```

Current key question:

```text id="lox769"
After 00bf1a and $0d06 set, which queue slot, lowmem flag, timer register, or completion signal should change to make the dispatcher leave f87f9a idle?
```

## Suggested next analysis prompt

```text id="lqejnq"
Analyze dispatcher queue/event payload after the 0x4e/0x4f service sequence.

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
- Recent dispatcher context says slot 2 was handler-cleared at f87fb0 before/around the service setter path.

Goal:
Determine what event/queue payload is missing after the service sequence.

Tasks:
1. Using existing logs first, compare baseline, 0x4e one-shot, 0x4f one-shot, and 0x4e/0x4f with FC6816 clear.
2. Extract all dispatcher queue slot transitions after LOADING SYSTEM:
   - slot number
   - record address
   - byte2
   - byte3
   - handler/callback pointer
   - return PC / last_rte_return_pc
   - when f87fb0 clears/equalizes a slot
   - when any slot becomes pending, i.e. byte2 != byte3
3. Focus on slot 2:
   - what handler/callback address does it contain?
   - what code runs before it is cleared?
   - how is it related to 00bf1a / $0d06 / $0e82?
4. Identify the last real non-idle event before final f87f9a idle.
5. Determine whether after the service sequence any queue slot is supposed to be re-armed but is not.
6. Search for lowmem flags near:
   - $0d06
   - $0e82
   - queue base $00c6
   - queue end $00c8
   - active/current record $0b6a if relevant
7. Explain whether the missing next step looks like:
   - missing queue re-arm
   - missing timer tick/event cadence
   - missing FC6884/FC6894 completion
   - missing panel/DUART event
   - missing disk/FDC event
   - OS simply waiting for a later periodic source
8. Produce a compact timeline:
   LOADING SYSTEM -> first accepted IACK -> FC6814 clear -> 00bf1a service set -> $0d06 set -> FC6816 optional clear -> final dispatcher idle.
9. End with one best next diagnostics-only experiment.
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

The ASR-10 boot harness now reaches `LOADING SYSTEM` and executes an accepted-looking MC68302/FC68xx `0x2400` service sequence through vectors `0x4e/0x4f`; FC6814 clears, runtime code at `00bf1a` sets FC6816 `c080 -> e480`, `$0d06` is set, and FC6816 can be gated-cleared back to `c080`, but the machine still returns to dispatcher idle at `f87f9a`, so the next blocker is likely missing dispatcher event payload, timer cadence, queue re-arm, or hardware completion semantics rather than simple FDC/media, interrupt vectoring, or FC6816 service-clear behavior.
