
# fdc.md

# ASR-10 FDC findings

## Hardware

FDC:

```text
NEC uPD72069
```

Current candidate mapping:

```text
$FC4000-$FC4003
```

Current MAME device:

```cpp
UPD72069(config, m_fdc, XTAL(16'000'000));
FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35dd", asr10_boot_state::floppy_formats, true);
```

Connector tag:

```text
fdc:0
```

The no-media baseline proves the FDC sees an attached drive:

```text
drive_attached=1
media_mounted=0
ready=0
```

## Important conclusion

The `fdc:0` child connector is likely working.

No explicit `m_fdc->set_floppy(...)` has been needed so far because the conventional child connector tag appears to be discovered by the uPD72069 device.

Do not add explicit connection hacks unless evidence shows the controller cannot see the drive.

## Observed command sequence

Early command sequence included:

```text
36, 0B, 4F, 1E, 0E
```

After passing the input gate, later sequence includes:

```text
88, F3, 03, 07, 00, 08
```

Interpretation of later sequence:

```text
0x88 -> uPD72069 data-rate command, selects 250 kbit/s
0xF3 -> uPD72069 precomp/precompensation auxiliary command
0x03 E1 09 -> Specify
0x07 00 -> Recalibrate drive 0
0x08 -> Sense Interrupt Status
```

## No-media result

Without media:

```text
CMD 88 result: 80
F3 transaction includes FIFO reads: 80,68,00
```

Important decoded meaning:

```text
68,00 is Sense Interrupt Status result:
ST0=0x68
PCN=0x00
```

`0x68` means approximately:

```text
abnormal termination / failure
seek end
drive not ready
```

ROM stores:

```text
$04C6 = 68
```

Then ROM checks:

```text
$04C6 & 0xC0
```

Since bit 6 is set:

```text
ROM writes $04AE=2B
ROM writes $049D=0D
```

## Current raw image problem

Raw `.img` images:

```text
V161.img = 1,638,400 bytes
V350.img = 1,638,400 bytes
```

Expected ASR geometry:

```text
80 tracks
2 sides
20 sectors per track
512 bytes per sector
total = 1,638,400 bytes
```

Current error:

```text
Unable to identify image file format
```

Current format registration:

```cpp
void asr10_boot_state::floppy_formats(format_registration &fr)
{
    fr.add_mfm_containers();
    fr.add(FLOPPY_ESQIMG_FORMAT);
    fr.add(FLOPPY_HFE_FORMAT);
}
```

Conclusion:

```text
FLOPPY_ESQIMG_FORMAT does not recognize these ASR-10 1.6 MB raw .img files.
Need new raw ASR-10 .img format.
```

## Required image format

Add format:

```text
name: asr10_img
description: Ensoniq ASR-10 1.6MB raw disk image
extension: img
media: 3.5"
encoding: MFM
tracks: 80
heads: 2
sectors/track: 20
sector size: 512
total size: 1,638,400 bytes
```

Important unknown:

```text
Sector IDs may be 0..19 or 1..20.
```

After mounting, command `0x46 Read Data` will reveal what R value the ROM requests.

## First Read Data command

Command to trace:

```text
0x46 Read Data
```

Trace should decode:

```text
command byte
drive/head select byte
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
result C
result H
result R
result N
```

Relevant lowmem layout:

```text
$04C6 high = ST0
$04C6 low  = ST1
$04C8 high = ST2
$04C8 low  = C
$04CA high = H
$04CA low  = R
$04CC high = N
```

Potential failure:

```text
ST0=40 ST1=01
```

Interpreted as:

```text
abnormal termination + missing address mark
```

Likely causes:

```text
wrong sector ID numbering
wrong side/head
wrong track
wrong data rate/density
wrong image geometry
wrong HFE/raw decoding
```


---

# panel-input-display.md

# ASR-10 panel, display and input findings

## Candidate window

Current candidate panel/frontpanel/DUART window:

```text
$FC4800-$FC481F
```

This is separate from current MC68302 internal candidate window:

```text
$FC6800-$FC68FF
```

Interpretation:

```text
Display and button input likely go through an external panel/frontpanel/DUART/glue path, not directly through MC68302 internal SCC in the current evidence.
```

## Display output

Known display/text path:

```text
ROM print routine at $F89CB0 writes printable ASCII-like bytes to $FC4817.
```

Harness logic:

```cpp
if (address == 0x00fc4817 && ACCESSING_BITS_0_7)
{
    const u8 character = u8(data);
    panel_text_byte(character, pc);
}
```

Text is reconstructed only when:

```text
PC == $F89CB0
byte is printable ASCII: 0x20..0x7E
```

Observed panel logs:

```text
ASR10PANEL text="   ENSONIQ  ASR-10    "
ASR10PANEL text="  PLEASE INSERT DISK  "
```

Conclusion:

```text
The ROM sends display text as ASCII-compatible bytes to $FC4817.
```

Caution:

```text
This is a log sniffer, not yet a real display device.
Control bytes, cursor movement, clear display, row selection and handshaking may exist and are currently ignored.
```

## Input/event gate

Known input/status check:

```asm
FB7C84 btst #4,$FFFC4809
```

Observed behavior:

```text
If $FC4809 bit 4 is clear:
  $04EE becomes 00
  ROM writes $049D=05

If $FC4809 bit 4 is experimentally set:
  $04EE becomes 01
  ROM avoids that $049D=05 write
  ROM progresses to later FDC/media path
```

Interpretation:

```text
$FC4809 bit 4 is likely an input-change/event-available/status bit.
```

It is not necessarily the actual button code.

## Actual button data unknown

Current known:

```text
$FC4809 bit 4 = event/status gate candidate
```

Unknown:

```text
which register contains actual button/event code
which register acknowledges/clears event
whether RX/TX share $FC4817 or use adjacent addresses
how panel MCU encodes keys
```

## Likely panel model

Possible model:

```text
frontpanel buttons/display
  -> 80C52 or panel MCU / DUART / glue
      -> $FC4800-$FC481F
          -> main CPU reads status/events and writes display bytes
```

Known board clue:

```text
ENS5702000102 + 80C52 may be frontpanel/keyboard/display logic.
```

## Future minimal MAME model

Start behavioral:

```text
- display buffer accepts bytes written to $FC4817
- status register reports ready/event bits
- input port events create queued panel event
- ROM reads event status and event code
```

Do not attempt exact 80C52 emulation initially unless required.

## Next trace task

After passing the bit-4 gate, trace all reads/writes in:

```text
$FC4800-$FC481F
```

Log:

```text
pc
address
value
mem_mask
D0-D3
A0-A1
return address
nearby opcodes
```

Goal:

```text
identify event available bit
identify actual event/button code register
identify acknowledge behavior
separate display TX, status, RX/input paths
```


---

# experiments.md

# ASR-10 experiments and stubs

This file records path-opening experiments. Every experiment must state whether it is proof of hardware behavior or only a way to expose the next ROM path.

## Rule

```text
Log first.
Stub minimally.
Mark all behavior that depends on a stub.
Move only verified behavior into the clean driver.
```

## Experiment: panel/input bit 4 at `$FC4809`

### Purpose

Pass the ROM input/status gate at:

```asm
FB7C84 btst #4,$FFFC4809
```

### Experiment

Set bit 4 only at the semantic reader PC:

```text
PC == FB7C84
address == $FC4809
return value |= 0x10
```

Current/old flag name:

```cpp
ASR10_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

Preferred future name:

```cpp
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

### Result

With bit 4 clear:

```text
ROM writes $049D=05 at FB7C9E
```

With bit 4 set:

```text
ROM avoids FB7C9E $049D=05 write
$04EE becomes 01
execution reaches later FDC/media path
```

### Conclusion

```text
$FC4809 bit 4 is an input/event/status gate candidate.
```

### Caution

This does not prove the real panel hardware always returns bit 4 set. It only proves what ROM does if that status bit is set.

## Experiment: FDC result stubs for commands 1E/0E

### Purpose

Earlier path-opening experiments forced some FDC result bytes.

### Caution

When enabled, these stubs make byte-level FDC result semantics invalid for those commands.

They do not invalidate ROM control-flow discoveries, but they must not be treated as real hardware evidence.

### Current policy

Keep FDC result stubs disabled by default.

Important:

```text
The later 0x68 result from F3/Recalibrate/Sense came from real upd72069_device FIFO, not from result stubs.
```

## Experiment policy for future 0x46 success forcing

Avoid forcing command 0x46 success until raw image format and geometry are tested.

If ever added, use an explicit flag:

```cpp
static constexpr bool ASR10_EXPERIMENT_FORCE_CMD46_SUCCESS = false;
```

and log clearly:

```text
stubbed=1
```

Do not let forced FDC success leak into clean driver behavior.


---

# vfx-reuse.md

# VFX/TS/SD reuse for ASR-10

## Principle

VFX/TS/SD are references, not automatic facits.

Use them for Ensoniq-family component behavior, but let ASR-10 ROM/OS prove ASR-specific address decoding.

## Likely reusable

### ES5506 / OTIS

Likely useful for:

```text
- voice/sample playback behavior
- host register model
- sample RAM access pattern
- audio routing
```

ASR-specific questions:

```text
- CPU address window to ES5506
- sample RAM size and banking
- address translation between CPU and OTIS
- glue/bus arbitration behavior
```

### ES5510 / ESP

Likely useful for:

```text
- effect engine device
- parameter/config protocol
- status/ready behavior
- audio processing behavior
```

ASR-specific questions:

```text
- CPU/GLU address window to ESP
- effect program/parameter load sequence
- external delay/work RAM mapping
- reset/init/status behavior
```

### Pump

The MAME pump appears to model the standard Ensoniq audio pipeline:

```text
ES5506 -> pump -> ES5510
```

This is a good sign for ASR-10 audio reuse.

ASR goal:

```text
Use same or similar ES5506/ES5510/pump audio path once ASR address map and sample RAM are known.
```

## Not directly reusable without verification

```text
- VFX address map
- VFX DUART/panel addresses
- VFX FDC addresses
- VFX memory layout
- TS reference windows
```

Current harness contains VFX/TS reference candidate windows. These are useful probes but should not be treated as verified ASR addresses.

## Suggested search commands

```sh
rg "ES5510|es5510|ESP|esp" src/mame/ensoniq src/devices -g'*.cpp' -g'*.h'
rg "5506|5505|OTIS|otis|ES550" src/mame/ensoniq src/devices -g'*.cpp' -g'*.h'
rg "pump|PUMP" src/mame/ensoniq src/devices -g'*.cpp' -g'*.h'
```

## Strategy when ASR OS runs

1. Log VFX ES5510 init/config sequence.
2. Log ASR unknown write bursts after OS startup.
3. Compare patterns:

```text
control writes
address/index writes
data writes
status polling
program load
parameter writes
```

4. Connect ASR candidate window to existing ES5510 device only when the pattern is convincing.


---

# open-questions.md

# ASR-10 open questions

## Boot/media

- Does raw ASR `.img` use sector IDs `0..19` or `1..20`?
- What exact C/H/R/N/EOT/GPL/DTL does ROM request for first `0x46 Read Data`?
- Does V161 or V350 boot further once raw format mounts?
- Does ROM require SCSI probe behavior before floppy path on SCSI-equipped unit?

## ROM loading

- Are high/low EPROM byte lanes definitely correct in current ROM_LOAD16_BYTE lines?
- Where is OS loaded in RAM?
- When does ROM jump to loaded OS?
- Is there a remap/overlay switch before OS execution?

## MC68302

- Where is the true internal register block?
- Does ROM write BAR/SCR?
- Are chip selects configured dynamically?
- Which 68302 ports control FDC motor/side/density/drive select?
- Which timers/interrupts does OS require?
- Is MIDI handled through 68302 SCC or external DUART?

## Panel/input/display

- Is `$FC4817` actual DUART TX, panel data latch, or glue register?
- Which register contains actual button/event code?
- How is input event acknowledged?
- Is there an 80C52 panel MCU protocol?
- Are display control bytes currently ignored by ASCII sniffer?

## FDC

- Is uPD72069 clock correct?
- Is data rate `0x88` correctly interpreted as 250 kbit/s?
- Does ASR 1.6 MB format require unusual sector numbering/gaps?
- Are motor/ready/density signals fully correct?
- Is `FLOPPY_35_DD` sufficient as drive type?

## SCSI

- Confirm exact SCSI controller.
- Confirm ASR SCSI address window.
- Determine boot priority and probe behavior.
- Determine minimum SCSI behavior needed for OS to continue.

## Super-GLU / ES5701

- Which address windows are decoded by GLU?
- How does CPU access OTIS/ES5506?
- How does CPU access ESP/ES5510?
- How is sample RAM shared/arbitrated?
- Is any timing/waitstate behavior required for boot or OS?

## Audio

- Exact ASR ES5506 address window?
- Exact ASR ES5510 address window?
- Sample RAM size and bank mapping?
- Does existing VFX/TS pump connect correctly?
- Does ASR use same ESP parameter protocol as VFX?

## Sequencer/MIDI

- Which timer creates sequencer tick?
- Where is MIDI UART/SCC?
- Does OS sequencer work once input/display/timers/audio are functional?
- How are song/sequence files stored on floppy/SCSI?


---

# Suggested Codex prompt: create docs

Create the following docs directory and Markdown files for the ASR-10 MAME project:

```text
docs/asr10/README.md
docs/asr10/status.md
docs/asr10/roadmap.md
docs/asr10/running.md
docs/asr10/hardware-map.md
docs/asr10/boot-flow.md
docs/asr10/fdc.md
docs/asr10/panel-input-display.md
docs/asr10/memory-map.md
docs/asr10/experiments.md
docs/asr10/vfx-reuse.md
docs/asr10/open-questions.md
```

Use the content from this project summary.

Important requirements:

```text
- Do not invent new facts.
- Mark hypotheses as hypotheses.
- Keep experimental stubs separate from verified behavior.
- Emphasize that asr10booth is a research harness, not final driver.
- Do not commit floppy images.
- Include current build/run commands.
- Include current blocker: raw ASR 1.6MB .img is not recognized.
- Include next task: add raw ASR-10 .img floppy format.
```

After creating docs, run:

```sh
git diff --check
git status
```