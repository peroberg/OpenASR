# OpenASR — Development Roadmap & TODO

This document outlines concrete, actionable tasks for upcoming development sprints. Tasks are categorized by subsystem and formulated to allow independent implementation and verification.

---

## 1. Audio Subsystem & Synthesizer Accuracy

### Characterize Dense-Demo Audio Artifacts
- [ ] **Acoustic Profiling**: Record `-wavwrite` audio captures of complex multi-instrument demo songs (e.g. Tutorial Bank sequences) where crackling/noise is observed.
- [ ] **Voice Scaling Test**: Measure artifact occurrence rate as a function of simultaneous active ES5506 voices (sweep from 1 voice up to 31 voices) to test if artifacts correlate with voice allocation saturation or serial DMA timing.
- [ ] **Boundary Localization**: Localize the first corrupted audio buffer boundary in the host adapter / sound device stream before attempting any timing or signal-level modifications.
- [ ] **Serial Bus Interleave Audit**: Verify whether the 6-channel serial data transfer to the ESP or DAC incurs any sample-misalignment or FIFO underruns during burst processing.

### Audio Rate Modes & WaveSample Transposition
- [ ] **Extreme Transposition Mapping**: Test acoustic frequency and waveform stability for WaveSamples transposed across ±3 octaves in both Mode 0 (29.76 kHz) and Mode 1 (44.1 kHz).
- [ ] **Filter Envelope Fidelity**: Verify dynamic filter cutoff and resonance curves against reference hardware captures.

---

## 2. Sequencer Validation

### Sequence Playback & Event Dispatch
- [ ] **Event Timing Comparison**: Compare sequencer event dispatch timestamps in MAME against MIDI event logs captured from physical ASR-10 hardware playing identical sequence files.
- [ ] **Multi-Track Polyphony Verification**: Audit voice allocation, priority-based voice stealing, and note-off tracking across 8 simultaneous sequencer tracks.
- [ ] **Clock & Synchronization**: Verify external MIDI clock synchronization (MIDI In clock/start/stop) and internal tempo resolution across tempo changes (40–250 BPM).

---

## 3. Front Panel & User Interface

### Panel Geometry & Layout Realism
- [ ] **Chassis-Faithful Layout**: Replace the provisional debug button grid in `src/mame/ensoniq/asr10_boot.cpp` with an authentic 2D artwork layout mirroring the physical ASR-10 keyboard and rack front panels.
- [ ] **Control Grouping**: Visually group buttons into their authentic functional clusters: *Mode* (Load, Command, Edit), *Page* (Seq, Song, System, Wavesample, Instrument), *Track/Bank Select* (1–8), and *Numeric Keypad*.
- [ ] **LED & Annunciator Support**: Map virtual LEDs for disk activity, transport status (Record, Play), and audio peak indicators.

---

## 4. Hardware Fidelity & Board-Level Model

### Physical Clock Network
- [ ] **PCB Routing Trace**: Trace the physical divider and multiplexer circuitry between oscillators $Y_2$ (30.47618 MHz), $Y_3$ (33.8688 MHz), and the OTTO CLK pin on physical ASR-10 hardware boards to confirm the exact physical realization of the $Y_2/2$ and $Y_3/2$ rate domains.
- [ ] **Gate Array Modeling**: Determine whether Super-GLU (ES5701) or another custom ASIC performs clock division and bus gating on late-model ASR-10 revisions.

### Save-State & Runtime Maintenance
- [ ] **Full-System State Round-Trip**: Audit end-to-end save-state creation and restore during active audio synthesis and SCSI file operations.
- [ ] **Remove Dead Shadow State**: Prune any remaining unused diagnostic shadow variables in `asr10_boot_state`.

---

## 5. Public Project & Open-Source Infrastructure

### Testing & CI
- [ ] **Synthetic Test Fixtures**: Develop synthetic, unencumbered ROM and floppy disk test fixtures to allow running continuous integration (CI) tests on GitHub Actions without requiring proprietary Ensoniq binaries.
- [ ] **Automated Headless Test Runner**: Wrap `docs/asr10/regression-test.sh` in a standardized Python/shell runner with JUnit XML reporting for CI integration.

### Community & Contribution
- [ ] **Contributing Guide**: Add `CONTRIBUTING.md` defining code conventions, epistemic evidence standards (`[VERIFIED]`, `[OBSERVED]`, `[OPEN]`), and deletion-ledger rules.
- [ ] **Issue Templates**: Create GitHub issue and pull request templates for bug reports, hardware measurements, and documentation contributions.
