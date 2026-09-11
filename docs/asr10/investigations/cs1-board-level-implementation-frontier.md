# CS1 Per-Voice Sample Banking: Board-Level Implementation Frontier

**Status:** Canonical Board-Level Architectural Frontier  
**Scope:** Ensoniq ASR-10 Digital Board (4-layer PCB), MC68302 Chip Selects, ES5701 SuperGLU, ES5506 OTTO, Custom PAL U5  
**Primary References:**
- Ensoniq ASR-10 Service Manual (Model #9500, P/N 9310014701)
- Motorola MC68302 Integrated Multiprotocol Processor User's Manual
- Ensoniq ES5701 SuperGLU Specification (Bob Yannes, Rev. 2)
- Ensoniq ES5506 OTTO Sound Generator Specification (Rev. 2.3)
- `docs/asr10/investigations/cs1-voice-banking-and-sample-addressing.md`
- Commit `b7cd112199d` ("asr10: implement CS1 per-voice sample banking")

---

## 1. Executive Summary & Epistemic Boundaries

With commit `b7cd112199d`, the functional and architectural mechanism of MC68302 Chip Select 1 (`CS1`) has been completely verified and operationalized in MAME:
- **Firmware Verification:** ASR-10 OS firmware programs a 32-entry $\times$ 4-word per-voice sample banking table in the highest 256 bytes of CS1 (`$FF7F00-$FF7FFF`), mapping each playing voice's 4 MB addressing window to arbitrary 1 MB DRAM segments across the 16 MB address space.
- **Defect Resolution:** Implementing dynamic per-voice sample banking resolved the reproduced Bank 11 playability defect, achieving bit-identical waveforms across boot histories (99.89% cross-correlation). The CS1 banking defect explains the reproduced history-dependent BANK-load failure and is consistent with some earlier symptoms; it does not retroactively prove that every historical audio anomaly had the same cause.

However, a strict separation must be maintained between **firmware-visible functional architecture** and **physical board-level hardware ownership**:
- **[VERIFIED — Firmware & Functional Architecture]**: The MC68302 decodes an 8 KiB window at `$FF6000-$FF7FFF` under CS1. Firmware writes per-voice sample banking registers at `$FF7F00-$FF7FFF`. The ES5506 wavetable address translation depends dynamically on these registers based on the active voice index.
- **[DISPROVEN — Specified ES5701 Register/Storage Model]**: The ES5701 SuperGLU ASIC does not contain internal registers or RAM capable of storing the table, and its specified address outputs reach only up to LA19; while physical CS1 $\rightarrow$ ES5701 board wiring remains `[NOT ESTABLISHED / OPEN]`, ES5701 cannot store the 256-byte per-voice banking table.
- **[OPEN — Physical Board Receiver & Board Wiring]**: The exact physical IC(s) on the 4-layer ASR-10 Digital Board that receive CS1 writes, store the 256 bytes of banking state, the exact mechanism tracking active voice execution `[INFERRED]`, and the exact source of external DTACK `[OPEN]` are unknown because the Digital Board schematic was never published in the service documentation.
- **DOCUMENTATION FRONTIER REACHED — BOARD OWNERSHIP**: Physical IC attribution is not an active functional emulation blocker. MAME correctly models the functional behavior. Reopening hardware investigations without physical board reverse engineering or authentic schematics is prohibited.

---

## 2. MC68302 Chip Select Programming (CS0–CS3)

Firmware in the ASR-10 OS ROM initializes the MC68302 System Integration Block (SIB) Base Register (`BR`) and Option Register (`OR`) for CS0–CS3 at `$F8001E-$F8005D` during cold reset:

### MC68302 BR/OR Bit Layout
```text
BR:  15-13 FC2-FC0 | 12-2 BASE ADDRESS (A23-A13) | 1 RW  | 0 EN
OR:  15-13 DTACK   | 12-2 BASE ADDRESS MASK      | 1 MRW | 0 CFC
```
- `EN` (BR bit 0): Chip Select Enable (1 = enabled).
- `RW` (BR bit 1): Read/Write direction select (0 = read-only, 1 = write-only).
- `MRW` (OR bit 1): Mask Read/Write (0 = RW masked, accepts read and write; 1 = RW unmasked, restricted by BR bit 1).
- `DTACK` (OR bits 15–13): `111` = external DTACK; `000`–`110` = 0 to 6 internal wait states.
- `CFC` (OR bit 0): Compare Function Code (0 = ignore function codes; 1 = match FC2–FC0).

### Decoded Chip Select Configuration

| Chip Select | Base Register (`BR`) | Option Register (`OR`) | Decoded Window | Direction | DTACK Mode | Physical Receiver / Subsystem | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **CS0 (Reset)** | `$0001` | `$3F82` | `$000000-$03FFFF` | Read-only | Internal 1 WS | Boot ROM Overlay (256 KB) | [Verified] |
| **CS0 (Runtime)**| `$1F01` | `$3F82` | `$F80000-$FBFFFF` | Read-only | Internal 1 WS | System OS ROM (256 KB) | [Verified] |
| **CS1** | `$1FEF` | `$FFFE` | **`$FF6000-$FF7FFF`** | **Write-only** (`RW=1, MRW=1`) | **External** (`DTACK=111`) | **Per-Voice Banking Table (`$FF7F00-$FF7FFF`)**; Physical IC [OPEN] | [Verified functional] / [OPEN physical] |
| **CS2** | `$1F85` | `$FFFC` | `$FC2000-$FC3FFF` | Read / Write (`MRW=0`) | External (`DTACK=111`) | ES5506 (`$FC2000`), ES5510 (`$FC3000-$FC31FF`) | [Verified] |
| **CS3** | `$1F89` | `$7FFC` | `$FC4000-$FC5FFF` | Read / Write (`MRW=0`) | Internal 3 WS | NEC uPD72069 FDC (`$FC4000`), DUART (`$FC4801`), WD33C93A SCSI (`$FC5001`) | [Verified] |

### Key Observations:
1. **Window Size vs. Register Offset:** CS1 defines an 8 KiB address window (`$FF6000-$FF7FFF`). The firmware strictly accesses the highest 256 bytes (`$FF7F00-$FF7FFF`, 32 entries $\times$ 8 bytes). The lower 7,936 bytes (`$FF6000-$FF7EFF`) remain unaccessed.
2. **Directionality:** CS1 is uniquely configured as **write-only** (`RW=1` with `MRW=1`). The MC68302 hardware will not assert `/CS1` during CPU read cycles.
3. **Termination:** CS1 requires **external DTACK** (`OR1 bits 15-13 = 111`). Whatever board hardware receives CS1 must assert `/DTACK` back to the MC68302 to terminate write bus cycles.

---

## 3. Disproving ES5701 (SuperGLU) as the Banking Table Receiver

Historically, hypotheses assumed that ES5701 (SuperGLU) housed all miscellaneous audio glue and might contain the voice banking registers. Audit of the authentic specification by Bob Yannes (`docs/ensoniq/ES5701.pdf`, Rev. 2) disproves this hypothesis:

### Architectural Capabilities of ES5701:
1. **No Internal Registers or Storage:** ES5701 is a pure combinatorial and interface gate array. It contains zero internal registers, zero GPRs, zero configuration latches, and zero RAM cells.
2. **Specified Interface Scope:** ES5701's documented pinout specifies chip-select inputs for `/ESP` and `/OTIS`, but shows no internal register addressing for MC68302 CS1. Whether CS1 physically connects to any ES5701 pin on the ASR-10 board is `[NOT ESTABLISHED / OPEN]`, but ES5701 possesses no internal storage to act as the table receiver.
3. **Address Line Limit (LA19):** ES5701's demultiplexed sound address bus outputs only provide address bits up to `LA19` (a 1 MB static RAM addressing range for OTIS). It has no high-order bank lines ($A20-A23$) required to map the 16 MB sample space.
4. **No Voice Tracking:** ES5701 has no voice index counter or voice state tracking mechanism. It operates strictly on clock division ($20 \text{ MHz} \rightarrow 10 \text{ MHz}$, $16 \text{ MHz} \rightarrow 8 \text{ MHz}$), bus isolation between host and sound memory, and dynamic bus sizing.

### Formal Status:
- `[DISPROVEN — specified ES5701 register/storage model]`: The ES5701 SuperGLU does not and cannot store the `$FF7F00-$FF7FFF` per-voice banking table.
- `[NOT ESTABLISHED / OPEN]`: Physical MC68302 CS1 $\rightarrow$ ES5701 board connectivity is unverified without Digital Board schematics.
- **Preserved Role:** ES5701 remains the legitimate bus interface between CPU, ES5510 (ESP), ES5506 (OTTO), and sound memory, handling bus isolation, waitstates, and clock division.

---

## 4. Voice Synchronization Mechanism: ES5506 Pin 45 ($BS0$) [INFERRED]

For external hardware to provide per-voice sample banking during ES5506 sound memory fetch cycles, the external logic must know which voice is currently accessing the sound bus.

The ES5506 specification (`docs/ensoniq/ES5506.pdf`, Rev. 2.3) establishes that:
1. **ES5506 Bank Select Outputs ($BS1:BS0$):**
   - In 4-bank mode, ES5506 outputs pin 46 ($BS1$) and pin 45 ($BS0$) to indicate which 4 MB bank is being addressed by the voice's Control Register (`CR`).
   - For all active playing voices, ASR-10 firmware sets `CR` bit 14 (`bset.b #$e, d0`), asserting $BS1=0, BS0=1$ (Bank 1).
2. **Plausible Voice-Tracking Reconstruction [INFERRED]:**
   - The ES5506 specification establishes that BS state can expose voice-dependent state externally. This makes an external voice-tracking mechanism a plausible implementation of the observed per-voice banking, but the specific counter/table wiring is not board-verified.
   - ES5506 processes voices sequentially in fixed time slots ($0 \dots \text{ACTV}$). Pin 45 ($BS0$) or a frame synchronization pulse could plausibly reset or synchronize an external voice-tracking counter indexing the banking table to drive upper DRAM address lines $A20-A23$, but this remains an inferred mechanism consistent with the specification and firmware behavior rather than a board-verified schematic fact.

---

## 5. Board-Level Physical Implementation Frontier

### The Missing Digital Board Documentation
The official Ensoniq ASR-10 Service Manual (Model #9500) includes comprehensive schematics for peripheral boards:
- Keyboard Interface (`KBD`)
- Front Panel Board (`PNL`)
- Power Supply (`PS`)
- Audio Filter Board
- Audio Jack Board
- Cue / Metronome Board
- SP-1 SCSI Board
- DI-10 Digital I/O Board
- OEX-8 Output Expander

However, **the 4-layer main Digital Board schematic and netlist were never published by Ensoniq** in the service manual or technical bulletins.

### Custom PAL U5 and Board Decoders
- **Component U5:** A 20-pin or 24-pin PAL labeled `"ASR-10 V1.1"`.
- **Ancillary Decoders:** Standard 74HC138 (3-to-8 decoder) and 74HC139 (dual 2-to-4 decoder) logic ICs.
- **Capacity Limitation:** A standard 20/24-pin PAL/GAL (e.g. 16L8, 20V8, 22V10) contains only 8 to 10 macrocells/flip-flops. It cannot store 256 bytes (128 words) of banking data `[NOT SUPPORTED / ruled out by capacity]`.
- **Inferred Role:** PAL U5 is a plausible participant in board-level subdecode (`PAL U5 receives CS1` `[INFERRED]`, `PAL U5 participates in CS1 subdecode` `[INFERRED]`). Its exact CS1 connectivity and role in DTACK generation remain unverified (`PAL U5 generates external CS1 DTACK` `[OPEN]`) without the Digital Board schematic or PAL equations.

### Candidate Physical Implementations for the Banking Table:
1. **Discrete Dual-Port SRAM / Fast Latch Array:**
   A small static RAM (e.g., 256 $\times$ 4 or 256 $\times$ 8/16 SRAM) or multiple 74HC574/670 latch ICs, written by MC68302 CS1 and read out sequentially by the voice-tracking counter.
2. **Proprietary Ensoniq Gate Array / ASIC:**
   An uncharacterized custom digital IC on the Digital Board incorporating both the register file and the address generation counter.
3. **Sub-decoded Board Region:**
   Logic decoding CS1 writes into DRAM upper address latch buffers.

### Status:
- `[OPEN — physical board receiver]`: The physical identity of the IC(s) implementing the 256-byte table on the ASR-10 Digital Board remains open.
- **DOCUMENTATION FRONTIER REACHED — BOARD OWNERSHIP**: Because physical board reverse engineering (desoldering, decapping, logic analyzer traces, or continuity testing) is outside the repo scope and no schematics exist, this constitutes the documented boundary of board-level hardware knowledge.

---

## 6. MAME Driver Modeling Fidelity & Discrepancies

### Read/Write Mapping Discrepancy [VERIFIED — MODEL DIFFERENCE]
- **Hardware Truth:** MC68302 CS1 is strictly configured as write-only (`BR1=$1FEF`, `OR1=$FFFE`, `RW=1, MRW=1`). A 68000 CPU read in `$FF6000-$FF7FFF` will not assert `/CS1` and will encounter a bus error or open bus.
- **MAME Implementation:** In `src/mame/ensoniq/asr10_boot.cpp`:
  ```cpp
  map(0xff7f00, 0xff7fff).rw(FUNC(asr10_boot_state::voice_bank_r), FUNC(asr10_boot_state::voice_bank_w));
  ```
- **Fidelity Assessment:** Firmware disassembly confirms the OS strictly writes to `$FF7F00-$FF7FFF` (`move.w d0, (a0)+`). No firmware read dependence has been observed in the established paths. Permitting reads in MAME is a model difference that does not reflect the hardware's write-only configuration. If desired in future cleanups, `voice_bank_r` may be replaced with `voice_bank_w` and `nopr` / `unmapped`.

### Wavetable Translation Alignment
- MAME's `asr10_boot_state::es5506_wavetable_r` faithfully reproduces the exact mathematical transformation:
  $$\text{phys\_byte\_address} = ((\text{megabyte} \ll 20) \mid (\text{sub\_offset} \ll 1)) \pmod{\text{SYSTEM\_RAM\_BYTES}}$$
- This provides full functional equivalence with authentic hardware while leaving unverified physical IC attribution decoupled from emulation correctness.
