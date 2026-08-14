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
- [Verified static] The FDC completion state machine uses vector `$51` after
  RECALIBRATE: `$0402 <- $BA5E`, command `07 00`, vector `$51`,
  `$F114B6`, FDC status/SENSE path `$FB7E8E`, `jmp [$0402]`, then `$BA5E`
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
  Instrumentinläsningen väljer FDC DMA-/avbrottsvägen, men ASR-10-modellen har
  ingen kopplad FDC intrq/drq-väg och ingen implementerad MC68302-intern
  interruptcontroller som kan leverera fullbordanssignalen.
- [Verified dynamic] ES5506 PAR now reads through the ASR-10 panel analog path
  rather than a fixed `$0200` constant. V3.50 still boots to
  `FILE 1  TUTORIAL BNK`; observed PAR reads returned raw `$0200` from channel 6
  (`left_aligned=$8000`), a centered 10-bit value.
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

## Next phase: dynamic verification

The static phase has produced what it usefully can. Further static work should be
targeted at a known runtime PC, vector, hot binding slot or observed register access —
not broad pattern search. Prompts for E1-E4 are in `static/prompts-E1-E4.md`.

1. **E4 — first real CS1 write.** Write-tap `$FF6000-$FF7FFF` logging address, width,
   value, PC and run phase, across boot, file browsing, instrument load, sampling,
   effect load, hardware test and option detection.
2. **E1 — the ROM→OS handover and vector installation.** ROM contains no `jsr`/`jmp`
   with a 32-bit absolute RAM target, so the transfer is a binding slot, a
   register-indirect jump, or an `rts` to a stacked address.
3. **E2 — `$FFxxxx ↔ $00xxxx` mirror.** [OPEN] The V3.50 read tap observed 566229 data
   reads in `$FF8000-$FFFFFF` and a sampled mismatch (`$FF8D44 = $F9`, `$008D44 = $00`),
   but it did not answer the hardware mirror question: opcode fetches were not visible
   to Lua, the original V1.61 `$00BF0E` test case was not run, and the high window's
   decode/open-bus status in current `mem_map` was not established. The 1404 generated
   `mapping_basis=mirror-hypothesis` edges remain unchanged.
4. **Deterministic file-browse test.** One `DOWN` from `FILE 1  TUTORIAL BNK`, logging
   panel byte → DUART handler → dispatcher → binding slot → OS routine → file index →
   panel output. This is blocked at HEAD without implementation changes: the ASR-10
   driver has no input ports, and the existing panel harness only injects panel ACK/status
   bytes, not user key events.
5. **Minimal truthful SCSI model.** AM33C93A at `$FC5001`/`$FC5003`: reset accepted,
   stable status, option detection passes, no targets, commands terminate correctly.
   No fabricated disks.

## Open questions

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
- `reference/runtime-service-model.md` — dispatcher queue and service fields, historical
  V1.61 observations.
- `reference/boot-runtime-timeline.md` — dynamic reset-to-runtime timeline, IRQ6 source
  distribution and ROM/RAM execution responsibility.
- `reference/methods-static-analysis.md` — how the results were produced, and the
  method's blind spots.
- `reference/boot-sequence.md`, `reference/subroutine-index.md`,
  `reference/os-code-extraction.md`, `reference/hardware-map.md` — as before, updated.
- `static/README.md` — what the raw material is, how it was generated, what it does not
  prove.
