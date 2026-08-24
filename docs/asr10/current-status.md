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

The current session handoff is `reference/handoff-2026-08-23.md`. This document
retains the detailed status journal. `reference/architecture-handoff.md` preserves
the earlier pre-IDMA architecture checkpoint and remains useful for the service-
kernel model, but its runtime stall and implementation target are historical and
passed.

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

- Mono LEFT and L+R RECORD, sample-object creation and direct ES5506
  playback are both `[Verified runtime/current model]`. **Superseded
  2026-08-23/24**: the `$7CE510` blocker below is historical —
  `investigations/memory-size-alias-fix.md` fixed the RAM/decode category
  that caused it, and `investigations/stereo-round-trip-verification.md`
  round-trip-verified stereo byte-for-byte (injection through IDMA copy)
  and confirmed the two output channels are audibly distinct (mono
  control: channels bit-identical, correlation 1.0; stereo: correlation
  0.059, different peak/RMS) — not mono duplicated into two channels.
- The sampling firmware/data path is verified. Physical ADC-to-SCC routing and
  digital-board connector pinout remain `[OPEN]`.
- Factor two remains `[OPEN]`; no mechanism is assumed.
- ES5510 execution/effects integration is not started.
- PB9/PB10/PB11 physical sources and the keybed's digital-board termination are
  `[OPEN hardware]`, but do not block the verified mono recording path.

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
bit-exact evidence**, not just recalculation. `ACTV=0x1F` (31,
constant, never changes) confirmed; `m_sample_rate=31,250Hz` fixed;
neither 29.76kHz nor 44.1kHz reachable from 16MHz at any `ACTV`, and
moot regardless since `ACTV` never varies between measurements. No
code changes follow (Del 3/4 were conditioned on confirming the bug).
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
left `[OPEN]`. Checked ES5506's `MODE` (`$0D`, Single/Master/Normal)
and `ACT` (`$1F`, 31 voices) registers: both written once at boot,
never touched again in any measurement this series has taken -- no
runtime 29.76/44.1kHz mode switch observed, and no PB pin/register
bit/Port A output identified as a mode selector in existing docs.

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

**[OPEN]** REC SRC Field 1 remains unreachable through any currently
modeled panel control — determined to be a panel-control mapping gap
(the real Left/Right Arrow button code is not identified), not a
display rendering gap, since the display now correctly renders whatever
field/attribute structure it receives.

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
(`esqpanel.cpp`). The real Left/Right Arrow codes, and the Sequencer
Transport (Record/Stop•Continue/Play) codes, remain `[OPEN]` despite
systematic sweeping across three contexts — not guessed at.

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

**[OPEN]** Record/Stop•Continue/Play tried as a state machine (hold A,
hold B while A held, release both) across the codes with no visible
single-press effect from idle — no combined effect found. The
sequencer-status state variable to trace was not localized. One useful
side finding: raw `$15` reaches a genuine sequence file listing (`"FILE
9  TUTORIAL SEQ"`), a strong Seq•Song category-button candidate.
LOAD's blink was determined panel-local (zero display-channel traffic
during a 3-second idle window), which puts the 40 annunciator bits'
"simple on/off" reading specifically in question — no blink-mask
encoding was identified, so no reinterpretation was implemented; they
remain `[OPEN]`.

## Bank loading as sequencer context; transport still open (docs/asr10/investigations/bank-loading-and-transport-context.md)

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

**[OPEN], with substantially wider negative evidence than the prior
task**: Record/Stop•Continue/Play. This task's search added bank-loaded
context, prefix isolation, a 650-pair hold-combo sweep across every
code with zero single-press effect anywhere tried, a second 110-pair
sweep, correlated PC/state-variable tracking (both previously-tried
candidate variables, `$016F`/`$0D04`, never move), and a full memory
diff — all negative. `$26`-`$3F` (26 codes) show zero measured effect
in every context and combination tried. Two live possibilities:
Load/Command/Edit mode buttons (also `[OPEN]`) may need to be part of
the combination, or Record/Play may need actual audio/MIDI input this
button-only, `-sound none` harness cannot supply.

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

**First byte is now exhausted for transport identification** across
three rounds of investigation: single/paired-button sweeps from
multiple contexts, a 650-pair and a 110-pair hold-combination sweep,
prefix-isolated binary search, correlated PC/state-variable tracking,
a full memory diff, and (this round) forward disassembly from that
diff through two real subsystems to a named byte that was never
observed to move. Record/Stop•Continue/Play remain `[OPEN]`; the
service menu's entry condition remains `[OPEN]`.

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
