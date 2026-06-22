# ASR-10 MAME roadmap

This roadmap describes the path from the current research harness to a usable ASR-10 MAME driver.

The current target is not yet a clean production driver. It is an instrumented boot/runtime harness used to discover how the ASR-10 ROM and loaded runtime code talk to hardware.

## Current state

The emulator currently reaches:

```text id="v7vba3"
ENSONIQ ASR-10
LOADING SYSTEM
```

Then loaded runtime code returns to firmware dispatcher idle around:

```text id="vbv8ho"
f87f96 / f87f9a / f87fca
```

This is not treated as a crash. It is a dispatcher queue scan / idle state.

Current best hypothesis:

```text id="9l6jvv"
The missing piece is likely dispatcher queue re-arm, event payload, timer cadence, FC6884/FC6894 completion behavior, or lowmem service state around $0d06/$0e82 after the accepted-looking MC68302/FC68xx 0x2400 service sequence.
```

## Phase 0: Preserve research harness

Keep:

```text id="vegbwv"
asr10booth
src/mame/ensoniq/asr10_boot.cpp
```

Purpose:

```text id="azqovh"
- trace ROM and loaded runtime behavior
- identify MMIO windows
- test small path-opening stubs
- collect proof before writing clean driver behavior
- preserve negative results so old dead ends are not repeated
```

Do not over-clean too early. The harness is an instrumented lab.

Current discipline:

```text id="fv31cb"
Log first.
Stub minimally.
Keep experiments disabled by default.
Commit small checkpoints.
Move only verified behavior into the eventual clean driver.
```

## Phase 1: Reach stable control-plane progress beyond `LOADING SYSTEM`

Goal:

```text id="q4dx4n"
Loaded runtime leaves dispatcher idle and continues boot/init beyond LOADING SYSTEM.
```

Current known flow:

```text id="zeb4ub"
LOADING SYSTEM
-> accepted-looking 0x2400 service source
-> IACK vector 0x4e or 0x4f
-> FC6818 handler write
-> FC6814 clears naturally
-> runtime 00bf1a sets FC6816 c080 -> e480
-> runtime 00bf22 sets $0d06
-> optional gated clear can return FC6816 e480 -> c080
-> dispatcher still returns to f87f9a idle
```

Current conclusion:

```text id="a8cufy"
Vectoring works well enough for this experiment.
Interrupt flood is not the sole blocker.
FC6816 0x2400 staying set is not the sole blocker.
```

Main tasks:

```text id="h59vgn"
- analyze dispatcher queue record layout
- identify queue base/end/current record behavior around $00c6/$00c8/$0b6a
- identify slot 2 callback/context and why it is cleared near f87fb0
- determine what should make byte2 != byte3 after the service sequence
- trace readers/writers of $0d06 and $0e82
- determine whether FC6884/FC6894 should generate a later timer/completion event
- determine whether the missing event is timer, queue payload, panel, FDC, or board-glue related
```

Success criteria:

```text id="edl99w"
- runtime no longer ends in the same f87f9a dispatcher-idle signature
- a new queue event or hardware-completion path is observed
- panel progresses beyond LOADING SYSTEM or the next clear boot blocker is exposed
```

## Phase 2: Minimal MC68302 / FC68xx control-plane model

Goal:

```text id="lkflrt"
Replace ad-hoc MC68302/FC68xx experiments with a small named state model.
```

Known current candidates:

```text id="78eawx"
$FC6814  pending/status candidate
$FC6816  service/in-service candidate
$FC6818  control/ack/EOI-ish candidate
$FC6884  timer/control/reload candidate
$FC6894  timer/control/reload candidate
```

Current accepted-looking vectors:

```text id="tbz31i"
0x4e -> handler writes FC6818=4000
0x4f -> handler writes FC6818=8000
```

Potential model shape:

```cpp id="nvu9tn"
struct asr10_68302_state
{
    u16 pending;
    u16 in_service;
    u16 control;
    u16 timer_reload;
    u8 irq_vector;
    bool lrclk_present;

    void set_pending(u16 mask);
    void acknowledge(u8 level);
    void clear_pending(u16 mask);
    bool source_in_service(u16 mask) const;
    void end_of_interrupt(u16 mask);
};
```

Do not implement a full MC68302 yet unless the firmware proves it is required.

Success criteria:

```text id="joayhv"
- fewer experiment flags
- named control-plane behavior replaces PC-specific hacks
- accepted service sequence still works
- boot/runtime progresses at least as far as the harness did before cleanup
```

## Phase 3: Floppy image support

Goal:

```text id="2o7c17"
ASR-10 raw .img images mount in MAME.
```

This was an earlier primary blocker and remains important, but it is not the immediate blocker once the emulator reaches `LOADING SYSTEM` and dispatcher idle.

Tasks:

```text id="xwyii1"
- inspect src/lib/formats/esq16_dsk.cpp/.h
- add ASR-10 1.6 MB raw img format if still needed
- register in asr10_boot_state::floppy_formats()
- verify V161.img and V350.img mount
```

Expected raw geometry:

```text id="358l99"
80 tracks
2 sides
20 sectors per track
512 bytes per sector
total = 1,638,400 bytes
```

Success criteria:

```text id="xiqlf1"
media_mounted=1
ready=1
no "Unable to identify image file format"
```

## Phase 4: First sector/system read

Goal:

```text id="i4qmhx"
Bootloader reads from mounted ASR OS disk.
```

Trace command:

```text id="vo3ekb"
0x46 Read Data
```

Log and decode:

```text id="g8c4hs"
command byte
drive/head select
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
result C/H/R/N
```

Success criteria:

```text id="ss4n0a"
ST0/ST1/ST2 indicate successful read
ROM does not write $049D=0D due to FDC read result
execution advances beyond PLEASE INSERT DISK path
```

Likely failure modes:

```text id="vuh8wa"
wrong sector numbering: 0..19 vs 1..20
wrong side/head
wrong track
wrong N/sector size
wrong EOT
wrong density/data rate
HFE/raw geometry mismatch
```

## Phase 5: OS load and RAM execution

Goal:

```text id="8jq3aw"
ROM loads OS from disk into RAM and jumps into it.
```

Current note:

```text id="7lno7l"
The harness has already observed loaded runtime code around 00bf1a.
The exact OS load destination, relocation, and runtime base still need to be documented.
```

Tasks:

```text id="j2ptuy"
- identify OS load destination
- identify runtime base for code around 00bf1a
- match runtime bytes around 00bf1a against OS/SW artifacts
- identify RAM/remap/overlay behavior
- trace jump from boot ROM to loaded OS
- ensure RAM aliases and vectors behave correctly
```

Potential need:

```text id="no80ux"
68302 BAR/SCR/chipselect modeling
Super-GLU/remap modeling
high/low ROM/RAM alias cleanup
```

## Phase 6: Minimal panel/display/input model

Goal:

```text id="faq1ue"
Replace pure log-sniffing with a functional panel/frontpanel device model.
```

Start with behavior, not exact 80C52 emulation.

Minimum model:

```text id="xms7w6"
write display byte -> display buffer
read status -> ready/event bits
button input -> event queue
ROM/runtime reads event code from panel window
```

Known candidate window:

```text id="zg8npt"
$FC4800-$FC481F
```

Known candidates:

```text id="33l70x"
$FC4817 display/panel TX data
$FC4809 bit 4 input/event/status gate
```

Current caution:

```text id="kkbs63"
Panel input is probably not the immediate current blocker while runtime is still in LOADING SYSTEM and dispatcher idle.
Do not fake LOAD/CMD/EDIT/instrument-select input until logs show the runtime is actually waiting for user events.
```

Success criteria:

```text id="6bqz5e"
OS/UI can display text and react to mapped MAME key presses.
```

## Phase 7: Clean ASR-10 driver

Create clean driver:

```text id="riebqp"
src/mame/ensoniq/asr10.cpp
```

Move only verified behavior from harness.

Keep out of clean driver:

```text id="qg16dp"
- PC-specific trace hacks
- forced stubs
- lowmem watchers
- branch probes
- VFX/TS stale probe windows
- ASR10HANG debug timers
- disabled experiments that only proved negative results
```

Keep in clean driver:

```text id="mkr5te"
- ROM definitions
- verified address map
- verified FDC mapping
- verified floppy formats
- verified panel model
- verified MC68302/FC68xx control-plane subset
- verified RAM/ROM overlay behavior
- verified ES5506/ES5510 mapping when known
```

## Phase 8: Audio path

Goal:

```text id="377t3j"
ES5506 -> pump -> ES5510 -> output
```

Reuse VFX/TS/SD MAME components where possible.

Main ASR-specific tasks:

```text id="kuxc9p"
- identify ES5506 host register address window
- identify ES5510 host register address window
- identify sample RAM CPU window
- connect ES5506 to shared sample RAM
- connect ES5506/ES5510 through existing pump if applicable
- verify OS writes voice/effect init sequences
```

A/D and PCM caution:

```text id="53ugpw"
Audio sample data probably does not flow through the MC68302 as ordinary CPU-readable input.
MC68302 may still observe/control audio-related clocks or status, such as LRCLK.
```

## Phase 9: Storage beyond boot

Goal:

```text id="hbdlsv"
Load/save instruments, samples, songs.
```

Tasks:

```text id="uo3ivb"
- complete floppy read/write behavior
- identify SCSI controller mapping
- likely AM33C93/WD33C93-compatible SCSI controller
- model enough SCSI for OS/file access
```

## Phase 10: Sequencer / MIDI

Sequencer is probably OS/application logic, not a separate chip.

Hardware needed:

```text id="sr4zvo"
- timers/ticks
- interrupts
- panel input
- display
- RAM
- storage
- MIDI serial path
- audio voice engine
```

Once these work, original ASR OS sequencer should start working naturally.

Open questions:

```text id="x962lz"
- Which timer creates sequencer tick?
- Where is MIDI UART/SCC?
- Is MIDI handled through MC68302 SCC or external DUART?
- How are song/sequence files stored on floppy/SCSI?
```

## Roadmap discipline

The roadmap should be updated after every major checkpoint.

A phase can be “partially passed” without being fully clean. For example:

```text id="3nbv7k"
FDC/media work may expose later paths but still need cleanup.
MC68302 service work may prove vector/lifecycle facts but still require a better model.
Panel sniffing may reveal display behavior before a real device model exists.
```

Prefer:

```text id="wg2bev"
diagnostics -> narrow experiment -> negative/positive result -> docs update -> small commit
```

over broad behavior stubs.
