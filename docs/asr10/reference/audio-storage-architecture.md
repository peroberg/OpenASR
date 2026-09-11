# ASR-10 Audio and Storage Boundary Architecture

> **Runtime status update 2026-08-22.** The original storage-boundary pass
> predated working IRQ1, IDMA and note playback. Later verified runtime reaches
> `FILE LOADED`; instrument selection plus panel/MIDI note stimulus allocates
> voices and produces pitch-checked dry ES5506 audio. ES5510/pump/effects and
> actual sampling/RECORD remain open. Historical boundary reasoning below is
> retained, but old "not reached" statements are corrected in the table.

> **Sampling-path update 2026-08-23.** Full RECORD reaches `WAITING`. Static
> firmware now verifies a separate recording path: completed SCC RX ranges are
> copied by MC68302 IDMA into an advancing recording destination. The current
> LEFT run prepares destination `$02C110`, whose physical sound-memory decode is
> still `[OPEN]`; no RX transfer has occurred in the model.

## Scope

This document records the current boundary model between ASR-10 storage
completion and the Ensoniq audio subsystem. It is intentionally narrow: it does
not attempt to describe the whole ASR-10, and it does not implement or propose
new instrumentation.

Evidence levels:

- `[Verified silicon spec]` local Ensoniq chip specification.
- `[Verified firmware]` V3.50/ROM static firmware evidence.
- `[Verified runtime]` reproduced current MAME runtime result.
- `[Likely]` inference supported by multiple local facts, but not yet directly
  proven.
- `[OPEN]` plausible or important, but not established.
- `[DISPROVEN]` contradicted by current evidence.

Chip specifications describe silicon capabilities. They do not verify ASR-10
board wiring. Firmware proves how the ROM/OS addresses devices, but not the
physical PAL/GAL or interrupt routing unless the source explicitly contains that
board evidence.

## Boundary summary

```text
MC68302/68000
  CS1 $FF6000-$FF7FFF (table at $FF7F00-$FF7FFF)
    -> per-voice sample banking table (32 voices x 4 words)

  CS2 $FC2000-$FC3FFF
    -> ES5506/OTTO host window at $FC2000
    -> ES5510/ESP host window at $FC3000-$FC31FF

  CS3 $FC4000-$FC5FFF
    -> uPD72069 FDC at $FC4000
    -> SCSI controller window at $FC5001/$FC5003

  MC68302 internal window
    -> SIB/IDMA registers and interrupt controller state

storage async completion
  -> $0402 continuation pointer
  -> vector $4B IDMA/SIB completion
  -> vector $51 shared FDC/SCSI completion

audio runtime
  -> CS1 per-voice sample banking (translates ES5506 wavetable bus to DRAM)
  -> ES5701/Super-GLU-class audio/sound-memory glue
  -> ES5506 sample playback
  -> ES5510 effects DSP
```

## ES5701 / Super-GLU

[Verified silicon spec] `docs/ensoniq/ES5701.pdf` describes Super-GLU as a gate
array that interfaces a 68000-family host with the Ensoniq ESP DSP and an OTIS
chip, and also interfaces the OTIS multiplexed address/data bus with static
sound memory.

[Verified silicon spec] The documented functions are audio and memory glue:

- 68000-to-ESP interface, including multiplexing host address/data for the ESP
  bus and generating `/DTACK` for the 68000.
- 68000-to-OTIS interface, including isolating the processor bus from the sound
  bus during OTIS sound-memory access and passing it through for processor
  access to OTIS registers or sound memory.
- OTIS-to-static-memory interface, including demultiplexing/latching OTIS
  address/data and adapting sound-memory word width.
- Clock generation from a crystal source, including buffered 10 MHz and 8 MHz
  outputs in the spec.
- Bus/control signals including `/ESP`, `/DBUS`, `/UDBEN`, `/LDBEN`, `/DTACK`,
  OTIS `/RAS` and `/CAS`, and latched address/data lines for static sound
  memory.

[Verified firmware] The ASR-10 memory model has a verified CS2 window
`$FC2000-$FC3FFF` containing the ES5506 host window at `$FC2000` and the ES5510
host window at `$FC3000-$FC31FF`.

[Verified runtime] The current MAME model boots V3.50 to `FILE 1  TUTORIAL BNK`
using real ES5506 and ES5510 host-register devices for the accessed host
windows.

[OPEN] The exact ASR-10 board-level Super-GLU variant and wiring are not
verified. The spec supports audio/sound-memory glue, not storage interrupt glue.

[DISPROVEN] Nothing in the ES5701 silicon spec justifies describing ES5701 as
the verified source of FDC/SCSI IRQ routing. Storage IRQ routing remains separate
and open.

[DISPROVEN — specified ES5701 register/storage model] ES5701 contains zero internal
registers or RAM capable of storing the table, and its specified address outputs reach
only up to LA19. Physical board wiring between CS1 and ES5701 is [NOT ESTABLISHED / OPEN],
but ES5701 cannot store the per-voice sample banking table ($FF7F00-$FF7FFF).

## ES5506 / OTTO

[Verified silicon spec] `docs/ensoniq/ES5506.pdf` describes OTTO/ES5506 as a
sample playback synthesizer. It has 32 independent voices, a dedicated host
register interface, a separate sound-memory interface, sample start/end/loop
state, pitch/frequency control, filtering, volume/panning and hardware envelope
support.

[Verified silicon spec] The ES5506 host register model includes:

- `PAGE`, selecting the voice/register page.
- `IRQV`, the voice interrupt vector register.
- `PAR`, a 10-bit pot/wheel A/D register whose conversion is initiated by a PAR
  read.
- per-voice control, start, end, accumulator, frequency, volume, ramp and filter
  registers.

[Verified silicon spec] ES5506 fetches sample data autonomously from external
sound memory while processing voices. Its voice accumulator addresses sample
memory; loop/control state governs stop, loop, interrupt and direction behavior.

[Verified silicon spec] ES5506 has an `IRQB` interrupt output. The spec ties it
to voice interrupt events and `IRQV`; reading `IRQV` is part of the service
protocol.

[Verified firmware] ASR-10 accesses ES5506 through the `$FC2000` host window.
The verified MOVEP thunks access PAR, PAGE and IRQV-like registers through the
`$FC2001` odd-lane convention.

[Verified source] The ASR-10 Musician's and Service Manuals define two
effect-selected **system** rate/polyphony classes: 29.7619 kHz / 31 voices and
44.1000 kHz / 23 voices. The user-facing count must not be silently equated
with ES5506's zero-based ACTV slots (32 and 24 would be `$1f` and `$17`). The
firmware/board representation of this documented state remains `[OPEN]`; see
`../investigations/audio-rate-mode-state-v350.md`.

[Verified runtime] ES5506 `PAR` now reads through the ASR-10 panel analog path
instead of a fixed constant. Current V3.50 still reaches `FILE 1  TUTORIAL BNK`;
observed PAR reads returned raw centered 10-bit data.

[Likely] The PB9/vector `$47` chain is audio/ES5506-related rather than
storage-related. Evidence: firmware unmasks PB9, the handler differs from PB10
and PB11, and surrounding documented code points toward audio/ES5506 service.

[OPEN] The physical ES5506 `IRQB` to MC68302 PB9 routing is not board-verified.
Do not treat PB9 as a proven ES5506 wire until there is board evidence or a
firmware chain that requires that exact source.

[Verified firmware / runtime] The note-on to ES5506 voice-programming path and
per-voice sample banking table ($FF7F00-$FF7FFF) are verified (commit b7cd112199d).
The general loaded instrument root structure, exact sample-object allocation policy,
and physical board IC implementing the banking table remain [OPEN].

## ES5510 / ESP

[Verified silicon spec] `docs/ensoniq/ES5510.pdf` describes ESP/ES5510 as a DSP
with a host interface, internal GPR memory, microinstruction memory, special
purpose registers, external RAM/I/O access and a sample-period execution model.

[Verified silicon spec] The ESP host interface maps the chip into a 256-byte
host block. The documented select/write registers include:

- `$80` read select for GPR plus instruction fields.
- `$A0` write select for GPR.
- `$C0` write select for instruction.
- `$E0` write select for GPR plus instruction.

[Verified silicon spec] Host access to internal GPR/instruction memory is
synchronized through the host-control status. During normal execution, host
transfers occur at the `END` boundary; when halted, the host can access internal
state more freely.

[Verified silicon spec] ESP has an external RAM/I/O interface for delay-line,
table and I/O addressing under microinstruction control.

[Verified firmware] ASR-10 uses the `$FC3000-$FC31FF` ES5510 host window. The
current map routes `$FC3001-$FC303F` to the stock ES5510 host register block and
uses fixed-offset wrappers for `$FC3101`, `$FC3141`, `$FC3181` and `$FC31C1`,
corresponding to host offsets `$80`, `$A0`, `$C0` and `$E0`.

[Verified runtime] The current MAME model constructs a stock ES5510 device for
host-interface state and calls `set_disable()`. That allows host register,
GPR/instruction-array and select/commit behavior to be modeled without executing
ESP microcode.

[OPEN] Full ESP execution, external delay/work RAM routing, audio routing and
effect output are not yet verified in the ASR-10 MAME model.

## Instrument-load boundary model

The current architecture chain is:

```text
storage request
 -> FDC/SCSI state machine
 -> $0402 async continuation pointer
 -> device completion dispatcher
 -> data transfer
 -> CPU-visible instrument metadata
 -> sample/sound memory
 -> runtime voice setup
 -> ES5506 playback
```

Evidence by boundary:

| boundary | status |
|---|---|
| FDC/SCSI async continuation pointer `$0402` | [Verified firmware] general async device/storage-I/O continuation pointer. |
| vector `$4B` completion path | [Verified firmware] MC68302 IDMA/SIB completion dispatcher using `$0402`. |
| vector `$51` completion path | [Verified firmware] shared FDC/SCSI storage completion dispatcher using `$0402`. |
| FDC RECALIBRATE -> status -> continuation | [Verified firmware] `$0402 <- $BA5E`, command `07 00`, vector `$51`, FDC status/SENSE path, then continuation. |
| SCSI RESET -> status -> continuation | [Verified firmware] `$0402 <- $B1A4`, writes `$18/$00` to `$FC5001/$FC5003`, vector `$51`, SCSI status path, then continuation. |
| async FDC READ with IDMA | [Verified runtime] completed: 21 IDMA arms, 337 sectors and 172,544 bytes reach low RAM; storage completion uses vector `$51`, while IDMA's internal vector `$4B` remains masked for this path. |
| sampling SCC RX -> recording destination | [Verified firmware] type `$0E` through `$14DA/$00B478` programs IDMA from the completed SCC object range to object `+$20`; completion `$00AA48` advances the destination. [Verified runtime] LEFT mode prepares `$02C110`; physical decode and actual transfer remain [OPEN]. |
| historical instrument-load stall point | [Historical, passed] the earlier run stopped before IDMA. Current runtime passes RECALIBRATE, SEEK, READ DATA and terminal count and reaches `FILE LOADED`. |
| sample-data destination | [Verified runtime] the tested instrument payload reaches low RAM and is consumed through the ES5506 bank-1 low-memory mapping; exact general sample-object ownership and physical sound-memory topology remain [OPEN]. |
| instrument metadata/root structure | [OPEN] top-level loaded instrument root is not localized; ROM runtime voice records consume instrument/sample object pointers. |
| "loaded instrument becomes playable" boundary | [Verified runtime] after `FILE LOADED`, `BTN_02` selects instrument slot 1; panel/MIDI note stimulus allocates and programs ES5506 voices and produces dry audio. The general top-level instrument root remains [OPEN]. |
| ES5506 programming at load vs note-on | [Verified runtime] for the tested instrument, voice-specific CR/START/END/ACCUM programming occurs after instrument selection and note stimulus. |

## Storage vs audio interrupts

Keep these interrupt domains separate:

| vector/source | firmware status | subsystem boundary |
|---|---|---|
| vector `$4B` | [Verified firmware] MC68302 IDMA/SIB completion dispatcher at `$F01B1A/$F01B4E`. | Storage/data movement completion, not ES5506 voice service. |
| vector `$51` | [Verified firmware] shared FDC/SCSI storage completion dispatcher at `$F114B6/$F114E2`. | Storage device completion. Physical IRQ1 routing remains [OPEN]. |
| vector `$47` / PB9 | [Verified firmware] handler `$F8D072` reads ES5506-like PAGE/IRQV, maps `IRQV & $1F` to `$8000 + voice*$D8`, and calls the voice callback at `+$26`; physical source remains [OPEN]. | Audio voice-event service candidate. Physical ES5506 `IRQB` routing remains [OPEN]. |

[DISPROVEN] vector `$51` should not be described as "FDC interrupt" only. It is
the firmware-side shared storage/device completion entry and has both FDC and
SCSI preludes before the common `$0402` jump.

[OPEN] Whether storage devices electrically share IRQ1, how the line is
acknowledged, and whether PAL/GAL glue participates are not verified.

## Remaining open boundaries

- [OPEN] Physical storage IRQ1 routing.
- [OPEN] PAL/GAL glue for storage completion.
- [OPEN] Electrical interrupt sharing, polarity, acknowledge timing and line
  clearing for FDC/SCSI completion.
- [Verified runtime] Dynamic per-voice sample banking ($FF7F00-$FF7FFF) translates
  ES5506 wavetable reads to DRAM across both low memory and sample RAM; exact
  general sample-object ownership and physical board IC receiver on the 4-layer
  PCB remain [OPEN] at the documentation frontier.
- [OPEN] Top-level loaded instrument metadata root and producer.
- [Verified firmware] Runtime voice record to ES5506 programming path; see
  `instrument-to-otto-runtime.md` and the object ownership model in
  `runtime-object-model.md`.
- [Verified runtime] Tested panel/MIDI note paths reach voice allocation after
  instrument selection; the real external keybed protocol remains [OPEN].
- [OPEN] Physical ES5506 `IRQB` to PB9 connection.
- [OPEN] ES5510 execution, external RAM and audio routing.
- [OPEN] Exact Super-GLU/ES5701 variant and wiring on the ASR-10 board.

## Practical implementation boundary

Future implementation should preserve the architectural split:

- Storage completion belongs to the MC68302/SIB plus FDC/SCSI model and the
  `$0402` firmware continuation flow.
- ES5701/Super-GLU-class logic belongs to audio host/sound-memory bus glue,
  clocking and arbitration once board evidence requires it.
- ES5506 voice/sample playback and ES5510 DSP execution are downstream audio
  runtime problems, not evidence for storage IRQ wiring.
