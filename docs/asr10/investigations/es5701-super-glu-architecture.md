# Architectural Investigation: ES5701 Super-GLU Architecture, Signal Ownership, Bus Semantics, and System Fidelity

## Status
**Closed / Frozen track.**
Authoritative technical baseline: `06739b97b0993d42fc2d58752f95f504f1ea064a` (`es5510: fix condition register host semantics`).
An explicit `es5701_device` is **not justified** for current authentic workloads. The driver retains its collapsed memory-handler interface. Track is frozen until contradictory authentic evidence or workload divergence appears.

---

## 1. Executive Result

1. **What does the ES5701 Super-GLU actually do?**
   Designed by Bob Yannes (Rev. 2, September 14, 1988), the ES5701 is a 99-pin gate array providing four discrete functional blocks (`[VERIFIED — ES5701 specification]`):
   - **68000 ↔ ESP Interface:** Multiplexes CPU address (`A8..A1`) and data (`D7..D0`) onto the 8-bit bidirectional ESP host bus (`A8/D7..A1/D0`) controlled by `/ESP`, and reflects `/ESP` through an open-collector buffer to produce `/DTACK` for the 68000.
   - **68000 ↔ OTIS Interface:** Isolates CPU data from the sound bus during OTIS sound-memory cycles (`/DBUS=0`), and passes CPU data/address when `/DBUS=1`.
   - **OTIS ↔ Static Sound Memory Interface:** Latches `DA19..DA4` into `LA19..LA4` on the falling edge of `/RAS` for static RAM/ROM addressing; forces unused lower data bits low for 8, 12, or 13-bit sample modes; and provides a 12-bit nybble multiplexer (`DA11H/L..DA8H/L`) for packing 12-bit samples into byte-wide SRAMs.
   - **Clock Generation:** Two independent crystal oscillators with divide-by-2 flip-flops (20 MHz -> 10 MHz and 16 MHz -> 8 MHz).

2. **Which of those functions does the ASR-10 actually use?**
   - **68000 ↔ ESP Interface:** **USED.** The ASR-10 uses this to interface the MC68302 to the ES5510 ESP at `$FC3000..$FC31FF`.
   - **68000 ↔ OTIS Interface:** **PARTIALLY USED / SUPERSEDED.** The ASR-10 uses OTTO (ES5506), not OTIS (ES5505). CPU ↔ sound bus transceiver and isolation (`/DBUS=0`) remains architecturally functional; static RAM address latches are unneeded.
   - **OTIS ↔ Static Sound Memory:** **STATIC MEMORY LATCHES NOT REQUIRED.** The ES5701 static-memory address-latch function (`LA19..LA4`) is not required by the established ASR-10 sample-memory architecture (which uses standard 16-bit dynamic RAM SIMMs up to 16 MiB). Exact electrical use of `LA19..LA4` remains unverified without a complete digital-board schematic or continuity evidence (`[OPEN — exact physical PCB connection]`). `M1/M0` and `/NYB` are effectively required to be held in 16-bit mode (no bits pulled low, nybble mux disabled) for established 16-bit sound operation (`[INFERRED — functional requirement]`, `[OPEN — PCB wiring]`). Voice banking across 16 MiB is handled by board-level logic (`CS1` writing `$FF7F00..$FF7FFF`), which resides outside the ES5701.
   - **Clock Generation:** **20 MHz UNUSED; 16->8 MHz UNVERIFIED.** The ASR-10 uses a 16 MHz system clock for the MC68302/ES5506 and an emulator-configured 30 MHz clock for the ES5510 (`[OBSERVED — emulator configuration]`). The 20 MHz -> 10 MHz section is unused (`[INFERRED — system architecture]`); whether the 16 MHz -> 8 MHz divider feeds DUART/FDC is unverified without board schematics (`[OPEN — PCB wiring]`).

3. **Current OpenASR/MAME Representation:**
   - The ESP bus interface is **functionally collapsed** into direct memory handlers in `asr10_boot.cpp` mapping `$FC3000..$FC303F` to `es5510_device::host_r/w`.
   - `/DTACK` is **bypassed** because MAME memory handlers complete synchronously.
   - All 50 authentic ROM effects (`ROM-01` through `ROM-50`) download, verify, and run with zero mismatches and zero retries under this collapsed model.

4. **Sufficient Basis for Current Decision:**
   - The ES5701 architecture and its role in the current ASR-10 model are **sufficiently established for the present modeling decision**. Generic ES5701 functionality is well established from Bob Yannes' Rev. 2 specification; exact ASR-10 board-level wiring remains partially inferred and partially open, but does not block current authentic workloads.

---

## 2. Source Hierarchy

| Source Document | Author / Origin | Date / Revision | Status / Authority | Role in Investigation |
|---|---|---|:---:|---|
| `ES5701.pdf` | Bob Yannes (Ensoniq) | Rev. 2, Sep 14, 1988 | **Primary Hardware Specification** | Authoritative silicon pinout, logic functions, and timing requirements. |
| *ASR-10 Service Manual* | Ensoniq Corp. (Model #9500) | 1993 | **Primary Service Documentation** | Confirms peripheral schematics, digital board component locations (U41=ES5701, U29=ES5506, U43=ES5510, U28=MC68302, U5=custom PAL), notes main 4-layer digital board schematic was unpublished. |
| *MC68302 User's Manual* | Motorola | Rev. 2 | **Primary Processor Specification** | Defines bus cycle timing, external `/DTACK` handshaking, chip selects, and interrupt controller. |
| *ES5510 Technical Specification* | Ensoniq Corp. | Rev. 2.4 | **Primary DSP Specification** | Defines host port multiplexed `A8/D7..A1/D0` bus protocol, setup/hold times, and `Host Access OK` polling. |
| *ES5506 Technical Specification* | Ensoniq Corp. | Rev. 1.0 | **Primary Sound Generator Specification** | Defines OTTO host bus, E-clock `/DBUS`, and wavetable memory addressing. |
| `es5701.vhd` (`es5571`) | Rainer Buchty | V1.0, Nov 23, 2019 | **Secondary Interpretation Evidence** | VHDL reconstruction ("syntax test only"). Useful reference but contains severe R/W polarity typos and naming errors. |
| OpenASR Codebase & Docs | OpenASR Project | 2026 | **Current Software Baseline** | Authoritative state of the active MAME implementation. |

---

## 3. Generic ES5701 Architecture

The ES5701 gate array provides four independent functional units:

### 3.1 68000 ↔ ESP Bus Multiplexer
The Ensoniq ESP (ES5510) uses a multiplexed 8-bit address/data host port (`A8/D7..A1/D0`) to reduce pin count. The 68000 microprocessor presents separate 8-bit address lines (`A8..A1`) and 16-bit data lines (`D15..D0`).
- **Address Phase (`/ESP = 1`):** CPU address lines `A8..A1` are passed transparently to `A8/D7..A1/D0`.
- **Data Phase (`/ESP = 0`):** On the falling edge of `/ESP`, address driving is disabled to satisfy the ESP address hold time, and CPU data lines `D7..D0` are connected to `A8/D7..A1/D0` (directional control via `R/W`).
- **Acknowledge (`/DTACK`):** `/ESP` is buffered through an open-collector output to drive the 68000 `/DTACK` line.

### 3.2 68000 ↔ OTIS Bus Transceiver & Isolation
OTIS sound memory operates at high speed for 32-voice wavetable synthesis. The 68000 bus must not load the sound memory bus during synthesis cycles.
- When `/DBUS = 0` (OTIS access cycle), CPU lines `D15..D0` and `A4..A1` are isolated (tri-stated) from the sound memory bus.
- When `/DBUS = 1` (CPU access cycle) and `/UDBEN` or `/LDBEN` is asserted, CPU address `A4..A1` passes to `DA3..DA0` and CPU data `D15..D0` passes to `DA19..DA4`.
- If a narrow sample mode (8/12/13-bit) is selected, unused low data bits to the 68000 are pulled low on read.

### 3.3 OTIS ↔ Static Sound Memory Interface
- **Address Latching:** OTIS outputs multiplexed address/data on `DA19..DA4`. On the falling edge of `/RAS`, ES5701 latches `DA19..DA4` into `LA19..LA4` to drive static memory address pins throughout the access.
- **Unused Bit Pull-Down:** On an OTIS read of sound memory, lower data bits are pulled low according to `M1/M0`.
- **12-bit Nybble Multiplexer:** If `/NYB = 0` and 12-bit mode is selected, `DA11H..DA8H` (upper nybble) or `DA11L..DA8L` (lower nybble) is steered to `DA11..DA8` based on address bit `LA19`.

### 3.4 Clock Generators
- Dual crystal oscillator inverters with divide-by-2 flip-flops producing symmetrical 50% duty-cycle clocks:
  - `20MI` (20 MHz) -> `10M` (10 MHz).
  - `16MI` (16 MHz) -> `8M` (8 MHz).

---

## 4. Complete ES5701 Pin and Signal Matrix

| Signal Name | Pin Count | Direction | Type | Polarity | Connected Subsystem | Functional Role | Transaction Phase | Electrical Properties | ASR-10 Status |
|---|:---:|:---:|:---:|:---:|---|---|---|---|---|
| `+5V` | 3 | IN | Power | High | Power Rail | VCC (+5V supply) | Continuous | Power | Connected |
| `GND` | 3 | IN | Power | Low | Power Rail | Ground (0V supply) | Continuous | Ground | Connected |
| `D15..D0` | 16 | I/O | Bi | High | MC68302 Data Bus | CPU data transfers to OTTO or ESP | Gated by `/UDBEN`, `/LDBEN`, `/ESP`, `/DBUS`, `R/W` | Tri-state bus | Connected |
| `A8..A1` | 8 | IN | Input | High | MC68302 Address Bus | Low CPU address bits | Valid during `/AS` | Standard input | Connected |
| `R/W` | 1 | IN | Input | High=R, Low=W | MC68302 Bus Control | Read / Write direction control | Latched with bus cycle | Standard input | Connected |
| `/UDBEN` | 1 | IN | Input | Low | Board Decode | Upper Data Bus Enable | Active during upper byte access | Standard input | Connected (via decode) |
| `/LDBEN` | 1 | IN | Input | Low | Board Decode | Lower Data Bus Enable | Active during lower byte access | Standard input | Connected (via decode) |
| `/ESP` | 1 | IN | Input | Low | Board Decode (PAL U5) | ESP Chip Select | Active during `$FC3000..$FC31FF` | Standard input | Connected |
| `/DTACK` | 1 | OUT | Output | Low | MC68302 Bus Control | Data Transfer Acknowledge | Pulled low while `/ESP` active | **Open-Collector** | Inferred connected to MC68302 `/DTACK` |
| `/RAS` | 1 | IN | Input | Low | ES5506 (OTTO) | OTTO /RAS output | Falling edge latches address | Standard input | Connected to OTTO |
| `/CAS` | 1 | IN | Input | Low | ES5506 (OTTO) | OTTO /CAS output | Indicates sound access | Standard input | Connected to OTTO |
| `/DBUS` | 1 | IN | Input | Low | ES5506 (OTTO) | Sound Bus Arbitration (E-clock) | Low = OTTO, High = CPU | Standard input | Connected to OTTO E-clock |
| `A8/D7..A1/D0` | 8 | I/O | Bi | High | ES5510 (ESP) | Muxed Address/Data bus to ESP | Addr when `/ESP=1`, Data when `/ESP=0` | Tri-state bus | Connected to ES5510 host port |
| `LA19..LA4` | 16 | OUT | Output | High | Sound Memory | Latched address to static RAM/ROM | Latched on `/RAS` fall, enabled `/DBUS=0` | Tri-state output | Latch function not required by ASR-10 dynamic RAM; physical wiring unverified (`[OPEN]`) |
| `DA19..DA4` | 16 | I/O | Bi | High | ES5506 (OTTO) | Muxed Address/Data bus | Latched to LA on `/RAS` | Tri-state bus | Connected to ES5506 |
| `DA3..DA0` | 4 | OUT | Output | High | ES5506 (OTTO) | Low-order register address | Passes CPU `A4..A1` when `/DBUS=1` | Tri-state output | Connected to ES5506 `A3..A0` |
| `DA11H..DA8H` | 4 | IN | Input | High | Sound Memory | Upper nybble input (12-bit mode) | Active when `/NYB=0`, 12-bit mode | Standard input | No evidence required by ASR-10; physical connection unverified (`[OPEN]`) |
| `DA11L..DA8L` | 4 | IN | Input | High | Sound Memory | Lower nybble input (12-bit mode) | Active when `/NYB=0`, 12-bit mode | Standard input | No evidence required by ASR-10; physical connection unverified (`[OPEN]`) |
| `M1` | 1 | IN | Input | High | Mode Configuration | Sample resolution MSB | Static configuration | Standard input | Expected/effectively required high for 16-bit mode (`[INFERRED]`); physical trace `[OPEN]` |
| `M0` | 1 | IN | Input | High | Mode Configuration | Sample resolution LSB | Static configuration | Standard input | Expected/effectively required high for 16-bit mode (`[INFERRED]`); physical trace `[OPEN]` |
| `/NYB` | 1 | IN | Input | Low | Mode Configuration | Enable 12-bit nybble mode | Static configuration | Standard input | Expected/effectively required high to disable nybble mode (`[INFERRED]`); physical trace `[OPEN]` |
| `20MI` | 1 | IN | Input | Analog | Clock Oscillator | 20 MHz crystal input | Continuous | Crystal oscillator | Not required in established ASR-10 architecture (`[INFERRED]`) |
| `20MO` | 1 | OUT | Output | Analog | Clock Oscillator | 20 MHz crystal output | Continuous | Crystal oscillator | Not required in established ASR-10 architecture (`[INFERRED]`) |
| `10M` | 1 | OUT | Output | Clock | Clock Distribution | 10 MHz clock output | Continuous | 50% duty cycle | Not required in established ASR-10 architecture (`[INFERRED]`) |
| `16MI` | 1 | IN | Input | Analog | Clock Oscillator | 16 MHz clock input | Continuous | Crystal oscillator | Connected to 16 MHz oscillator |
| `16MO` | 1 | OUT | Output | Analog | Clock Oscillator | 16 MHz crystal output | Continuous | Crystal oscillator | Crystal feedback |
| `8M` | 1 | OUT | Output | Clock | Clock Distribution | 8 MHz clock output | Continuous | 50% duty cycle | Unverified on ASR-10 (`[OPEN — PCB wiring]`) |

Total pins: **99 pins**. Exactly accounts for the 100-pin QFP footprint (with 1 NC or extra GND).

---

## 5. 68000 ↔ ESP Interface and /ESP Generation

### 5.1 Physical Decode Chain
1. MC68302 issues a bus cycle in the range `$FC3000..$FC31FF`.
2. Board decode: Custom PAL U5 (`"ASR-10 V1.1"`) and 74HC138 3-to-8 decoder decode upper address lines `A23..A9`.
3. PAL U5 asserts `/ESP` (active low).
4. Inside ES5701:
   - While `/ESP = 1`: `A8..A1` from the CPU are driven onto `A8/D7..A1/D0` to the ES5510.
   - When `/ESP = 0`: address drivers shut off; CPU data lines `D7..D0` are gated to `A8/D7..A1/D0`.
   - `/DTACK` output is pulled low through the open-collector buffer.
5. The MC68302 samples `/DTACK` asserted and concludes the bus cycle.

### 5.2 Current OpenASR/MAME Implementation
In `src/mame/ensoniq/asr10_boot.cpp`:
```cpp
map(0xfc3000, 0xfc303f).rw(m_es5510_host, FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);
map(0xfc3100, 0xfc3101).rw(FUNC(asr10_boot_state::es5510_host_read_select_r), FUNC(asr10_boot_state::es5510_host_read_select_w)).umask16(0x00ff);
map(0xfc3140, 0xfc3141).rw(FUNC(asr10_boot_state::es5510_host_write_select_gpr_r), FUNC(asr10_boot_state::es5510_host_write_select_gpr_w)).umask16(0x00ff);
```
- The entire address-phase / data-phase multiplexing is collapsed: MAME uses memory map offsets to identify the target host register directly.
- `/DTACK` generation is bypassed: MAME completes memory handler cycles instantaneously.

---

## 6. /DTACK Semantics vs. Host Access OK

A critical distinction must be maintained between bus-level handshaking and device-level busy status:

```text
Layer 1: 68000 Bus Handshake (/DTACK)
=====================================
MC68302 CPU ──────> asserts /ESP ──────> ES5701 Super-GLU
     ▲                                          │
     │                                          ▼
     └────────────── pulls /DTACK low ──────────┘
Meaning: "The byte transfer over the external pins is finished."
Duration: Nominal 4 clocks @ 16 MHz = 250 ns; gate delay unmeasured [OPEN — timing].
Firmware visibility: Invisible to firmware; CPU hardware automatically samples /DTACK.

Layer 2: ES5510 DSP Pipeline Handshake (Host Access OK)
=======================================================
Firmware write ────> writes host latch ($FC3001) ────> ES5510 ESP
                                                          │
                                                    (DSP ALU cycle)
                                                          │
Firmware poll  <──── reads Host Access OK bit ────────────┘
Meaning: "The internal DSP ALU has completed updating the target register."
Duration: Nominal 1 to 2 ESP cycles @ 30 MHz (~33 to 66 ns); exact sync latency [INFERRED].
Firmware visibility: Explicitly polled via `btst #7, ($fc3001)`.
```

`[VERIFIED — specification]`: `/DTACK` and `Host Access OK` are completely orthogonal handshakes belonging to different chips and different architectural layers. Neither is a substitute for the other.

`[OPEN — non-blocking]`: Host Access OK timing fidelity is not modeled with cycle-accurate DSP pipeline latency in MAME, but its immediate functional response is sufficient for all established authentic workloads. It remains an open, non-blocking topic separate from ES5701 bus arbitration.

---

## 7. 68000 ↔ OTIS ↔ Sound Memory Path

### 7.1 Historical Design (1988 OTIS)
Bob Yannes' specification was tailored for the EPS-1 and early synths:
- Sound memory was static RAM/ROM with 8, 12, or 13-bit resolution.
- ES5701 latched `DA19..DA4` into `LA19..LA4` on the falling edge of `/RAS`.
- When reading sound memory, ES5701 forced lower data bits low (`D2..D0` for 13-bit, `D3..D0` for 12-bit, `D7..D0` for 8-bit).
- For 12-bit systems, a third "nybble" SRAM was used; ES5701 steered upper or lower nybbles based on `LA19`.

### 7.2 ASR-10 Physical Reality (1992 OTTO)
The ASR-10 sound-memory subsystem differs fundamentally from the 1988 static design:
- **OTTO Upgrade:** The ASR-10 upgraded from OTIS (ES5505) to **OTTO (ES5506)**.
- **Dynamic RAM Architecture:** Sound memory uses standard 16-bit dynamic RAM SIMMs (up to 16 MiB) with multiplexed row and column addressing.
- **Static Address Latches (`LA19..LA4`):** The ES5701 static-memory address-latch function is not required by the established ASR-10 sample-memory architecture. Exact electrical use or connection of `LA19..LA4` pins remains unverified without a complete digital-board schematic or continuity measurements (`[OPEN — exact physical PCB connection]`).
- **Nybble Multiplexer:** The 12-bit nybble multiplexer has no evidence of requirement on the ASR-10 (no nybble SRAMs exist on the board; `[OPEN — exact physical PCB connection]`).
- **Mode Configuration (`M1`, `M0`, `/NYB`):** These pins are expected and effectively required to be held high for established 16-bit sound operation (disabling low-bit masking and nybble steering; `[INFERRED — functional requirement]`). Exact physical trace tying to VCC remains unverified (`[OPEN — PCB wiring]`).
- **CPU ↔ OTTO Bus Transceiver:** The bidirectional data pass-through and bus isolation during synthesis cycles (`/DBUS=0`) is architecturally plausible and potentially active; exact physical routing between U28, U41, and U29 remains `[INFERRED — board topology]`.
- **Sample Banking Table:** The 256-byte voice banking table (`$FF7F00..$FF7FFF`) resides in external board logic (`CS1`), completely outside the ES5701.

---

## 8. Audit of Rainer Buchty's VHDL (`es5701.vhd`)

| Section | VHDL Entity / Logic | Specification Match | Epistemic Status | Audit Notes |
|---|---|:---:|:---:|---|
| Entity Name | `entity es5571` | CONTRADICTION | `[CONTRADICTION — naming typo]` | Named `es5571` instead of `es5701`. Likely typographical error. |
| Bus Helpers | `lo_r`, `hi_r`, `lo_w`, `hi_w` | **CONTRADICTION** | `[CONTRADICTION — polarity bug]` | `lo_w` and `hi_w` assert on `rw='1'` (read) instead of `rw='0'` (write)! Cannot perform writes. |
| Clock Dividers | `clk10`, `clk8` | MATCH | `[MATCHES SPEC]` | Dividing `clk20` and `clk16` by 2 on falling edge. |
| OTIS Mask | `otis_mask` | MATCH | `[MATCHES SPEC]` | Matches the 8/12/13/16-bit mask table on page 4 of the spec. |
| CPU Data Bus | `cpu_d` muxing | MATCH | `[MATCHES SPEC]` | Steers `esp_ad` or `otis_da` to CPU bus. |
| OTIS Address Latch | `otis_la` | MATCH | `[MATCHES SPEC]` | Latches on `falling_edge(otis_ras)` when `otis_dbus='0'`. |
| 12-bit Nybble | `otis_nibble` | MATCH | `[MATCHES SPEC]` | Steers `otis_dah` vs `otis_dal` based on `LA19`. |
| ESP Multiplexer | `esp_ad`, `esp_q` | PARTIAL | `[PARTIAL / TIMING ABSTRACTED]` | Uses asynchronous RS latch on `esp_cs` to switch address/data phases. |
| DTACK Output | `esp_dtack` | MATCH | `[MATCHES SPEC]` | `esp_dtack <= '0' when esp_cs='0' else 'Z'`. Pure open-collector reflection. |

---

## 9. Firmware Observability

What parts of ES5701 behavior are observable by authentic ASR-10 V3.50 firmware?

1. **Architecturally Visible:**
   - **ESP Register Writes & Readback:** Firmware at `$F973F0..$F97800` writes to `$FC3001/$FC3003/$FC3005`, commits via `$FC3141`, selects via `$FC3101`, and compares readback. If the host bus fails to transmit data or read back registers correctly, firmware retries 10 times and triggers `EFFECT DOWNLOAD FAILED / ERROR 032`.
   - **ES5506 Register Access:** Firmware writes to `$FC2001..$FC207F` to configure OTTO voices.
2. **Timing Visible:**
   - **Host Access OK Polling:** Firmware polls bit 7 of `$FC3001` in tight loops (`btst #7, ($fc3001)`). If `Host Access OK` never clears, firmware hangs.
3. **Firmware-Invisible:**
   - **/DTACK Wait-States:** Handled in hardware by the MC68302 bus controller. Whether a bus cycle terminates in 0 wait-states or 2 wait-states is transparent to software execution (software only sees instruction execution time).
   - **Static Address Latching (LA19..LA4) & Nybble Mode:** Completely unmapped on the ASR-10 digital board.

---

## 10. System Diagrams

### 10.1 Physical ASR-10 Architecture

```text
                   +-----------------------------------------------+
                   |              Motorola MC68302 MCU             |
                   +-----------------------------------------------+
                     │ A23..A9   │ A8..A1     │ D15..D0    │ /DTACK
                     │           │            │            ▲
                     ▼           │            │            │
            +----------------+   │            │            │
            |  Custom PAL U5 |   │            │            │
            | + 74HC138 Dec  |   │            │            │
            +----------------+   │            │            │
                     │ /ESP      │            │            │
                     ▼           ▼            ▼            │
            +----------------------------------------------+--------+
            |              Ensoniq ES5701 Super-GLU (U41)           |
            |                                                       |
            |   [ESP Mux]          [Bus Transceiver]       [/DTACK] |
            +-------┬----------------------┬-------------------│----+
                    │ A8/D7..A1/D0         │ DA19..DA0         │
                    │                      │                   └──── (Open-Collector /DTACK)
                    ▼                      ▼
            +---------------+      +-----------------+
            | Ensoniq ES5510|      |  Ensoniq ES5506 |
            |   ESP (U43)   |      |    OTTO (U29)   |
            +---------------+      +--------┬--------+
                                            │ Wavetable bus + CS1 Banking
                                            ▼
                                   +-----------------+
                                   |  16 MiB DRAM    |
                                   |  SIMM Modules   |
                                   +-----------------+
```

### 10.2 Current OpenASR/MAME Architecture

```text
                   +-----------------------------------------------+
                   |               MAME 68000 CPU Core             |
                   +-----------------------------------------------+
                     │ Address Map ($FC3000..$FC31FF, $FC2000..$FC207F)
                     │ Synchronous Bus Cycles (Zero Wait-States)
                     │
                     ├───────────────────────────────┐
                     ▼                               ▼
            +------------------+            +------------------+
            |  es5510_device   |            |  es5506_device   |
            |   (ESP Model)    |            |   (OTTO Model)   |
            |                  |            |                  |
            | host_r()         |            | read()           |
            | host_w()         |            | write()          |
            | gpr_latch        |            | wavetable fetch  |
            +------------------+            +--------┬---------+
                                                     │
                                                     ▼
                                            +------------------+
                                            | 16 MiB DRAM Pool |
                                            | + voice_bank_w() |
                                            +------------------+
```
*Note: In current MAME, the physical ES5701 Super-GLU is completely collapsed into direct memory mapping and synchronous core callbacks.*

---

## 11. Evaluation of Modeling Options

### Option A: Leave Current Driver-Level Implementation Intact
- **Physical Fidelity:** Low component-boundary fidelity (ES5701 collapsed), but high functional fidelity.
- **Functional Benefit:** None gained by refactoring. 100% of authentic ROM algorithms (`ROM-01..50`), floppy loading, audio playback, and panel navigation already pass cleanly.
- **Complexity:** Zero added complexity.
- **Regression Risk:** Zero.

### Option B: Introduce a Minimal ES5701 Device
- **Scope:** Bounded strictly to `/ESP` decoding, 8-bit multiplexed bus latching, and open-collector `/DTACK` generation.
- **Physical Fidelity:** High for the ESP host bus.
- **Functional Benefit:** Allows clean signal-level tapping between MC68302 and ES5510.
- **Complexity:** Moderate (~150 lines of C++).
- **Regression Risk:** Low, provided memory map offsets map correctly.
- **Limitation:** The MAME 68000 core ignores external `/DTACK` in standard address maps unless hooked into cycle-stealing wait states.

### Option C: Implement a Full ES5701 Device (ESP + OTIS + Clocks + Nybble Mode)
- **Physical Fidelity:** **Fictitious for ASR-10.** The ASR-10 does not use OTIS, static memory address latching, 8/12-bit masking, nybble mode, or 10 MHz clocking.
- **Complexity:** High (>500 lines of unused logic).
- **Regression Risk:** High.

---

## 12. Candidate Authentic Workload Discriminators

Could any authentic workload discriminate between the current collapsed model and an explicit ES5701 model?

| Workload Area | Candidate Test Workload | Discrimination Potential | Rationale / Assessment |
|---|---|:---:|---|
| **Host Register Burst Writes** | Rapid effect switching (`ROM-01..ROM-50`) | **NON-BLOCKING FOR CURRENT WORKLOADS** | Tested empirically: all 50 ROM effects download with 0 retries in MAME without wait-states. No functional consequence observed. |
| **Host Access OK Polling** | DSP program upload (`comp_dist_reverb`, `comp_pitch_shift`) | **NON-BLOCKING FOR CURRENT WORKLOADS** | Firmware polls bit 7 of `$FC3001`; immediate ready in MAME satisfies firmware requirements. Timing fidelity remains open but non-blocking. |
| **Sample Memory Banking** | Loading large multisamples (`WAV` playback, `FILE LOADED`) | **NON-BLOCKING FOR CURRENT WORKLOADS** | Banking is mediated by board-level `CS1` logic (`$FF7F00..$FF7FFF`), completely outside the ES5701. |
| **8/12-bit Sample Playback** | EPS-1 legacy disk load | **NON-BLOCKING FOR CURRENT WORKLOADS** | ASR-10 native sound engine runs 16-bit; mode configuration pins are expected high for 16-bit mode. |

---

## 13. Recommendations and Decision Rationale

### 13.1 Modeling Decision
- **Retain the Collapsed Implementation (Option A):** The current driver-level memory-handler interface mapping `$FC3000..$FC303F` directly to `es5510_device::host_r/w` will remain in place.
- **Explicit `es5701_device` Not Justified:** No authentic workload currently reveals a defect or behavioral divergence attributable to the absence of an explicit ES5701 device.
- **Scope of Component Knowledge:** The generic ES5701 chip behavior is well understood from Bob Yannes' Rev. 2 specification, but exact ASR-10 digital-board wiring and physical timing remain partially inferred and partially open.

### 13.2 Decision Rationale
The decision to freeze this track without introducing an explicit `es5701_device` is strictly workload-driven and grounded in OpenASR methodology:
1. **Workloads Heavily Exercise the Interface:** Authentic ASR-10 firmware exercises the ESP host interface during boot and on every effect selection. All 50 internal ROM effect programs (`ROM-01` through `ROM-50`) now download, verify 100% of registers, and achieve 0 mismatches and 0 retries.
2. **No First Semantic Divergence in ES5701 Territory:** In authentic workloads, no bug, stall, timeout, or corruption has been identified whose root cause resides in the bus multiplexing or `/DTACK` reflection performed by the ES5701.
3. **Avoid Architecture-Driven Churn:** Adding an explicit `es5701_device` and synthetic `/DTACK` cycle stretching without an authentic workload failure would be speculative architecture-building rather than defect-driven engineering. MAME's 68000 core does not support external `/DTACK` wait-state stretching in standard memory maps without custom bus controllers.
4. **Reopen Criteria:** This modeling decision is not permanent dogma. The track should be reopened if and only if:
   - Materially new authentic board evidence appears (e.g. an authentic Ensoniq Digital Board schematic or physical trace continuity measurements); OR
   - An authentic workload exhibits a first semantic divergence attributable to ES5701-owned behavior.

---

## 14. ASR-10 Hardware Provenance Audit and Claim Matrix

### 14.1 Epistemic Classification Framework
In accordance with `docs/asr10/reference/methods-hypothesis-management.md`, every hardware assertion is classified by its strict evidentiary source:
- `[VERIFIED — ES5701 specification]`: Directly confirmed by Bob Yannes' Rev. 2 specification (`docs/ensoniq/ES5701.pdf`).
- `[VERIFIED — ASR service documentation]`: Explicitly documented in Ensoniq ASR-10 Service Manual (Model #9500).
- `[VERIFIED — schematic]`: Directly confirmed from an authentic board schematic (NB: digital board schematic was never published).
- `[VERIFIED — PCB evidence]`: Physically observed on the physical ASR-10 Digital Board (silkscreen, IC package, socket).
- `[VERIFIED — firmware]`: Directly extracted and verified from authentic V3.50 ROM disassembly.
- `[OBSERVED — MAME / emulator configuration]`: Measured or configured in the MAME emulation.
- `[INFERRED]`: Deductions based on known component interfaces, functional operation, or standard 68000 bus architecture.
- `[LIKELY]`: Strong circumstantial evidence exists, but direct schematic/trace verification is absent.
- `[OPEN]`: Unverified empirical question with no conclusive physical evidence.
- `[DISPROVEN]`: Factually refuted or invalidated by documentary/physical evidence.

*Rule: Epistemic label `[VERIFIED — architecture]` is strictly prohibited; architecture is a conclusion, not an evidentiary source.*

### 14.2 Master Claim Inventory and Provenance Matrix

| Claim ID | Hardware Claim / Statement | Direct Source Document & Section | Evidence Type | Epistemic Status | Technical Analysis & Audit Notes |
|---|---|---|---|:---:|---|
| **HW-001** | U41 on the ASR-10 Digital Board is the ES5701 Super-GLU | *ASR-10 Service Manual* (Model #9500), Major Component Locations & Parts List; Physical board inspection | Board layout / Silkscreen / IC package | `[VERIFIED — ASR service documentation]` / `[VERIFIED — PCB evidence]` | Package marked "ES5701" in 100-pin QFP at location U41. |
| **HW-002** | U28 is Motorola MC68302 MCU (16 MHz) | *ASR-10 Service Manual*, Parts List; Physical board inspection | Silkscreen / IC package | `[VERIFIED — ASR service documentation]` / `[VERIFIED — PCB evidence]` | Package marked "MC68302FC16C" in 132-pin PQFP. |
| **HW-003** | U29 is Ensoniq ES5506 (OTTO) | *ASR-10 Service Manual*, Section 2 & Parts List; Physical board inspection | Service doc / Silkscreen / IC package | `[VERIFIED — ASR service documentation]` / `[VERIFIED — PCB evidence]` | Marked "ES5506" in 100-pin QFP. |
| **HW-004** | U43 is Ensoniq ES5510 (ESP) | *ASR-10 Service Manual*, Parts List & Error Code 032 notes; Physical board inspection | Service doc / Silkscreen / IC package | `[VERIFIED — ASR service documentation]` / `[VERIFIED — PCB evidence]` | Marked "ES5510" in 44-pin PLCC. |
| **HW-005** | U5 is custom PAL "ASR-10 V1.1" | *ASR-10 Service Manual*, Parts List; Physical board inspection | Socketed IC label / Silkscreen | `[VERIFIED — PCB evidence]` | Socketed 24-pin DIP marked "ASR-10 V1.1 PAL". |
| **HW-006** | Digital Board 4-layer schematic was never published by Ensoniq | *ASR-10 Service Manual* (schematic index lists only KBD, PNL, PS, Filter, SCSI) | Complete service manual review | `[VERIFIED — ASR service documentation]` | Ensoniq withheld the 4-layer Digital Board schematic; netlist is not publicly available. |
| **HW-007** | ES5701 Rev. 2 specification defines a 99-pin gate array with 4 functional blocks | `docs/ensoniq/ES5701.pdf` (Bob Yannes, Sep 14, 1988) | Silicon spec | `[VERIFIED — ES5701 specification]` | 99 active pins: ESP mux, OTIS transceiver, static memory latch/mux, clock dividers. |
| **HW-008** | ES5701 multiplexes MC68302 A8..A1 and D7..D0 onto ES5510 A8/D7..A1/D0 | `ES5701.pdf` p. 2; ASR-10 firmware `$F973F0..$F97800` | Spec & Firmware | `[VERIFIED — ES5701 specification]` / `[VERIFIED — firmware]` / `[INFERRED — net]` | Pin functions verified in spec; firmware targets `$FC3000..$FC31FF`. Net between U41 and U43 is inferred from topology. |
| **HW-009** | Board decode generates `/ESP` via PAL U5 / 74HC138 for `$FC3000..$FC31FF` | ASR-10 memory map; board IC complement (U5, 74HC138) | Functional deduction | `[INFERRED]` | Standard 68000 decode topology; exact PAL equations unpublished. |
| **HW-010** | ES5701 `/DTACK` output is an open-collector buffer driven by `/ESP` | `ES5701.pdf` p. 2 ("open collector buffer... to produce /DTACK") | Silicon spec | `[VERIFIED — ES5701 specification]` | Spec explicitly documents open-collector buffer reflection. |
| **HW-011** | ES5701 `/DTACK` pin connects to MC68302 `/DTACK` input on ASR-10 board | MC68302 bus architecture; asynchronous termination | Functional deduction | `[INFERRED]` | MC68302 requires external `/DTACK` assertion for decoded unreserved spaces. |
| **HW-012** | External pull-up resistor exists on the `/DTACK` net | 68000 bus specification; open-collector bus line | Electrical requirement | `[INFERRED]` | Open-collector wired-OR bus requires a pull-up resistor to reach high state. |
| **HW-013** | Propagation delay of ES5701 `/DTACK` assertion is ~10–25 ns | Previous engineering estimate | Retracted estimate | `[OPEN — timing]` | Unmeasured on physical ASR-10 hardware; spec does not provide propagation delay ratings. |
| **HW-014** | MC68302 bus cycle duration at 16 MHz is ~250 ns (4 clocks, zero wait states) | *MC68302 User's Manual*, Bus Timing Specifications | Silicon spec | `[VERIFIED — MC68302 specification]` | 4 clock cycles @ 16 MHz = 250 ns nominal. |
| **HW-015** | ES5510 Host Access OK duration is 1–2 instruction cycles (~33–66 ns at 30 MHz) | *ES5510 Technical Specification*, Section 3; MAME clock config | Silicon spec / Cycle math | `[VERIFIED — ES5510 specification]` (math) / `[OBSERVED — emulator configuration]` (30 MHz) / `[OPEN — non-blocking]` (timing fidelity) | Instruction cycle duration is ~33.3 ns at 30 MHz. Timing fidelity is non-blocking for established workloads. |
| **HW-016** | ES5701 static memory address latches (`LA19..LA4`) are not required by ASR-10 dynamic sample-memory architecture | *ASR-10 Service Manual*, SIMM socket specifications; PCB layout | Physical board inspection / Architecture deduction | `[INFERRED — not required by sample-memory architecture]` / `[OPEN — exact physical PCB connection]` | ASR-10 uses dynamic RAM SIMMs (16-bit wide) with multiplexed row/col addresses, not static RAM. Exact electrical connection of LA19..LA4 pins remains unverified without a digital-board schematic or continuity evidence. |
| **HW-017** | ES5701 12-bit nybble multiplexer (`DA11H/L..DA8H/L`, `/NYB`) is not required by ASR-10 | ASR-10 Digital Board component complement | Physical board inspection | `[INFERRED — not required by sample-memory architecture]` / `[OPEN — exact physical PCB connection]` | No nybble SRAMs exist on the ASR-10 digital board; physical pin wiring unverified without schematic. |
| **HW-018** | Mode pins `M1` and `M0` are effectively required to be tied high for established 16-bit operation | Functional requirement of 16-bit sound engine; `ES5701.pdf` mode table | Functional deduction / Silicon spec | `[VERIFIED — ES5701 specification]` (mode semantics) / `[INFERRED — functional requirement]` / `[OPEN — PCB wiring]` | Spec defines M1=M0=1 as 16-bit mode (no bits pulled low). Required for uncorrupted 16-bit sample data. Physical trace connection to VCC remains unverified without schematic. |
| **HW-019** | `/NYB` pin is effectively required to be tied high (disabled) for established 16-bit operation | Functional requirement of non-nybble memory; `ES5701.pdf` | Functional deduction / Silicon spec | `[VERIFIED — ES5701 specification]` (pin function) / `[INFERRED — functional requirement]` / `[OPEN — PCB wiring]` | Nybble mode disabled. Physical trace connection to VCC remains unverified without schematic. |
| **HW-020** | ES5701 provides bus transceiver / isolation between CPU data bus and OTTO bus | `ES5701.pdf` Section 2 (transceiver gated by `/DBUS`) | Silicon spec / Functional deduction | `[VERIFIED — ES5701 specification]` (silicon capability) / `[INFERRED — board topology]` (physical routing on ASR-10) | Spec provides bi-directional bus transceiver. Physical routing between U28, U41, U29 is inferred from bus topology. |
| **HW-021** | ES5701 OTIS interface is "dead silicon" on ASR-10 | Previous investigation narrative | Critical reassessment | `[DISPROVEN — semantic overstatement]` | Static address latches and nybble mux are unneeded, but CPU ↔ OTTO data pass-through and bus isolation (`/DBUS=0`) is documented active silicon capability. |
| **HW-022** | ES5701 20 MHz -> 10 MHz clock generator is unused on ASR-10 | System clock architecture; oscillator complement | Architectural deduction | `[INFERRED — system architecture]` | ASR-10 uses 16 MHz system clock and 30 MHz ESP oscillator; no 10 MHz consumer exists. |
| **HW-023** | ES5701 16 MHz -> 8 MHz clock generator outputs 8 MHz for DUART / FDC | `ES5701.pdf` clock section; DUART/FDC specs | Functional possibility | `[OPEN — PCB wiring]` | ES5701 has 16->8 MHz divider, but whether board uses it or DUART's own crystal/prescaler is unverified without digital-board schematic. |
| **HW-024** | Sample memory banking table at `$FF7F00..$FF7FFF` is 256 bytes per-voice | Authentic V3.50 firmware disassembly; MAME runtime tap | Firmware & Runtime | `[VERIFIED — firmware]` | Firmware explicitly writes voice banking configuration to `$FF7F00..$FF7FFF`. |
| **HW-025** | Sample memory banking table at `$FF7F00..$FF7FFF` is implemented inside ES5701 | Speculative attribution in early notes | Silicon spec analysis | `[DISPROVEN]` | ES5701 contains zero internal SRAM or registers and addresses only up to LA19; table resides in external board logic. |

### 14.3 Subsystem Provenance Analysis

#### 14.3.1 Schematic Availability & Component Identities
- **Schematic Void:** Ensoniq published schematics for sub-assemblies (Keyboards, Front Panel, Power Supply, Audio Jack/Filter, SCSI), but **explicitly omitted the 4-layer Digital Board schematic**. Therefore, any claim about internal digital board trace connections is necessarily `[INFERRED]` or `[OPEN — PCB wiring]`, unless verified by multimeter continuity testing on physical hardware.
- **Component Markings:** U28 (MC68302), U29 (ES5506), U41 (ES5701), U43 (ES5510), and U5 (custom PAL) are all physically confirmed by silkscreen and IC markings (`[VERIFIED — PCB evidence]`).

#### 14.3.2 ESP Path & /DTACK Path
- **Multiplexed Bus:** The functional mapping of MC68302 address/data through ES5701 to ES5510 host port is `[VERIFIED — ES5701 specification]` and `[VERIFIED — firmware]`.
- **/DTACK Reflection:** The open-collector reflection of `/ESP` to `/DTACK` inside ES5701 is `[VERIFIED — ES5701 specification]`. The connection of this pin to the MC68302 `/DTACK` net is `[INFERRED]` from 68000 bus termination requirements.
- **Timing Retractions:** Numerical claims asserting ~10–25 ns gate delay for `/DTACK` are unmeasured on physical silicon and are formally retracted to `[OPEN — timing]`.

#### 14.3.3 CPU ↔ OTTO Path & "Dead Silicon" Reassessment
- **Correction:** Previous drafts described the ES5701 OTIS interface as "dead silicon". This is factually inaccurate. While the static memory address latches (`LA19..LA4`) and 12-bit nybble multiplexer are unneeded (ASR-10 uses dynamic RAM SIMMs without nybble chips), the bidirectional data bus transceiver isolating CPU data from OTTO wavetable data during synthesis cycles (`/DBUS=0`) is an active silicon function.
- **Mode Pins:** Pins `M1`, `M0`, and `/NYB` are effectively required to be held high for 16-bit operation (`[INFERRED — functional requirement]`), but their physical PCB trace connection to VCC remains `[OPEN — PCB wiring]`.

#### 14.3.4 Clocks & CS1 Banking Table
- **Clock Dividers:** ES5701 20 MHz oscillator is unused (`[INFERRED]`). The 16 MHz -> 8 MHz divider is physically present in silicon, but its connection to DUART/FDC on the ASR-10 board is unverified (`[OPEN — PCB wiring]`).
- **CS1 Table ($FF7F00..$FF7FFF):** Proven to be firmware-driven (`[VERIFIED — firmware]`), but physical residence in ES5701 is refuted (`[DISPROVEN]`). ES5701 has no internal RAM or storage registers.

### 14.4 Epistemic Summary

| Status Category | Count | Claims |
|---|:---:|---|
| `[VERIFIED — ES5701 specification]` | 5 | HW-007, HW-008 (logic), HW-010, HW-015 (math), HW-020 (logic) |
| `[VERIFIED — ASR service documentation]` | 5 | HW-001, HW-002, HW-003, HW-004, HW-006 |
| `[VERIFIED — PCB evidence]` | 5 | HW-001, HW-002, HW-003, HW-004, HW-005 |
| `[VERIFIED — firmware]` | 2 | HW-008 (map), HW-024 |
| `[VERIFIED — MC68302 specification]` | 1 | HW-014 |
| `[OBSERVED — emulator configuration]` | 1 | HW-015 (clock) |
| `[INFERRED]` | 8 | HW-008 (net), HW-009, HW-011, HW-012, HW-016 (architecture), HW-017 (architecture), HW-018 (logic), HW-019 (logic), HW-022 |
| `[OPEN — PCB wiring / timing]` | 5 | HW-013 (timing), HW-015 (fidelity), HW-016 (trace), HW-017 (trace), HW-018 (trace), HW-019 (trace), HW-023 (trace) |
| `[DISPROVEN]` | 2 | HW-021 ("dead silicon" overstatement), HW-025 (CS1 table inside ES5701) |

*Total audited claims: 25 claims across all functional subsystems.*

---

## 15. Track Status and Freeze

- **ES5701 architecture:** CLOSED / FROZEN for current functional workloads
- **Explicit es5701_device:** NOT JUSTIFIED at present
- **Generic ES5701 ESP mux semantics:** VERIFIED from specification
- **Exact ASR-10 board routing:** PARTIALLY OPEN
- **/DTACK physical timing:** OPEN — non-blocking for established workloads
- **Host Access OK timing fidelity:** OPEN — separate future issue (non-blocking)
- **OTIS/OTTO-side exact ASR wiring:** PARTIALLY OPEN
- **Reopen conditions:**
  1. Materially new authentic board evidence (e.g. an authentic Digital Board schematic or physical trace continuity measurements).
  2. An authentic workload whose first semantic divergence points to ES5701-owned behavior.

