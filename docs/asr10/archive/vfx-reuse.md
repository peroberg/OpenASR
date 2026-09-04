
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