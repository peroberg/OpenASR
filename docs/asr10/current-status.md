# ASR-10 current status

Current truth for the ASR-10 MAME bring-up. This file is deliberately short:
verified reference facts belong in `reference/`, reproducible analysis output belongs in
`static/`, and experiment history belongs in `investigations/` or `archive/`.

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
  **Uppdaterat:** kontraktet ar nu implementerat och landat
  (`m_fdc->set_ready_line_connected(false)` + IRQ1-koppling,
  `investigations/ready-line-artifact-probe.md`). Runtime nar nu
  RECALIBRATE-completion, SEEK och READ DATA-utfardande korrekt genom
  vektor `$51`. Blockeraren ar IDMA
  (`investigations/idma-implementation-plan.md`), inte langre en
  saknad grind till `$51`.
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
  aldrig levereras).
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
  integration are not working end-to-end. **Update:** the structural gap
  (ES5506 bank 0 disconnected from CPU RAM, no output routing) is closed
  (`investigations/sample-topology-closure.md`) but unverified audibly —
  no keyboard model exists yet to trigger a voice, so a post-load
  `-wavwrite` capture is silence by construction, not evidence the wiring
  works or doesn't.
- DUART channel A RX is not wired to a real external source.
- ES5506 PAR has a real panel-analog route, but the wider ADC channel identity
  and audio-side effects are not fully verified.

## Next implementation target

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
- **IDMA's three debts:** SAPR not payable now (needs a `mem_map` change,
  blocked on E2) but guarded (alarm if ever ≠ `$FFFC5803`, the only value
  ever observed). CMR bit layout added to `docs/mc68302/idma-spec.md`
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

## Open questions

- **Critical next:** what happens after `FILE LOADED` — the concrete
  `$23F6` post-completion consumer, next request class, and whether
  payload `$02B600` survives to `$043E` (`architecture-handoff.md`'s
  original open questions, now finally reachable). The completion-chain
  side of this investigation (vector `$51`, IDMA transfer, terminal count,
  READ DATA's own completion) is done.
- **Physical board policy for storage IRQ1** remains formally `[OPEN]`
  (what physically drives IRQ1 on real hardware is still unverified), but
  is no longer blocking progress: the board-policy wiring plus a
  ready-line fix is tested and known to work up to IDMA.
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
