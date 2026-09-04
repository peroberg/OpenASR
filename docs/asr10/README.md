# OpenASR — Technical Documentation Index

This directory contains the reverse-engineering documentation, architectural models, and investigation reports produced during the OpenASR bring-up.

---

## Epistemic Evidence Standards

OpenASR documentation uses standardized epistemic labels to explicitly distinguish verified facts from hypotheses:

- **`[VERIFIED]`**: Confirmed by repeatable execution, mathematical derivation, or authentic firmware disassembly.
- **`[OBSERVED]`**: Measured behavior from runtime logs, hardware captures, or register readouts without full causal attribution.
- **`[INFERRED]`**: Logically deduced conclusion supported by multiple consistent observations.
- **`[LIKELY]`**: Strong candidate hypothesis awaiting conclusive falsification or confirmation.
- **`[DISPROVEN]`**: Tested hypothesis conclusively refuted by evidence or a counter-example.
- **`[OPEN]`**: Unresolved engineering question or unmapped hardware detail.

---

## Documentation Categories

### 1. Architecture & Machine Model
- [`current-status.md`](current-status.md): Canonical technical baseline, verified subsystems, and active findings.
- [`reference/hardware-map.md`](reference/hardware-map.md): Global memory, I/O, and peripheral address map.
- [`reference/memory-map.md`](reference/memory-map.md): CPU address space and chip-select ranges.
- [`reference/vector-map.md`](reference/vector-map.md): Exception vector assignments and MC68302 interrupt routing.
- [`reference/architecture-handoff.md`](reference/architecture-handoff.md): High-level system architecture and component boundaries.
- [`reference/audio-storage-architecture.md`](reference/audio-storage-architecture.md): Shared bus architecture between host, storage, and sound RAM.
- [`reference/call-graph.md`](reference/call-graph.md): OS execution flow and dispatcher structure.
- [`reference/subroutine-index.md`](reference/subroutine-index.md): Disassembled firmware routine index.

### 2. Processor & MC68302 Integrated Multiprotocol Processor
- [`reference/mc68302-status.md`](reference/mc68302-status.md): Implementation status of MC68302 SIM, timers, and serial channels.
- [`reference/interrupt-topology-gaps.md`](reference/interrupt-topology-gaps.md): Autovectored vs non-autovectored interrupt decoding.
- [`reference/movep-library.md`](reference/movep-library.md): Disassembly and handling of `MOVEP` peripheral instructions.
- [`reference/scc-hardware-gap.md`](reference/scc-hardware-gap.md): Serial Communication Controller (SCC) channel mapping.
- [`reference/scc-board-source-question.md`](reference/scc-board-source-question.md): Analysis of physical SCC keyboard link wiring.

### 3. Boot & Operating System Progression
- [`reference/boot-sequence.md`](reference/boot-sequence.md): Step-by-step cold boot progression from ROM vector to OS execution.
- [`reference/boot-runtime-timeline.md`](reference/boot-runtime-timeline.md): Timing benchmarks for power-on self-test and disk polling.
- [`reference/rom-os-abi.md`](reference/rom-os-abi.md): System call ABI and parameter passing between boot ROM and OS.
- [`reference/os-code-extraction.md`](reference/os-code-extraction.md): Methods for extracting and disassembling OS binaries.
- [`investigations/filesystem-browser-map.md`](investigations/filesystem-browser-map.md): OS directory parsing and file browser progression.

### 4. Storage Subsystems (Floppy & SCSI)
- [`investigations/upd72069-standby-auxcmd-fix.md`](investigations/upd72069-standby-auxcmd-fix.md): Analysis and fix for uPD72069 standby commands.
- [`investigations/writable-scsi-hdd-and-format.md`](investigations/writable-scsi-hdd-and-format.md): SCSI low-level format, partition mounting, and write support.
- [`investigations/scsi-initiator-cdb-reconstruction.md`](investigations/scsi-initiator-cdb-reconstruction.md): WD33C93A initiator phase sequence and CDB handling.
- [`investigations/scsi-idma-read-reconstruction.md`](investigations/scsi-idma-read-reconstruction.md): SCSI-to-system-RAM DMA transfers via MC68302 IDMA.
- [`reference/scsi-operation-example.md`](reference/scsi-operation-example.md): Walkthrough of authentic SCSI sector read/write commands.
- [`reference/storage-completion-dispatch.md`](reference/storage-completion-dispatch.md): Storage interrupt completion and event queue signaling.

### 5. Disk & File Formats
- [`reference/os-image-layout.md`](reference/os-image-layout.md): Sector layout of Ensoniq OS floppy and hard disk images.
- [`reference/e2-address-model.md`](reference/e2-address-model.md): Block addressing and directory structure on Ensoniq media.

### 6. Runtime Object Model
- [`reference/runtime-object-model.md`](reference/runtime-object-model.md): Internal memory representations of Banks, Instruments, and WaveSamples.
- [`reference/runtime-service-model.md`](reference/runtime-service-model.md): Task scheduling and event loop dispatcher.

### 7. Instrument, WaveSample & OTTO Traversal
- [`reference/instrument-to-otto-runtime.md`](reference/instrument-to-otto-runtime.md): How high-level instrument parameters translate to OTTO voice registers.

### 8. Sound Synthesis (ES5506 / OTTO)
- [`reference/methods-hypothesis-management.md`](reference/methods-hypothesis-management.md): Methodology for verifying pitch, clock divisors, and sample stepping.
- [`../ensoniq/README.md`](../ensoniq/README.md): Bibliographic index for ES5506 and ES5510 silicon datasheets.

### 9. Effects Processing (ES5510 / ESP)
- [`reference/es5701-wiring.md`](reference/es5701-wiring.md): ESP host interface, control lines, and glue logic.

### 10. Sequencer Subsystem
- [`investigations/sequencer-event-mask-and-playback-dispatch.md`](investigations/sequencer-event-mask-and-playback-dispatch.md): Sequencer event stream decoding, transport state machine, and voice trigger dispatch.

### 11. Front Panel & Display
- [`reference/front-panel-model.md`](reference/front-panel-model.md): Vacuum Fluorescent Display (VFD) protocol and key matrix encoding.
- [`reference/display-protocol.md`](reference/display-protocol.md): Display escape codes, underline control, and field flashing.
- [`reference/panel-manual.md`](reference/panel-manual.md): Front-panel operational commands and button codes.
- [`investigations/panel-input-model.md`](investigations/panel-input-model.md): Keypress debouncing and serial transmission to the main CPU.

### 12. Verification & Regression Testing
- [`regression-test.sh`](regression-test.sh): Shell script executing the full 12-test automated regression suite.
- [`lua/README.md`](lua/README.md): Documentation of individual Lua test probes and harness utilities.
- [`DOCUMENTATION-MANIFEST.md`](DOCUMENTATION-MANIFEST.md): Complete Swedish-language inventory and manifest of project documentation.

### 13. Historical Research & Provenance
- [`HANDOFF.md`](HANDOFF.md): Engineering handoff document and evidence boundaries.
- [`archive/`](archive/): Historical worklogs, superseded hypotheses, and exploratory notes.
