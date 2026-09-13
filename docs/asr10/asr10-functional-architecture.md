# Ensoniq ASR-10 Functional Architecture and Machine Model

**Document Status:** Primary Technical Architecture Reference
**Scope:** Ensoniq ASR-10 Digital System, MC68302 IMP, ES5506 OTTO, ES5510 ESP, ES5701 SuperGLU, Storage, Sequencer, and Memory Architecture
**Target Driver:** `src/mame/ensoniq/asr10_boot.cpp` (`asr10booth`)
**Baseline Production Commit:** `cca8be1ae339398461e53bc8ebaf303072edcca7` ("asr10: add 16 MiB sample memory backing")

---

## 1. Architectural Overview and Three-Layer Epistemic Framework

This document consolidates the complete functional architecture of the OpenASR Ensoniq ASR-10 machine model. It establishes the durable architectural understanding achieved across major project milestones, including MC68302 chip-select decoding, CS1 per-voice dynamic sample-memory translation, 16 MiB distinct sample-memory backing, authentic cross-megabyte WaveSample playback, the ES5701 documentary boundary, corrected ES5506 clock domains, firmware-driven ES5510 audio routing, and quiescent audio save-state support.

### The Three Architectural Layers

To avoid conflating observed software behavior with physical board circuitry, every subsystem in this document is analyzed across three distinct layers:

1. **Firmware-Visible Functional Model:**
   What the authentic Ensoniq boot ROM (1.50B) and operating system (V3.50) demonstrably configure, inspect, calculate, and depend upon at runtime.
2. **Current MAME Implementation:**
   How OpenASR models that behavior in MAME C++ (`asr10_boot.cpp`, `mc68302.cpp`, `es5506.cpp`, `cpu/es5510/es5510.cpp`, etc.).
3. **Physical Hardware Model:**
   What is definitively established, inferred, or remains open regarding the physical 4-layer ASR-10 Digital Board PCB, discrete logic, custom ASICs, and pin interconnections.

### Epistemic Terminology

The following standardized epistemic qualifiers are used throughout:

- **`[VERIFIED]`**: Directly established through reproducible execution, mathematical identity, or unambiguous hardware specification.
- **`[OBSERVED]`**: Directly witnessed via static disassembly or runtime bus/trace instrumentation.
- **`[INFERRED]`**: Deductive or inductive conclusion strongly supported by evidence, but lacking direct schematic or silicon proof.
- **`[LIKELY]`**: Converging circumstantial evidence without definitive falsification.
- **`[DISPROVEN]`**: Falsified under the stated model or abstraction.
- **`[OPEN]`**: Unresolved; requires discriminating empirical evidence or authentic documentation.
- **`[RULED OUT — origin / path / globally]`**: Negated within a specifically defined operational scope.
- **`[SUPERSEDED]`**: Historical project assumption replaced by later evidence.

---

## 2. Machine-Level Functional Architecture

```text
======================================================================================================
                                   ENSONIQ ASR-10 SYSTEM TOPOLOGY
======================================================================================================

               +--------------------------------------------------------------+
               |                    Motorola MC68302 IMP                      |
               |                                                              |
               |   +-----------------------+     +------------------------+   |
               |   |   68000 Core 16 MHz   |     |  System Integration    |   |
               |   |  (Y1 = 16.000000 MHz) |     |  Block (SIB) Registers |   |
               |   +-----------+-----------+     +-----------+------------+   |
               |               |                             |                |
               |   +-----------+-----------+     +-----------+------------+   |
               |   |  2 KB Internal DPRAM  |     |   Chip Selects CS0-3   |   |
               |   |   ($FC6000-$FC67FF)   |     |    (Decode Windows)    |   |
               |   +-----------------------+     +----+---+---+---+-------+   |
               +--------------------------------------|---|---|---|-----------+
                                                      |   |   |   |
         +--------------------------------------------+   |   |   +--------------------------+
         | CS0                                            |   |                              | CS3
         v                                                |   v CS2                          v
+------------------+                                      | +--------------------+   +-------------------+
|  Boot / OS ROM   |                                      | |  Audio Subsystem   |   | Peripheral Glue   |
| 256 KB ($F80000) |                                      | |                    |   |                   |
+------------------+                                      | |  ES5506 OTTO Sound |   | NEC uPD72069 FDC  |
                                                          | |  ($FC2000-$FC207F) |   | ($FC4000-$FC4003) |
         +------------------------------------------------+ |                    |   |                   |
         | CS1 (Write-qualified MC68302 decode, Ext DTACK)  |  ES5510 ESP DSP    |   | SCN2681 DUART     |
         v                                                  |  ($FC3000-$FC31FF) |   | ($FC4800-$FC481F) |
+------------------------------------+                      +--------------------+   |                   |
| Per-Voice Sample Banking Registers |                                               | WD33C93A SCSI     |
| 32 voices x 4 words ($FF7F00)      |                                               | ($FC5000-$FC5003) |
+-----------------+------------------+                                               +-------------------+
                  |
                  | [INFERRED translation]
                  v
+--------------------------------------------------------------------------------------------------------+
|                                16 MiB DRAM Sound & System Memory                                       |
|                                                                                                        |
|  $000000-$0FFFFF : 1 MiB Lowmem / System Variables / Buffers (Writes tracked for $0CE3 rate mode)      |
|  $100000-$1FFFFF : 1 MiB Sample-RAM region                                                             |
|  $200000-$F7FFFF : 14 MiB Expanded Sample-RAM / Recording Buffers                                      |
+--------------------------------------------------------------------------------------------------------+
```

---

## 3. CPU and System Control (Motorola MC68302 IMP)

### 3.1 Architecture and Processor Core
- **Firmware-Visible:** The central processor behaves as a standard 16-bit big-endian Motorola 68000 microprocessor operating at 16 MHz, executing code out of ROM, lowmem RAM, and internal DPRAM.
- **Current MAME:** Implemented via `mc68302_device` (derived from `m68000_device`) instantiated at `XTAL(16'000'000)` (`asr10_boot.cpp:825`).
- **Physical Hardware:** Motorola MC68302 Integrated Multiprotocol Processor (IMP), driven by crystal oscillator `Y1 = 16.000000 MHz`.

### 3.2 System Integration Block (SIB) & Internal DPRAM Window
- **Firmware-Visible:**
  - Base Address Register (`BAR`, internal word at `$0000F2`) is initialized during cold boot to relocate the internal 4 KB SIB window to `$FC6000-$FC6FFF`.
  - `$FC6000-$FC67FF` (2 KB) serves as internal dual-port RAM (DPRAM). The OS uses DPRAM for:
    1. A reset bridge at `$FC6200` to reprogram `CS0` out of ROM execution space `[VERIFIED]`.
    2. Exception vectors and jump dispatchers on a stride-6 grid beginning at `$FC6014` `[VERIFIED]`.
    3. Fast register-write thunks utilizing the `$4AFC` (ILLEGAL) and `$F000` (Line-F) exception mechanisms to perform `MOVEP` accesses and advance the PC `[VERIFIED]`.
  - `$FC6800-$FC6FFF` maps the internal memory-mapped registers (SIM, IDMA, Communications Processor, Timers, and Port A/B registers).
- **Current MAME:**
  - `mc68302_device::bootstrap_map` handles accesses to `$000000F0-$000000FF` (`bar_scr_r` / `bar_scr_w`).
  - Writing `BAR` invokes `mc68302_device::install_internal_window()`, which dynamically overlays the 4 KB range into the CPU program space (`$FC6000-$FC6FFF`).
  - In `device_post_load()`, the internal window is unmapped and reinstalled to guarantee save-state integrity `[VERIFIED]`.
- **Physical Hardware:** Internal static RAM and peripheral registers integrated on the MC68302 silicon die.

### 3.3 Parallel I/O Ports (Port A and Port B)
- **Port A:**
  - **Firmware-Visible:** Bit 4 of Port A Data Register (`PADAT`, `$FC6822/$FC6823`) controls hardware execution of the ES5510 ESP DSP:
    - `PA4 = 0`: ESP halted during program/GPR microcode upload (`$FFF977B0`).
    - `PA4 = 1`: ESP running (`$FFF977C6`).
  - **Current MAME:** `mc68302_pa_w` monitors bit 4:
    ```cpp
    const bool run = BIT(data, 4);
    if (m_es5510_host)
        m_es5510_host->set_HALT(!run);
    ```
  - **Physical Hardware:** Dedicated GPIO line on MC68302 pin `PA4` routed to the ES5510 `/HALT` or control logic.
- **Port B:**
  - **Firmware-Visible:**
    - `PB2-PB0`: Outputs driving the analog multiplexer channel select (0 = Pitch Wheel, 2 = Mod Wheel, 3 = Volume Slider, 4 = Foot Pedal, 5 = Data Entry Slider, 7 = 2.5V calibration reference) `[VERIFIED]`.
    - `PB3`: General-purpose input receiving the board-level LRCLK framing signal `[OBSERVED]`.
    - `PB9`: Input dedicated to ES5506 OTTO interrupt request line (`IRQB`) `[VERIFIED]`.
  - **Current MAME:**
    - `m_maincpu->pbdat_latch()` exposes low bits to `analog_r()`.
    - `m_lrclk_timer` toggles `PB3` at 44.1 kHz (`asr10_boot.cpp:535`).
    - `es5506_irq_w` invokes `m_maincpu->set_pb_input(9, state != 0)`.
    - `PBCNT` (`$FC6824`) and `PBDDR` (`$FC6826`) readback latches preserve programmed state (`commit fdd8f734bd1`) `[VERIFIED]`.
  - **Physical Hardware:**
    - `PB2-PB0` connect to a 74HC4051 8-channel analog multiplexer.
    - `PB3` connects to external audio framing logic.
    - `PB9` connects to ES5506 pin 98 (`/IRQB`). Active-low physical pin polarity vs active-high internal latch status remains `[OPEN]`.

### 3.4 Interrupt Architecture
- **Firmware-Visible:**
  - Level 1 (`IRQ1`): External peripheral interrupt shared by NEC uPD72069 FDC (`INTRQ`) and WD33C93A SCSI (`IRQ`). Acknowledged via vector `$51` (`$0144`) with continuation handler at `$0402` `[VERIFIED]`.
  - Level 4 (`IRQ4`): MC68302 internal interrupt controller, aggregating SCC1, SCC2, IDMA, and Port B pin interrupts (PB9). Vectors generated dynamically via `GIMR` base:
    - SCC1: vector `$0D` (e.g. `$4D`)
    - IDMA: vector `$0B` (e.g. `$4B`)
    - SCC2: vector `$0A` (e.g. `$4A`)
    - PB9 (ES5506 OTTO): vector `$07` (e.g. `$47`, jumping to `$F8D072`) `[VERIFIED]`.
  - Level 6 (`IRQ6`): External DUART interrupt from SCN2681, driving system tick timers and MIDI/panel serial processing `[VERIFIED]`.
- **Current MAME:**
  - `cpu_space_map` intercepts M68000 interrupt acknowledge cycles:
    - Level 1 / Level 6 dispatch via `maincpu_iack_r()` returning `irq1_ack_vector()` / `irq6_ack_vector()`.
    - Level 4 dispatches via `m_maincpu->irq4_ack_vector()`, resolving pending bits in `IPR` masked by `IMR` and `ISR`.
- **Physical Hardware:** Standard 68000 IPL2-IPL0 lines driven by internal MC68302 interrupt priority encoder and external board glue.

### 3.5 DMA Subsystem (IDMA)
- **Firmware-Visible:** Single-channel Independent Direct Memory Access (IDMA) controller programmed via `CMR`, `SAPR`, `DAPR`, `BCR`, `CSR`, and `FCR`:
  - Mode `$0D51`: External peripheral byte-feed transfer used for floppy disk transfers (`dma_r()` pops bytes from uPD72069 FIFO) `[VERIFIED]`.
  - Mode `$37A1`: Internal incrementing 16-bit word transfer used for block copies `[VERIFIED]`.
  - `SAPR` / `DAPR` preserve byte lanes across 8-bit writes (`commit 5c6a8993390`) `[VERIFIED]`.
- **Current MAME:** Integrated in `mc68302.cpp`. Transfers proceed synchronously or via timer; terminal count (`TC`) for FDC is delivered via zero-delay timer `m_idma_tc_timer` to avoid reentrancy deadlock inside MFM decoding loops (`asr10_boot.cpp:504`) `[VERIFIED]`.
- **Physical Hardware:** Dedicated on-chip IDMA hardware engine on MC68302.

---

## 4. Memory Architecture and Address Decoding

### 4.1 Address Map Overview

The 16 MB (24-bit) CPU address space is structured as follows:

| CPU Byte Address Range | Bus Decoding Mechanism | Description / Target Device |
| :--- | :--- | :--- |
| **`$000000-$03FFFF`** | **MC68302 CS0** (at Reset) | Boot ROM overlay (256 KB) until reprogrammed |
| **`$000000-$0FFFFF`** | Board DRAM Logic | System RAM (Lowmem, 1 MiB): vectors, OS variables, heaps, buffers |
| **`$100000-$1FFFFF`** | Board DRAM Logic | Sample Memory (1 MiB base region) |
| **`$200000-$F7FFFF`** | Board DRAM Logic | Expanded Sample Memory (up to 14 MiB; total 15.5 MiB RAM) |
| **`$F80000-$FBFFFF`** | **MC68302 CS0** (Runtime) | System OS ROM (256 KB, EPROM pair) |
| **`$FC2000-$FC207F`** | **MC68302 CS2** | Ensoniq ES5506 OTTO Sound Generator Host Interface |
| **`$FC3000-$FC31FF`** | **MC68302 CS2** | Ensoniq ES5510 ESP DSP Host Interface |
| **`$FC4000-$FC4003`** | **MC68302 CS3** | NEC uPD72069 Floppy Disk Controller |
| **`$FC4800-$FC481F`** | **MC68302 CS3** | Philips/Signetics SCN2681 Dual UART (Panel / MIDI) |
| **`$FC5000-$FC5003`** | **MC68302 CS3** | Western Digital WD33C93A SCSI Controller |
| **`$FC6000-$FC6FFF`** | **MC68302 BAR** | Internal MC68302 DPRAM (2 KB) and SIB Registers (2 KB) |
| **`$FF6000-$FF7FFF`** | **MC68302 CS1** | External Voice Banking Window (active table at `$FF7F00-$FF7FFF`) |

### 4.2 System RAM Backing Model
- **Firmware-Visible:**
  - During boot, V3.50 OS executes an alias-detection probe at ROM `$F8A166-$F8A244`. It writes test patterns (`$0000`, `$1111`, `$2222`, `$3333`) to `$008000`, `$408000`, `$808000`, and `$C08000`, then reads back `$008000` and `$808000`.
  - When distinct physical RAM is present at high addresses, `$008000` reads `$0000` and `$808000` reads `$2222`. The OS takes its 16 MiB branch, configuring the system heap with base `$00000000` and size `$00F80000` (15.5 MiB available) `[VERIFIED]`.
  - Writes to `$000CE2/$000CE3` set the current-effect audio operating mode (`0` = 29.76 kHz, `1` = 44.1 kHz) `[VERIFIED]`.
- **Current MAME:**
  - Backed by a single, contiguous 16 MiB dynamically allocated memory array:
    ```cpp
    m_system_ram = make_unique_clear<u16[]>(SYSTEM_RAM_WORDS); // 16 MiB / 2 = 8,388,608 words
    ```
  - Both CPU access handlers (`system_memory_r/w`, `sample_memory_r/w`, `expanded_memory_r/w`) and the ES5506 wavetable fetch handler (`es5506_wavetable_r`) access `m_system_ram` directly.
  - The historical modulo-`$200000` (2 MiB) alias wraparound is completely eliminated (`commit cca8be1ae33`) `[VERIFIED]`.
- **Physical Hardware:**
  - Base machine digital board contains 2 MB of DRAM (typically eight 256K $\times$ 4 DRAM DIPs or SIMM sockets).
  - Expanded via two standard 30-pin or 72-pin SIMM sockets to 4, 8, 10, or 16 MB.
  - Exact board-level DRAM refresh, multiplexing, and RAS/CAS generation are implemented in external PALs and glue logic `[OPEN]`.

---

## 5. MC68302 Chip Select Subsystem (CS0–CS3)

### 5.1 Programmed Configuration Table

Firmware in ROM initializes the MC68302 Base Registers (`BR0-BR3`) and Option Registers (`OR0-OR3`) at `$F8001E-$F8005D`:

| Chip Select | Base Register (`BR`) | Option Register (`OR`) | Address Decode Window | Directionality | DTACK Mode | Mapped Hardware Subsystem |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **CS0 (Reset)** | `$0001` | `$3F82` | `$000000-$03FFFF` (256 KB) | Read-only (`RW=0, MRW=1`) | Internal 1 WS | Boot EPROM Overlay |
| **CS0 (Runtime)**| `$1F01` | `$3F82` | `$F80000-$FBFFFF` (256 KB) | Read-only (`RW=0, MRW=1`) | Internal 1 WS | System OS EPROM |
| **CS1** | `$1FEF` | `$FFFE` | **`$FF6000-$FF7FFF`** (8 KB) | Write-qualified (`RW=1, MRW=1`) | **External** (`DTACK=111`) | **Per-Voice Sample Banking Registers** (`$FF7F00-$FF7FFF`) |
| **CS2** | `$1F85` | `$FFFC` | `$FC2000-$FC3FFF` (8 KB) | Read / Write (`MRW=0`) | External (`DTACK=111`) | ES5506 OTTO (`$FC2000`), ES5510 ESP (`$FC3000`) |
| **CS3** | `$1F89` | `$7FFC` | `$FC4000-$FC5FFF` (8 KB) | Read / Write (`MRW=0`) | Internal 3 WS | uPD72069 FDC, SCN2681 DUART, WD33C93A SCSI |

### 5.2 Chip Select Nuances and Epistemic Boundaries
1. **Static Driver Mapping vs. Dynamic Pin Decoding:**
   - **Current MAME:** `mem_map()` installs static address handlers for devices in the `$FC2000-$FC5FFF` and `$FF7F00-$FF7FFF` windows rather than dynamically routing bus accesses through live MC68302 `/CS0`..`/CS3` output pins. This is a functional modeling convenience, not an architectural defect `[VERIFIED MODEL STATUS]`.
   - **Exception:** `CS0` overlay behavior is dynamically evaluated in `low_rom_or_lowmem_r` via `m_maincpu->cs0_covers(0)` `[VERIFIED]`.
2. **MC68302 CS1 Configuration vs. Firmware-Visible Readback:**
   - **[VERIFIED — MC68302 configuration]:** `BR1`/`OR1` program the MC68302 CS1 decode as write-qualified/write-only (`RW=1, MRW=1`). Under strict MC68302 specification, the chip will not assert its internal `/CS1` logic on CPU read cycles.
   - **[OBSERVED — firmware-visible board behavior]:** At OS `$007C68`, during ordinary voice setup, firmware executes a read from table word 2:
     ```assembly
     move.w ($FF7F04), D0   ; Read table word 2
     addq.w #1, D0          ; Derive word 3
     move.w D0, ($FF7F06)   ; Write table word 3
     ```
   - **MAME Implementation:** `asr10_boot.cpp` maps `$FF7F00-$FF7FFF` as `.rw(voice_bank_r, voice_bank_w)`. The current MAME `.rw()` mapping is therefore a functional approximation supported by observed firmware behavior and must not be portrayed as contradicting an established board-level write-only interface.
   - **[OPEN — physical]:** The physical mechanism providing that read decode is unknown. External board decode may respond to CPU read cycles at `$FF7F00-$FF7FFF` independently of the MC68302 `/CS1` pin, or board glue qualifies the read.

---

## 6. Sample Addressing, Voice Banking, and WaveSample Object Model

### 6.1 WaveSample Data Structures

The Ensoniq ASR-10 firmware organizes audio playback around an object hierarchy stored in system RAM:

```text
+------------------------------------------------------------------------------------+
|                                 INSTRUMENT OBJECT                                  |
|                                                                                    |
|  +$00 : Instrument header and total allocation size                                |
|  +$64 : Layer pointer table (up to 8 layers, 4-byte packed relative pointers)      |
|  +$84 : WaveSample pointer table (up to 127 WaveSamples, 4-byte packed pointers)   |
+------------------------------------------+-----------------------------------------+
                                           |
                                           v
+------------------------------------------------------------------------------------+
|                                 WAVESAMPLE OBJECT                                  |
|                                                                                    |
|  +$00 : Allocator block metadata (extent and free flag)                            |
|  +$0A : WaveSample name (12 characters, alternating ASCII bytes)                   |
|  +$22 : PCM Owner WaveSample ID (Word: 0 = owns PCM; >0 = references another WS)   |
|  +$EE : Playback / Loop Mode:                                                      |
|         0 = Forward One-Shot, 1 = Reverse One-Shot,                                |
|         2 = Forward Loop,     3 = Bidirectional Loop                               |
|  +$F0 : Sample START offset (32-bit big-endian fixed point, MOVEP.L)               |
|  +$F8 : Sample END offset   (32-bit big-endian fixed point, MOVEP.L)               |
|  +$100: Loop START offset   (32-bit big-endian fixed point, MOVEP.L)               |
|  +$108: Loop END offset     (32-bit big-endian fixed point, MOVEP.L)               |
+------------------------------------------+-----------------------------------------+
                                           |
                                           v
+------------------------------------------------------------------------------------+
|                               PCM OWNER DATA BLOCK                                 |
|                                                                                    |
|  +$000..+$11F : 288-byte ($120) WaveSample Header overhead                         |
|  +$120        : First byte of raw signed 16-bit PCM audio data                     |
+------------------------------------------------------------------------------------+
```

#### Key Object Model Principles:
1. **Selected WaveSample $\ne$ PCM Owner:**
   A selected WaveSample often contains only playback parameters (envelopes, pitch, loop points) and points via `+$22` to an entirely separate WaveSample that owns the actual PCM allocation `[VERIFIED]`.
2. **Endpoint Representation:**
   Offsets at `+$F0`, `+$F8`, `+$100`, `+$108` are read using `MOVEP.L` into a 32-bit register $R$:
   - Integer 16-bit word offset = $R \gg 9$
   - Integer byte offset = $(R \gg 8) \ \& \ \sim 1$
   - Loop-END sub-sample fraction = $R \gg 5$ (upper 4 bits of fraction utilized by ES5506 END register) `[VERIFIED]`.
3. **Disambiguation of Sizing Quantities:**
   - *Instrument Allocation Size:* Total memory allocated for instrument structure, layers, WS headers, and PCM.
   - *WaveSample Header Allocation:* 288 bytes (`$120`) of parameter metadata.
   - *PCM-Owner Allocation:* Contiguous memory block holding `$120` header plus audio samples.
   - *Sample END Extent:* Playback endpoint offset relative to owner PCM start.
   - *Total Sample Memory:* Global DRAM occupied across all loaded instruments.

### 6.2 CS1 Per-Voice Sample Banking Translation

The Ensoniq ES5506 OTTO sound generator natively features a 21-bit wavetable address bus ($A20-A0$), allowing it to directly address 2 MegaWords (4 MegaBytes) per voice. To allow 32 independent voices to dynamically access up to 16 MegaBytes of sample RAM, Ensoniq implemented external per-voice bank translation logic mapped to MC68302 CS1:

```text
======================================================================================================
                        ES5506 LOGICAL-TO-PHYSICAL SAMPLE ADDRESS TRANSLATION
======================================================================================================

     ES5506 Voice Output Channel (0..31)
                    |
                    v
     Voice Index Selection:
       entry = (voice > 0) ? (voice - 1) : 0
                    |
                    +----------------------------------------+
                                                             |
     ES5506 Logical Word Address (21 bits: A20..A0)          |
                    |                                        |
     +--------------+--------------+                         |
     |                             |                         |
     v (Bits 20:19)                v (Bits 18:0)             |
  Page Index (0..3)          Sub-Offset (512K words)         |
     |                             |                         |
     v                             |                         v
  +--------------------------------+----------------------------+
  | CS1 Per-Voice Banking Table ($FF7F00 + entry * 8 bytes)     |
  |                                                             |
  |   Word 0 (Page 0) -> Physical Megabyte Index (4 bits: 0..15)|
  |   Word 1 (Page 1) -> Physical Megabyte Index (4 bits: 0..15)|
  |   Word 2 (Page 2) -> Physical Megabyte Index (4 bits: 0..15)|
  |   Word 3 (Page 3) -> Physical Megabyte Index (4 bits: 0..15)|
  +--------------------------------+----------------------------+
                                   |
                                   v
             megabyte = table[entry][page]
                                   |
                                   v
  +-------------------------------------------------------------+
  | Translated Physical Byte Address:                           |
  |   phys_byte_addr = (megabyte << 20) | (sub_offset << 1)     |
  +--------------------------------+----------------------------+
                                   |
                                   v
                   16 MiB DRAM Backing Store
```

#### Formal Mathematical Mapping:
For a PCM Owner beginning at physical 24-bit byte address $P$:
$$N = (P \gg 20) \ \& \ \text{\$0F} \quad (\text{Starting Megabyte Index})$$
$$B = P \ \& \ \text{\$0FFFFF} \quad (\text{Intra-Megabyte Byte Offset})$$
Firmware initializes the active voice's CS1 table entry with:
$$\text{table}[\text{entry}][0..3] = [N, \ N+1, \ N+2, \ N+3]$$
When the ES5506 fetches sample word $W$:
$$\text{entry} = (V > 0) \ ? \ (V - 1) : 0$$
$$\text{page} = (W \gg 19) \ \& \ 3$$
$$\text{sub\_offset} = W \ \& \ \text{\$7FFFF}$$
$$\text{physical\_byte\_address} = (\text{table}[\text{entry}][\text{page}] \ll 20) \mid (\text{sub\_offset} \ll 1)$$

### 6.3 Historical Defect Resolution (Bank 11 Playability)
- **Defect:** Under the historical MAME implementation, ES5506 Bank 1 was hardcoded to low memory (`m_lowmem_shadow`, Chunk 0) and Bank 0 was hardcoded to sample memory (`m_sample_ram`, Chunk 1).
- **Failure Mode:** Because ASR-10 firmware always programs `CR = 0x4300` (selecting Bank 1), any instrument with PCM allocated in odd megabytes (`BLUES DRUMS` in `ATRK TUT BNK` at physical MB 7) was fetched out of Chunk 0 instead of Chunk 1. This resulted in near-silent clicks (peak 486) or phantom playback of leftover data from earlier loads.
- **Causal Fix:** Implementing dynamic CS1 voice banking resolved the defect immediately (`commit b7cd112199d`). `BLUES DRUMS` output peak jumped from 486 (1.5%) to 14,536 (44.4%), with 0.998892 cross-correlation between fresh-boot and pre-warmed execution paths `[VERIFIED CAUSAL]`.

### 6.4 Authentic Cross-Megabyte Playback Verification
Authentic confirmation of CS1 translation occurs in the `JM DRUMS` fixture (Layer 1, WS 15, MIDI Note 100):
- **PCM Allocation:** Starts at `$6F7F20` (Megabyte 6).
- **CS1 Mapping:** Programmed as `[6, 7, 8, 9]`.
- **Sample Boundary:** The sample payload spans 43,832 bytes, crossing the 1 MiB boundary from physical Megabyte 6 into Megabyte 7:
  - Logical Word `$07FFFE` $\rightarrow$ Physical `$6FFFFC` (MB 6, Page 0)
  - Logical Word `$07FFFF` $\rightarrow$ Physical `$6FFFFE` (MB 6, Page 0)
  - Logical Word `$080000` $\rightarrow$ Physical `$700000` (MB 7, Page 1)
  - Logical Word `$080001` $\rightarrow$ Physical `$700002` (MB 7, Page 1)
- **Bit-Identical Verification:** All 43,832 fetched bytes matched the raw source data extracted from the V3.50 floppy disk image (`SHA-256: 09baa2adb3...`) with zero mismatches `[VERIFIED / OBSERVED]`.

### 6.5 Large WaveSample Continuation
- **Documentary Evidence:** The Ensoniq ASR-10 Service Manual (Nov 1995, p. 31) explicitly references playback of regular WaveSamples exceeding 2M samples (4 MegaBytes), ruling out a universal 4 MB hardware ceiling `[VERIFIED]`.
- **Firmware Continuation Architecture:**
  - ROM `$F8E27E` checks whether the endpoint exceeds `$40000000` (2 MegaWords / 4 MegaBytes).
  - For large endpoints, firmware initializes temporary equal loop boundaries (`START == END`) at 2 MegaBytes, arms `IRQE` + transwave mode (`BLE`), and binds ISR continuation callbacks (`$FFFF8EE2` forward, `$FFFF8EEE` reverse) `[VERIFIED]`.
  - Continuation handler updates CS1 translation window pages, rescales boundaries across alternating buffer slices, and finally reinstates one-shot mode (`CR = $4304`) with the true terminal endpoint `[VERIFIED]`.
- **Resolution in Generic MAME:**
  - Historical check `if (voice->start == voice->end) voice->control |= CONTROL_STOP0;` in `src/devices/sound/es5506.cpp` removed (aligning with upstream 2016 ES5505 commit `9ded714f316c` where zero-length loops run on real hardware for transwaves) `[VERIFIED UPSTREAM PARITY]`.
- **Epistemic Classification:**
  - `[VERIFIED FUNCTIONAL MODEL / ACCEPTANCE PASSED]`: Authentic end-to-end execution of an individual WaveSample $>4\text{ MiB}$ verified on CDR-03 `AUDIO DEMOS/ICY TACO` WS1 (~6.4 MiB PCM). 3 dynamic continuation transitions observed, Level 4 Vector $47$ interrupt serviced, clean terminal transition, 28.87 seconds continuous audio playback.

---

## 7. Audio Generation (Ensoniq ES5506 OTTO)

### 7.1 Architecture and Clock Domains
The Ensoniq ES5506 (OTTO) sound generator produces 32 independent multiplexed voice channels:
- **Sample Rate Equation:**
  $$F_s = \frac{\text{Master Clock}}{16 \times (\text{ACTV} + 1)}$$
- **Authentic Operating Modes (Firmware Variable `$0D66`):**
  - **Mode 0 (Standard 30 kHz Mode):**
    - Master Clock: $Y2 / 2 = 30.476180\text{ MHz} / 2 = 15,238,090\text{ Hz}$
    - Active Voices: $\text{ACTV} = 31$ (32 slots)
    - Output Rate: $F_s = 15,238,090 / (16 \times 32) = 29,761.895\text{ Hz}$ `[VERIFIED]`
  - **Mode 1 (High-Quality 44.1 kHz Mode):**
    - Master Clock: $Y3 / 2 = 33.868800\text{ MHz} / 2 = 16,934,400\text{ Hz}$
    - Active Voices: $\text{ACTV} = 23$ (24 slots)
    - Output Rate: $F_s = 16,934,400 / (16 \times 24) = 44,100.000\text{ Hz}$ `[VERIFIED]`
- **Historical Clock Fix:** Earlier MAME revisions supplied undivided crystal clocks ($30.476\text{ MHz}$), doubling the internal stream rate and causing octave-high playback. Clocking the device at $Y2/2$ and $Y3/2$ established exact 1:1 mathematical alignment with firmware pitch tables and eliminated the defect `[VERIFIED]`.
- **Physical Mux/Divider:** The physical board divider and multiplexer routing $Y2/Y3$ to ES5506 pin `CLKIN` remain `[OPEN]`.

### 7.2 Voice Stepping and Loop Behavior
- **Accumulator:** 32-bit register (21 integer word address bits, 11 fractional bits). Frequency Control (`FC`) register adds an intra-sample delta on every active frame.
- **Loop Modes (Control Register bits 4:3):**
  - Mode 0 (`LPE=0, BLE=0`): One-shot forward. Halts with `STOP0` upon reaching `END`.
  - Mode 1 (`LPE=0, BLE=1`): Transwave. Jumps to opposite boundary, clears loop bits, and asserts `LEI`.
  - Mode 2 (`LPE=1, BLE=0`): Unidirectional forward loop. Wraps to `START` preserving fractional overshoot.
  - Mode 3 (`LPE=1, BLE=1`): Bidirectional loop. Reverses direction bit (`DIR`) upon reaching `END` or `START`. Autonomous multi-cycle turns verified on `JM DIGI SYN` without CPU intervention `[OBSERVED]`.

---

## 8. Audio Processing and Signal Routing (Ensoniq ES5510 ESP)

### 8.1 Device Architecture and Host Protocol
The Ensoniq ES5510 (ESP) is a programmable digital signal processor executing 24-bit audio effects:
- **Host Interface Window (`$FC3000-$FC31FF`):**
  - `$FC3001-$FC303F`: Register / latch block (offsets `$00-$1F`).
  - `$FC3101`: Read Select (`$80`).
  - `$FC3141`: Write Select GPR (`$A0`).
  - `$FC3181`: Write Select INSTR (`$C0`).
  - `$FC31C1`: Write Select GPR + INSTR (`$E0`, required by firmware effect record type 1) `[VERIFIED]`.
- **Execution Control:** Controlled by MC68302 `PA4`. When `PA4 = 0`, ESP is held in reset/halt during program microcode upload. When `PA4 = 1`, ESP execution begins (`asr10_boot.cpp:639`).

### 8.2 Inter-IC Serial Audio Routing
Audio data streams serially between the ES5506 sound generator, ES5510 effects processor, and output converters via `esq_5505_5510_pump_device`:

```text
+-----------------------+              +-----------------------+              +-----------------------+
|      ES5506 OTTO      |              |      ES5510 ESP       |              |    Audio Output       |
|                       |              |                       |              |                       |
| Pair 0 (Ch 0/1, BUS1) +------------->| SER0 (Inputs 2/3)     |              |                       |
| Pair 1 (Ch 2/3, BUS2) +------------->| SER2 (Inputs 6/7)     |              |                       |
| Pair 2 (Ch 4/5, BUS3) +------------->| SER3 (Inputs 0/1)     |              |                       |
|                       |              |                       |              |                       |
|                       |              | SER1 (Outputs 0/1)    +------------->| Final Stereo Output   |
|                       |              |                       |              | (Speaker L / R)       |
+-----------------------+              +-----------------------+              +-----------------------+
```

- **AUX Channels (CA3-CA5):** Auxiliary channels on ES5506 outputs 6–11 are routed to optional external expansion outputs (OEX-6/OEX-8) and are unmodeled in the current base machine `[UNIMPLEMENTED FEATURE]`.
- **Effect Upload Lifecycle:** ASR-10 OS downloads valid effect files (Type `$21`, e.g. `44LUSH`) into RAM, transfers microcode through `$FC3000-$FC31FF`, and toggles `PA4`. The historical `EFFECT DOWNLOAD FAILED` causal defect in the emulator was the missing `$FC31C1` (host offset `$E0`, write select GPR + INSTR) memory-map write handling / verify path. Separately, selecting `FX=BANK` or `FX=INST` in user workflow without an effect file present in RAM triggers a benign firmware descriptor validation abort (`cmpi.l`) before any FDC/ESP bus traffic occurs `[OBSERVED]`.

---

## 9. ASIC and Board Glue Boundary (Ensoniq ES5701 SuperGLU)

### 9.1 Specified Capabilities of ES5701
Audit of the authentic Bob Yannes specification (`docs/ensoniq/ES5701.pdf`, Rev. 2) establishes the functional scope of the ES5701 SuperGLU gate array:
1. **Host Bus Interface:** Decodes 68000 CPU bus cycles to ES5510 (`/ESP`) and ES5506 (`/OTIS`).
2. **Sound Memory Interface:** Demultiplexes OTIS address/data lines to static memory with support up to address line `LA19` (1 MB addressing range).
3. **Clock Generation:** Divides a 20 MHz oscillator by 2 to 10 MHz, and a 16 MHz oscillator by 2 to 8 MHz.
4. **Dynamic Bus Sizing:** Manages byte-to-word translation and DTACK generation for peripheral cycles.

### 9.2 Epistemic Frontiers and Storage-Model Boundary
- **`[DISPROVEN — specified storage model]`:** The specified ES5701 storage/register model cannot itself store the complete per-voice CS1 translation table or act as the sole table store (its documented interface lacks a 256-byte register/RAM array, and specified address outputs reach only up to `LA19`).
- **`[OPEN]`:** Whether physical CS1, derived CS1 decode, voice/page signals, or related board signals reach or pass through ES5701 remains unestablished without Digital Board schematics.
- **OpenASR Model:** OpenASR does not instantiate a monolithic `es5701_device`. Its functional responsibilities (bus adaptation, clock scaling, memory mapping) are cleanly partitioned between `asr10_boot.cpp`, `mc68302.cpp`, and the sound devices `[FUNCTIONALLY SUFFICIENT APPROXIMATION]`.

---

## 10. Storage Subsystem (Floppy Disk & SCSI)

**Status:** `[VERIFIED / FUNCTIONALLY CLOSED / FROZEN]` (Milestone 2026-09-03)

```text
+------------------------------------------------------------------------------------+
|                             ASR-10 STORAGE PIPELINE                                |
+------------------------------------------------------------------------------------+

   +--------------------------+                     +--------------------------+
   |  NEC uPD72069 / uPD765   |                     |     WD33C93A SCSI        |
   |  Floppy Disk Controller  |                     |  Bus Controller ($FC5000)|
   |       ($FC4000)          |                     +------------+-------------+
   +------------+-------------+                                  |
                |                                                |
                | DRQ                                            | DRQ
                v                                                v
   +---------------------------------------------------------------------------+
   |                     MC68302 IDMA Controller Engine                        |
   |                                                                           |
   |  - Mode $0D51: External Peripheral Byte Ingress via dma_r()               |
   |  - Interrupt: IRQ1 (Vector $51) shared by FDC and SCSI completions        |
   |  - Delayed TC Timer: Delivers TC to FDC outside reentrant live_run() loop |
   +-------------------------------------+-------------------------------------+
                                         |
                                         v
   +---------------------------------------------------------------------------+
   |                      Ensoniq File Systems & Media                         |
   |                                                                           |
   |  - 3.5" HD Floppy (1.6 MB MFM, ASR10IMG, ESQIMG, HFE)                     |
   |  - SCSI ID 0: Hard Disk Drive (NSCSI_HARDDISK, Ensoniq FS)                |
   |  - SCSI ID 4: CD-ROM Drive (NSCSI_CDROM, 512-byte sectors, CDR-1 .. CDR-4)|
   +---------------------------------------------------------------------------+
```

### Key Storage Findings:
1. **uPD72069 Standby Auxcmd Fix:**
   Upstream MAME `upd765.cpp` previously mishandled standby auxcmds (`$35` / `$34`), leaving `MSR_CB` stuck high. Correcting this in generic MAME enabled clean SCSI device switching and flawless loading from authentic CD-ROM media (`investigations/upd72069-standby-auxcmd-fix.md`) `[VERIFIED]`.
2. **Ready Line Modeling:**
   `set_ready_line_connected(false)` models the ASR-10 physical board wiring where drive ready is tied permanently asserted, preventing spurious interrupt storms `[VERIFIED]`.

---

## 11. Sequencer Subsystem

The ASR-10 firmware includes a 16-track MIDI / audio sequencer executing within the main operating system:

```text
Sequence Stream Decoder ($FFB180 -> $B380)
      | (allokerar spårnod via TRAP #3)
Track Event Queue (+$1E(A4))
      | (traverseras av Steppern vid $F902D2)
Delta-Time Countdown (subq.w #1, 2(A5) == 0)
      |
Vector 0 Dispatch ($FF9502 -> $FFF90318)
      |
Opcode $0538: BTST D2, $0CDE.w  (D2 = Event Type - 1)
      | (Bit 0 = 1 för NOTE-event när instrument är laddat, $0CDE = $3F)
TRAP #3 (os_alloc_event_node -> allokerar utgående nod A5)
      | (kopierar timing & notdata från A3 till A5, sätter flagga $40)
TRAP #C / TRAP #12 (os_scheduler_post till Sound Queue $00DC)
      |
Sound Scheduler Consumer
      |
ES5506 Voice Allocator & Register Writes ($FC2000-$FC207F)
```

- **Event Mask `$0CDE`:** Dynamic filter mask established during instrument/bank loading (`$00` idle $\rightarrow$ `$3C` bank $\rightarrow$ `$3F` ready). NOTE events (Type 1) test bit 0 ($D2 = 1 - 1 = 0$) `[VERIFIED]`.
- **Disproven Bit 5 Hypothesis:** The historical belief that NOTE playback requires bit 5 was disproven: opcode `$0538` is a dynamic `BTST D2, $0CDE.w` instruction, not a static bit 5 test `[DISPROVEN]`.

---

## 12. State Serialization and Save-State Architecture

**Status:** `MACHINE_SUPPORTS_SAVE` (Tag: `openasr-audio-savestate-ready`)

OpenASR supports full serialization and restoration of quiescent machine state:

### 12.1 Covered Subsystems
- **Canonical RAM:** Entire 16 MiB array `m_system_ram` serialized directly (`save_pointer`).
- **CS1 Voice Banking Table:** $32 \times 4$ array `m_voice_bank` serialized directly.
- **ES5510 DSP Pipeline:** Execution state, ALU latch, MAC latch, and 3-stage RAM pipeline registers serialized in `es5510_device`.
- **MC68302 IMP:** SIB registers, internal DPRAM, Port A/B states, IDMA state, and dynamic BAR window base.
- **Audio State:** Effect rate mode, pump state, analog ADC values.

### 12.2 Post-Load Restoration
In `asr10_boot_state::effect_audio_rate_postload()`:
1. `apply_effect_audio_rate_policy()` reapplies the active clock to ES5506.
2. `mc68302_device::device_post_load()` unmaps stale address spaces and reinstalls the internal 4 KB window from restored `BAR`.
3. Bit-identical 48 kHz 3-channel audio reproduction confirmed across save/restore cycles `[VERIFIED]`.

### 12.3 Operational Limitation
- Live, in-progress floppy disk transfers (`upd765`) cannot be serialized. Save states must be captured when storage operations are quiescent `[OPEN / DOCUMENTED LIMITATION]`.

---

## 13. Known Architectural Debt and Model Boundaries

The following matrix classifies all known open architectural questions, approximations, and latent model debt in OpenASR:

| Item | Architectural Area | Classification | Current Epistemic Status & Evidence Boundary |
| :--- | :--- | :--- | :--- |
| **1. Physical DRAM Topology** | Memory Architecture | `OPEN PHYSICAL QUESTION` | MAME models a unified 16 MiB linear array. Physical board uses SIMMs/discrete DRAMs with uncharacterized RAS/CAS glue logic. |
| **2. Physical CS1 Receiver / Path** | Chip Selects / Glue | `OPEN PHYSICAL QUESTION` | Physical IC(s) storing the 256-byte voice banking table and the board signals reaching them are unknown due to absent Digital Board schematic. Functional behavior verified. |
| **3. CS1 Read-Decode Mechanism** | Chip Selects | `OPEN PHYSICAL QUESTION` / `APPROXIMATION` | MC68302 CS1 is programmed write-qualified (`RW=1, MRW=1`), yet firmware executes read at `$007C68`. MAME models `.rw()` as a functional approximation. Physical read-decode mechanism open. |
| **4. Large WaveSample Continuation** | Sample Addressing | `NEEDS AUTHENTIC FIXTURE` / `INFERRED` | Generic MAME stops when `START == END`. Firmware has transwave/IRQE continuation paths. Needs authentic $>4\text{ MiB}$ single-sample fixture. |
| **5. ES5506 END / Transwave Edge Cases** | Sound Generation | `APPROXIMATION` / `LATENT MODEL DEBT` | Generic ES5506 device boundary handling differs subtly from Rev 2.3 spec for equal boundaries and rapid turnaround. |
| **6. ES5510 Silicon-Level Fidelity** | Audio DSP | `APPROXIMATION` | Host protocol and pump routing fully verified. Internal arithmetic pipeline matches functional needs, but full cycle-accurate timing open. |
| **7. AUX / OEX-6 Audio Routing** | Audio Outputs | `UNIMPLEMENTED FEATURE` | Channels CA3-CA5 and expansion output jacks (OEX-6/8) are not modeled in current driver. Base stereo output is complete. |
| **8. PB9 / IRQB Physical Polarity** | Interrupt Handling | `OPEN PHYSICAL QUESTION` / `FUNCTIONALLY SUFFICIENT` | MAME models active-high rising callback to SIB pending bit `$0080`. Physical active-low pin connection to ES5506 open. |
| **9. MC68302 SCC Completeness** | Communications | `FUNCTIONALLY SUFFICIENT` | SCC1/SCC2 implement minimal packet ingress descriptor rings required for MIDI/panel. General multiprotocol engine unneeded. |
| **10. MC68302 Timers** | System Control | `UNIMPLEMENTED FEATURE` / `FUNCTIONALLY SUFFICIENT` | Timers 1 and 2 are unmodeled shadow storage. Firmware uses DUART counter/timer for ticks; no active defects observed. |
| **11. IDMA Generality** | DMA Engine | `FUNCTIONALLY SUFFICIENT` | Models peripheral byte ingress (`$0D51`) and internal word copy (`$37A1`). Full burst/cycle-stealing modes unneeded for covered paths. |
| **12. Reset vs. Clear Semantics** | Driver State | `OPEN PHYSICAL QUESTION` / `EVIDENCE NEEDED` | Potential asymmetry between hardware reset line and soft clear loops has no identified defect commit. Documented as open. |
| **13. Explicit ES5701 Device** | Glue Logic | `APPROXIMATION` / `FUNCTIONALLY SUFFICIENT` | ES5701 functions (bus sizing, clock dividing, peripheral decoding) distributed cleanly across driver rather than monolithic device. |
| **14. Static CS vs. Pin Routing** | Chip Selects | `APPROXIMATION` / `FUNCTIONALLY SUFFICIENT` | CS1-CS3 windows statically mapped in driver address space rather than dynamically asserted via live output pin lines. |

---

## 14. Superseded Historical Models and Hypotheses

Future developers and automated agents must not resurrect the following falsified or superseded models:

| Historical Hypothesis / Model | Evidence That Superseded / Disproved It | Current Replacement Architecture |
| :--- | :--- | :--- |
| **2 MiB Modulo Sample RAM Model** | Firmware alias probe at `$F8A166` naturally detects 16 MiB distinct backing; CDR-04 `9FT-BALDWIN` failed under 2 MiB model. | **16 MiB Distinct Canonical Backing Store:** Single 16 MiB array (`m_system_ram`) shared by CPU and ES5506 (`commit cca8be1ae33`). |
| **Static ES5506 Bank 1 $\rightarrow$ Chunk 0 Model** | Disassembly of `$F8CD22` and `$F8E270` proved firmware programs dynamic per-voice translation table at `$FF7F00-$FF7FFF`. | **CS1 Per-Voice Dynamic Banking:** ES5506 wavetable fetches dynamically translated via 32-entry $\times$ 4-word table (`commit b7cd112199d`). |
| **ED-002 Non-Equivalent Audio Fixtures** | Direct hex and object analysis revealed ED-002 test files had differing layer configurations and sample counts. | **Controlled Autocorrelation Fixtures:** Use identical MIDI fixtures (`JM DRUMS`, `JM DIGI SYN`) with bit-for-bit file verification. |
| **H0/H2 Audio History Discrepancy** | `investigations/audio-history-agy-handoff.md` established discrepancy was caused by DUART timer phase at `$0B6E`, not sample corruption. | **DUART Timer Phase Alignment:** H0/H2 explained by interrupt timing differences; sample memory was verified uncorrupted. |
| **ESP Microcode Upload Race Hypothesis** | The controlled PRE/POST experiment established that PRE already halted the audio pump while `PA4=0`. Measured 4,431 verify comparisons/run showed zero mismatches and zero failures/retries, and PRE/POST audio was bit-for-bit identical across 1,856,641 frames. | **Lifecycle Hardening / Model Policy:** Commit `d9494` represents lifecycle hardening, not a causal fix for an upload race `[DISPROVEN]`. The historical emulator `EFFECT DOWNLOAD FAILED` defect was caused by missing `$FC31C1` write handling, not an upload race. |
| **Simplistic Bank 11 "Missing Prerequisites"** | Pre-warming tests showed differing audio outcomes even with prerequisites present due to static bank register mapping. | **Causal Bank 1 Translation Defect:** `BLUES DRUMS` in odd MB was mapped to wrong RAM chunk under static bank model. |
| **Sequencer Requires Event Mask Bit 5** | Static analysis proved opcode `$0538` is `BTST D2, $0CDE.w` where $D2 = \text{Type} - 1$. NOTE is Type 1, which tests bit 0. | **Dynamic Event Mask `$0CDE`:** Bit 0 filters NOTE events; bit 5 was a disassembly artifact of misunderstanding opcode `$0538`. |
| **Doubled ES5506 Master Clock Domain** | Full-crystal clock ($30.476\text{ MHz}$) produced octave-high audio; spec states $F_s = \text{CLK} / (16 \times (\text{ACTV}+1))$. | **Corrected Half-Crystal Clocks:** $Y2/2$ (15.238 MHz) for 29.76 kHz mode; $Y3/2$ (16.934 MHz) for 44.1 kHz mode. |
| **ES5701 as Sole CS1 Banking Table Store** | Bob Yannes ES5701 specification confirms its documented interface lacks a 256-byte table store and address lines reach only up to `LA19`. | **Specified Storage Model Boundary:** The specified ES5701 storage model cannot act as the sole table store `[DISPROVEN]`. Whether physical CS1, derived decode, voice/page signals, or related board signals reach or pass through ES5701 remains `[OPEN]`. |

---

## 15. Architectural Reference Diagrams

### 15.1 Sample Path and Audio Routing

```text
+------------------------------------------------------------------------------------+
|                         COMPLETE ASR-10 AUDIO DATA PIPELINE                        |
+------------------------------------------------------------------------------------+

   [DISK MEDIA / SCSI / RAM FILE]
                 |
                 v
   +-------------------------------+
   | System Memory ($000000)       |
   | Instrument Object             |
   |   -> Layer -> WaveSample      |
   |   -> Resolves PCM Owner       |
   +---------------+---------------+
                   |
                   v
   +-------------------------------+
   | Dynamic Voice Setup           |
   |   Programs ES5506 START, END, |
   |   ACCUM, FC, Control Reg      |
   |   Programs CS1 Banking Words  |
   |   [N, N+1, N+2, N+3] @ $FF7F00|
   +---------------+---------------+
                   |
                   v
   +-------------------------------+
   | ES5506 OTTO Sound Generator   |
   |   Logical Word Fetch          |
   |   Transformed by CS1 Table    |
   |   Reads from 16 MiB DRAM      |
   +---------------+---------------+
                   |
                   | 6 Serial Output Lines (CA0..CA2)
                   v
   +-------------------------------+
   | ESQ Pump Frame Adapter        |
   |   BUS1 -> SER0 (Inputs 2/3)   |
   |   BUS2 -> SER2 (Inputs 6/7)   |
   |   BUS3 -> SER3 (Inputs 0/1)   |
   +---------------+---------------+
                   |
                   v
   +-------------------------------+
   | ES5510 ESP Effects DSP        |
   |   Gated by PA4 (HALT/RUN)     |
   |   Executes 24-bit Microcode   |
   |   Emits SER1 (Outputs 0/1)    |
   +---------------+---------------+
                   |
                   v
   +-------------------------------+
   | Stereo Output Conversion      |
   |   Speaker Left / Right        |
   +-------------------------------+
```

### 15.2 Epistemic Evidence Layers

```text
+------------------------------------------------------------------------------------+
|                         OPENASR EPISTEMIC MAPPING LAYERS                           |
+------------------------------------------------------------------------------------+

LAYER 1: FIRMWARE-VISIBLE FUNCTIONAL MODEL
  - What 68000 CPU reads and writes via bus cycles
  - SIB registers, CS0-CS3 decode windows, DPRAM layout
  - CS1 voice banking array at $FF7F00-$FF7FFF
  - Probed 16 MiB DRAM space ($000000-$00F80000)
  - ES5506 / ES5510 host registers and audio rate policy ($0CE3)
  ==================================================================================
                                    |
                                    | Implemented In MAME
                                    v
LAYER 2: CURRENT MAME IMPLEMENTATION
  - `mc68302_device`: 68000 core + SIB + minimal IDMA/SCC/IRQ
  - `asr10_boot_state`: statically maps $FC2000, $FC3000, $FC4000, $FF7F00
  - Unified 16 MiB `m_system_ram` backing store
  - `voice_bank_r/w` and `es5506_wavetable_r` translation
  - `esq_5505_5510_pump_device` serial audio routing
  ==================================================================================
                                    |
                                    | Inferred / Documented Boundary
                                    v
LAYER 3: PHYSICAL HARDWARE MODEL
  - MC68302 IMP (16 MHz Y1)
  - 4-layer ASR-10 Digital Board PCB (schematic unpublished)
  - Base DRAM + SIMM expansion slots
  - Custom PAL U5 ("ASR-10 V1.1") + 74HC logic
  - ES5701 SuperGLU ASIC (bus glue / clock division; cannot act as sole CS1 table store)
  - Physical CS1 latch/SRAM receiver and voice tracker [OPEN]
  - Physical Y2/Y3 clock multiplexer and divider [OPEN]
```

---

## 16. Document Provenance and Relationships

- **Supersedes:**
  - Historical architectural summaries in `docs/asr10/archive/architecture.md`
  - Historical pre-IDMA checkpoints in `docs/asr10/reference/architecture-handoff.md`
  - Historical 2 MiB memory descriptions in older investigation files
- **Complements (Detailed Primary Records):**
  - `docs/asr10/investigations/cs1-voice-banking-and-sample-addressing.md` (CS1 discovery and Bank 11 fix)
  - `docs/asr10/investigations/cs1-board-level-implementation-frontier.md` (Board frontier and ES5701 storage-model boundary)
  - `docs/asr10/investigations/wavesample-addressing-cs1-and-continuation.md` (WaveSample data model and continuation)
  - `docs/asr10/investigations/cdr04-16m-baldwin-experiment.md` (16 MiB sample-memory backing)
  - `docs/asr10/investigations/mc68302-unimplemented-register-coverage.md` (SIB register coverage)
  - `docs/asr10/investigations/save-state-audio-mvp.md` (Save-state architecture)
  - `docs/asr10/investigations/upd72069-standby-auxcmd-fix.md` (Storage freeze)
  - `docs/asr10/current-status.md` (Operational project state and regression baseline)
