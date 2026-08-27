# ASR-10 current status

Current truth and cumulative revision journal for the ASR-10 MAME bring-up.
Start with `reference/handoff-2026-08-23.md` for the five-minute freeze snapshot.
Verified reference facts belong in `reference/`, reproducible analysis output belongs
in `static/`, and experiment history belongs in `investigations/` or `archive/`.
Older paragraphs below preserve provenance; an explicit later update or the freeze
handoff takes precedence over a historical runtime boundary.

**Instrument warning, general form:** two *independent* ways for a Lua tap
to die silently (no error, just no further callbacks) are now on record:
`mc68302_device::install_internal_window()`
(`src/devices/machine/mc68302.cpp`) re-issues `install_readwrite_handler()`
for `$FC6000-$FC6FFF` on every BAR write, dropping any tap installed on
that range beforehand (`reference/methods-static-analysis.md` §8.5); and an
`install_read_tap()`/`install_write_tap()` return value that isn't kept in
a persisted variable can be silently reclaimed by the Lua garbage collector
almost immediately, regardless of address (§8.6). Two independent failure
modes in the same investigation is a pattern, not a coincidence — treat
**every** prior zero-result measurement from a Lua tap in this tree as
unmeasured, not confirmed, until it has a witness proving the tap was alive
for the *whole* measurement window, not just at install time. This applies
to any address, not only `$FC6000-$FC6FFF`. See
`reference/methods-static-analysis.md` §8.7 for the general rule, and
`investigations/irq1-imr-unmask-probe.md` /
`investigations/irq1-vector-and-sr-probe.md` for both worked examples and
their fixes.

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

The current resume point is this document together with
`investigations/display-protocol-state-machine-v350.md`,
`investigations/transport-ab-test-play-stop-continue.md` and
`investigations/slot5-pc-correlation-and-atrk-slot-table.md`.
`reference/handoff-2026-08-23.md` is the five-minute freeze snapshot and now
contains an explicit post-freeze correction; its dated resumption notes are
provenance, not the current work priority. `reference/architecture-handoff.md`
preserves the earlier pre-IDMA architecture checkpoint and remains useful for
the service-kernel model, but its runtime stall and implementation target are
historical and passed.

## Current display protocol boundary

- **[Verified mechanism, implemented]** On the reproducible
  `EDIT SEQUENCE -> TEMPO` path, the full redraw marks the selected value field
  at columns 6-8 with `$62 $60 $03 "90 " $72`. Up then emits exactly
  `$63 "91 " $72`. `$62` establishes the selected-field anchor and `$63`
  now restores it and its underline for a partial rewrite. Runtime acceptance
  visibly round-trips `90 -> 91 -> 90` without trailing text or LOOP corruption;
  REC SRC independently uses the same mechanism at column 17.
- **[Verified implementation boundary]** `asr10panel_device` now owns the ASR
  cursor, current attribute, selected-field anchor/attribute and pending operand
  state. The stale linear `m_text_chars` shadow was removed; the ASR path sends
  explicit position/glyph/underline operations to the generic 1x22 renderer.
  VOLUME `$14 "98"`, field underline and `$77-$7B` retention remain verified.
- The saved V3.50 fixture and contract replay live in
  `lua/fixtures/display_tempo_v350.lua` and
  `lua/display_protocol_stream_replay.lua`; `lua/display_field_rewrite.lua`
  verifies the same transition through firmware and the actual device path.
- The established suite is now 16 tests and 17 `PASS` lines, including the new
  firmware/device field-rewrite acceptance, exit 0.
- **[Verified runtime, bounded stability]** Representative real V3.50 workflows
  now pass across TEMPO (including BAR round-trip and repeated rewrites), LOAD
  file browsing, Command/Master Tune, an Edit Instrument layer page, REC SRC,
  VOLUME and retained annunciator traffic. No trailing glyphs, cursor drift,
  stale screen state or underline leakage was observed. The decoder is therefore
  workable/stable for navigation in these workflows, not fully understood;
  see `investigations/display-stability-workflows-v350.md`.
- **[DISPROVEN address label]** Channel-B THRB is CPU byte address `$FC4817`,
  not the older display reference's `$FC480D`. The old value came from
  mis-converting Lua's aligned 16-bit tap bucket. The captured traffic itself
  remains valid.
- **[OPEN, corrected]** `$74-$76` are one-operand panel-control/output traffic,
  but not a proven nibble-only animation family: `$74 $40`, `$75 $00/$08` and
  `$76 $00` occur. `$67`, high transition controls, output-bit identities and
  blink remain open.

## Current transport and sequencer boundary

- **[Verified runtime] Transport and sequencer execution work.** With a sequence
  loaded, raw panel code `$1D` starts Play. During active playback `$17` stops
  the observed ES5506 register activity and a second `$17` continues it with
  freshly programmed voices. `$17` is genuinely context-dependent: the earlier
  `CREATE NEW SEQUENCE` observation belongs to a different, deep Command
  context, while bare `$17` is inert without an active sequence. Playback
  produces audible sound.
- **[OPEN, highest-priority functional problem] Musical/audio-correct sequencer
  playback is not verified.** `LOAD/INST -> TUTORIAL BNK`, then `LOAD/SEQ ->
  TUTORIAL SEQ`, then Play produces audible playback that works substantially
  better, but has not been shown to reproduce the original music correctly.
  Fresh boot -> `LOAD/INST -> ATRK TUT BNK` also starts sequencer activity but
  produces mainly clicks or otherwise clearly incorrect audio. The open boundary
  is the sequencer -> track/instrument -> voice -> ES5506 result, not transport
  start/stop/continue.
- **[DISPROVEN as an execution entry]** `$007C7C` was a 68000 prefetch/read-tap
  false positive. The underlying measured note-to-voice connection remains:
  `$007830` and `$007CA8` are PC-correlated executing addresses in the measured
  MIDI-note case. `$007E24` executes frequently, but its semantics remain
  `[OPEN]`.
- **[DISPROVEN as a loader-bug indicator]** ATRK's `$001098` population shape
  matches the bank file's three instruments plus bundled Song and the fixed
  eight-track control group. Its cycling pattern is not evidence of a loader
  defect.

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
- [DISPROVEN] Unconditional `FDC INTRQ -> MC68302 external IRQ1` board
  policy, as originally implemented (no ready-line fix). Implemented and
  regression-tested; it broke plain boot-to-FILE1 itself (`ERROR 129 -
  REBOOT`, a genuine 68000 Address Error — **corrected**: earlier entries
  in this project described `129` as firmware detecting/protesting
  something; it is a CPU-level exception, see
  `investigations/ready-line-artifact-probe.md`) during the OS's own
  polled boot-time disk load. Reverted. The chip-level vector fact
  (`mc68302_device::irq1_ack_vector()`, formula `0x40 | 0x11`) and the
  level-1 IACK dispatch branch are kept as inert, verified infrastructure.
  → `investigations/irq1-storage-completion-probe.md`
- [DISPROVEN] "MC68302 IMR gates external IRQ1" (the natural follow-up
  hypothesis after the wiring above broke boot). Measured zero writes to
  GIMR/IMR/ISR (`$FC6812/$FC6816/$FC6818`) between FILE 1 and the
  instrument-load stall, on a tap now witnessed live for the entire window
  (BAR, which governs whether the SIB window's handler gets silently
  reinstalled, was last written at t≈5.4s — over ten seconds before FILE 1
  and over twenty before the tap installed; see the instrument warning
  above). Supported, not independently proven, by
  `docs/mc68302/interrupt-source-map.md`'s narrow claim that the specific
  `IRQ1`/`EXRQ` mechanism has no IMR bit — PB8-11 show that "external" does
  not generally imply "no IMR bit". No interrupt-controller work is
  motivated by this hypothesis.
  → `investigations/irq1-imr-unmask-probe.md`
- [Verified dynamic] **Root cause of the naive wiring's `ERROR 129 -
  REBOOT` failure, measured to completion.** Neither a wrong vector nor a
  timing/masking problem, and not a missing-IDMA problem — the interrupt is
  delivered correctly (vector `$51`, target `$FFFF87CE`) and the handler
  runs correctly through `SENSE INTERRUPT STATUS`
  (`ST0=$C8`: a spontaneous drive-ready-line-change status, not tied to any
  RECALIBRATE/SEEK/READ-DATA completion). It then crashes with a genuine
  68000 **Address Error exception** (vector 3, confirmed by tapping the
  CPU's own internal exception-vector-table reads): the dispatcher's final
  `movea.l $0402.w,A0 / jmp (A0)` reads `$0402=$00000000`, because the
  boot's own polled FDC code path never installs a `$0402` continuation —
  only the instrument-load path's FDC RECALIBRATE issuer does. `jmp` to
  `$000000` executes the reset vector's data as code and odd-address-faults
  within microseconds, exactly matching `archive/troubleshoot.md`'s
  documented meaning of error `129` ("odd address error"). Of the
  documented six-step completion chain, only steps 1-2 are reached in
  *this* crashed run; SEEK, READ DATA, IDMA programming, and vector `$4B`
  are never reached here. **Update:** this made "IDMA is the real blocker"
  look superseded rather than merely out of reach — that reading did not
  survive `investigations/ready-line-artifact-probe.md`, which found the
  interrupt itself was a ready-line artifact (see below), and that fixing
  it lets the chain run correctly all the way to a real IDMA-shaped
  blocker. → `investigations/irq1-handler-chain-probe.md`
  (supersedes the SR-mask framing in `irq1-vector-and-sr-probe.md`, which
  remains useful for the vector-delivery and tap-lifetime findings but not
  for the crash's actual cause)
- [Historical boundary, superseded] FDC-/instrumentinläsningsspåret var vid denna
  checkpoint avslutat i dåvarande omfattning;
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
  **Uppdaterat:** kontraktet ar nu implementerat och landat
  (`m_fdc->set_ready_line_connected(false)` + IRQ1-koppling,
  `investigations/ready-line-artifact-probe.md`). Runtime nar nu
  RECALIBRATE-completion, SEEK och READ DATA-utfardande korrekt genom
  vektor `$51`. Blockeraren ar IDMA
  (`investigations/idma-implementation-plan.md`), inte langre en
  saknad grind till `$51`. **Freeze-status:** även IDMA-gränsen är passerad:
  instrumentinläsningen omfattar 21 armeringar, 337 sektorer och 172,544 byte.
- [Verified runtime/static] Instrument-load-requestens class/subtype är
  `$049A.b=$03` och `$049B.b=$02` efter storage entry. `$0302` ska inte
  beskrivas som ett enda enkelt storage-opcode; `$049A` används som
  high-level dispatch byte och `$049B` som separat subtype/tag byte.
  -> `reference/runtime-service-model.md`, `reference/runtime-object-model.md`
- [Verified static/runtime] Om class `$03/$02` senare når common storage exit
  returneras samma node från `$0466`. Den observerade noden har
  `node +2=$0302`, alltså positivt word. Statiskt väljer common exit då
  target `$23F6`; den tidigare `$2438`-returmodellen gäller endast negativ
  node. `$23F6` ar scheduler slot 0, inte verifierad instrument-owner.
  Concrete post-completion consumer var [OPEN] har eftersom runtime da
  annu inte nadde RECALIBRATE completion. **Uppdaterat:** runtime nar nu
  RECALIBRATE-completion, SEEK och READ DATA-utfardande
  (`investigations/ready-line-artifact-probe.md`), men stannar vid en
  IDMA-overrun fore common storage exit -- konsumenten ar fortfarande
  [OPEN], nu av en annan anledning (IDMA saknas, inte att completion
  aldrig levereras). **Freeze-status:** denna runtimegräns är historisk;
  instrumentet blir valt och når note-to-voice samt ES5506-fetch.
- [Verified static] Class `$03` har en verifierad statisk väg mot verklig
  dataöverföring: `$B64C -> $FB7F9E -> $FBA5A2 -> $FB9C5E -> $FB9FE2 ->
  $FB84DA -> $FB85C0 -> IDMA setup -> $FB8672 -> FDC READ DATA $46`.
  IDMA använder source `$FFFC5803`, destination `$040E`, count från
  transfer/sector-state, och MC68302 IDMA-register `$FC6802`, `$FC6804`,
  `$FC6808`, `$FC680C` och `$FC6810`. Påståendet att runtime ännu inte nått
  vägen är historiskt; den senare 172,544-byte-mätningen verifierar den.
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
  and calls the per-voice callback. Den ursprungliga gränsen där loaded root och
  sample-RAM-producer var `[OPEN]` är historisk; senare load/note-prober verifierar
  bank-1-backing och live sample-fetch. -> `reference/instrument-to-otto-runtime.md`
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
- **Historical MC68302 implementation boundary.** At HEAD `47318563942`, the
  internal source model was shadow storage and external IRQ6 was the only working
  interrupt path. The current working tree now has the narrow SCC1/SCC2 receive and
  recording-IDMA `$37A1` behavior required by the measured ASR-10 path. This is not
  a general interrupt controller, SCC or IDMA implementation; unobserved modes and
  physical signal producers remain `[OPEN]`. → `reference/mc68302-status.md`
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

## Current incomplete areas

Priorities are deliberately ordered by present functional value:

1. **Functional:** the reproducible incorrect/clicking sequencer playback
   described above. Transport is no longer the blocker; musical/audio-correct
   playback across the sequencer-to-ES5506 chain is.
2. **UI/front panel:** the bounded ASR-specific `$62/$63` selected-field state
   machine is implemented and regression-locked. Representative LOAD, Command,
   Edit, REC SRC, VOLUME, TEMPO and annunciator workflows are workable/stable
   for navigation. Complete application coverage, OPEN command semantics,
   indicator identities and blink remain `[OPEN]`; the display protocol as a
   whole is not declared complete.
3. **Tooling:** regenerate the 68000 static graph with BSR, BRA/Bcc,
   PC-relative effective addresses, register-indirect JMP/JSR and
   TRAP/callback dispatch, with call/control/data edges kept separate. This is
   a separate tooling task, not a blocker ahead of the playback defect.
4. **Later/parked hardware:** ES5510 execution/effects, the ES5506 factor-two
   question, SCSI, PB9/PB10/PB11, physical keybed/controller and ADC routing,
   and expanded RAM configurations.

Mono and stereo sampling, sample-object creation and direct ES5506 playback
remain `[Verified runtime/current model]`. Physical ADC-to-SCC routing and the
digital-board connector pinout remain `[OPEN hardware]`, but are parked below
the functional and UI work above.

## Latest verified recording boundary

The current working-tree model accepts post-framing SCC RX bytes, completes the
descriptor, delivers vector `$4D`/`$4A`, copies big-endian signed 16-bit PCM by
IDMA into recording RAM and lets ES5506 consume that backing directly. Mono LEFT
continues through stop, root-key assignment and a complete WaveSample object.

**Superseded 2026-08-23/24.** L+R used to stop earlier in allocator
`$F8A44E` (the free-remainder header computed at `$7CE510` read back as
zero) because the model did not map that firmware-accessible heap
address. `investigations/memory-size-alias-fix.md` fixed the underlying
RAM/decode category (ROM's own memory-size probe now correctly concludes
`base=$600000, size=$200000`, folding `$7CE510` into real backing), and
L+R now reaches `WAITING` the same as mono and builds a real `UNNAMED WS`
object. `investigations/stereo-round-trip-verification.md` closes the
loop with data: byte-exact injection/readback for two genuinely
different channel patterns (source==destination copy 794/794 both
channels, own-pattern-fits/other-pattern-doesn't cross-check), and an
audible-output comparison against a mono control (mono: L/R channels
bit-identical in the WAV capture; stereo: L/R channels statistically
uncorrelated, different peak/RMS/onset). Historical `$7CE510` context:
`investigations/record-completion-analysis.md`,
`investigations/record-stereo-allocator-analysis.md` and
`investigations/stereo-ram-decode-analysis.md`.

## Completed storage implementation sequence

The section below preserves the implementation progression that removed the old
RECALIBRATE/READ/IDMA blockers. It is historical context, not the current target.

**Update:** the IRQ1/vector-`$51` path this section originally called for
is landed (not experimental anymore):
`mc68302_device::irq1_ack_vector()`, the level-1 IACK dispatch branch,
`m_fdc->set_ready_line_connected(false)`, and
`m_fdc->intrq_wr_callback().set_inputline(m_maincpu, 1)` are all in
`asr10_boot.cpp`/`mc68302.h` as of this commit. The full documented
completion chain runs correctly through RECALIBRATE, SIS, SEEK, and READ
DATA. **Update: minimal IDMA is now implemented and landed**
(`investigations/idma-implementation-plan.md`, built on the measured
register map in `investigations/idma-register-map-probe.md`). Real
register storage for `CMR`/`SAPR`/`DAPR`/`BCR`/`CSR`/`FCR` on
`mc68302_device`, per-DRQ transfer + terminal-count in `asr10_boot.cpp`'s
`idma_drq_w()`. Does **not** implement vector `$4B` delivery: measured
`IMR=$E480` at every observed vector-`$51` IACK has bit 11 (IDMA's INRQ
source) clear, not set — an earlier draft of the plan mis-read this as set
and is corrected in place — and all four completions observed for this
request arrived via the already-working external-IRQ1/vector-`$51` path
instead, so nothing in this flow needs `$4B`.

Result, measured: the `DISK ERROR - LOST DATA` overrun is gone. The
instrument-load sequence then hit a different firmware error, `DISK NOT
RESPONDING`, diagnosed and **now fixed**
(`investigations/tc-reentrancy-probe.md`): `idma_drq_w()`'s `tc_w()` call
was invoked synchronously/reentrantly from inside `upd765_family_device`'s
own live per-bit engine (`fifo_push() -> enable_transfer() -> drq_cb`,
itself inside `live_run()`'s own loop); `tc_w() -> live_sync()` could
re-enter `live_run()` while the outer invocation was still on the stack,
mid-iteration, with shared mutable state (`cur_live`) only partially
updated. Measured effect: `main_phase` stuck permanently in `PHASE_EXEC`
(confirmed by passively polling `$FC4001`/MSR, which stayed `$10` for the
whole 4.994s silence and never showed `$D0`/`PHASE_RESULT`), so
`command_end()` never ran and INTRQ was never asserted at all — not
merely undelivered. Not a data-rate problem: the standing `upd72069`
aux-`$88`-decodes-as-250kbit/s question was checked and is moot here —
`asr10_boot.cpp` already forces `set_rate(500000)` right after that exact
aux write (`ASR10_MISSING_FDC_RATE_SOURCE`, already active) — and even a
genuinely halved rate could only account for ~8ms on a 512-byte transfer,
not a ~5000ms gap.

**Fix, one variable:** only `tc_w()` moved to a zero-delay `emu_timer`
(`asr10_boot_state::idma_tc_deliver`), so it runs on its own call stack
outside `live_run()` entirely; `dma_r()`/`idma_transfer_in()` stayed
exactly where they were. Result: `DISK NOT RESPONDING` is gone, 32 clean
vector-`$51` IACKs observed across multiple distinct sectors, and the
instrument-load sequence now reaches **`FILE LOADED`**.

**Update, `FILE LOADED` independently verified:**
`investigations/file-loaded-verification-probe.md` measured the load
directly rather than trusting the display string. The 32 vector-`$51`
deliveries above cover RECALIBRATE/SEEK/SIS/READ DATA together across the
*whole* dialogue, not 32 sectors — the real transfer is **21 IDMA arms,
337 sectors, 172,544 bytes**, measured from `DAPR`/`BCR` SIB register taps
directly (16 of the 21 arms move a full 20-sector track per single
interrupt). Destination range `$000944`-`$0552FF`, entirely low RAM, none
of it sample RAM. 19/21 transfers verified byte-for-byte against the
source `.img`; the other 2 are a reused scratch buffer overwritten before
end-of-run comparison, not corruption. This is now locked behind a 6th
regression test (`docs/asr10/lua/file_loaded.lua`) that checks the exact
byte count, not just the display text — suite is 6/6.

**Update, post-`FILE LOADED` observation (no code, Lua only):**
`asr10panel_device` has no piano-keyboard ioport at all — only panel
buttons and analog wheels; a literal key-press stimulus cannot be produced
without new input modeling, which was out of scope. What *is* measured:
ES5506 (`$FC2000-$FC207F`) and ES5510 (`$FC3000-$FC303F`) register traffic
is continuous and richly varied from `t≈0` (essentially at reset) onward,
independent of the instrument load and of any button press — 12,634 +
8,390 events in 5 idle seconds post-load alone. This disproves, by
measurement, the standing concern that firmware might never reach the
voice/effects registers, or that ES5510 Host Control is stuck returning a
hardcoded zero (`$FC3025` is polled in tight busy-wait bursts, written
values vary richly). ES5506's missing `SPEAKER`/`add_route` and guessed
clock remain the likely next blockers for *audible* output specifically,
not for firmware reaching the register interface.

**Update, sound path mapped (no code, Lua + source reading only):**
`investigations/sample-ram-and-voice-registers.md` resolves which of three
explanations fits the `$100000-$1FFFFF`-stays-empty finding. Measured with
a 10s-post-load window and a witness tap proving liveness throughout:
`$100000-$1FFFFF` gets 524,290 writes total, but every one of them lands
*before* `FILE 1` (a one-time boot-time RAM-clear sweep, 0x100000 bytes /
2 ≈ one write per word) and zero occur during or after the load. Source
reading (`es5506.h`/`.cpp`, not modified) settles the third explanation:
the ES5506 reads samples exclusively through its own private, per-device
address space (`m_cache[bank].read_word()`), never through the CPU's own
memory — `asr10_boot.cpp`'s `es5506_wavetable_map` (bank 0, populated
`.ram()`) is a *separate* allocation from the CPU's `$100000-$1FFFFF`
(different `address_map` functions, no `.share()` tag, and different
addressing units — word- vs byte-addressed). Voice-register decoding
(all 32 voices' CR/START/END/ACCUM, via a corrected accumulator that
mirrors `es5506_device::write()`'s real shared-latch semantics) shows
voice 0 alone uses bank 0; **all 31 other voices point at bank 1, which is
`.noprw()` — completely unmapped** in this driver's machine config. Also
confirmed by source: no `SPEAKER`/`add_route`/`set_channels` anywhere in
`asr10_boot.cpp` (zero matches); family precedent (`esqkt.cpp`, the same
ES5506 chip) uses the identical `16MHz` clock this driver guesses,
upgrading that guess to family-precedent-supported. Remediation order
sketched (real sample-memory topology first — shared RAM vs. real DMA —
then bank/output wiring, then keyboard input modeling), not built.

RECALIBRATE completion, vector `$51`, SIS, SEEK, READ DATA `$46` issuance,
the READ DATA transfer/terminal-count byte-counting, and READ DATA's own
completion interrupt are no longer open questions — see
`investigations/ready-line-artifact-probe.md`,
`investigations/idma-implementation-plan.md`, and
`investigations/tc-reentrancy-probe.md`.

## MC68302 consolidation (silent assumptions made loud)

`investigations/mc68302-consolidation.md`: before any keyboard/new
functionality work, every place the MC68302 model guesses, is missing, or
stays silent was inventoried and, where possible, turned into an
aggregated Lua guard (`docs/asr10/lua/lib/asr10_guards.lua`) — zero new
C++, zero new environment flags, per the standing Lua-first rule.
- **Exceptions:** a blanket vector-table read tap produces obvious false
  positives (one vector "fired" 1.16 million times in ~22s) — proof
  firmware reuses that address range for ordinary data, not exception
  activity. No Lua-exposed API exists for MAME's own exception-point
  mechanism either. TRAP/internal-CPU-exception inventory is therefore
  `[OPEN]`. What *is* reliable and now guarded: `cpu_space` IACK taps show
  exactly two `(level, vector)` pairs ever fire during a clean boot+load
  — level 1→`$51`, level 6→`$56`.
- **SIB coverage:** every `$FC6000-$FC6FFF` offset firmware touches,
  classified via the device's own `classify_offset()`/`classify_full()`.
  Zero `unknown` accesses; 88 distinct `known_unimplemented` offsets
  (SCC parameter RAM, GIMR/IPR/IMR/ISR, Port A, SCC1-3 command/mode —
  legitimate, unmodeled, not bugs). Guarded: alarm on anything outside
  this calibrated set.
- **IDMA's three debts:** SAPR not payable now (needs a `mem_map` change)
  but guarded (alarm if ever ≠ `$FFFC5803`, the only value ever
  observed). **Update, `reference/e2-address-model.md`:** the address-
  model consistency question (the current meaning of "E2") is settled
  for this address, not open — `mem_map`, runtime, and the reused BR/OR
  static analysis all agree the catch-all is what decodes `$FC5803`
  today, and a dedicated dynamic measurement confirms **zero** bus
  accesses ever reach `$FC5803` in this build (SAPR is never
  dereferenced, per `idma-implementation-plan.md`'s own design). The
  catch-all masks a real hardware target, but that target is currently
  inert, not silently wrong. The short-address mirror hypothesis
  (`$FF8000-$FFFFFF`, the *other* thing "E2" has meant) remains
  genuinely `[OPEN]` and is untouched. CMR bit layout added to
  `docs/mc68302/idma-spec.md`
  (sourced from the manual's OCR text, `[Likely]` not `[Verified]`) and
  cross-validated — decoding the one known value, `SAPI=0`/`DAPI=1`
  matches the already-hardcoded transfer direction exactly, and
  `INTN=INTE=0` independently explains why vector `$4B` never fires.
  Not reimplemented in C++ (one, now two, data points don't validate a
  general field decoder) — guarded instead: alarm on any CMR value
  outside `{$0002, $0D51}`, any BCR outside `{$0201, $0E01, $2801}`.
- **Gate:** `docs/asr10/lua/mc68302_guards.lua`, 7th regression test,
  fault-injection tested (a deliberately-broken allowlist produced one
  aggregated alarm, not thousands, and correctly failed). Suite: 7/7.

**Update, `investigations/mc68302-consolidation-2.md`:** closed most of
the exception-guard `[OPEN]` by tapping handler addresses (not the
vector table) — calibrated against the naive-IRQ1-wiring's known vector-3
Address Error (technique confirmed: tap fired once, matching that
crash's already-established timing). Vectors 2/3/8 close cleanly (ROM
handlers, zero hits); vector 10 is a real, frequently-used A-line OS
syscall mechanism; vectors 4/11 stay `[OPEN]` because their vector-table
slots resolve into the SIB window itself, not code space — ambiguous
signal, not guarded. Traced `$51`/`$56`'s actual origin: the hardcoded
formula never reads a modeled GIMR (unimplemented); firmware writes
GIMR=`$8040` once at `t≈0.002s` and never changes it, and that value's
bits 7-5 happen to equal what the formula assumes — coincidence backed by
stability, not a real implementation. Hand-check: a different GIMR (even
its own reset default) would give a different real vector while the
hardcoded code kept delivering `$51`. Not built — guarded instead
(alarm if GIMR bits 7-5 ever change). Five investigated items: catch-all
RAM's active portion is only the top ~36KB (stack/variables), the rest
(~680KB) untouched; LRCLK edge/period ambiguity stays `[OPEN]` (PBDAT
read rate doesn't match either candidate, ruling out tight polling but
not resolving it); `m_sim`'s redundant double-construction was proven
safe and removed (not just journaled); save-state incompleteness noted
as a candidate; BAR relocation given the specified status text verbatim.
7th regression test now runs five guards, fault-injection tested. 7/7.

## Keyboard (docs/asr10/investigations/keyboard-and-sample-bridge.md)

`asr10panel_device` now has a playable keyboard: 61 keys (`$00-$3F`),
one computer-keyboard octave (`Z`..`,`) plus octave shift (`-`/`=`),
wired to the already-existing `key_down()`/`key_up()` base-class
protocol. Fixed velocity (100), explicitly a simplification. Mid-hold
octave-shift handled correctly (tracks which absolute key was actually
sent, so release always matches). Measured, not assumed: `xmit_char()`'s
`XMIT_RING_SIZE=16` really does overflow under fast/chorded play (32 of
52 bytes lost in a 13-key stress test) — fixed with a proper full-ring
check, made loud with `osd_printf_error()` (not `logerror()` alone,
which needs `-log` to go anywhere observable — checked directly against
`machine.cpp`, not assumed). The overflow counter matches the measured
byte deficit exactly.

**What a key press actually does, measured**: firmware receives the
bytes (4 RHRB reads, matching key_down+key_up exactly) but zero ES5506
register writes on any of the 32 voices, zero `$100000-$1FFFFF` writes,
zero writes to the ROM voice-management table at `$8000` — firmware's
own note-processing logic never runs at all, not merely "runs but can't
reach the chip." Root cause identified, not guessed: the inherited
`key_down()` encoding (`0x80|(key&0x3f)`) uses the *same byte range*
`set_button()` already uses for panel buttons. A direct `BTN_18` press
(an undefined button number) produces the identical signature (4 RHRB
bytes, zero reaction) as the equivalent-numbered key press — the
keyboard is wire-protocol-indistinguishable from an unmapped button.
The real ASR-10 note-event protocol remains `[OPEN]`, genuinely unknown
(checked the local user manual PDF; it's not a service manual and has no
protocol detail) — needs firmware dispatch-table tracing to resolve, out
of scope this round. No 8th regression test added (no reproducible
positive behavior to lock in yet, per instruction); the five
consolidation guards confirmed green with a key press exercised in the
same run. Bank 1 still `[OPEN]`, untouched — no voice was ever
programmed, so nothing to report about it yet.

Also this task: closed two more `mc68302-consolidation-2.md` loose ends.
Vectors 4/11 (previously `[OPEN]`, ambiguous) are now resolved by PC
correlation — neither has a real installed handler; both table slots
have been overwritten by ordinary firmware low-RAM reuse, proven (not
inferred) by watching the CPU's own PC at each tap hit. And the previous
task's "~680KB untouched" catch-all-RAM figure was an arithmetic error
in the summary (not the measurement) — corrected to ~196KB, arithmetic
shown explicitly.

## Note protocol search (docs/asr10/investigations/keyboard-and-sample-bridge-2.md)

**Correction to the previous task**: "byte-for-byte identical to a
button press" was wrong — `KEY_VELOCITY=100` is already nonzero, so
key and button frames genuinely differ on the second byte, and a live
disassembly (RAM code, not ROM — dumped via Lua, disassembled offline
with Capstone in an isolated venv) confirms `$FFB20A` branches on
exactly that: a nonzero second byte takes a distinct path
(`$FFB258`→`$FFB2DC`→`$FFB43E`) from the button path (`$FFB1E0`). A real
key press was measured reaching `$FFB43E` (`PC==$FFB43E`, genuine
execution) and calling **`TRAP #3`/`TRAP #4`** — which disassemble to
**generic ring-buffer enqueue/dequeue primitives** (`$B6C`/`$B7F`/`$B80`
head pointer, count, cap), not note-specific logic. The event is
correctly classified and correctly queued (`trap3_hits=8`,
`trap4_hits=16`) — still zero ES5506 writes; the queue's consumer is
unidentified.

Also found: `$C0-$FF` (the previous task's `[HYPOTHESIS]` for where
notes might live, being the only byte range that doesn't collide with
button/key-up/pressure encoding) is **refuted** — `$FFB392`'s own
disassembly routes `$C0-$FF` to the control-command branch (`$FFB3C0`,
shared with `$FF`/`$FC`/`$F7`), not a note path.

**A prior, previously-unconnected investigation
(`panel-completion-consumer-v350.md`) already swept `$00-$BF`
comprehensively** (192 frames) and found zero ES5506 writes for any of
them — combined with this task's `$C0-$FF` finding, the entire
first-byte space is effectively exhausted. No further byte-sweep was
run; it would only reconfirm what the disassembly already proves for a
branch-target reason a black-box sweep can't see. The open question
isn't "which byte plays a note" — it's what drains the TRAP-#3 queue and
why a correctly-classified, correctly-queued event still doesn't program
a voice. Flagged, not chased further: the real ASR-10 keybed may not
share this serial channel with the front panel at all (no separate
keyboard-scan device exists anywhere in `asr10_boot.cpp`'s machine
config).

**Method point, journaled** (`methods-static-analysis.md` §8.9):
`logerror()` is confirmed dead under this project's own run conditions
(`-log` forbidden, and its callback is only registered when `-log` is
passed) — every loud mechanism must use `osd_printf_error()` or Lua
`print()` instead.

## The consumer found (docs/asr10/investigations/keyboard-and-sample-bridge-3.md)

**Fork quarantine**: a background fork exceeded its "read and summarize"
mandate last task and independently continued this investigation. Per
instruction, none of its material is used as evidence here — every
address and value below was re-derived fresh, this session, from new
measurement (the base/limit/slot values happen to match both the fork's
own claim and the legitimate, pre-existing `runtime-cycle.md`; that is
corroboration, not reuse).

Re-measured the six-slot scheduler (base `$23F6`, limit `$247A`, stride
`$16`) fresh, after `FILE LOADED` specifically (prior measurements only
covered plain idle boot). Five of six slots sit idle at any snapshot —
saturation was tested as one of three possible outcomes, not assumed,
and is **refuted**. A key press was correlated against all six slots,
`$B6C` (the TRAP #3/#4 queue pointer), and the dispatch point: **TRAP
#3/#4's queue nodes are a separate memory pool (`$14F4-$150C`), never
inside the six-slot table** — settling that open question. The actual
bridge is **TRAP #9** (vector 41, `$F88138`, previously unidentified),
called with `A1` = a target slot address — its `bclr.b #7,$2(a1)`
instruction is exactly what flips a slot from idle to pending, matching
the measured write precisely. Slots 2 and 3 (previously permanently
idle) were installed into and genuinely dispatched by a real key press —
not an installation failure, not a scan-threshold failure. The
dispatched code (`$0073EA`/`$F8F2FA`, already known from
`runtime-cycle.md`) calls `TRAP #6` (a re-arm primitive) then reaches
real, multi-level jump-table dispatch mechanisms (`$740C` indexing a
15-entry table at `$67AC`; `$FF9650`, a shared vector table). The chain
is alive through at least four levels — but zero ES5506 writes still
result (cross-checked against the previous task's own dedicated
measurement). **The blocker is not the scheduler — it's further down
this call chain**, a new, narrower lead for a future task. The
"no separate keyboard-scan device" hypothesis stays `[OPEN]`, neither
strengthened nor weakened by this trace.

## The MIDI gate, and where the note stops (docs/asr10/investigations/keyboard-and-sample-bridge-4.md)

Wired DUART channel A for MIDI (the `esq5505.cpp` idiom:
`MIDI_PORT`/`midiin_slot`/`rxd_handler().set(m_duart,
FUNC(scn2681_device::rx_a_w))`) — the one machine-config change this
task made. Injected a real note-on (`$90 $3C $64`) headless, via a
30-byte Standard MIDI File loaded into the wired `MIDIIN` image
device's Lua `image:load()` binding (no OS MIDI hardware needed).

**Result: silent, same signature as the panel path** — 3 RHRA reads
(exactly the message), zero ES5506 voice writes, zero
`$100000-$1FFFFF` writes. **This exonerates the panel dispatch chain**:
the three-turn scheduler/TRAP-#9/jump-table descent
(`keyboard-and-sample-bridge-3.md`) was correct tracing of a path that
turned out not to be panel-specific.

Followed the MIDI byte parser (not the scheduler) forward instead:
`$F889A2`'s status/data byte classifier resolves Note On's data-byte
vector via a table at `$FF871E`, which for a nonzero note number
reaches **`$FFB43E` — the exact address `panel-completion-consumer-
v350.md` already identified as the panel protocol's own completion
consumer.** MIDI and panel data provably funnel into the same
function. Live-disassembled `$FFB43E`'s previously-undocumented
decline branch: `rts` at `$FFB486` iff `lowmem[$171]==1` AND
(`$FFB6C4`'s result `& lowmem[$CDE]`)`==0`; `$FFB6C4` performs a
**key-range check** (note number against `(lowmem[$330])+$3C`/`+$3E`,
a low/high split-point pair in an instrument/keygroup descriptor) —
not a residency flag. For the actual test note, `lowmem[$171]==0`, so
this decline was **not** taken — the message instead proceeded through
`$FFB56E` into `TRAP #4`/a new primitive **TRAP #D** (a deferred-work
queue that can synchronously invoke a stored function pointer), landing
in `$F884FC` — generic DUART-channel continuation plumbing, not
voice/sample code. The trace ran past where note-specific logic would
plausibly live without ever finding a residency check or reaching
ES5506.

**Sample-residency hypothesis: not confirmed as framed.** The real
gate found is a key-range check, not a residency/pointer/length read,
and in the live run it wasn't even the branch taken.
`lowmem[$330]==0` before the note arrived is `[HYPOTHESIS]`-level
suggestive of "no voice/keygroup set up yet" rather than "sample not
resident" — not measured this task.

## Instrument selection was the missing step (docs/asr10/investigations/keyboard-and-sample-bridge-5.md)

The ASR-10 manual states loading and *selecting* an instrument are two
separate operations (eight Instrument•Sequence Track buttons, yellow
LED = selected; unselected = no sound regardless of what's loaded).
This project's entire prior series never pressed a select button.
Swept all 64 panel buttons after `FILE LOADED`, watching lowmem
`$330`/`$332` (the exact instrument-slot state `$FFB6C4` was already
found reading): `BTN_02`, pressed from the idle screen (same button
code used mid-load-dialog for a different, context-sensitive purpose),
selects Instrument slot 1 — `$330` `$0000->$1098`, `$332` bit 0 set,
display becomes the instrument name/volume screen.

**With the instrument selected, both a panel key press and a MIDI
note-on now produce real ES5506 voice allocation** — voice 1 and voice
2 respectively, each with a full, correctly-shaped `CR`/`START`/`END`/
`ACCUM` program. The three-turn "declining condition" descent
(`-3.md`, `-4.md`) was tracing correctly-behaving firmware that
legitimately does nothing without a selected instrument — not a bug.

Still silent on `-wavwrite` (`peak=0`) despite the real voice writes:
both voices land in **bank 1**, and `es5506_wavetable_map` only maps
bank 0 (shared with CPU RAM) — banks 1-3 are `.noprw()`
(`sample-ram-and-voice-registers.md`'s finding, now connected to a
live play event for the first time). **Next lead**: why voice
allocation picks bank 1 over bank 0 — either the instrument data
itself specifies bank 1 (real hardware too, fix is mapping bank 1) or
voice 0/bank 0 is firmware-reserved and this driver's bank wiring
doesn't match real hardware's split (fix is on the `es5506_wavetable_map`
side). Not measured yet.

## Bank 1 is CPU lowmem — real audio confirmed (docs/asr10/investigations/keyboard-and-sample-bridge-6.md)

Corrected an earlier wrong premise: `$100000-$1FFFFF` staying zero
does not mean no bank mapping helps — bank 1 need not alias bank 0's
physical RAM at all. Decoded voice 1/2's live `CR`/`START`/`END`
(identical for both): bank field `(CR>>14)&3=1`; the bank-relative
byte range (`START>>11`, `END>>11`, doubled) is `$3FA2A-$54FFE` —
**inside `$000944-$0552FF`**, the already-proven real loaded-instrument
range, when read as a direct `$000000`-based CPU offset. Confirmed via
`es5506.h`/`.cpp` (read only): `es5506_device::get_bank() =
(control>>14)&3` (4 banks); `es5505_device` differs, `(control>>2)&1`
(2 banks) — genuinely different fields, not a naming variant.

Checked `$100000-$1FFFFF` content after select+play with a live
witness through the whole window (§8.7): zero new writes (still
exactly the 524,290-write boot sweep), and the content itself is a
`word=address>>10` RAM self-test pattern, not zero-fill — a correction
to prior "clear sweep" phrasing. **Hypothesis 1 (bank 1 = same RAM as
bank 0, not yet moved) refuted numerically. Hypothesis 2 (bank 1 =
other CPU-visible memory, where the instrument already lives)
confirmed.**

Minimal fix: new `es5506_wavetable_bank1_map()` reusing `mem_map`'s
own `low_rom_or_lowmem_r`/`lowmem_w` (same backing store, `mem_map()`
itself untouched), wired to ES5506 bank 1 instead of `.noprw()`.
**Real, sustained, ~720Hz periodic audio now comes out of
`-wavwrite`** — peak ~12% of full scale, starting 0.475ms after the
voice program completes. Regression 7/7 before and after.

Also extended `docs/asr10/reference/subroutine-index.md` (an existing
file matching what a new "routine-index.md" would have been) with 15
newly-established addresses from this series, rather than creating a
duplicate catalog.

## Is the sound right, not just present? Locked. (docs/asr10/investigations/keyboard-and-sample-bridge-7.md)

Scope note for all of this: ES5510 stays `set_disable()`'d — every
judgment below is the dry ES5506 path only.

**Pitch**: FC (frequency control) is vibrato-modulated (~±3-4%,
~11-12ms period), not static — the first post-onset write is the base
pitch. Octave test (`$3C` vs `$48`): FC ratio 2.0011, and re-measured
audio pitch via autocorrelation (the prior turn's zero-crossing
~720Hz figure was wrong — overcounted harmonics) gives a *stable*
~136.8-141Hz for `$3C`, doubling to ~272.7-274.3Hz for `$48` — the
firmware's internal pitch math is confirmed correct end to end.
Absolute pitch (measured ~137Hz vs. MIDI-nominal 261.6Hz for note 60)
stays `[OPEN]`: the octave test is clock-invariant by construction, so
it cannot distinguish "sample's own recorded pitch isn't tuned to
261.6Hz" from "uniform clock error" — searched lowmem for a
wavesample-header template explaining it and found none (register
values are computed at note-on, not copied verbatim). Clock left
unchanged, per instruction; a for-reference-only "what clock would
match" figure (~30.6MHz, not a clean crystal value) is reported but
not applied.

**Voice behavior**: note-off measurably accelerates silencing via
`LVRAMP`/`RVRAMP` going negative ~46ms after note-off (not a `CR`
`STOP`-bit change) — confirmed audibly, ~17x quieter at a matched
timestamp vs. held-without-note-off. Looping: audio persists well past
the single-pass loop duration (3.15s) when held without note-off,
consistent with `CR`'s `LOOPMASK` bits both being set. Polyphony: 3
simultaneous notes → 3 distinct voices, 3 correctly-scaled FC values.
10 sequential notes → 10 distinct voices, no premature stealing (full
32-voice exhaustion not tested, `[OPEN]`, explicitly bounded).

**8th regression test landed**: `note_audio.lua` (structural) +
`check_note_audio.py` (the real audio-level gate: peak amplitude and
autocorrelation-measured frequency against a band built from the
above measurements, not an external assumption) wired into
`regression-test.sh`'s new `run_test_audio()`. Both halves
fault-injection-tested. Suite is now 8/8.

**Journaled**: `es5506_wavetable_bank1_map()` reusing
`low_rom_or_lowmem_r`/`lowmem_w` gives ES5506 bank 1 visibility into
the boot-time ROM overlay real hardware's sample bus (DRAM-only) never
has — harmless in practice (notes only trigger post-boot) but written
down as a known simplification. Also restated the `keyboard-and-
sample-bridge-6.md` correction: `$100000-$1FFFFF` holds a
`word=address>>10` RAM-test pattern, not zero-fill — prior "clear
sweep" phrasing was imprecise about content, not write count.

## Does ES5506 read where we think it reads? Yes -- addressing hypothesis refuted (docs/asr10/investigations/keyboard-and-sample-bridge-8.md)

Tested directly (not recomputed) whether `es5506_wavetable_bank1_map()`
reusing `low_rom_or_lowmem_r` (a byte-addressed-CPU-space handler)
against ES5506's word-addressed sample bus caused a word-vs-byte
addressing bug that would halve playback rate/pitch. Tapped
`es5506_host.spaces["bank1"]` directly (a real, Lua-addressable
`addr_space`) instead of inferring from CPU-side register math.
Calibrated the tap's units against a known transient placeholder
`ACCUM` value (`0xDC4BC000>>11` matched the first observed fetch
address exactly), isolated our note's voice from one other, unrelated,
statically-parked background voice that dominates raw bank-1 traffic,
and tracked its fetch position across three time windows: measured
position matched predicted position (`start_word + rate*t`) within
~0.05%, and measured advance rate matched the predicted 13,870
words/sec within bucket-quantization noise. Closing proof: 20
consecutive (address, fetched-value) pairs compared bit-exact against
the CPU's own view of the same backing store — **20/20 exact
matches**. **The addressing hypothesis is refuted with direct,
bit-exact evidence**, not just recalculation. In that historical 16MHz
test `ACTV=0x1F` (31, constant) and `m_sample_rate=31,250Hz`; the
later V3.50 A/B/A effect-commit result supersedes only its old
"never changes" conclusion. Neither 29.76kHz nor 44.1kHz was reachable
from that historical 16MHz configuration at any `ACTV`; no code change
followed because Del 3/4 were conditioned on confirming an addressing bug.
The ~1.91x absolute-pitch gap stays `[OPEN]`, now with the entire
signal path (register → live fetch → output) verified bit-exact,
narrowing the remaining candidate to the sample's own data/tuning
rather than any addressing-layer cause.

## The clock was wrong, not the chip -- ES5506 moved to Y2 (docs/asr10/investigations/keyboard-and-sample-bridge-9.md)

Changed the ES5506 clock from `XTAL(16'000'000)` (Y1, the MPU crystal,
borrowed from `esq5505.cpp` precedent only because it shared a number)
to `XTAL(30'476'180)` (Y2, the board's own documented ES5506/ES5510
crystal). Two independent lines converged on this value within 0.4%: a
backwards calculation from measured `$3C` pitch
(`keyboard-and-sample-bridge-7.md`, ~30.6MHz) and the board's
documented crystal complement (Y1=16MHz/MPU, Y2=30.47618MHz,
Y3=33.8688MHz/ES5506+ES5510+AD-DA).

Measured with autocorrelation (never zero-crossing): `$3C` now lands
at ~260-262Hz against MIDI-nominal 261.6Hz (~0.3-0.6% low, not growing
with note number across a fifth-plus range); semitone/whole-tone/
fifth/octave interval ratios all hold within 0.3% of equal
temperament. **The sound is now right, not just present.**

Investigated (not blocking) why the raw numbers look doubled against
ASR-10's documented 29.76kHz/44.1kHz modes: `es5506.cpp` uses an
**identical** `16*(voices+1)` divisor for both `es5505_device` and
`es5506_device` -- no code-visible bug to point to either way.
`esq5505.cpp`'s own precedent (`30.47618_MHz_XTAL / 2` fed to ES5505)
supports a crystal-network `/2` as the better-explained candidate,
left `[OPEN]`. The preceding ACTV conclusion is historical and superseded by
the later V3.50 effect-commit A/B/A witness: ROM HALL REVERB -> 44LUSH PLATE
-> ROM HALL REVERB writes `$1F -> $17 -> $1F` at `$00E826`. Current MAME
therefore changes its generic ES5506 stream rate from 59,523.789 to
79,365.052 Hz and back, while its synthetic PB3 timer remains invariant at
44.1k toggles/s (22.05k full cycles/s). This is a current-model split, not
proof of physical ASR-10 clock routing; see
`investigations/audio-frame-timing-actv-differential-v350.md`.

8th regression test's pass band moved `100-180Hz` -> `230-290Hz`, with
the reason (clock correction, not loosened tolerance) stated in the
test's own comment and the commit message. Suite is 8/8.

## Factor of two: mapped, not explained. Clock provenance cleaned up. (docs/asr10/investigations/keyboard-and-sample-bridge-10.md)

Self-correction carried forward: the prior turn's "MAME's FC/ACCUM
fractional bits are probably a bit position off" was a plausible
mechanism promoted to an explanation before being tested. Retracted.
The real ES5506 datasheet (`docs/ensoniq/ES5506.pdf`, Ensoniq OTTO
Spec Rev 2.3 -- a primary source now in the tree) confirms MAME's FC
(17-bit, 6+11) and ACCUM (32-bit, 21+11) formats are **bit-exact**
matches to the real chip. That candidate has positive evidence
against it, not just an absence of support.

The same datasheet states OTTO is rated **"UP TO 16MHZ OPERATION."**
Feeding it the undivided Y2 (30.476MHz, this project's own corrected
clock) exceeds that rating ~2x. `esq5505.cpp`'s own precedent halves
the identically-named crystal before it reaches the same chip family.
Both facts genuinely support a divided real-hardware clock. But
measured pitch is only correct (`keyboard-and-sample-bridge-9.md`)
at the *undivided* Y2 in MAME -- halving it would put audio an octave
low. Freshly re-measured live fetch rate at the current clock
(25,600-27,840 words/sec) matches the full-rate prediction, not the
half-rate one, confirming MAME's own execution is self-consistent
with itself at every layer measured (no separate compensating bug
inside MAME). **These two facts do not reconcile via any mechanism
confirmed this task.** Recorded as a named, standing debt, `[OPEN]`,
no mechanism named -- not resolved, not buried.

**Clock provenance table** (now in `subroutine-index.md` and the
driver's own comments): `MC68302`=Y1 (measured/derived); `UPD72069`
and `ES5510`=guesses, both unchanged, `ES5510` unmeasurable while
`set_disable()`'d; `SCN2681`=derived and empirically confirmed (IP3/16
= 31,250 baud matches the byte-exact-verified MIDI protocol, IP5/16 =
62,500 baud matches the panel channel); `ES5506`=Y2, doubly-converged
(backwards calculation + board crystal list), carrying the open
factor-of-two question.

**Address inventory**: re-swept the full boot->load->select->play
timeline (not just idle boot) -- still exactly 9 touched catch-all
buckets, nothing new. `$FC5000-$FC501F` is touched once
(`t≈2.95s`, early ROM boot) in a write/write/read-back burst matching
`memory-map.md`'s already-documented WD33C93 SCSI reset sequence at
the address level -- a confirmation, not a new "SCSI candidate"
discovery (that framing was stale; the region is already attributed).
Nothing cyclic found in either watched range.

`TUNING KEYBOARD` strings and the `MODE=$0D`/`ACT=$1F` mode switch
remain `[OPEN]`. **Update, `scc-hardware-gap.md`:** both OS images and
the analyzed ROM were searched byte-for-byte (positive-controlled: the
reconstructed ROM's SHA-256 matches this manifest's own recorded
`asr10.bin` hash) for `TUNING KEYBOARD`/`KEYBOARD TUNED` — zero hits,
any form. The one partial match (`HANDS OFF`, ROM `$F81196`) sits in
an unrelated factory diagnostic-menu string pool, not a boot-time
message, and has no found reference to it anywhere in ROM. The
`ACT=$1F` connection is independently corroborated from an unrelated
angle: the `$FF7F00-$FF7FF6` stride-8 structure flagged in
`interrupt-topology-gaps.md` has exactly 31 entries, matching
`ACT=$1F` numerically — `[Likely]` a per-voice/per-event table sized
to it, `[OPEN]` whether it is specifically a voice-assignment table
(see `scc-hardware-gap.md` Del 1.2). Keyboard-hypothesis status:
**upgraded, not confirmed** (`scc-hardware-gap.md`, follow-up round):
both SCC1/SCC2 buffer-descriptor rings are real, standards-shaped,
block-oriented (`MRBLR`-sized, wrap-marked) — built for a continuous
stream, not an occasional poll, which is what a keyboard-scanner link
would look like and a status query would not. A silent shutdown path
was traced and named (`$F8C0E6`, disables both channels with no error
and no display text on one specific SCC event) — matching the exact
"a silent timeout would explain why nothing is noticed" shape the
hypothesis needed, though not proof of it.

**Update, `scc-board-source-question.md`:** the source of "`TUNING
KEYBOARD - HANDS OFF`" is **owner testimony from Per**, not a project
artifact — a real, distinct, and stronger evidence category than
anything in the analyzed disk images, correctly identified as such
this round rather than left as an unattributed assistant claim.
Cross-checked against `ASR10_manual.pdf` (present in the tree): the
manual documents the exact behavior, using the abbreviation `KBD`
(`"TUNING KBD - HANDS OFF"`, `"KBD FAILED - RETRY?"`), which the
literal-ASCII search had never tried (`TUNING KEYBOARD` was the wrong
string). Re-searched with the correct wording: still absent as literal
ASCII in either OS image or the analyzed ROM — every matching fragment
(`TUNING`, `HANDS`, `FAILED`, `RETRY`, `KBD`) belongs to a different,
identifiable, unrelated string pool. **But a fine-grained (20ms)
display poll across boot — a resolution no prior probe used, since all
of them jumped straight to waiting for `FILE 1` — shows this project's
own model already displays both halves of the exact message,
"TUNING KBD - HANDS OFF" then "KEYBOARD TUNED", at `t≈15.06-15.2s`
every boot.** Correlated against `SCM1`/`SCM2`/`IMR` writes in the same
run: all three display-phase transitions in that window (including the
transition to `FILE 1`) are preceded, within single-digit-to-low-
double-digit milliseconds, by a real SCC1/SCC2 arm/disarm cycle, with
zero level-4 IACKs throughout — the sequence completes by timing out
on each cycle, never by a real interrupt. **Keyboard hypothesis status:
confirmed on structure and timing, still open on physical byte
source** — the message is real, present, and precisely timed to real
SCC activity in this project's own model; what component on the real
board drives that activity remains `[OPEN]`, and no board-level
documentation exists in this tree to answer it (`scc-board-source-
question.md` Del 0/3 — a question list for Per, not a guess).

**Update, board-source recalculation round.** The byte source is named,
`[External]` evidence: the real ENSONIQ ASR Service Manual (downloaded,
`docs/asr10/sources/ASR10_service_manual.pdf`) states plainly that "the
digital board communicates with the keyboard over a two-line
asynchronous interface carried by the 20-pin keyboard ribbon cable" —
and separately describes a *second*, 3-line **synchronous** link
between the keyboard and the keypad/display board, pass-through wired
over the same cable. A downloaded keyboard coil-board schematic
(`ASR10_upper_coil_board_schematic.pdf`, R. Grieb/Tauntek reconstruction)
shows the physical device on the other end: an 80C52 MCU with its own
hardware UART, `SERIN`/`SEROUT` wired straight to the 20-pin connector.
This project's own dynamic measurement cannot yet tell SCC1 and SCC2
apart (both configured identically, armed in lockstep) — which channel
is the async keyboard link versus the sync keypad/display link is
`[Likely]`, motivated by the manual's two-link description, not proven
by a measured difference. Mode-field recalculation: cross-validated the
existing `ENR`/`ENT`/`DIAG`/`MODE` bit positions against an indexed copy
of the real MC68302 manual, and learned Transparent mode is restricted
to SCC2/SCC3 only — undermining the old "MODE=3 = BISYNC/Transparent"
label for SCC1 specifically — but the exact numeric MODE value meaning
UART was not found despite a genuine search; baud rate remains
uncomputed. **A real correction, caught this round**: `scc_rx_common`'s
`trap #0` error codes are **5 and 6, not 45/46** (the `ori.b #$28,D0`
instruction runs *after* the trap, not before — misread previously);
checked against the manual's own error list, codes 005/006 both read
"could not synchronize audio input" verbatim — a real match, where
45/46 do not appear in the manual at all. `ERROR 032` ("bad download")
and `145` ("unknown DUART interrupt error") both independently confirmed
against the same list. See `scc-board-source-question.md`'s
"Recalculation" section for the full detail and all source citations.

**Update, sample-mode test round.** The error-code correction above
("could not synchronize audio input") reopened whether SCC1/SCC2 are
the stereo audio input rather than the keyboard link. Tested directly
using the machine's own `Sample-Source Select` procedure
(`ASR10_manual.pdf`'s own button sequence): SCC1/SCC2 do re-arm on
entering that mode, specifically — not a generic button-press effect
(a clean control, an unrelated button, produces zero SCC writes) — but
every rewritten descriptor points to the *same* small buffers already
used at boot calibration, never the sample-RAM pool, never resized,
zero content, zero IACKs. **This does not look like bulk audio DMA.**
**Keyboard-hypothesis status downgraded from "confirmed on structure
and timing" to `[OPEN]`** — neither hypothesis is now cleanly
supported: the arm/timing evidence for "keyboard" is real and
unchanged, but so is the buffer-scale evidence against "bulk audio."
The keyboard link itself remains real (80C52 UART, 20-pin cable,
service-manual-documented) — which digital-board wire terminates it is
what's unresolved, not whether it exists. A related, newly-named open
question: the service manual's own "three-line synchronous"
keypad/display link sits in tension with this project's own
`[Verified]` fact that DUART channel B (async-only hardware) is the
panel link — not resolved this round. See `scc-board-source-
question.md`'s second recalculation for the full detail.

The known `scc_rx_common` path raises 005/006 when a descriptor ready bit is
already set, i.e. a full or undrained ring. It is not verified as a no-input
timeout. RECORD producing 005/006 would strengthen an SCC/audio relationship,
but would not by itself prove SCC carries PCM; absence of 005/006 would not by
itself disprove that relationship.

**Update, full RECORD/start 2026-08-22.** The manual sequence was driven through
unused Instrument 1, Level-Detect, a BTN_0A endpoint and `Enter-Yes`. Firmware
accepted the start command and remained at `WAITING...272 SEC LEFT` for 12 s;
it never reached `RECORDING` and showed no ERROR 005/006. All SCC descriptor
rewrites and `$7033`/`$703B` arm cycles occurred while Level-Detect was being
built, ending at 17.143 s. `Enter-Yes` produced no further SCC/IMR writes. Across
start +12 s: all lengths stayed zero, all 12 800 buffer bytes stayed zero and
unchanged, level-4 IACK=0, SCC-buffer writes=0 and sample-RAM writes=0, each with
a live positive witness. The narrow claim "RECORD/start immediately produces
005/006 in the current model" is `[DISPROVEN]`; `SCC <-> audio input` and
`SCC <-> keyboard` both remain `[OPEN]` because no signal crossed the threshold
and no RX data identified either channel. See
`investigations/full-record-start-probe.md`.

**Correction and WAITING decode, 2026-08-23.** BTN_0A did not select minimum
threshold: a live tap showed `$017C` increasing `2 -> 20`, the numeric maximum.
The active loop is `$005BD0-$005C48`, state `$0D04=2`; it dequeues scheduler
events and accepts tag `$90E8`. V3.50 has exactly one literal producer, in the
SCC receive continuation (`$00643C -> $0064BA -> $00665C`), which changes the
state and posts `$90E8`; `$005C50` then branches to setup at `$005C6C` because
the state is no longer 2. Before that post, `$0064BA` must process a received
descriptor range past `$FFD15C`; this boundary measured 58 and stayed unchanged
while the panel threshold index rose `2 -> 20`, so the two values are not a
direct identity. During the measured WAITING window, SCC event accesses
and sample-RAM writes remained zero with live witnesses. PBDAT low byte was
active (4,560 reads/2,280 writes) solely through the verified PB2-PB0 analog-mux
selector at `$0069A2/$0069AA`, paired with 4,560 ES5506 PAR reads. This does not
touch PB9/PB10/PB11. At the time of that bounded trace the bulk consumer and
bus master remained `[OPEN]`. This is now superseded by the SCC RX
source/consumer pass below; see `investigations/waiting-exit-condition.md` for
the original boundary.

**SCC RX source/consumer closure, 2026-08-23.** `$006608/$00660A` converts a
completed descriptor's absolute payload range into offsets relative to the
sampling object. `$0066C6/$0066CA` stores that range, and the type `$0E` service
node sent to `$14DA` reaches `$00B478-$00B4E4`. That callback programs MC68302
IDMA with `SAPR=object+range_start`, `DAPR=object+$20`, `BCR=end-start`, and
`CMR=$37A1`; completion `$00AA48` advances both the range index and object
destination. The first payload consumer and intended recording bus master are
therefore `[Verified firmware]` IDMA, not `[OPEN]`. A narrow runtime snapshot
resolved `$12D8->$F76400` and `$1320->$F74900`; their first relative range
`+$0200` equals the known first SCC buffers `$F76600/$F74B00`. At `WAITING` in
`INPUTDRY LEFT`, `$016F=0`, SCC1's continuation is selected, destination
`+$20=$02C110`, and remaining count `+$24=$00F44710`. `$02C110` is outside the
current model's `$100000-$1FFFFF` sample-RAM candidate, so physical sound-memory
decode/banking remains `[OPEN]` and no `mem_map` change follows.

The service manual independently says the analog board digitizes analog audio,
that microphone threshold crossing changes WAITING to RECORDING, and that LRCLK
to the 68302 originates on the analog board. Together with the verified
SCC-payload-to-recording-IDMA chain, `analog-board A/D serial stream -> SCC RX`
is now `[Likely]`, while direct SCC pin/glue wiring remains `[OPEN]`. The
narrower sampling-source claim `SCC RX payload = keyboard protocol` is
`[DISPROVEN]`; a separate SCC/keyboard use outside this sampling transfer is
still `[OPEN]`. See `investigations/scc-rx-source-and-consumer.md`.

**Physical SCC audio-input boundary, 2026-08-23.** The service manual narrows
the analog/digital boundary to a 34-pin ribbon from analog-board `J1` to
digital-board `J6`; an installed SP-3 board is explicitly interposed between
those connectors. Independent keyboard and rack analog-board photographs show
an `ANALOG DEVICES AD1879JD` stereo ADC. They do not provide a pinout, and the
digital board is four-layer, so the exact J1/J6 pins for serial data, clocks,
PB3/LRCLK and MC68302 RXD1/RXD2 remain `[OPEN]`. The photographed revisions do
not use CS5336. No located source puts ES5701 or an ES5506 serial pin on the
ADC-to-SCC route. See `investigations/physical-scc-audio-input-boundary.md` for
the source audit and power-off continuity matrix required to close the wiring.

**Virtual SCC RX chain, 2026-08-23.** A temporary opt-in bridge supplied one
CP-shaped SCC1 receive completion while a separate Lua probe observed the real
firmware. The 800-byte descriptor contained an explicit `$7FFF` threshold
sample at offset 64. Firmware acknowledged level-4 vector `$4D`, returned the
descriptor to SCC ownership, passed `$0064BA/$0065CC`, displayed `RECORDING`,
selected pretrigger range `$F76606..$F76920`, and programmed IDMA
`SAPR=$F76606`, `DAPR=$02C110`, `BCR=$031A`, `CMR=$37A1`. The existing IDMA
byte-input path wrote 793 bytes, all exact matches, delivered vector `$4B`, ran
`$00AA48`, and advanced destination to `$02C42A`. This upgrades the live
firmware path from a virtual SCC completion through recording IDMA to
`[Verified runtime]`; SCC/CP descriptor production itself remains
unimplemented, and the physical ADC source/wiring status is unchanged.

The sole data mismatch was the final byte: the current FDC-derived IDMA model
uses `BCR-1`, so BCR 794 produced 793 writes while firmware advanced by 794.
Exact BCR-length transfer for the SCC memory-source mode is therefore
`[DISPROVEN]` in the current model, with the first stop precisely identified.
The 95-line temporary C++ bridge was removed after measurement; the Lua
observer remains archived as provenance. See
`investigations/virtual-scc-rx-chain.md`.

**SCC/CP RX boundary audit, 2026-08-23.** The Phase 1 bridge cannot be moved to
an existing SCC RX input because the current `mc68302_device` has none. It
inherits only `m68000_device`, exposes no serial RX pin/byte callback, allocates
no CP/SCC scheduler, and has no level-4 SCC producer. Parameter RAM
`$0400-$07FF` and SCC registers `$0880-$08B5` are `known_unimplemented`
`m_shadow` storage; only CPU-visible writes can change them. No code consumes
MRBLR/E/descriptor pointers, writes RX payload or length, advances the receive
BD, generates SCCE, or arbitrates SCC1 through IPR/IMR/ISR to vector `$4D`.

Therefore current-model SCC/CP descriptor production is `[OPEN capability]`,
while its absence is `[Verified source/model]` (outcome C). No runtime stream
was fabricated because writing buffer/descriptor/SCCE would merely recreate
Phase 1 and could not test CP ownership. The minimum future boundary is a
receive-only byte entry followed by CP-owned buffer write, length/E/wrap/pointer
update, SCCE/SCCM semantics and level-4 delivery. Physical ADC source and wiring
statuses are unchanged. This historical Phase 2 boundary is superseded by the
Phase 3B result below. See `investigations/scc-cp-rx-chain.md`.

**Minimal CP receive engine, 2026-08-23.** `mc68302_device` now exposes a
post-framing SCC RX-byte ingress and owns the minimum measured receive effects:
buffer writes through the current BD, MRBLR count, E clear, producer advance/W
wrap, SCCE bit 0, SCCM/IPR/IMR/ISR gating and SCC1/SCC2 level-4 vectors. A Lua
source supplied only 800 deterministic SCC1 bytes. Before firmware resumed,
the model had produced 800/800 exact bytes, BD0 `$D000/$0000 -> $5000/$0320`,
`SCCE1=$0100` and `IPR=$2000`. Firmware then acknowledged vector `$4D`, reached
`$00643C/$0064BA`, entered state 3/`RECORDING`, and programmed IDMA with
`SAPR=$F76606`, `DAPR=$02C110`, `BCR=$031A`, `CMR=$37A1`.

Single-full-descriptor SCC1 CP completion is therefore `[Verified runtime]` at
the post-framing boundary. SCC2 and eight-descriptor/W-wrap behavior are
implemented but remain `[OPEN runtime]`. Normal execution has no connected RX
producer; physical serial framing, ADC source and wiring remain `[OPEN]`.
Phase 3B intentionally did not restore Phase 1's separate IDMA byte helper:
vector `$4B` and recording-destination writes remained zero while a 660,747
low-RAM-write witness stayed live. The separate IDMA memory-source transfer is
still `[OPEN implementation]` at this historical boundary; firmware IDMA setup
itself is verified. This is superseded by Phase 4A below. See
`investigations/scc-cp-rx-minimal-engine.md`.

**SCC-to-IDMA transfer, 2026-08-23.** The exact observed CMR `$37A1` mode now
performs an internal incrementing word copy from SAPR to DAPR and produces IDMA
normal completion. Starting from the Phase 3B 800-byte SCC1 feed, firmware
selected `$F76606-$F7691F`; the model performed exactly 794 source reads and
794 destination writes to `$02C110-$02C429`, with 794/794 byte matches. At
level-4 IACK, `CMR=$37A0`, advanced `SAPR=$F76920`, advanced `DAPR=$02C42A`,
`BCR=0`, `CSR=$0100`, `IPR=$0800`, `IMR=$EC80`, `ISR=$0800`, and vector `$4B`
was delivered. Firmware reached `$00AA48` and advanced object `+$20` to
`$02C42A`.

The complete current-model `SCC1 RX -> descriptor -> $4D -> IDMA -> recording
RAM -> $4B` chain is `[Verified runtime]` for this full-descriptor case. The
destination lies in low-memory backing shared with the ES5506 bank-1 wavetable
view, so it is `[Verified current model]` sample-accessible RAM. The physical
ASR-10 RAM decode/bank remains `[OPEN hardware]`. CMR `$0D51` retains its
separate external/FDC byte path and `BCR-1` convention; Phase 4A does not
generalize `$37A1` behavior to other IDMA modes. See
`investigations/scc-idma-transfer.md`.

**SCC RX payload format, 2026-08-23.** Three SCC1 descriptors copied 2,394
bytes with 2,394/2,394 exact source/destination matches, including alternating
`AA 55` and `0000/8000/FFFF` word vectors. Live `$FFD54A` reads big-endian
16-bit words, takes signed magnitude and checks one word per 16 bytes against
the threshold. After recording stopped, the CPU read the resulting
`$02C110-$02CA69` range zero times while ES5506 bank 1 fetched it 2,403 times;
256 retained fetches matched the CPU-visible words 256/256. The measured
digital contract is therefore `[Verified current model]` big-endian signed
16-bit PCM with no CPU conversion between SCC/IDMA and ES5506 playback.

Mode 0/LEFT is SCC1/vector `$4D`; mode 1/RIGHT is now independently verified
through SCC2/vector `$4A` with 794/794 exact bytes and one `$4B`. Mode 2/L+R
enables both firmware continuations, and ROM `$F95EB2` allocates two separate
destination halves, not an interleaved buffer. Runtime stereo remains `[OPEN]`:
the current L+R run reaches System Error 57 on destination-instrument
selection before Level-Detect and before any SCC/IACK event. See
`investigations/scc-rx-payload-format.md`.

**RECORD completion and System Error 57, 2026-08-23.** The prior reading of
raw `ERR0R ?57 - REB00T` as Error 157 is `[DISPROVEN]`: live `trap #0` had
`D0=$0039`, `$00C0=$0039`, from ROM `$F8A55E move.b #$39,D0 / $F8A562 trap
#0`. It is System Error 57. The L+R failure is before Level Detect: allocator
`$F8A44E` splits a valid block at `$02C0D0`, attempts remainder-header writes
to `$7CE510/$7CE512`, then reads back zero because the current CPU map has no
RAM there. LEFT reaches Level Detect under the same control. The model's
missing CPU-visible RAM at this split is `[Verified current model]`; physical
ASR-10 RAM decode remains `[OPEN hardware]`.

Mono RECORD completion is independently `[Verified runtime/firmware]`.
`BTN_22` caused no state/display/metadata change; `BTN_23` changed `$0D04`
from 3 to 0 through `$FFC04C/$FFC06C`. `$FFC230` finalized object `$02BFF0`
with start/loop-start 0 and end/loop-end `$095A`, exactly the 2,394 recorded
bytes. Root-key C set object `+$AA=$3C` through `$FFBF10/$FFC118`. Firmware
then completed the parent/keymap structures and ES5506 fetched the exact range
2,395 times while CPU reads remained zero (256/256 retained value matches).
See `investigations/record-completion-analysis.md`.

**Stereo allocator classification, 2026-08-23.** `$7CE510` is now
`[Verified firmware heap]`, specifically the free-remainder header calculated
inside instrument `$02B600`'s nested heap `$02B890-$F70A00`. ROM's packed
allocator resizes first WaveSample `$02C0D0`: planned per-channel extent
`$0BD6=$7A2310`, plus `$120`, rounds to `$7A2440`; therefore
`$02C0D0+$7A2440=$7CE510`, with expected free remainder `$7A22C0`. Neighbor
blocks contain `2 HALL REVERB`, two `UNNAMEDLAYER` records, and `UNNAMED WS`,
showing that this is the ordinary instrument/object heap rather than a stereo
register or separate pool.

The service manual verifies 2/4/8/16 MB hardware configurations and ROM
`$F8A166-$F8A244` verifies a real alias-based RAM sizing algorithm. In the
current model it selects the maximum branch, but exact board decode at
`$7CE510` lacks schematic/bus proof: `[Likely hardware RAM in 8/16 MB
configurations]`, not unconditional `[Verified hardware RAM]`. Stereo object
creation remains `[OPEN runtime]`: the two companion layer records and first
WaveSample exist, but the first extent split fails before companion continuation
`$0174F4`. See `investigations/record-stereo-allocator-analysis.md`.

**RAM-decode closure for the stereo stop, 2026-08-23.** The current model's
ROM probe locations `$008000/$408000/$808000/$C08000` are four independent
two-word shadows. ROM therefore observes `D4=0`, `D5=$2222` and selects base
`$000000`, size `$F80000`, but the CPU map does not back that interval
coherently. `$7CE510` lies inside the resulting firmware heap and outside all
mapped RAM windows: both allocator writes are discarded and the immediate
read returns zero. This model-level RAM decode/backing mismatch is `[Verified
current model]`; the exact physical board decode remains `[OPEN hardware]`.
LEFT and L+R use the same `$120` WaveSample metadata layout, and the L+R path
never reaches continuation `$0174F4`, so an alternate stereo-object path does
not explain the failure. See
`investigations/stereo-ram-decode-analysis.md`.

## ES5510: what firmware asks for, before anything is turned on (docs/asr10/investigations/keyboard-and-sample-bridge-11.md)

**Factor of two, parked verbatim, per instruction — not investigated
further this task**:

> Vid verkliga hårdvaruvärden — CLKIN 15,238 MHz, utgångstakt
> 29 762 Hz — producerar MAME:s ES5506 halva tonhöjden mot vad riktig
> hårdvara producerar för samma FC. FC- och ACCUM-bredderna matchar
> databladet. Vår dubblade klocka kompenserar exakt. Orsaken är
> okänd.

Measured ES5510's host-register traffic (device still
`set_disable()`'d) across a full boot->load->select->play run: Host
Control (offset `0x12`) is polled 6,705 times from three call sites,
every single read returning `0`. The real ES5510 datasheet
(`docs/ensoniq/ES5510.pdf`) confirms this is the *correct* direction
for a stub to fail in -- "Host Access OK/" is active-low, so `0` means
ready, not busy. Firmware's real handshake degenerates to "always
immediately ready," which is exactly why nothing ever hangs waiting
on it. Separately, a genuine 160-position sequential write-select
sweep was measured on the combined GPR+INSTR select register --
matching the datasheet's own documented INSTR address range
(`$00-$9F`, 160 addresses) and maximum program length ("64 to 160
microinstructions") exactly. **A real, complete DSP program is being
downloaded**, even with the device disabled and never executing it.

Clock: `XTAL(10'000'000)` gained real (if partial) support --
the datasheet's own VDD spec cites "< 100mA @ 10MHz clock" and its
timing tables cover 8/10/12MHz grades; `esq5505.cpp` turns out to
contain *two* different clocking patterns, and the second (flat,
undivided `10_MHz_XTAL` shared by M68000/ES5510/ES5505/DMAC, ES5510
disabled) matches this driver's own choices more closely than the
crystal-halving pattern cited previously. Still `[OPEN]` which grade
or crystal the real board uses.

Scratch-enabled ES5510 (built, measured, reverted via `git checkout`,
never committed): does **not** hang MAME -- `button.lua`'s load
sequence fails with the already-documented, already-understood
`EFFECT DOWNLOAD FAILED`/`ERROR 032` path (`filesystem-browser-map.md`
§4.20-4.24), a graceful firmware error, not a lockup. `note_audio`'s
shorter sequence still passed with unchanged pitch/amplitude.

**Decisive structural finding**: `es5510_device` is a `cpu_device`,
**not** a `device_sound_interface` -- no `sound_stream`, no
`add_route()`, nothing wiring it into MAME's audio mixer at all, in
this driver or the device class itself. Enabling it cannot change what
is heard on the notes this project currently tests, regardless of
program content -- there is no path for its output to reach the
speaker yet. This is a real argument for prioritizing other work over
extending ES5510 wiring further, not just an assumption.

## The pump: a real missing block, but it doesn't explain factor two (docs/asr10/investigations/keyboard-and-sample-bridge-12.md)

Confirmed directly from `esqpump.cpp`/`.h`: `esq_5505_5510_pump_device`
**is** a `device_sound_interface` (its own `sound_stream`), owns a
`required_device<es5510_device>`, and calls `m_esp->run_once()` once
per output sample when not halted -- the pump, not ES5510 itself, is
what would own the audio stream and drive the ESP. Our own driver
routes ES5506 straight to `SPEAKER`, skipping this block entirely;
`esq5505.cpp` (both of its board patterns) always inserts the pump.

Mapped **both** `esq5505.cpp` clock patterns this time (a
self-corrected method gap -- see below): pattern A (VFX family) runs
M68000/ES5510/ES5505 all at a flat `10_MHz_XTAL`, pump at
`10MHz/(16*21)`; pattern B (SD-1/32-bit family) halves a named
`30.47618_MHz_XTAL` for M68000/ES5505 but keeps ES5510 on a
*separate*, undivided `10_MHz_XTAL`, pump at
`30.47618MHz/(2*16*32)`. **Both patterns' pump lands on the identical
29,761.9Hz** -- matching ASR-10's documented mode -- via completely
different crystal paths. ES5510 is `set_disable()`'d in *both*
patterns in upstream MAME's own driver, matching this project's own
choice.

**Does the pump explain factor two? Reasoned answer: no.** The pump's
own `clock()` sets only its *own* output stream rate
(`device_clock_changed()`); MAME's sound core resamples between
differently-rated connected streams by design (`sound.h`'s own
documented behavior, backed by a real `audio_resampler` class) --
resampling preserves pitch, it doesn't correct it. Pitch is set
entirely by the oscillator's *own* clock via the same
`clock/(16*(voices+1))` formula used throughout this project. A halved
ES5506 clock (matching the datasheet-compliant/real-hardware
hypothesis) would still produce audio an octave low internally,
regardless of any pump resampling it afterward to 29,762Hz. **The
factor-of-two statement stands exactly as parked, unresolved.**

Del 4 corrected an over-precise carryover: the specific hypothesized
`EFFECT DOWNLOAD FAILED` mechanism (`ffc896: cmpi.l
#$fff9bca0,$e8e.w`) was measured directly (scratch-enabled ES5510,
`button.lua`'s exact failing stimulus) and **that compare never
executes** in the failing run; `$e8e.w` actually *does* reach the
expected sentinel value there. The real trigger for the failure
remains genuinely `[OPEN]` -- not resolved, corrected rather than
confirmed.

**Method note**: this is the third time an `esq5505.cpp` clock pattern
was cited as *the* pattern when the file contains several -- a partial
reading of a reference file is the same failure class as a tap without
a live witness. Grep for all machine configs before citing one as
representative, going forward.

## Open questions

Prioriteringen och evidensdomänerna är normerande i
`reference/handoff-2026-08-23.md` sektion C:

1. Stereo recording: mono `[Verified]`, L+R `[OPEN runtime]`; klassificera
   modellens RAM backing/decode runt firmwareheap `$7CE510`.
2. Physical ADC routing: firmwarekedjan `[Verified]`, board routing `[OPEN]`.
3. Keybed link: extern 80C52/tvåtrådsevidens finns, digital terminering `[OPEN]`.
4. Factor two: `[OPEN]`; pumpen är falsifierad som lösning.
5. E2/address model: stickprov `[Verified]`, generell mirrorhypotes `[OPEN]`.
6. PB9/PB10/PB11: `[OPEN hardware]`, ej blockerande.
7. ES5510: separat framtida implementationsgren, ej påbörjad.
8. Övriga avgränsade frågor: fysisk bank-1-dekod, generell CMR-tabell,
   three-line synchronous-länken, CS1, Timer 2 och `$F95EAA`.

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
- `reference/methods-hypothesis-management.md` — normative hypothesis requirements,
  evidence status, falsification criteria, and revision trail.
- `investigations/irq1-storage-completion-probe.md` — first IRQ1 wiring
  attempt: chip-level vector fact kept, unconditional board policy
  disproven and reverted.
- `investigations/irq1-imr-unmask-probe.md` — "IMR gates IRQ1" disproven by
  measurement and by chip architecture; also documents a Lua tap-timing trap
  on the MC68302 internal window.
- `investigations/irq1-vector-and-sr-probe.md` — confirms the ERROR-129
  failure fetches the correct vector `$51`; documents a second, distinct
  Lua tap-lifetime trap. Its SR-mask/timing framing for *why* the failure
  happens is superseded by `irq1-handler-chain-probe.md`'s direct
  root-cause measurement.
- `investigations/irq1-handler-chain-probe.md` — the naive wiring's
  `ERROR 129` is a genuine 68000 Address Error from an uninitialized
  `$0402` continuation pointer, not a timing or masking problem.
- `investigations/ready-line-artifact-probe.md` — current authoritative
  account: the interrupt that crashed the chain above was never a storage
  completion, but a MAME FDC-model ready-line artifact. Fix landed
  alongside the IRQ1 wiring; the full completion chain runs correctly
  through RECALIBRATE/SEEK/READ DATA and stops at a genuine IDMA overrun.
- `investigations/idma-register-map-probe.md` — measured IDMA register map
  (CMR/SAPR/DAPR/BCR/CSR/FCR) for one concrete READ DATA request, derived
  from real writes, not a datasheet guess; corrects an earlier static DAPR
  prediction.
- `investigations/idma-implementation-plan.md` — minimal MC68302 IDMA,
  implemented and landed: register storage, per-DRQ transfer, terminal
  count; corrects an IMR bit-11 arithmetic error that would have wrongly
  concluded vector `$4B` was needed; measured result: `DISK ERROR - LOST
  DATA` gone; four known limitations of this slice journaled (direction-
  locked FDC→memory only, `BCR-1` unverified beyond one sector, `$FC5803`
  a `mem_map` gap not an implementation gap, `DAPR` confirmed honored).
- `investigations/disk-not-responding-probe.md` — diagnoses the failure
  that followed minimal IDMA: READ DATA's own completion interrupt never
  fired (reentrant `tc_w()` call, hypothesized here); refutes the standing
  FDC-rate hypothesis by source (already forced to 500kbit/s) and by
  magnitude (a rate difference is ~8ms, the observed gap was ~5000ms).
- `investigations/tc-reentrancy-probe.md` — confirms the reentrancy
  diagnosis by measurement (INTRQ genuinely never asserted, not just
  undelivered — `main_phase` stuck in `PHASE_EXEC`, 576 open mask windows
  with zero IACKs), grounds it in `upd765.cpp` source, and fixes it:
  `tc_w()` alone deferred to a zero-delay timer. Result: `DISK NOT
  RESPONDING` gone, instrument-load reaches `FILE LOADED`. Argues for
  (but does not land) moving the whole per-DRQ transfer to timer context.
- `investigations/file-loaded-verification-probe.md` — independently
  verifies `FILE LOADED`: 21 IDMA arms / 337 sectors / 172,544 bytes,
  measured from SIB register taps, destination entirely in low RAM,
  19/21 transfers byte-identical to the source disk image (2 explained as
  a reused scratch buffer, not corruption). Adds a 6th regression test
  that checks the exact byte count, not just display text. Also documents
  post-load observation: ES5506/ES5510 register traffic is continuous and
  richly varied from near-reset onward, independent of the load and of any
  button press — disproves the "never reaches voice registers" and
  "ES5510 stuck at zero" concerns by measurement.
- `investigations/sample-topology-closure.md` — closes the shared-RAM
  question numerically: voice 0's own `START`/`END`/`ACCUM` convert to
  word addresses inside `$00000-$7FFFF`, the exact range corresponding to
  `$100000-$1FFFFF`, confirming the hypothesis with a measured value, not
  just architectural reasoning. Searched the tree for the real
  CPU-RAM-to-ES5506 sharing idiom (`esq5505.cpp`, `esqkt.cpp`, and
  upstream's own `esqasr.cpp` ASR-10 skeleton) and found **none exists
  anywhere** — `esqasr.cpp` itself uses a static `ROM_REGION(...,
  ROMREGION_ERASE00)` placeholder, not shared RAM. Landed the fix anyway,
  as a deliberate new construction verified against MAME's own
  `memory_share` size/width/endianness validation before writing it:
  `mem_map`'s `$100000-$1FFFFF` and `es5506_wavetable_map`'s bank-0
  `$000000-$07FFFF` now share one allocation via a root-relative
  `.share(":asr10_sample_ram")` tag (the narrow, named `mem_map`
  exception the standing rule allows), plus `SPEAKER`/`add_route` output
  wiring (channels/clock/bank untouched). Regression 6/6 before and after.
  `-wavwrite` capture post-load: complete silence (`peak=0`, every
  sample) — expected, not a failure, since no keyboard model exists yet
  to trigger a note; the wiring is unverified audibly until that exists.
- `investigations/sample-ram-and-voice-registers.md` — maps the sound
  path: `$100000-$1FFFFF` gets writes only from a one-time pre-`FILE 1`
  boot sweep, never during/after the load (witnessed, not a dead tap).
  Source reading settles why: the ES5506 reads samples through its own
  private per-device address space, never the CPU's — `mem_map`'s
  `$100000-$1FFFFF` and the device's own bank-0 `.ram()` are two
  unconnected allocations in different units. Voice-register decoding
  (all 32 voices) finds voice 0 uses the one populated bank (0); all 31
  others point at bank 1, which is entirely unmapped (`.noprw()`). No
  `SPEAKER`/`add_route`/`set_channels` exist in the driver at all; family
  precedent (`esqkt.cpp`, same chip) uses the same 16MHz clock this driver
  guesses. Remediation order sketched, not built.
- `reference/boot-sequence.md`, `reference/subroutine-index.md`,
  `reference/os-code-extraction.md`, `reference/hardware-map.md` — as before, updated.
- `static/README.md` — what the raw material is, how it was generated, what it does not
  prove.

## Display protocol: cursor/underline and annunciators (docs/asr10/investigations/display-protocol-inventory.md)

**[Correction 2026-08-26]** The following paragraphs describe the 2026-08-24
checkpoint. The THRB CPU byte address is `$FC4817`, not `$FC480D`; `$74-$76`
remain an `[OPEN]` one-operand panel-control/output family rather than a
verified animation family; and later live capture verifies `$62/$63`
selected-field state. The current result and fault boundary are in **Current
display protocol boundary** above and
`investigations/display-protocol-state-machine-v350.md`.

**[Verified runtime]** The panel display byte stream (DUART channel B
THRB, `$FC480D`) was traced end to end and inventoried across boot,
load, note-press and menu navigation. `esq1x22_device::write_char()`
(`src/mame/ensoniq/esqvfd.cpp`) previously handled only clear (`$66`)
and printable text (`$20-$5f`); every other byte hit an unconditional,
un-aggregated `printf` (the "Unhandled control code NN" noise every Lua
probe's output has carried all session). Implemented: a 2-byte
field-attribute opcode (`$60 <attr>`, `attr&0x02` = underline) plus two
field-boundary markers (`$62`, `$72`), driving the underline output
(`"vfd22"-"vfd43"`) that already existed in the shared
`esqvfd_device::update_display()` and was already read by
`asr10_panel.lay` but never driven. Verified against the manual's own
"cursor (underline) beneath the field" description and against two
independent screens (REC SRC, FX Select); regression-locked
(`docs/asr10/lua/display_protocol.lua`, 12th test).

**[Verified narrow]** One annunciator bit (`$77` bit 0) confirmed
reversible for Instrument-1 select/deselect in isolation; the other 39
candidate bits are wired raw (`asr10_annbit0`-`39`) and rendered but
`[OPEN]` semantically. An aggregated, first-occurrence-per-code alarm
(`osd_printf_error`) now reports every unrecognized display control
code instead of staying silent or flooding — see
`reference/display-protocol.md` for the full code table and what
remains `[OPEN]` (`$74`/`$75`/`$76`'s animation family, `$E7 $71` and
neighbors, two more small clusters).

**[Superseded 2026-08-24]** REC SRC Field 1 was unreachable at this
checkpoint because Left/Right had not yet been identified. The later
partial-update investigation verifies `$10`=Left and `$11`=Right and
round-trips between Field 1 and Field 2. This closes that particular
navigation gap; it does not make the complete cursor/parameter/value-editing
UI model verified.

## Panel controls: matrix survey and transport gap (docs/asr10/investigations/panel-button-and-transport-map.md)

**[Verified runtime]** Every one of the 64 raw button codes (`$00`-`$3F`)
produces real, distinct firmware dispatch and returns cleanly to the
scheduler idle loop — the wire protocol (two-byte `(0x80|code, 0x00)`
press frame, `(code, 0x00)` release, both sharing the display's own
THRB/RHRB register slot) works correctly across the whole code space,
not just the small set of previously-confirmed codes. Full sweep:
`static/button-routine-sweep-v350.csv`.

**[Verified runtime]** `BTN_0C`/`BTN_0D` are **not** Left/Right Arrow —
they reach real, distinct, working handlers for an unrelated menu
category (Audio Track utilities: COPY/ERASE/FILTER/SHIFT), not a cursor
move. The pilot keymap's `KEYCODE_LEFT`/`KEYCODE_RIGHT` bindings on
those codes were wrong, not just unverified, and have been removed
(`esqpanel.cpp`). **[Superseded at this historical boundary]** The real
Left/Right and transport codes were still `[OPEN]` in this round; later
runtime work verifies `$10`/`$11` as Left/Right, `$1D` as Play in the
loaded-sequence context, and `$17` as Stop/Continue during playback.

**[Verified runtime]** Simultaneous button holds (needed for "hold
Record, press Play") already work correctly at the infrastructure
level: two buttons held together generate four distinct, correctly-
ordered wire events, not a merged/ghosted pair. Nothing was broken here;
verified and regression-locked (`docs/asr10/lua/panel_input.lua`, 13th
test).

**[OPEN]** The manual's sequence-recording procedure stops at "hold
Record, press Play" — Record/Play codes are unidentified, so the
procedure cannot proceed past selecting a loaded instrument. This is a
more fundamental blocker than Left/Right Arrow's own (separately
confirmed) failure, since the sequencer flow never reaches the step
where Left/Right would matter.

**[OPEN]**, marked visually now, not just in prose: 39 of the 40 wired
annunciator bits have no confirmed meaning. `asr10_panel.lay` renders
them dim/desaturated with an explicit "OPEN, UNKNOWN MEANING" label,
distinct from the one confirmed lamp (instrument-1-select), so an
unidentified lit bit doesn't read as confirmed information.

## Left/Right Arrow and Up/Down confirmed; a real partial-update rendering bug found and fixed (docs/asr10/investigations/partial-update-position-probe.md)

**[Verified runtime]** A real display bug, not just an open question:
partial field updates (a value changing without a full-screen clear)
had no cursor-position mechanism, so a changed value got appended after
the old one instead of overwriting it (measured live: `"VOLUME=99"` ->
appended garbage across repeated Up/Down presses). Root cause found by
diffing full-redraw vs. partial-update byte streams: a standalone byte
`$00`-`$1F` sets the write column directly (`$14`=column 20, an exact
match to where `"VOLUME="`'s digits start; corroborated by a follow-up
sweep observing values `$00`-`$0F`/`$15`, all within the valid 0-21
column range). Implemented in `esq1x22_device::write_char()`; the same
fix also resolved a second, previously-misread symptom (a field-switch
redraw that silently wrote off-screen for lack of a cursor reset).
Downgrades the prior task's full-redraw character-placement validation
to `[Verified, coverage: full-redraw]` — it never covered this case.

**[Verified runtime]** Left Arrow = raw `$10`, Right Arrow = raw `$11`
— measured using the display's own underline output as ground truth
(REC SRC Field 2 <-> Field 1, both directions), not guessed from the
pre-existing ROM raw->mapped table alone (that table only supplied the
candidate shortlist). Implemented as `KEYCODE_LEFT`/`KEYCODE_RIGHT` on
`BTN_10`/`BTN_11`. Separately, `$0A`/`$0B` (Up/Down) turned out to have
their keyboard shortcuts swapped relative to the measured effect (`$0A`
is genuinely Up, `$0B` genuinely Down) — fixed; the wire-level codes
and their effect were never wrong, only which computer key triggered
which.

**[Superseded transport search]** Record/Stop•Continue/Play were tried as a state machine (hold A,
hold B while A held, release both) across the codes with no visible
single-press effect from idle — no combined effect found. The
sequencer-status state variable to trace was not localized. One useful
side finding: raw `$15` reaches a genuine sequence file listing (`"FILE
9  TUTORIAL SEQ"`), a strong Seq•Song category-button candidate.
LOAD's blink was determined panel-local (zero display-channel traffic
during a 3-second idle window), which puts the 40 annunciator bits'
"simple on/off" reading specifically in question — no blink-mask
encoding was identified, so no reinterpretation was implemented; they
remain `[OPEN]`. The negative transport result was scoped to states with no
loaded, playable sequence; later context-correct runtime measurement verifies
`$1D` Play and `$17` Stop/Continue.

## Bank loading as sequencer context; transport still open at this historical boundary (docs/asr10/investigations/bank-loading-and-transport-context.md)

**[Verified runtime]** `$15` is the Seq•Song category button (confirmed,
not just candidate): reliably shows `TUTORIAL SEQ`'s file listing
(`"FILE 9  TUT0RIAL 5EQ  "`); loading it (`$15` then `$23`, generous
settle — the disk-completion transition is timing-sensitive, not
instant) reaches `"DI5K C0MMAND C0MPLETED"` reproducibly. `$15` and
`$20` (Sample•Source Select, confirmed prior task) now have mnemonic
keyboard bindings (`KEYCODE_Q`/`KEYCODE_S`) alongside `$10`/`$11`'s
existing `KEYCODE_LEFT`/`KEYCODE_RIGHT` — `KEYCODE_S` collides with the
note-typing keyboard's C# key, documented rather than silently avoided.

**[Verified runtime]** `$17`, in a specific 23-button preceding
navigation context (`$00`-`$16` pressed in sequence), is the exact,
prefix-isolated trigger for a guard state ("CREATE NEW SEQUENCE";
blocks `$20` with `"STOP SEQUENCER FIRST"` until resolved) — most
likely the manual's own "Create New Sequence" command, not a dedicated
transport button (211-byte memory diff on the trigger press matches
allocating a real sequence object, not a flag flip).

**[Superseded, bounded negative], with substantially wider negative evidence than the prior
task**: Record/Stop•Continue/Play were not found in the states this task
could create. This task's search added bank-loaded
context, prefix isolation, a 650-pair hold-combo sweep across every
code with zero single-press effect anywhere tried, a second 110-pair
sweep, correlated PC/state-variable tracking (both previously-tried
candidate variables, `$016F`/`$0D04`, never move), and a full memory
diff — all negative. `$26`-`$3F` (26 codes) show zero measured effect
in every context and combination tried. Two live possibilities:
Load/Command/Edit mode buttons may need to be part of the combination,
or the missing condition may be a loaded playable sequence. The later
transport investigation demonstrates the latter: `$1D` and `$17` become
functional in the correct sequence state. The negative result here never
established that transport was absent.

**[Verified, source-level]** `asr10panel_device` implements no blink
mechanism at all (no timer, no light-state attribute) — confirmed both
by code inspection and a live 6-second, 40-bit output poll (zero
changes). A sibling class in the same file, used by other Ensoniq
panels, does implement blinking as a separate light-state attribute
(not a redefinition of on/off) driven by a panel-local timer — the
strongest available evidence for which of the three Del 5
interpretations the real protocol likely follows, though it's evidence
by analogy from a different device class, not a direct ASR-10
measurement.

## The sequencer's own clock, traced from allocation to dispatch; transport reframed as a consequence, not a code (docs/asr10/investigations/sequencer-clock-and-service-menu.md)

**[Verified runtime + static]** Tracing backward from `$17`'s
211-byte allocation (rather than sweeping more button codes) found a
real, disassembly-confirmed chain: `$17` ("Create New Sequence") sets
`$000B70` (a tempo/step value) from `0` to `$5A`, and `$000B6E` (a
phase accumulator inside the already-documented `irq6_tick_producer`,
`$F88300`) then actively accumulates it, wrapping at `$271` (625) to
fire a type-`$E` event through the jump table at `$67AC`
(`jumptable_dispatch_15entry`, `$00740C` — this task live-disassembled
its indexing arithmetic for the first time: `index*2`, not `*4`, table
base `+$67AC`) to handler `$F8C588`, which writes byte `$000F58`. This
is the sequencer's own clock-divider mechanism, switched on by
allocating a sequence — not the transport itself. **`$00017E`** gates
which of two clock paths runs (three-way: negative/zero/positive) and
is structurally shaped like a run-state selector, but was measured at
`0` throughout this entire session and never observed to change from a
button press — a strong candidate, not a confirmed flag.

**[Verified runtime]** `$21` = Cancel•No, tested against a genuine
confirmation-screen signature (Enter/Yes advances into a sub-step from
`"CREATE NEW SEQUENCE"` to `"NEW NAME?SEQUENCE ??"`; Cancel/No backs
back out of it). Supersedes the prior `$22 [Likely]` tag, which shows
no effect on the same screen.

**[Verified, corrected mid-task]** The ROM contains a real factory
diagnostic string table (`GPR MONITOR`, `INSTRUCTION MONITOR`,
`A/D TO D/A`, `ESP TESTS`, `RAM TEST1/2 FAILED`, etc.) at file offsets
`$1000`-`$1700`/`$53E0`-`$7CB4`, distinct from the service manual's
jumper-only keypad self-test mode (which isn't modeled at all — no
keypad-board CPU device exists in this driver). Not reached by any
single or paired button held at boot, nor by three rounds' worth of
COMMAND-mode navigation sweeps. An initial read-tap finding ("the
string is touched during normal boot") was traced to address-range
aliasing between ROM (early boot) and unrelated RAM structures
(`trap3_enqueue`'s queue pool, already documented) at the same
addresses once `cs0_covers(0)` goes false — retracted before being
reported as a real result, not left standing.

**[Verified, no bug found]** `esqpanel_device::set_button()` sends a
`$00` second byte unconditionally for every one of the 64 raw button
codes; the documented `$FFB392`/`$FFB20A`/`$FFB0E0` second-byte
classification (already how the ROM tells key events from button
events) is correctly and uniformly exercised by every `$00`-`$3F`
code. `$26`-`$3F`'s silence is a real property of those button IDs,
not a framing/classification bug — no `asr10panel_device` change was
warranted or made.

**[Historical negative, superseded by context-correct transport
measurement]** First-byte transport search appeared exhausted across
three rounds of investigation: single/paired-button sweeps from
multiple contexts, a 650-pair and a 110-pair hold-combination sweep,
prefix-isolated binary search, correlated PC/state-variable tracking,
a full memory diff, and (this round) forward disassembly from that
diff through two real subsystems to a named byte that was never
observed to move. Those contexts still lacked a loaded playable sequence;
later work verifies `$1D` Play and `$17` Stop/Continue in that state.
Record and the service menu's entry condition remain `[OPEN]`.

## The tempo chain closed two levels deeper; a real crash found and isolated (docs/asr10/investigations/tempo-clock-consumer-chain.md)

**[Verified runtime, independent method]** Tick rate is exactly
`1000.0000 Hz` (wrap-counting `$000B6E` over a precise window: 2000
ticks / 2.000000s), and the default pulse rate is exactly `144.0000 Hz`
(`1000×90/625`) — both measured, not just derived from the DUART-timer
math. `$000B70` is not independently settable: a correctly-persisted
write-tap shows it reasserted from `$00828E` roughly once per pulse
(`$F919D2: move.w $828e.w,$b70.w`) — explains, rather than refutes, why
a direct-poke proportionality test didn't show the expected scaling.

**[Verified runtime, disassembled]** The pulse (`$000F58`, set by
`$F8C588`) has two real consumers: `$00E66E` (a second clock-division
stage, dispatching into scheduler slot `$DA` at ~83Hz) and `$0073A8`
(a mainline poll/consume loop that, when no pulse is pending, services a
`$00D42` linked list via `$00E68E`, touching MC68302 PIO-region
addresses — shaped like MIDI-clock transmission). Neither consumer
touches sequence-event data; the real event-consumer for `TUTORIAL

**[OPEN, prefetch-osäkert — §8.10 audit]** The `~83Hz` figure for
`$00E66E` and the positive-execution claim for `$0073A8` above rest on
`tempo-clock-consumer-chain.md`'s own read-taps with no PC-correlation
anywhere in that file (checked directly: zero `PC`/`CURPC` references
in the source). `$00E66E` is reached via a `jsr` from `$007822`, not
by falling through past an unconditional branch the way `$782A` was —
structurally less exposed to that specific adjacent-branch pattern,
but that is a plausibility argument, not a measurement, and §8.10's own
criterion is documented PC-correlation or equivalent, which neither
finding has. Both stay `[Verified runtime, disassembled]` for the
disassembly itself (the routines' existence and shape are static-read
facts, not at risk); only the live-execution/frequency claim is
downgraded, pending re-measurement with `asr10_taps.lua`'s
`pc_correlated_read_tap()`.
SEQ`'s own allocated object remains `[OPEN]`.

**[Self-correction, recorded]** Several early taps this round returned
silent zeros from a real methodology bug, not a hardware finding: tap
handles whose return value wasn't saved to a persisted variable were
garbage-collected almost immediately (SS8.6, already documented in this
project's own methods reference) — caught mid-task via a direct
before/after comparison, not left standing as a false result.

**[Found, not fixed, out of scope]** A real, reproducible `SIGSEGV`:
installing a tap on `$008E50` while a bank and a created sequence are
both active crashes deterministically at `t≈0.72s` into a wait;
isolated to the tap itself (the identical button sequence without the
tap completes cleanly). Neither of the two documented voice-allocation
addresses (`$008E50`, `$00F8CAFA`) could be validated with a working
positive control even against a confirmed-real note (1140 ES5506
writes) — the voice-allocation appendix question is `[inconclusive]`,
not answered either way.

**[OPEN, corroborated]** The GPR MONITOR/ESP TESTS diagnostic menu:
Per's own recollection of its contents (reference DC level, ADC/DAC
test, MIDI loopback, ~6-8 tests, one hanging the machine) independently
matches the ROM string table found last round, but neither of his two
best-recalled entry combinations (`$06`+`$0D`, `$05`+`$0D`, both
pre-existing unverified layout labels) reproduces it in this emulation.

## Command pages catalogued; three predicted numbers didn't hold (docs/asr10/investigations/command-pages-and-clock-verdict.md)

**[Verified runtime]** `$06` = Command, confirmed exactly as Per
described (`$06` then `$0C` → `"NO COMMANDS ON PAGE"`, meaning Command
mode is reached, that page just has none). Pages browse cleanly with
`$11` (Right) to a confirmed wrap. **Nine full category catalogs**
walked and recorded verbatim (INSTRUMENT×8, SEQ*SONG×13,
SYSTEM/GLOBAL×19, EFFECTS×2, TRACK/EVENT×9, PITCH TABLE×4, DISK/
SYSTEM×15, WAVESAMPLE PROCESSING×7, DATA EDITING×7, WAVESAMPLE
CREATION×15) — `GPR MONITOR`/`ESP TESTS` appear in **none** of them;
the diagnostic menu's entry stays `[OPEN]`, narrowed rather than found.

**[Retracted]** `$00828E` as the tempo master (prior round's
disassembly-based claim): a persisted write-tap shows it last written
at `t≈18.4s` (boot-time, value `$81B0`) and never again, while
`$000B70` is independently confirmed rewritten to `$5A` from the same
caller PC well after that — `$5A ≠ $81B0`. The true source is
`[OPEN]` again; not renamed, per this task's own rule against naming
an unmeasured variable.

**[Verified runtime, two independent methods]** The predicted 36Hz
MIDI-clock output was not measured — instead, `$000F58` was found
**constant at `$00`** across 15,000 direct 200µs-resolution samples
(3.000000s) even with the accumulator confirmed actively running
(`$B70=$5A`, `$17E=0`), and a persisted write-tap on the decrement
instruction (`$00E674`) recorded zero writes over repeated 5-second
windows. The second clock-division stage is gated shut in every state
this session reached — informative on its own (something not yet
triggered arms it), not a refutation of the 144Hz measurement.

**[Verified runtime]** The `$000D42` list (`$0073A8`'s consumer target)
is **read-verified empty** at idle, after loading/creating
`TUTORIAL SEQ`, and after a full `$00`-`$3F` sweep — Outcome 2 per the
task's own framework: a real but unrelated service list (touched only
by boot-time keyboard tuning, `$F8CD04`), not the sequencer's event
dispatch. Ruled out, not confirmed by absence of counter-evidence.

**[OPEN]** The sequence object's own field map and read/event pointer:
24 candidate addresses from the original 211-byte diff were polled:
several change continuously, but `$000C38` is already documented (this
session) as part of `$F88F60`'s general 4kHz hardware-scan loop, and
the rest show no monotonic, event-count-shaped pattern. No stepper
found. A `SIGSEGV` interrupted this specific polling window at
`t≈6.26s` — a second reproducible-looking crash context on record
(after last round's `$8E50`-tap crash), neither investigated, both
flagged for a dedicated future task.

**[Tooling]** `docs/asr10/lua/lib/asr10_taps.lua`: wraps
`install_read_tap`/`install_write_tap` so every handle is
automatically persisted (the SS8.6 mistake bit this project's own
scripts twice; this makes forgetting structurally harder, not just
documented against). Wired into `asr10_regression.lua` as `M.taps`.

## The `$000F58` contradiction resolved by execution trace; a real, independent sequencer stepper found (docs/asr10/investigations/execution-traced-clock-and-sequencer-stepper.md)

**[Verified runtime, resolved]** `$F8C588` is **never executed**
(0 hits over 3s, against a validated `$F88300` positive control at an
exact 1000.0000Hz). The producer chain runs exactly as measured through
`trap #9` (144Hz at every step), but installs into **Slot 3**
(`$D6=$2438`), not Slot 2 — Slot 3's real resident task is `$F8F2FA`,
confirmed executing at 144Hz. The `$67AC`/`jumptable_dispatch_15entry`/
`$F8C588` path was an unverified inference carried over from an
unrelated (keyboard) investigation and is retracted, not softened.
`$F8F2FA`'s own per-type dispatch (table at `$8258`) was traced to a
`jmp` that fires at 144Hz, landing near `$006014` — not fully resolved.

**[OPEN, prefetch-osäkert — §8.10 audit]** `$F8F2FA` executing at 144Hz
itself stays `[Verified]` — that leg is trap-based (`TRAP #9`/`#C`
register capture, `A1` matching Slot 3's own header), which is
inherently execution-confirmed and not exposed to instruction-prefetch
false positives (a TRAP exception only fires if the TRAP instruction
itself actually executes). The second leg — the `jmp (a0)` landing near
`$006014` — has no PC value shown for that specific tap anywhere in
`execution-traced-clock-and-sequencer-stepper.md`, unlike this same
file's other 144Hz/1000Hz claims (`$00F902D8`, `$F88366`), which do
show explicit PCs. Downgraded pending a `pc_correlated_read_tap()`
remeasurement.

**[Verified runtime, new lead]** A real, independent sequencer-stepper
candidate: `$00F902D8` executes at exactly the 144Hz pulse rate,
anchored on address `$001098` (register-captured, distinct from the
six-record cluster it sits near), chaining to a second handler
(`$00F91F00`) that receives that same address. Ends in a previously
undocumented `trap #c`. The strongest event-consumption lead this
investigation has produced across every round — named with real PCs
and registers, not claimed as the fully-resolved sequence object.

**[Verified runtime, narrow]** `$00E66E`'s gate (`$000F58`) is
measured to be **MIDI-clock-only**: its only two readers anywhere are
the already-characterized MIDI-clock consumers; Del 2's stepper chain
never touches `$000F58` in the same window. Not unified with anything.

**[Downgraded]** `$00017E`: both writer (`$FB8ABE`, boot-only) and
reader (`$F88366`, `$F88300`'s own internal branch, 1000Hz, exclusive)
are now identified — and both are boot-time/internal-only, with no
connection to user input or either measured chain. Retired as a
transport-flag candidate rather than promoted, per the user's own
caution against a second "mask-is-the-gate" promotion.

**[OPEN, structural]** The diagnostic menu's strings were tapped for
reads from `t=0` through boot; the range is confounded by the same
ROM→RAM overlay switch (`cs0_covers(0)`) already documented in the
prior round's own retraction — an address-based read-tap after the
switch cannot distinguish the ROM string from whatever RAM structure
now shares its address. No new backward trace achieved; not chased
further, the confound is structural.

## TRAP #C identified as Slot 3's own self-rearm; the real note path found elsewhere entirely (docs/asr10/investigations/trap-c-and-the-real-note-path.md)

**[Verified runtime]** `TRAP #C` (`$F88174`, cross-validated against
the live vector table alongside `#3`/`#4`/`#9`, which match this
project's own prior addresses exactly) is a generic queue-append
primitive, same shape as its siblings. Its own data during the tempo
chain: `A1=$002438` (Slot 3's own header), node type `$0E` with a
visibly decrementing countdown field — **Slot 3 re-arming itself**,
not a handoff of sequence content to a new destination. Called at
176.33Hz overall (higher than the 144Hz pulse), confirming multiple
unrelated callers share this primitive.

**[Verified runtime, address later corrected]** Tracing backward from
confirmed-real ES5506 writes during an actual `KEY_C` press (matching
`note_audio.lua`'s own proven pattern) localized a code block at
**`$007C7C`-`$007CEE`** that reads a mode byte from `$11C(a3)`,
conditionally transforms data, and writes `$FC2001`-window registers
from `$15(a4)`/`$2A(a4)`. **This PC region
never once appears in any `TRAP #9`/`TRAP #C` register capture** — the
tempo/clock chain and the real note-to-voice path are disjoint
subsystems, not one pipeline with a gate in the middle. This revises
prior tasks' framing: there is no single gate to find between the two;
they were never connected.

**[Corrected — see "Slot 5, PC-correlated, and the $007C7C/$007E24
correction" below]** The block's real, PC-confirmed entry point is
`$007CA8`, not `$007C7C` — the two are 44 bytes apart within this same
range, and only `$007CA8` is ever seen with `PC` matching it. The block
identity, disassembly, and behavior described here are unaffected;
only the specific entry address is corrected.

**[Corrected, not resolved]** `$001098` as memory content is static
zero (4 boot-time writes only); the *register value* `$1098` used by
`$F902D8`'s own `A4` was not traced to its load source this task —
reported as an open, precisely-scoped gap rather than conflated with
"the memory cell changes." Does not overlap any address from `$17`'s
own allocation diff, so no link to the sequence object is claimed.

**[Partial]** Type table: `$0E` (Slot 3 self-rearm) and `$02` (routed
to Slot 2) execution-confirmed this task; the table's other ~10
entries were not re-verified for actual firing.

**[OPEN, unchanged]** Diagnostic menu backward trace — not
re-attempted this task, same structural blocker as before.

## Slot 5 note-to-voice connection; six slots identified, execution claims corrected (docs/asr10/investigations/slot5-connects-notes-to-voice-programming.md)

**[Historical negative, narrowly scoped]** A bare read-tap at `$007C7C`
gave zero hits during the specific `$17` sequence-creation chain. It did
not test active playback and cannot support “the sequencer path is silent”.
Later work verifies that sequencer execution and transport run by a different
measured route and produce audible sound.

**[Corrected]** This section's own tap was a bare read-tap on `$007C7C`'s
entry, no PC-correlation recorded — at risk under §8.10, and now
re-measured: `$007C7C` never shows `PC==$007C7C` in any state tried,
including a fresh, real, MIDI-confirmed note. The zero-vs-nonzero
*shape* of the original measurement (0 idle, 1 per note) survives and
is independently reconfirmed below, but attributed to the wrong
address — see "Slot 5, PC-correlated" below for the corrected entry
point (`$007CA8`) and full accounting.

**[Verified, corrected key finding]** All six scheduler slots read (base `$23F6`,
stride `$16`, task at `+6`): Slot 2=`$0073EA` (MIDI-clock poll,
known), Slot 3=`$F8F2FA` (tempo producer, known), **Slot 5=`$00780C`
participates in the measured note-to-voice connection**. The old
“continuous ~83Hz check at `$00782A`” is `[DISPROVEN]`: `$00782A` was
prefetch after an adjacent branch. In the later MIDI-note run, `$007830`
and its target `$007CA8` are PC-correlated executing addresses. This
supports the measured connection without claiming that Slot 5 is the
universal sequencer note path. Slots 0 (`$002B4C`), 1 (`$FFC8B0`), 4
(`$0069CC`) read but not characterized — `[OPEN]`.

**[Partial]** The `$8258` type-dispatch table re-read, stable; only
type `$0E` remains execution-confirmed (144Hz, prior round). The other
twelve entries' target-address decoding is ambiguous (word
zero-extend vs. paired-longword) and untested for actual firing —
kept explicitly separate from confirmed findings.

**[OPEN, new lead]** A static ROM search (not live tapping — sidesteps
the overlay confound) found a genuine 32-bit reference to `GPR
MONITOR`'s own address at `$00A304`: a bounds check against exactly
`$101C`-`$103C`. Semantic connection to the string itself is
unconfirmed (could be a coincidental address-range check, the same
trap this project has hit before) — a real, concrete next address
rather than a repeat of the same blocked method a fourth time.

## Slot 5 reads two plain bytes ($D08/$D11), not a queue; the sequencer never writes them (docs/asr10/investigations/note-velocity-structure-and-sequencer-silence.md)

**[Verified, self-correction]** `$00782A` firing at a steady 83.5Hz
regardless of notes turned out to be a CPU prefetch artifact from the
adjacent unconditional `bra.b $77ca` (Slot 5's own tick-divider
branch), not a genuine per-poll check — caught by tapping the same
instruction's own extension word (`$782C`, 0 idle / 1 per note) before
being reported as "Slot 5 polls note state at 83Hz," which would have
been subtly wrong.

**[Verified]** The structure Slot 5's call chain reads is
`$000D08`/`$000D11` — two plain global bytes, not a queue/flag/ring
buffer. Value-confirmed: at rest both `$00`; during a held `KEY_C`
note, `$D08=$64` (100, exactly `esqpanel.cpp`'s own `KEY_VELOCITY`
constant) and `$D11=$3C` (60, Middle C). Writers identified:
`$0171B4` (key-down, both fields as one word), `$017276` (note
number, both key-down and key-up), `$0169C0`/`$F8C2F6` (key-up
touches). MIDI note-on untested this round.

**[Verified]** With `TUTORIAL SEQ` loaded and `$17` executed, `$D11`
is never written and `$D08`'s three touches carry the key-*release*
signature (`$0169C0`, value `$60`), not a new note. Scoped strictly
per the task's own instruction: this rules out only "the sequencer's
`$17` chain via this specific structure" — it says nothing about
whether the sequencer uses another path, or whether playback starts at
all by any mechanism.

**[OPEN, narrowed]** `$00A304`'s containing routine (`$00A2FA`) never
executes across idle or ten steps of Command-mode navigation; its
target on success (`$8C72`) reads as repeating table data, not code.
Real ROM content, confirmed unreached in every state found so far.

## The call graph as a set-intersection search; ROM strings found (docs/asr10/investigations/call-graph-intersection-and-rom-string-search.md)

**[Verified, negative — structural, not a coverage gap in the search
itself]** Computed the intersection of "forward-reachable from the panel
classification chain" (`$FFB0BC`/`$FFB392`/`$FFB20A`/`$FFB43E`) and
"backward-reachable to Slot 3's task" (`$F8F2FA`) over
`call-graph-edges.csv`'s full 5243 edges. **Intersection: 0 nodes**,
identical with and without the 1404 mirror-hypothesis edges, and
identical with or without adding the panel cluster's own documented
(not newly reverse-engineered) bridge points into ROM (`$F87FD2`,
`TRAP #2`/`#3`/`#4`/`#9` targets). Cause, confirmed directly: the entire
panel cluster uses only `bsr`/`bra`/`jmp (An)` internally (none of the
four opcodes the graph's own construction method scans for), and
`$F8F2FA` is dispatched via `TRAP #9`, not any absolute jsr/jmp — neither
side of the question is visible to this graph by construction. Does not
say no path exists; says this technique cannot see either end of it.

**[Verified]** `GPR MONITOR` (`$F8101C`), `INSTRUCTION MONITOR`
(`$F81028`), `ESP TESTS` (`$F814F4`), `SOFTWARE INFORMATION` (`$F813E7`),
`A/D TO D/A` (`$F81046`), `DC OFFSET` (`$F81051`), `MIDI LOOP`
(`$F8105B`), and the manual's `" BARS - KEEP TRACK?"` (`$F81C70`),
`TEMPO` (`$F802EB`), `CLICK` (`$F802F7`) all located as plain-ASCII,
null-terminated fragments in the boot ROM (`asr10.bin`, hash-verified
against `DOCUMENTATION-MANIFEST.md`) — none in either floppy OS image.
Menu lines are assembled at display time from independently-stored word
fragments (`KEYBOARD`/`EXAMINE `/`CALIBRATE`-style pieces), not stored as
complete strings; the `O`→`0`/`S`→`5` substitution seen on the live VFD
is a display/transcription-layer effect, not a ROM encoding choice — ROM
bytes use real ASCII letters throughout.

**[Verified, negative, structural]** Zero absolute 32-bit references to
any of the 12 string addresses above exist anywhere in the ROM or either
floppy image (same domain-filtered technique as the call graph). Expected
given the fragment-table structure: an index-based walker (small integer
in, N-null-terminator count) has no literal address to find by this
method — the same addressing-mode blind spot as the panel-cluster result
above. Who reads each string, and under what condition, remains `[OPEN]`
— not unsearched, structurally invisible to static absolute-reference
search.

## Transport confirmed dynamically; A/B click investigation (docs/asr10/investigations/transport-ab-test-play-stop-continue.md)

**[Verified runtime]** `$1D`=Play, `$17`=Stop/Continue, confirmed via
ES5506 register-write activity: Play produces sustained voice-register
traffic; Stop drops it to zero in-window; Continue resumes it with
freshly-programmed, varied voice addresses (not a reset). `$17`'s prior
`"CREATE NEW SEQUENCE"` attribution (a specific 23-button Command-menu
prefix context) stands alongside this — both real, reached from
different navigation states, the same raw-code reuse already
established for `$10`/`$11`. Bare `$17` from idle or with only a bank
loaded has zero effect in either display or guard state, confirmed this
task.

**[OPEN, highest-priority functional problem]** These measurements verify
transport, sequencer execution and audible output, not musical/audio-correct
playback. Case A (`TUTORIAL BNK` -> `TUTORIAL SEQ` -> Play) sounds
substantially better but is not verified against original musical behavior.
Case B (fresh boot -> `ATRK TUT BNK`) runs but produces mainly clicks or
otherwise clearly incorrect audio. The sequencer -> track/instrument -> voice
-> ES5506 result is therefore still open.

**[DISPROVEN as the claimed execution entry; corrected below]** A
PC-correlated tap at `$007C7C` during Play shows 374 reads and zero with
`PC==$007C7C`; the earlier positive read-tap was a prefetch/data-read false
positive. The real, repeated reader is `$007E24` (373/374 hits). Follow-up:
the measured note-to-voice connection itself holds. `$007830` and the call
target `$007CA8`, 44 bytes into the same block, are PC-correlated during the
measured MIDI-note case. `$007E24` executes, but its exact semantics are
`[OPEN]`.

**[Verified]** A per-slot association table at `$001098` (~72-byte
stride, ≥8 slots): case A (`TUTORIAL BNK`) shows 5-6 distinct
per-slot references with unused slots empty; case B (`ATRK TUT BNK`)
shows every slot populated but cycling through only 3 distinct
references (matching its 3 real instruments) — confirmed deterministic
across repeat loads (Del 4's control), not history-dependent.

**[Verified, structural — not yet tied to an audio measurement]** Case
B's active ES5506 voices redundantly share only 4 distinct sample
regions across ~18 voices (9-way/3-way duplication); case A's are
diverse. Neither case shows a literal START≈END degenerate voice. This
task's own WAV click-heuristic found no signature in either case's
audio (miscalibrated for B's much lower overall level, not a negative
result) — the redundancy is the strongest candidate mechanism for the
reported clicking, not a closed case.

**[OPEN, real lead, imperfectly controlled]** An effects-preset string
table (`"HALL REVERB"` etc.) is present after a fresh-boot `ATRK TUT BNK`
load and zeroed after a second `ATRK` load with something else loaded
between — found despite the intended middle step (`TUTORIAL BNK`) not
being reached due to a genuine post-bank-load navigation-context switch
(`$0A` starts filtering to Song-type entries after a Song-bundling bank
loads; `$0B` still walks the full catalog). Flagged for a rerun with
correct navigation, not resolved this task.

**[Verified runtime]** Single-instrument loading (e.g. `BLUES DRUMS`,
a standalone catalog file separate from any bank) uses the identical
LOAD/INST file browser as bank loading; after `$23` it shows
`"PICK IN5TRUMENT BUTT0N"` verbatim, requiring explicit destination-slot
selection — the one real difference from a bank load, which populates
every slot from its own predetermined list without this prompt. No
Command-mode category provides equivalent placement; the closest
entries (`COPY INSTRUMENT`, `IMPORT NON-ASR SOUNDS`) are not it.

## Slot 5 re-checked against §8.10; the $001098 table explained (docs/asr10/investigations/slot5-pc-correlation-and-atrk-slot-table.md)

**[Verified, PC-correlated]** Slot 5's connection to voice programming
holds: `$007830` (the `jsr` inside Slot 5's own code) and `$007CA8` (its
target) both show `PC` matching the tapped address on a real,
MIDI-confirmed note. **The entry address is corrected: `$007CA8`, not
`$007C7C`** — 44 bytes apart in the same block. `$007C7C` itself is
never PC-matched in any test; it is read as data by `$007E24`, a real,
previously-undocumented, frequently-executing routine in its own right
(`[OPEN]`). Spread through every prior conclusion citing `$007C7C` as
the callee.

**[Verified]** `ATRK TUT BNK`'s `$001098` slot-table cycling (3 values
across 8 slots) is normal, not a bug: the bank file's own on-disk
content references exactly 3 instruments (`BLUES DRUMS`/`BASS`/`ORGAN`)
plus its bundled song, while `TUTORIAL BNK` references 6; the manual
documents a fixed 8-button `Instrument/sequence track` control group
independent of how many instruments a bank provides. A 3-instrument bank
driving an up-to-8-track song has no other correct way to fill the
table. B's load also touches three writer PCs (`$00DE16`/`$00DE10`/
`$00DE0A`) plus `$F95470` that a plain bank-only load never reaches —
consistent with Song-specific population code.

**[OPEN]** The effects-preset table's content is reconfirmed but its
writer evades address-keyed write-tapping entirely (only a boot-time
filler write is caught; the real reverb strings appear by the end of
the load through an uncaught mechanism) — a real method gap, not
chased further. The intended `ATRK`→`TUTORIAL BNK`→`ATRK` control run
was attempted properly this time and is still blocked: a Song-bundling
bank load permanently locks file browsing to a 3-entry Seq/Song cycle,
and the manual's own dedicated `Instrument` object-page button (which
should escape it) has no known code in this project — a bounded sweep
found `$04` opens an Instrument edit page (new data for that gap) but
not the file browser.

**[OPEN, ambiguous]** Voice-lifetime intervals (time between successive
`CR` writes to the same ES5506 voice) were measured for both cases —
A: mean 142.8ms, 52.5% under 10ms; B: mean 200.6ms, 55.3% under 10ms —
but do not discriminate the two cases, and every measured interval in
both cases paired with an identical `START`/`END` as the prior write,
meaning most `CR` writes are gate/envelope touches on an
already-programmed voice, not full retriggers. Reading this as "note
lifetime" needs a `CR`-bit decode this task didn't reach. The clicking's
strongest lead remains the prior round's redundant-sample-sharing
finding, not this metric.

## Analog/control acquisition characterized (docs/asr10/investigations/analog-control-acquisition-v350.md)

**[Verified runtime + firmware]** V3.50 uses ES5506 global PAR/POT register
`$0D`. Firmware writes MC68302 PBDAT PB2-PB0, waits/yields, then reads PAR
through `$FC60B0`'s MOVEP sequence at `$FC2069/$6B/$6D/$6F`. The continuous
selector scan is 0,2,5,3,4 plus periodic 7. A clean four-second window measured
2,000 complete reads: 500.0/s total, approximately 100/s per ordinary channel
and 1.75/s for the reference selector. Mean final-selector-to-read delay was
1,980.1 us.

**[Verified runtime]** `EXAMINE ANALOG INPUTS` is passive, not an active ADC
mode. Idle, menu, every analog page and two A/B/A loops retain the same PAR
rate and selector histogram. UI choice changes `$0EA4` and which continuously
maintained table byte is copied to `$0EA6`: PITCHWHL `$0D8F` (selector 0),
MODWHEEL `$0D9D` (2), PEDAL `$0DAB` (4), VOLUME `$0DB9` (3), MR. KNOB
`$0DC7` (5), REFRENCE `$0DD6` (7). DUART A and SCC accesses were zero with
live witnesses; DUART B traffic on value pages is the known display path.

**[Verified firmware arithmetic]** The raw ten-bit value is left-shifted six,
then each control has its own calibration/dead-zone/multiply/clamp path. The
simple universal `>>3`/`>>2` hypothesis is `[DISPROVEN]`.

**[Likely physical]** U55 HC4051 is now the strong acquisition-mux candidate,
but COM/select/input pin routing remains `[OPEN]` until schematic or continuity
evidence.

**Panel correction.** Raw `$0C` as Env1/service entry is `[DISPROVEN]` in the
tested Command context (`QUANTIZE TRACK`). Raw `$0D`, the bounded ROM-table
candidate for mapped `$31`, reaches the service family and then `EXAMINE
ANALOG INPUTS`; `$0D = Env1` is `[Verified runtime, Command/service context]`.

This is a later hardware/input-model item and does not displace the sequencer
playback defect or practical UI work in the project priority order.

## Analog selector/control domain mapped (docs/asr10/investigations/analog-selector-control-map-v350.md)

**[Verified firmware + runtime]** The complete bounded V3.50 acquisition
domain is now characterized. The hard-coded ordinary path selects
0=PITCHWHL, 2=MODWHEEL, 5=MR. KNOB/Data Entry, 3=VOLUME and 4=PEDAL, with
periodic 7=REFRENCE. Selector 7 is a filtered calibration source used to
derive `$0DF2`, not a host control. The generic ES5506 callback contract is a
right-justified raw ten-bit value; firmware performs the control-specific
calibration, filtering, dead-zone, clamp and slew work.

**[Verified conditional path; Likely semantic]** Selector 1 is gated by the
boot-ROM model flag `"88"` at `$FFCCD1` and a post-key-event countdown at
`$FFD0EA`. Its producer at `$000172EC` filters and scales PAR to 0..127 and
dispatches shared controller index `$0E`. Together with the ASR-88-only mono
pressure hardware documented by Ensoniq, this makes selector 1 likely ASR-88
mono/channel pressure; direct ASR-88 runtime/name evidence is still absent.

**[Verified, bounded]** Selector 6 has no generator in the analyzed V3.50
select/settle/PAR path. All direct calls and literal PBDAT writers were checked,
and a 10,737-read live-witness run observed every known selector but neither 1
nor 6. Selector 6 is therefore unreachable in this acquisition path, while its
physical mux-pin role remains `[OPEN]`.

**[Implemented, runtime-verified]** `asr10_boot_state` now takes selector
PB2-PB0 from the generic MC68302 PBDAT output latch and routes it through an
ASR-owned raw 10-bit source mux to generic ES5506 PAR. The panel sources are
Pitch=0, Mod=2, Volume=3, Pedal/CV=4 and Data Entry=5; 7 is the fixed `$300`
calibration reference. `lua/analog_pot_wiring_verify.lua` observed the
firmware PBDAT -> PAR path for 0/2/3/4/5/7 with retained RAM witnesses and
reached `PITCHWHL 64` via `EXAMINE ANALOG INPUTS`. Selector 1 remains an
unclassified ASR-88 conditional fallback, selector 6 has no assigned role,
and Input Level remains outside the POT scan.

**[Corrected source model]** MR. KNOB is the Service Manual's diagnostic name
for the Data Entry slider: one semantic control. Before this implementation,
Data Entry was misrouted to index 3, Volume to 5, and Input Level occupied
index 4. The implemented sources use the firmware selectors stated above.
Input Level is `[DISPROVEN]` as a member of this PAR scan and belongs to the
separately open audio-input/gain model. ASR-88 pressure remains `[Likely]`;
physical U55 routing and selector 6 remain `[OPEN]`.
