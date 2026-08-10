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

## Works

- `asr10booth` boots `floppies/asr10booth/V350.img` with no `ASR10_*` environment
  variables to:

  ```text
  ENSONIQ ASR-10 -> LOADING SYSTEM -> FILE 1  TUTORIAL BNK
  ```

- The acceptance test is `docs/asr10/regression-test.sh`. Tag
  `asr10-file1-2026-08-03` marks the first documented milestone.
- Current boot uses real MAME devices for the DUART host path (`mc68681`), ES5506 host
  registers, ES5510 host registers, and the FDC path used by this boot.
- Channel B panel RX is owned by `mc68681_device`.

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
- **Current MC68302 implementation has no internal interrupt-source model.** IPR/IMR/ISR,
  SCC/SMC parameter RAM, IDMA, Port A, timers, watchdog and SCC/SMC/SCP are
  `known_unimplemented` shadow storage in `mc68302.cpp`. The only working interrupt path
  at HEAD is external IRQ6 via `irq6_ack_vector()`, which explains why the panel path
  works while SCC, Timer 2 and PB9-PB11 appear inactive. → `reference/mc68302-status.md`
- **A control-flow database** of 5243 call-site-level edges with normalised addresses,
  evidence level and execution status. → `static/call-graph-edges.csv`,
  `reference/call-graph.md`

## Does not work

- Button navigation is not implemented far enough to prove `FILE 2` or normal
  file-browser interaction.
- Audio output, sampling, sequencer behaviour, and complete ES5506/ES5510 sound
  integration are not working end-to-end.
- DUART channel A RX is not wired to a real external source.
- The fixed PAR value is plumbing only.

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
3. **Deterministic file-browse test.** One `DOWN` from `FILE 1  TUTORIAL BNK`, logging
   panel byte → DUART handler → dispatcher → binding slot → OS routine → file index →
   panel output. This is blocked at HEAD without implementation changes: the ASR-10
   driver has no input ports, and the existing panel harness only injects panel ACK/status
   bytes, not user key events.
4. **Minimal truthful SCSI model.** AM33C93A at `$FC5001`/`$FC5003`: reset accepted,
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
- **`$F97662`** — 203 ROM call sites, unidentified. **`$F95EAA`** — 1 call in V1.61,
  33 in V3.50, unidentified. Both tracked in `static/routines.csv` with empty `name`.
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
- **The V3.50 boot path uses `$FF8000-$FFFFFF` as a plain mirror of `$000000-$00FFFF`.**
  A read-tap over `$FF8000-$FFFFFF` observed 566229 data reads during the V3.50 reference
  boot, but sampled high-window values did not match corresponding `$00xxxx` contents
  (for example `$FF8D44 = $F9` while `$008D44 = $00`). This disproves the broad mirror
  hypothesis for current MAME behavior on that run; opcode-fetch visibility remains open
  because Lua exposed no opcode space.
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
- `reference/runtime-service-model.md` — dispatcher queue and service fields, historical
  V1.61 observations.
- `reference/methods-static-analysis.md` — how the results were produced, and the
  method's blind spots.
- `reference/boot-sequence.md`, `reference/subroutine-index.md`,
  `reference/os-code-extraction.md`, `reference/hardware-map.md` — as before, updated.
- `static/README.md` — what the raw material is, how it was generated, what it does not
  prove.
