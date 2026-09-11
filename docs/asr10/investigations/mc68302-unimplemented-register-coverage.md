# MC68302 Unimplemented Register Coverage Audit and Workload Classification

**Date:** 2026-09-11  
**Status:** `[VERIFIED for covered workloads]` across boot, instrument load, voice playback, panel navigation, and audio rate switching workloads.  
**Audited Baseline:** Master after Track A (`PBCNT`/`PBDDR` readback latch), Track B (`SAPR`/`DAPR` byte-lane preservation), and Track C (`$0CE2/$0CE3` lowmem alias canonicalization).

---

## 1. Executive Summary

This investigation audits every documented register and memory region in the Motorola MC68302 System Integration Block (SIB) window (`$FC6000-$FC6FFF` / internal offset `$0000-$0FFF`) against actual ASR-10 firmware execution.

The objective is to establish:
1. Exactly which MC68302 registers are modeled with full hardware semantics versus backed only by temporary shadow storage (`m_shadow`) or fallback stubs in `src/devices/machine/mc68302.cpp`.
2. The empirical runtime access frequency (reads and writes) across representative ASR-10 workloads: ROM bootstrap, floppy OS load (V3.50), instrument bank load (`TUTORIAL BNK`, `JM DIGI SYN`), voice selection, MIDI note playback, audio rate switching, and panel interaction.
3. A formal taxonomy (Classes 0 through 5) ranking the risk and necessity of full hardware emulation for each register.
4. Whether any remaining unimplemented register constitutes a latent defect or causal blocker for current ASR-10 emulation.

### Key Findings
- **Zero Unknown Accesses in Covered Workloads:** Across all measured workloads, 100% of firmware accesses fall into documented MC68302 SIB architectural blocks. No undocumented registers outside the documented CP command latch (`$FC6860`) are accessed.
- **Modeled Core Subsystems:** Chip selects (`BR0-3`/`OR0-3`), Parallel I/O (`Port A`, `Port B`), minimal `IDMA`, `FC6860` CP command latch, and the level-4/level-1 `Interrupt Controller` (`GIMR`, `IPR`, `IMR`, `ISR`) are actively modeled and verified for the covered workflows.
- **Zero Unmitigated Class 5 (Critical) Registers in Tested Workloads:** The review items identified prior to this audit (`PBCNT`/`PBDDR` readback latching, `SAPR`/`DAPR` partial-write byte preservation, and lowmem alias bypassing `$0CE3` rate policy) have been cleanly resolved and verified in Tracks A, B, and C. In the tested workloads, no remaining unimplemented register caused an observable failure, crash, or audio degradation.
- **Shadow Storage (Class 1 & 2):** Unimplemented registers touched by firmware (`WRR`, `TMR2`, `TRR2`, `TCN2`, `SIMODE`, `SCM1/2`, `DSR1/2`, `SCCM1/2`, `SCCS1/2`) are configuration-only or one-time boot probes in the tested workloads. Firmware was not observed to poll these registers in wait-loops or stall on unmodeled autonomous hardware state transitions. However, active hardware side effects (e.g. Timer 2 counting, autonomous CP RISC execution) remain unmodeled.
- **Unused Peripherals (Class 0):** `Timer 1`, `Timer 2` event/capture, the entire `SCC3` subsystem, `SCP`, and `SMC` are completely unconfigured and untouched across all tested Ensoniq firmware paths. Their status in untested workloads (e.g. live ADC sampling, external sync) remains unverified.

---

## 2. Workload Classification Taxonomy (Classes 0–5)

To prevent speculative or unbounded reimplementation of MC68302 features not required by the ASR-10, all internal offsets are classified into six rigorous tiers:

| Class | Name | Definition | Emulation Requirement |
|:---:|:---|:---|:---|
| **Class 0** | **Unused** | Registers defined in MC68302 specification but never read or written in tested ASR-10 workloads. | Stub / unmapped. Emulation not required for covered workloads; status in untested modes remains unverified. |
| **Class 1** | **Config-Only** | Registers written once or during mode changes, never polled or read back at runtime in tested paths. | Shadow storage (`m_shadow`) sufficient for covered paths. Active hardware side effects remain unmodeled. |
| **Class 2** | **Probe-Only** | Registers read once or a few times during boot/init probing; not polled in loops. | Constant or shadow readback sufficient. Dummy 0 readback accepted by firmware does not imply full peripheral fidelity. |
| **Class 3** | **Polled / Status** | Status/event registers read repeatedly in loops during operation. | Active status bits and event clearing semantics required. |
| **Class 4** | **Active Data Path** | Parameter RAM, buffer descriptors, or streaming registers handling live data movement. | Must match data transfer protocol (handled via high-level injection or autonomous engine). |
| **Class 5** | **Critical / Causal** | Discrepancy between model and hardware that causes crashes, corruption, wrong pitch, or test failures. | Immediate, verified hardware fix required. |

---

## 3. Comprehensive MC68302 SIB Register Catalog & Empirical Measurement

Measurements were collected using dynamic Lua taps covering boot, OS startup, instrument load, instrument selection, MIDI note playback (`noteon.mid`), audio rate mode toggling, and panel navigation:

| Offset | Address (`BAR=$0FC6`) | Name / Block | Status in `mc68302.cpp` | Measured Reads | Measured Writes | First Value | Class | Notes |
|:---:|:---:|:---|:---|:---:|:---:|:---:|:---:|:---|
| `$0000-$03FF` | `$FC6000-$FC63FF` | **Dual-Port RAM** | Modeled (RAM storage) | 71,438+ | 71,446+ | Various | **Class 4** | General scratchpad, stack, and OS tables. Heavy runtime traffic. |
| `$0400-$047F` | `$FC6400-$FC647F` | **SCC1 Parameter RAM** | Shadow storage | 0 | 56 | `$D000` / `$7280` | **Class 4** | SCC1 Rx buffer descriptors (status word + pointer pairs). Initialized for audio recording. |
| `$0480-$04BF` | `$FC6480-$FC64BF` | **SCC1 Protocol Specific** | Shadow storage | 4 | 16 | `$0320` | **Class 4** | Maximum receive buffer length (`MRBLR=$0320` = 800 bytes). |
| `$0500-$057F` | `$FC6500-$FC657F` | **SCC2 Parameter RAM** | Shadow storage | 0 | 56 | `$D000` / `$4B00` | **Class 4** | SCC2 Rx buffer descriptors (channel 2 audio recording). |
| `$0580-$05BF` | `$FC6580-$FC65BF` | **SCC2 Protocol Specific** | Shadow storage | 4 | 16 | `$0320` | **Class 4** | MRBLR and protocol parameters for SCC2. |
| `$0600-$065F` | `$FC6600-$FC665F` | **SCC3 Parameter RAM** | Shadow storage | 0 | 0 | — | **Class 0** | Unused. SCC3 is unconfigured. |
| `$0660-$067F` | `$FC6660-$FC667F` | **SMC/SCP Parameter RAM**| Shadow storage | 0 | 0 | — | **Class 0** | Unused. |
| `$0800` | `$FC6800` | IDMA Reserved | Stub | 0 | 0 | — | **Class 0** | Reserved in manual. |
| `$0802` | `$FC6802` | **IDMA CMR** | Modeled | 0 | 76 | `$0002` / `$0D51` | **Class 1/4** | Channel mode: `$0002` (reset) and `$0D51` (start transfer). Modeled for covered transfers. |
| `$0804` | `$FC6804` | **IDMA SAPR_HI** | Modeled | 0 | 21 | `$FFFC` | **Class 1/4** | Source pointer high word (`$FFFC5803`). Byte lanes preserved (Track B). |
| `$0806` | `$FC6806` | **IDMA SAPR_LO** | Modeled | 0 | 21 | `$5803` | **Class 1/4** | Source pointer low word. Byte lanes preserved (Track B). |
| `$0808` | `$FC6808` | **IDMA DAPR_HI** | Modeled | 0 | 21 | `$0000` | **Class 1/4** | Destination pointer high word. Byte lanes preserved (Track B). |
| `$080A` | `$FC680A` | **IDMA DAPR_LO** | Modeled | 0 | 21 | `$0944` | **Class 1/4** | Destination pointer low word. Byte lanes preserved (Track B). |
| `$080C` | `$FC680C` | **IDMA BCR** | Modeled | 0 | 21 | `$0201` | **Class 1/4** | Byte count (`$0201`, `$0E01`, `$2801`). Modeled for covered transfers. |
| `$080E` | `$FC680E` | **IDMA CSR** | Modeled | 33 | 0 | `$00` | **Class 3** | Channel status: polled for completion. Returns 0 / W1C. |
| `$0810` | `$FC6810` | **IDMA FCR** | Modeled | 0 | 21 | `$99` | **Class 1** | Function code register (`$99`). Stored. |
| `$0812` | `$FC6812` | **GIMR** | Modeled | 0 | 1 (boot) | `$8040` | **Class 1** | Global interrupt mode. Vector base prefix `$40`/`$50`, normal mode. Stored in shadow; core interrupt routing is modeled. |
| `$0814` | `$FC6814` | **IPR** | Modeled | 4 | 38 | `$0000` / `$FFFF` | **Class 3/4** | Interrupt pending register. Modeled, W1C, updates CPU IRQ lines. |
| `$0816` | `$FC6816` | **IMR** | Modeled | 42 | 42 | `$E480` / `$EC80` | **Class 3/4** | Interrupt mask register. Masks PB9, SCC1/2, IDMA. Modeled for active interrupt sources. |
| `$0818` | `$FC6818` | **ISR** | Modeled | 0 | 8 | `$FFFF` / `$0080` | **Class 1/4** | Interrupt in-service. Modeled, W1C priority arbitration. |
| `$081E` | `$FC681E` | **PACNT** | Modeled | 0 | 1 (boot) | `$E000` | **Class 1** | Port A control. PA13-15 dedicated IDMA pins (unused). |
| `$0820` | `$FC6820` | **PADDR** | Modeled | 1 | 2 (boot) | `$FFFF` | **Class 1** | Port A direction. Read-modify-write clears bit 4 at boot. |
| `$0822` | `$FC6822` | **PADAT** | Modeled | 35 | 35 | `$18FC` / `$58E8` | **Class 4** | Port A data latch. Bit 4 drives ESP RUN/HALT (`pa_out_cb`). |
| `$0824` | `$FC6824` | **PBCNT** | Modeled | 0 | 1 (boot) | `$0080` | **Class 1** | Port B control. PB7 = WDOG. Readback implemented (Track A). |
| `$0826` | `$FC6826` | **PBDDR** | Modeled | 0 | 2 | `$F097` | **Class 1** | Port B direction. PB3 = input. Readback implemented (Track A). |
| `$0828` | `$FC6828` | **PBDAT** | Modeled | 18,850+ | 9,156+ | `$001B` / `$0007` | **Class 4** | Port B data latch. Heavily accessed I/O port (`pb_out_cb`). |
| `$0830-$083E` | `$FC6830-$FC683E` | **BR0-3 / OR0-3** | Modeled | 0 | 8 (boot) | Various | **Class 1** | Chip select configuration. Decoded into base/size/enable. |
| `$0840-$0848` | `$FC6840-$FC6848` | **Timer 1 (TMR/TRR/TCR/TCN/TER)** | Shadow storage | 0 | 0 | — | **Class 0** | Completely untouched by ASR-10 firmware in tested workloads. |
| `$084A` | `$FC684A` | **WRR** (Watchdog) | Shadow storage | 0 | 1 (boot) | `$0000` | **Class 1** | Watchdog reference. Written with `$0000` at boot (disables watchdog). |
| `$084C` | `$FC684C` | **WCN** (Watchdog) | Shadow storage | 0 | 0 | — | **Class 0** | Watchdog counter. Untouched in tested workloads. |
| `$0850` | `$FC6850` | **TMR2** | Shadow storage | 0 | 1 (boot) | `$003B` | **Class 1** | Timer 2 mode register. Written during boot init. |
| `$0852` | `$FC6852` | **TRR2** | Shadow storage | 0 | 1 (boot) | `$3F01` | **Class 1** | Timer 2 reference register. Written during boot init. |
| `$0854` | `$FC6854` | **TCR2** | Shadow storage | 0 | 0 | — | **Class 0** | Timer 2 capture register. Untouched in tested workloads. |
| `$0856` | `$FC6856` | **TCN2** | Shadow storage | 1 (boot) | 0 | `$0000` | **Class 2** | Timer 2 counter. Read once by V3.50 OS startup probe (returns shadow 0; counter does not advance). |
| `$0858` | `$FC6858` | **TER2** | Shadow storage | 0 | 0 | — | **Class 0** | Timer 2 event register. Untouched in tested workloads. |
| `$0860` | `$FC6860` | **FC6860 (CP Command)** | Modeled | 48 | 12 | `$2200` | **Class 3/4** | Communications Processor command latch & busy bit countdown. |
| `$0880` | `$FC6880` | **SCON1** | Shadow storage | 0 | 0 | — | **Class 0** | SCC1 configuration register. Untouched. |
| `$0882` | `$FC6882` | **SCM1** | Shadow storage | 0 | 4 | `$7000` | **Class 1** | SCC1 mode register. Stored in shadow; serial controller hardware unmodeled. |
| `$0884` | `$FC6884` | **DSR1** | Shadow storage | 0 | 8 | `$7033` | **Class 1** | SCC1 data sync register. Stored in shadow. |
| `$0886` | `$FC6886` | **SCCE1** | Modeled (W1C) | 0 | 0 (direct) | — | **Class 1/4** | SCC1 event register. W1C wired to `update_internal_irq()`. |
| `$0888` | `$FC6888` | **SCCM1** | Modeled (Mask) | 0 | 4 | `$FFFF` | **Class 1/4** | SCC1 mask register. Wired to `update_internal_irq()`. |
| `$088A` | `$FC688A` | **SCCS1** | Shadow storage | 0 | 4 | `$0505` | **Class 1** | SCC1 status register. Stored in shadow. |
| `$0890` | `$FC6890` | **SCON2** | Shadow storage | 0 | 0 | — | **Class 0** | SCC2 configuration register. Untouched. |
| `$0892` | `$FC6892` | **SCM2** | Shadow storage | 0 | 4 | `$7000` | **Class 1** | SCC2 mode register. Stored in shadow; serial controller hardware unmodeled. |
| `$0894` | `$FC6894` | **DSR2** | Shadow storage | 0 | 8 | `$7033` | **Class 1** | SCC2 data sync register. Stored in shadow. |
| `$0896` | `$FC6896` | **SCCE2** | Modeled (W1C) | 0 | 0 (direct) | — | **Class 1/4** | SCC2 event register. W1C wired to `update_internal_irq()`. |
| `$0898` | `$FC6898` | **SCCM2** | Modeled (Mask) | 0 | 4 | `$FFFF` | **Class 1/4** | SCC2 mask register. Wired to `update_internal_irq()`. |
| `$089A` | `$FC689A` | **SCCS2** | Shadow storage | 0 | 4 | `$0505` | **Class 1** | SCC2 status register. Stored in shadow. |
| `$08A0-$08AA` | `$FC68A0-$FC68AA` | **SCC3 (SCON/SCM/DSR/SCCE/SCCM/SCCS)** | Shadow storage | 0 | 0 | — | **Class 0** | SCC3 is untouched across all tested firmware paths. |
| `$08B0` | `$FC68B0` | **SPMODE** | Shadow storage | 0 | 0 | — | **Class 0** | Serial peripheral mode. Untouched. |
| `$08B2` | `$FC68B2` | **SIMASK** | Shadow storage | 0 | 0 | — | **Class 0** | Serial interface mask. Untouched. |
| `$08B4` | `$FC68B4` | **SIMODE** | Shadow storage | 0 | 4 | `$4189` | **Class 1** | Serial interface mode. Stored in shadow. |

---

## 4. Subsystem Detailed Analysis

### 4.1 Timer 1, Timer 2, and Watchdog Timer
- **Timer 1 (`$FC6840-$FC6848`):** Unused in tested workloads (`[VERIFIED]`, Class 0). The ASR-10 derives its operational system ticks and MIDI timing from the external DUART (`mc68681` timer and counter modes), not from 68302 Timer 1. Zero accesses occur during the tested paths.
- **Watchdog Timer (`$FC684A-$FC684C`):** At ROM bootstrap PC `$FFFB8E3E`, firmware writes `WRR <- $0000`. In the MC68302 manual, bit 0 of WRR is `EN` (Enable). Writing 0 disables the hardware watchdog counter. Because the watchdog is disabled at step 1 of boot, no watchdog timeout or counter polling was observed. Shadow storage (`m_shadow`) is sufficient for tested paths.
- **Timer 2 (`$FC6850-$FC6858`):** During boot, `TMR2` is configured with `$003B` and `TRR2` with `$3F01`. The OS reads `TCN2` exactly once during bootstrap probing (which returns shadow 0 without advancing), but never configures interrupts for Timer 2 (`IMR` bit 6 is permanently masked: `IMR = $E480`/`$EC80`, bit 6 is 0) and never polls `TCN2` in an active loop. Emulation of an active counting timer for Timer 2 is not causally required for tested workloads.

### 4.2 Serial Communication Controllers (SCC1, SCC2, SCC3)
- **SCC3 (`$FC68A0-$FC68AA`):** Untouched in tested workloads (`[VERIFIED]`, Class 0). Firmware does not configure or access the third serial channel in covered paths; board-level physical connection is unverified.
- **SCC1 & SCC2 (`$FC6880-$FC689A`):** Used strictly as high-speed serial receivers for audio ADC sample streams (LEFT on SCC1, RIGHT on SCC2).
  - Firmware initializes parameter RAM buffer descriptors at `$FC6400..$FC643E` and `$FC6500..$FC653E`.
  - When recording is triggered, firmware sets up IDMA to drain the samples into system RAM.
  - In the MAME emulation, audio recording is fed via IDMA transfers and memory injection (`stereo_round_trip.lua`). The communications processor does not run a microcode RISC protocol engine. Storing configuration in shadow RAM (`m_shadow`) while providing write-one-to-clear on `SCCE1/2` satisfies the entire driver and regression suite without regressions.
  - Active hardware serial framing and autonomous DMA descriptor advancement remain unmodeled.

### 4.3 Parallel I/O Ports A and B
- **Port A (`$FC681E-$FC6822`):** Modeled (`[VERIFIED]`). `PADAT` bit 4 controls the ES5510 ESP RUN/HALT line. Writes to `PADAT` invoke `m_pa_out_cb`, faithfully toggling ESP execution state during microcode upload.
- **Port B (`$FC6824-$FC6828`):** Modeled (`[VERIFIED]`). `PBCNT` and `PBDDR` readback latching was corrected in Track A. `PBDAT` output lines drive front panel / display multiplexing (`m_pb_out_cb`), while PB3 reads board-level input.

---

## 5. Disposition & Epistemic Boundaries

1. **Model Sufficiency for Covered Workloads (`[VERIFIED for tested paths]`):** The current combination of active functional models (`BR0-3/OR0-3`, `Port A`, `Port B`, `IDMA`, `Interrupt Controller`, `FC6860`) and shadow storage for configuration registers is sufficient for the tested workloads (ROM boot, floppy OS load, bank load, MIDI voice playback, panel interaction, and rate switching). This does NOT constitute general MC68302 architectural completeness, nor does it guarantee zero emulation risk for untested modes (e.g. live ADC sampling, external SCC synchronization).
2. **Exclusion of Unimplemented Regs from Tested Audio Defect (`[VERIFIED]`):** None of the remaining shadow-only registers (`WRR`, `TMR2`, `TRR2`, `SIMODE`, `SCM1/2`, `DSR1/2`, `SCCM1/2`, `SCCS1/2`, parameter RAM) interact with sample playback, wavetable address translation, or ES5506 voice programming in tested workloads. They are ruled out as direct causes for wrong pitch, distortion, or audio degradation in the verified paths.
3. **No Speculative Reimplementation:** Under the project rules (`AGENTS.md` Rule 1 & Rule 5), full modeling of the MC68302 CP RISC engine, transparent serial framing, or Timer 1/2 counting must NOT be implemented without a concrete, breaking firmware dependency.
