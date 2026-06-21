# roadmap.md

# ASR-10 MAME roadmap

## Phase 0: Preserve research harness

Keep:

```text
asr10booth
src/mame/ensoniq/asr10_boot.cpp
```

Purpose:

```text
- trace ROM behavior
- identify MMIO windows
- test small path-opening stubs
- collect proof before writing clean driver behavior
```

Do not over-clean too early. The harness is an instrumented lab.

## Phase 1: Floppy image support

Goal:

```text
ASR-10 raw .img images mount in MAME.
```

Tasks:

```text
- inspect src/lib/formats/esq16_dsk.cpp/.h
- add ASR-10 1.6 MB raw img format
- register in asr10_boot_state::floppy_formats()
- verify V161.img and V350.img mount
```

Success criteria:

```text
media_mounted=1
ready=1
no "Unable to identify image file format"
```

## Phase 2: First sector/system read

Goal:

```text
Bootloader reads from mounted ASR OS disk.
```

Trace command:

```text
0x46 Read Data
```

Log and decode:

```text
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

```text
ST0/ST1/ST2 indicate successful read
ROM does not write $049D=0D due to FDC read result
execution advances beyond PLEASE INSERT DISK path
```

Likely failure modes:

```text
wrong sector numbering: 0..19 vs 1..20
wrong side/head
wrong track
wrong N/sector size
wrong EOT
wrong density/data rate
HFE/raw geometry mismatch
```

## Phase 3: OS load and RAM execution

Goal:

```text
ROM loads OS from disk into RAM and jumps into it.
```

Tasks:

```text
- identify OS load destination
- identify RAM/remap/overlay behavior
- trace jump from boot ROM to loaded OS
- ensure RAM aliases and vectors behave correctly
```

Potential need:

```text
68302 BAR/SCR/chipselect modeling
Super-GLU/remap modeling
high/low ROM/RAM alias cleanup
```

## Phase 4: Minimal panel/display/input model

Goal:

```text
Replace pure log-sniffing with a functional panel/frontpanel device model.
```

Start with behavior, not exact 80C52 emulation.

Minimum model:

```text
write display byte -> display buffer
read status -> ready/event bits
button input -> event queue
ROM reads event code from panel window
```

Known candidate window:

```text
$FC4800-$FC481F
```

Known candidates:

```text
$FC4817 display/panel TX data
$FC4809 bit 4 input/event/status gate
```

Success criteria:

```text
OS/UI can display text and react to mapped MAME key presses.
```

## Phase 5: Clean ASR-10 driver

Create clean driver:

```text
src/mame/ensoniq/asr10.cpp
```

Move only verified behavior from harness.

Keep out of clean driver:

```text
- PC-specific trace hacks
- forced stubs
- lowmem watchers
- branch probes
- VFX/TS stale probe windows
- ASR10HANG debug timers
```

Keep in clean driver:

```text
- ROM definitions
- verified address map
- verified FDC mapping
- verified floppy formats
- verified panel model
- verified RAM/ROM overlay behavior
- verified ES5506/ES5510 mapping when known
```

## Phase 6: Audio path

Goal:

```text
ES5506 -> pump -> ES5510 -> output
```

Reuse VFX/TS/SD MAME components where possible.

Main ASR-specific tasks:

```text
- identify ES5506 host register address window
- identify ES5510 host register address window
- identify sample RAM CPU window
- connect ES5506 to shared sample RAM
- connect ES5506/ES5510 through existing pump if applicable
- verify OS writes voice/effect init sequences
```

## Phase 7: Storage beyond boot

Goal:

```text
Load/save instruments, samples, songs.
```

Tasks:

```text
- complete floppy read/write behavior
- identify SCSI controller mapping
- likely AM33C93/WD33C93-compatible SCSI controller
- model enough SCSI for OS/file access
```

## Phase 8: Sequencer / MIDI

Sequencer is probably OS/application logic, not a separate chip.

Hardware needed:

```text
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


---
