# ASR-10 current status

Current truth for the ASR-10 MAME bring-up. This file is deliberately short:
verified reference facts belong in `reference/`, reproducible analysis output belongs in
`static/`, and experiment history belongs in `investigations/` or `archive/`.

Repo boundary: the separate MC68302 project is not part of this MAME implementation.
Its classes, architecture and monitor infrastructure (`TraceRecorder`, `Mc68302Bus`,
`Mc68302SystemIntegration`, Execution Monitor, Kotlin/Moira-specific design) are not
applicable concepts for ASR-10 MAME documentation. Verified ROM/OS observations from
this repository may later be used as evidence in the generic MC68302 project, but
architecture, class names and implementation do not move in either direction.
`docs/mc68302/` is source material in this tree; `src/devices/machine/mc68302.*`
defines the implementation.

Panel receive terminology:

| term | path |
|---|---|
| ROM receive path | `$F89CCA` (SRB) -> `$F89CEA` (RHRB) -> `$F82484` lookup |
| runtime receive path | `$FFB0BC` (SRB) -> `$FFB0D4` (RHRB) -> `jmp` via `$0003C0` |

## Current handoff

The current ASR-10 architecture checkpoint is summarized in
`reference/architecture-handoff.md`. It is the starting point for the next
implementation session: service-kernel model, observed instrument-load request,
storage completion boundary, implementation readiness, and the critical OPEN
items are consolidated there.

## Works

- Current `asr10booth` boots `floppies/asr10booth/V350.img` with no `ASR10_*`
  environment variables to:

  ```text
  ENSONIQ ASR-10 -> LOADING SYSTEM -> FILE 1  TUTORIAL BNK
  ```

  The failed intermediate Disk Ready trial wired DUART IP0 from floppy
  loaded + motor-active state and stopped in repeated `PLEASE INSERT DISK`.
  Current code instead drives DUART IP0 from the uPD72069 index callback; the
  old PC-dependent `$FC4809` bit-4 stub was not reintroduced.

- The acceptance test is `docs/asr10/regression-test.sh`. Tag
  `asr10-file1-2026-08-03` marks the first documented milestone.
- Current boot uses real MAME devices for the DUART host path (`mc68681`), ES5506 host
  registers, ES5510 host registers, and the FDC path used by this boot.
- Current boot to `FILE 1` still depends on synthetic driver behavior, but the
  `$FC4809` base-value stub and PC-specific bit-4 stub have been removed.
  [Verified dynamic] The isolated `$FC4809` base value `$00` was obsolete for
  V3.50 boot-to-FILE1.
- [Verified dynamic] DUART IP0 is ASR-10 floppy INDEX, driven from uPD72069
  `idx_wr_callback()` in parallel with the FDC's own internal index handling.
  V3.50 boots to `FILE 1  TUTORIAL BNK` without the old PC-dependent
  `$FC4809` bit-4 stub. Negative control without mounted disk fails with
  `$049D=$05` from `$FB7C9E`, confirming that rotation/index pulses are what the
  IPCR change test observes. [DISPROVEN] IP0 = floppy loaded && motor active;
  that predicate stayed in `PLEASE INSERT DISK` with `$049D=$05`.
- [OPEN] `ASR10_MISSING_FDC_RATE_SOURCE`: where ASR-10 sets the uPD72069 data
  transfer rate to 500 kbit/s for the boot read path. The current workaround
  still forces `set_rate(500000)` for aux command `$88`; retested after the
  IP0/index fix, disabling it still stalls at `PLEASE INSERT DISK` with
  repeated `$049D=$0D`. The failing transfer is reached after aux `$88`, aux
  `$F3`, and Read Data command `$46 ...`; MAME decodes `$88` as 250 kbit/s,
  while `$98`/`$C8` would select 500 kbit/s. No other locally inspectable
  firmware in this checkout was shown to exercise that 72069 rate table:
  `mpc2000`, `mpc3000`, and `s3000` instantiate `UPD72069`, but their ROMs are
  not present under local `roms/`.
- [Verified static] V3.50 uses `$0402` as a general async device/storage-I/O
  continuation pointer. Two completion dispatchers are identified:
  vector `$4B` -> `$FFFF87E8` -> `$87E8.w` -> `$F01B1A` for MC68302
  IDMA/SIB completion, and vector `$51` -> `$FFFF87CE` -> `$87CE.w` ->
  `$F114B6` for shared storage/device completion. The `$51` dispatcher has
  verified FDC and SCSI status/acknowledge branches before `jmp [$0402]`.
  → `reference/storage-completion-dispatch.md`,
  `reference/scsi-operation-example.md`
- [Verified device] MAME's `upd72069_device`/`upd765_family_device`
  completes `RECALIBRATE 07 00` by asserting its `intrq_wr_callback()`:
  track-0 completion runs `command_end(..., false)`, sets `irq` and
  `st0_filled`, and a later `SENSE INTERRUPT STATUS 08` returns `ST0=$20`,
  `PCN=$00` for successful drive-0 recalibrate. This is INTRQ, not DRQ;
  SIS/result reads clear the device-side IRQ/result state.
- [Verified firmware] The FDC completion state machine uses vector `$51` after
  RECALIBRATE: `$0402 <- $BA5E`, `$04AD <- 0`, command `07 00`, vector
  `$51`, `$F114B6`, FDC status/SENSE path `$FB7E8E`, SIS `08`, accepts
  `ST0 & $E0 == $20`, sets `$049D <- 0`, `jmp [$0402]`, then `$BA5E`
  installs `$B1A4` and starts SEEK `0F 00 01`.
- [Verified static] One SCSI completion state machine uses the same vector
  `$51` dispatcher: `$0402 <- $B1A4`, `$FC5001 <- $18`,
  `$FC5003 <- $00`, return, vector `$51`, `$F114B6`, SCSI status branch
  `$FBB370`, SCSI status register `$17` read, then `jmp [$0402]`.
- [OPEN] Physical storage completion interrupt path. Static code reading
  found no `m_fdc->intrq_wr_callback()` and no `m_fdc->drq_wr_callback()`
  in `asr10_boot.cpp`; the only FDC signal currently wired out is index
  into DUART IP0. The firmware-side vector `$51` completion entry is
  identified for both FDC and SCSI, but physical IRQ routing, PAL/GAL glue,
  electrical interrupt sharing, interrupt polarity, acknowledge timing, and
  board-level line clearing remain open.
- [OPEN] FDC-/instrumentinläsningsspåret är avslutat i nuvarande omfattning;
  se `investigations/instrument-load-v350.md`. Blockerare:
  Den observerade `LOADING JM DIGI SYN`-vägen konstruerar en konkret
  service-node-request vid `$FFA882-$FFA8AA`, använder trap `#12` immediate
  path till storage-target `$14DA`, och accepterar payloaden:
  node `+2/+3=$03/$02`, node `+4=$0002B600`, `$0466=$1504`,
  `$046A=$0002B600`. `$14E0/$14E2` förblir noll i detta runtimefall; det är
  inte en vanlig trap `#9` enqueue till den kö som `$F8822C` dränerar.
  `$043E` förblir `$00000000`, och class `$06/$0D` promotion-mekanismerna
  som kan skriva `$043E <- $046A` är endast statiskt identifierade. Runtime
  når RECALIBRATE `07 00` men inte RECALIBRATE-completion, SEEK, READ DATA
  eller IDMA-start. Detta är inte längre formulerat som att uPD72069 saknar
  RECALIBRATE-completion: device-sidan producerar INTRQ-completion. Det
  saknade emulerade kontraktet ligger mellan storage completion och
  firmware-ingången `$51`, utan att anta vilken fysisk board-source som driver
  MC68302 external IRQ1.
- [Verified runtime/static] Instrument-load-requestens class/subtype är
  `$049A.b=$03` och `$049B.b=$02` efter storage entry. `$0302` ska inte
  beskrivas som ett enda enkelt storage-opcode; `$049A` används som
  high-level dispatch byte och `$049B` som separat subtype/tag byte.
  -> `reference/runtime-service-model.md`, `reference/runtime-object-model.md`
- [Verified static/runtime] Om class `$03/$02` senare når common storage exit
  returneras samma node från `$0466`. Den observerade noden har
  `node +2=$0302`, alltså positivt word. Statiskt väljer common exit då
  target `$23F6`; den tidigare `$2438`-returmodellen gäller endast negativ
  node. `$23F6` är scheduler slot 0, inte verifierad instrument-owner.
  Concrete post-completion consumer är [OPEN] eftersom runtime ännu inte når
  RECALIBRATE completion.
- [Verified static] Class `$03` har en verifierad statisk väg mot verklig
  dataöverföring: `$B64C -> $FB7F9E -> $FBA5A2 -> $FB9C5E -> $FB9FE2 ->
  $FB84DA -> $FB85C0 -> IDMA setup -> $FB8672 -> FDC READ DATA $46`.
  IDMA använder source `$FFFC5803`, destination `$040E`, count från
  transfer/sector-state, och MC68302 IDMA-register `$FC6802`, `$FC6804`,
  `$FC6808`, `$FC680C` och `$FC6810`. Runtime har ännu inte nått detta.
- [Verified dynamic] ES5506 PAR now reads through the ASR-10 panel analog path
  rather than a fixed `$0200` constant. V3.50 still boots to
  `FILE 1  TUTORIAL BNK`; observed PAR reads returned raw `$0200` from channel 6
  (`left_aligned=$8000`), a centered 10-bit value.
- [Verified silicon spec] The Ensoniq audio specs are now separated from ASR-10
  board wiring in `reference/audio-storage-architecture.md`: ES5701/Super-GLU is
  audio/sound-memory glue, ES5506/OTTO is the voice/sample engine, and
  ES5510/ESP is the effects DSP host/execution device. Storage completion still
  remains separate: vector `$4B` = IDMA/SIB completion, vector `$51` = shared
  FDC/SCSI completion, and vector `$47`/PB9 is only a likely audio-event
  candidate until physical ES5506 `IRQB` routing is verified.
- [Verified static] The first localized audio-runtime boundary is the ROM
  voice table at `$8000`: 32 entries with stride `$D8`, per-voice callback at
  `+$26`, instrument/sample object pointer at `+$1E`, sample-address base at
  `+$22`, and ES5506 PAGE/register programming through `$FC2001`. PB9/vector
  `$47` reads ES5506-like `IRQV`, maps the voice number to `$8000+voice*$D8`,
  and calls the per-voice callback. The loaded instrument root and sample RAM
  producer remain [OPEN]. -> `reference/instrument-to-otto-runtime.md`
- [Verified static] The `$8000` table is now classified as a firmware-owned
  ROM voice-manager object, not an ES5506-owned data structure. The manager has
  verified init, allocation/list, preparation, callback and release/reset paths
  through `$F8C2xx-$F8E4xx` and binding slots `$8E38/$8E3E/$8E44/$8E50/$8E6E`.
  Producer-side refinement: `$F8CA38` consumes `A4+$1E`, `$F8C492` uses
  `$14AC[D5]`, `$F8C412` consumes `$0D18`, sample-object writer slots
  `$8FE4/$8FF0/$8FFC/$9008/$9014` produce `A2+$F0/$F8/$100/$108`, and
  `$F8DFAA` is one verified fixed-control producer for `A4+$22`. Direct or
  indirect producer of normal voice `A4+$1E`, `$14AC` ownership, normal
  `A4+$22` producer, sample allocator semantics and sample RAM writer remain
  [OPEN].
  -> `reference/runtime-object-model.md`
- Category A/B/C cleanup status: `src/mame/ensoniq/asr10_boot.cpp` was reduced
  from 3580 to 952 lines by the structural cleanup. No runtime experiment,
  trace, profile, summary or getenv-controlled instrumentation remains in the
  driver. No experimental fabrication remains in the driver except
  `ASR10_MISSING_FDC_RATE_SOURCE`. The normal boot path requires no
  experiment flags. Build, regression, normal boot and panel button navigation
  were rechecked after the cleanup.
- Channel B panel RX is owned by `mc68681_device`.
- V3.50 `FILE 1  TUTORIAL BNK` is a working runtime state under the Step 0 PC profile:
  20 s sampling showed 28315 samples, 385 distinct PCs and no `stop` samples. The
  calibrated loading window starts at first FDC access and is disk-dominated; the FILE 1
  window is scheduler-dominated. Runtime receive path activity in FILE 1 is a short
  entry burst, not continuous polling.

## What the static analysis established

A full static analysis of `asr10.bin`, `V161.img` and `V350.img` produced an
architectural model that did not exist before. Summary only — details in `reference/`.

- **ROM is not a bootloader that hands off and exits.** It remains a permanent service
  library, hardware layer and scheduler. The OS image is not a standalone executable:
  42 of its 48 real vectors point into ROM, and its reset PC field is zero.
  → `reference/rom-os-abi.md`
- **A 723-slot binding table** at RAM `$00801E-$009FF6` is the clearest identified ABI
  structure. ROM calls 342 of those slots from 1254 call sites. 567 slots have identical
  targets in V1.61 and V3.50; **74 move from a ROM target to an OS target**, which is
  how Ensoniq patched ROM routines without replacing ROM.
  → `reference/rom-os-abi.md`, `static/os-binding-table.csv`
- **The OS loads in at least two segments**, `RAM = OS_offset + 0xA00` below `$008000`
  and `RAM = OS_offset - 0x5A00` above. The boundary is bounded to
  `($009FF6, $00BEF2]` but not fixed. → `reference/os-image-layout.md`
- **The reset sequence executes out of DPRAM.** ROM copies 14 bytes to `$FC6200` and
  jumps there so it can reprogram BR0, which moves ROM from `$000000` to `$F80000`.
  → `reference/boot-sequence.md`, `reference/memory-map.md`
- **Chip selects decoded from ROM's own BR/OR writes**, including direction and DTACK.
  CS1 (`$FF6000-$FF7FFF`) is write-selected with external DTACK and remains functionally
  unidentified. → `reference/memory-map.md`
- **MC68302 mapped per block** against the vendor manual: interrupt controller bitmap,
  PIO, Timer 2 configuration and its version difference, CP reset, ENTER HUNT MODE for
  SCC1/SCC2, SIMODE, SCON/SCM, LRCLK-phased receiver start, complete SCC1/SCC2 interrupt
  handlers with correct EOI. → `reference/mc68302-status.md`
- **Current MC68302 implementation has no internal interrupt-source model.**
  [Verified] IPR/IMR/ISR, SCC/SMC parameter RAM, IDMA, Port A, timers, watchdog and
  SCC/SMC/SCP are `known_unimplemented` shadow storage in `mc68302.cpp`.
  [Verified] The only working interrupt path at HEAD `47318563942` is external IRQ6 via
  `irq6_ack_vector()`. [OPEN] W1C semantics for IPR/ISR remain a hardware-model question
  until the interruptcontroller block is implemented. → `reference/mc68302-status.md`
- **DUART panel path matches ROM hardware access.** [Verified] ROM-accesser till
  `$FC4813` (SRB, RxRDY-poll) och `$FC4817` (RHRB) bekräftar att den nuvarande
  DUART-panelmodellen motsvarar den hårdvaruväg firmwaren faktiskt använder.
  Registerlayout, bas, udda adressering, stride och handskakning stämmer samtliga.
  Kodsymbolen heter fortfarande `duart_panel_asr_candidate_r/w` av historiska skäl.
  Namnbytet är en separat kodändring.
- **Panel button frames are now proven dynamically.** [Verified] Knappframe
  `($80|$0A, $00)` flyttar `FILE 1  TUTORIAL BNK` till `FILE 2  JM DIGI SYN`
  i V3.50 via runtime receive path. [Likely] `$0A` är nästa fil.
- **Panel channel is functional through the ASR panel device.** [Verified]
  Panelkanalen är halv duplex fråga/svar. Host skriver en byte till THRB,
  pollar SRB tills RxRDY, skriver nästa. Intervall ~361 us = två teckentider
  vid 62500 baud 8N2 plus latens. [Verified] 62500 baud är mätt ur firmwarens
  egen kadens, inte ärvt från EPS-16. IP5 = 1 MHz vid CSRB selector `$E`.
  [Verified] Panelen fungerar i båda riktningar genom `ASR10PANEL`: 21/21 byte
  renderas, `set_button($0A)` ger `FILE 1 -> FILE 2`.
- **A control-flow database** of 5243 call-site-level edges with normalised addresses,
  evidence level and execution status. → `static/call-graph-edges.csv`,
  `reference/call-graph.md`

## Does not work

- Audio output, sampling, sequencer behaviour, and complete ES5506/ES5510 sound
  integration are not working end-to-end.
- DUART channel A RX is not wired to a real external source.
- ES5506 PAR has a real panel-analog route, but the wider ADC channel identity
  and audio-side effects are not fully verified.

## Next implementation target

Implement the smallest generic MC68302 external IRQ1/vector-`$51` path needed
to let the already verified firmware completion chain execute.

Generic MC68302 requirements:

- external IRQ1 input/state
- CPU level-1 assertion
- level-1 IACK
- `GIMR`/`IV1`-derived vector `$51`
- clean source assertion/deassertion contract

ASR-10 board-side policy:

- connect storage completion policy to MC68302 external IRQ1
- keep physical storage IRQ wiring [OPEN] / explicit board policy
- do not encode FDC-specific firmware knowledge into the generic MC68302 model

After implementation, repeat the `LOADING JM DIGI SYN` experiment and observe:
RECALIBRATE completion, vector `$51`, SIS, SEEK, the next vector `$51`,
class `$03`, READ DATA `$46`, IDMA register programming, vector `$4B`, later
vector `$51`, common exit, actual `$23F6` resume state/consumer, next request
class, and whether payload `$02B600` survives to `$043E`.

## Open questions

- **Critical next:** physical/board policy for storage IRQ1, runtime validation
  through first vector `$51`, and the concrete `$23F6` post-completion consumer.
- **Next architecture:** next request class after `$03`, `$043E` activation,
  storage -> sample/instrument bridge.
- **Later:** exact scheduler full-context semantics, semantic names of descriptor
  fields, UI-visible/voice-ready load boundary, physical IRQ glue/wiring.
- **CS1** `$FF6000-$FF7FFF`: enabled, write-selected, external DTACK, no function-code
  comparison, function unknown. No identified direct or immediate-base references.
- **What SCC1/SCC2 carry.** The firmware chain is documented end to end. The physical
  sender, the data semantics, and what `$00643C` produces are open. LRCLK phasing makes
  the audio path likely but unproven.
- **The exact ROM→OS edge**, and when the vector table is installed at `$000000`.
- **The segment boundary** and what the ~0x6400-byte gap in the OS file represents.
- **DPRAM contents**: SCC descriptors, buffer pointers, CP state, dynamically installed
  jump-table targets. One structured dump at chosen points would settle much of this.
- **Timer 2's consumer**: which V3.50 routine reads TCN2 and why.
- **PB9, PB10, PB11 consumers and physical sources.** ROM unmasks PB11/PB10/PB9 and the
  PB10/PB11 handlers are decoded, but the static absolute-search pass found no
  dekrementerare for `$0C3A/$0C3B` and no consumer for `$0C36/$0C37` within that method.
- **`$F95EAA`** — 1 call in V1.61, 33 in V3.50, unidentified. `$F97662` is no
  longer in this group: it is a `$03C8`-gated low-level host-port verified
  write/read service; see `investigations/panel-button-sweep-v350.md`.
- PAR value, ADC channel identity, PB3 LRCLK board frequency, channel A wiring
  (unchanged from previous status).

## Disproved hypotheses

Previous entries stand. Added by the static analysis:

- **CS1 is the SCSI option.** The SCSI controller is at `$FC5001`/`$FC5003` in **CS3**:
  ROM `$FBB5C0` loads both as pointers, and ROM and both OS versions write
  `#$18` (WD33C93 Command) followed by `#$00` (Reset).
- **The OS image is loaded flat at one base.** At least two segment rules apply.
- **The OS image contains a start address.** Vector 1 (PC) is `$00000000` in both
  versions; no soft reset from the image is possible.
- **`$7033` is written to DSR.** It is written to SCM. DSR has no identified absolute
  references in ROM or either OS version.
- **`$FC6816` is a service/in-service latch.** It is IMR. `$2400` = SCC1 + SCC2.
  Clearing it masks the interrupt; it acknowledges nothing. EOI goes to ISR
  (`$FC6818`), which the OS handlers already do correctly.
- **Observed class `$03/$02` returns to `$2438`.** [DISPROVEN runtime/static]
  The observed node has positive `node +2=$0302`; common exit therefore selects
  `$23F6`. `$2438` is the negative-node return target.

## Current documents

- `reference/rom-os-abi.md` — ROM/OS architecture and the binding table. **Read first.**
- `reference/call-graph.md` — control-flow model, established subgraphs, CSV schema.
- `reference/memory-map.md` — chip selects, DPRAM, SIB registers, the mirror hypothesis,
  the CS1 dossier.
- `reference/mc68302-status.md` — MC68302 per block, with evidence levels.
- `reference/os-image-layout.md` — disk format and the segment rules.
- `reference/vector-map.md` — the five vector categories kept apart.
- `reference/storage-completion-dispatch.md` — `$0402`, vector `$4B`,
  vector `$51`, and the FDC/SCSI async completion dispatchers.
- `reference/audio-storage-architecture.md` — Ensoniq ES5701/ES5506/ES5510
  chip-spec boundaries and how they meet the ASR-10 storage/load model.
- `reference/instrument-to-otto-runtime.md` — localized runtime voice table,
  ES5506 helper map, PB9/IRQV service and remaining instrument-root gaps.
- `reference/runtime-object-model.md` — firmware object ownership for storage,
  sample/instrument objects, the ROM voice manager, OTTO and remaining gaps.
- `reference/runtime-service-model.md` — dispatcher queue and service fields, historical
  V1.61 observations.
- `reference/architecture-handoff.md` — current architecture checkpoint and next
  implementation target.
- `reference/boot-runtime-timeline.md` — dynamic reset-to-runtime timeline, IRQ6 source
  distribution and ROM/RAM execution responsibility.
- `reference/methods-static-analysis.md` — how the results were produced, and the
  method's blind spots.
- `reference/boot-sequence.md`, `reference/subroutine-index.md`,
  `reference/os-code-extraction.md`, `reference/hardware-map.md` — as before, updated.
- `static/README.md` — what the raw material is, how it was generated, what it does not
  prove.
