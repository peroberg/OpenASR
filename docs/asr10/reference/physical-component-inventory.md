# ASR-10 Physical Component Inventory

**Canonical In-Tree Record of Physically Observed Components**  
**Document Status:** Reference / Observational Record  
**Epistemic Baseline:** `[OBSERVED — owner-supplied physical inspection]`  
**Target Hardware:** Ensoniq ASR-10 Digital Main Board & SP-1 / SP-3 SCSI Interface Module  

---

## 1. Provenance and Evidentiary Boundaries

This document provides the canonical in-repository record of physical IC package markings, designators, pin counts, and package types observed directly on authentic Ensoniq ASR-10 hardware.

### Evidentiary Principles:
1. **Source of Observations:**
   All entries in this document originate from direct physical inspection and high-resolution photographic evidence of physical ASR-10 units supplied by the project owner (Per). This information was previously referenced informally in historical working notes (e.g. historical `PLAN.md`, `scc-board-source-question.md`), but is codified here as an authoritative in-tree reference.
2. **Observational Evidence vs. Schematics:**
   These tables represent **observational evidence**, NOT an official Ensoniq Bill of Materials (BOM), engineering schematic, or netlist. Ensoniq never published a schematic or layout diagram for the 4-layer Digital Main Board.
3. **Incomplete and Incremental:**
   This inventory is intentionally incomplete. It records only what has been physically witnessed, photographed, and transcribed to date. Unobserved components, unreadable package markings, unpopulated footprints, and uncertain pin counts remain explicitly marked as such. Gaps are **not** filled in with speculative parts from general hardware knowledge or datasheets.
4. **Unit- and Revision-Specific Codes:**
   Date codes (e.g. `9322`, `9307`, `9607`) and lot identifiers are specific to the physically inspected units and PCB production runs (predominantly 1992–1993 for the Main Board and 1995–1996 for the SCSI module). Other production revisions may use alternative component vendors, speed grades, or date codes.
5. **Board-Local Reference Designators:**
   Reference designators (`U1`, `U2`, `U5`, etc.) are strictly **local to each individual PCB**. Main Board `U5` (custom PAL) has no electrical or physical relationship to SCSI Board `U5` (MC74HC161AN counter). Documentation must always qualify designators with board context (e.g. *Main Board U5* vs. *SCSI Board U5*) whenever ambiguity could arise.
6. **Presence Does Not Equal Connectivity:**
   Physical presence of an integrated circuit on a PCB establishes that the chip exists in the system. It does **not** by itself establish trace routing, bus connectivity, chip-select decoding, interrupt assignment, or functional behavior. Electrical connectivity must be verified through authentic schematics, multimeter continuity traces, or firmware address decoding, not assumed from chip proximity.
7. **Citation Discipline:**
   Future investigation and reference documents must cite this file directly when asserting physical component presence, using the epistemic tag:
   ```
   [OBSERVED — owner-supplied physical inspection]
   ```
   Stronger tags such as `[VERIFIED — schematic]` or `[VERIFIED — netlist]` must not be applied to Digital Board circuitry unless authentic circuit diagrams or direct continuity measurements are produced.

---

## 2. ASR-10 Main Board

* **PCB Description:** Ensoniq ASR-10 Digital Main Board (4-layer PCB).
* **Observed Units:** Production units with 1992–1993 date codes, Boot ROM V1.50B (`648C` lower, `65E0` upper).
* **Observation Status:** `[OBSERVED — owner-supplied physical inspection]` for all raw markings.

### 2.1 Integrated Circuits (U1 – U67)

| Ref | Raw marking | Identified part | Pins | Observation status | Notes |
|---|---|---|:---:|:---:|---|
| **U1** | `TI 3156 0 BK SN74F253N` | SN74F253N | 16 | Observed | Dual 4-to-1 data selector/multiplexer with 3-state outputs (Fast TTL) |
| **U2** | `6116 SA25TP delta 9322P` | 6116 SRAM | 24 | Observed | 2K x 8 static RAM (24-pin DIP, 25 ns, week 22 1993) |
| **U3** | `TI 3156 0BK SN74F253N` | SN74F253N | 16 | Observed | Dual 4-to-1 data selector/multiplexer with 3-state outputs (Fast TTL) |
| **U4** | `Motorola MC74HC4066N FFLY9249` | MC74HC4066N | 14 | Observed | Quad bilateral analog switch (High-speed CMOS, week 49 1992) |
| **U5** | `ASR-10 V1.1 6457` | Custom PAL / PLD | 20 | Observed | Socketed 20-pin DIP PAL/GAL; address decode / glue logic |
| **U6** | `TI 3156 0BK SN74F253N` | SN74F253N | 16 | Observed | Dual 4-to-1 data selector/multiplexer with 3-state outputs (Fast TTL) |
| **U7** | `Motorola MC74HC374AN FFHR9306` | MC74HC374AN | 20 | Observed | Octal D-type flip-flop with 3-state outputs (High-speed CMOS) |
| **U8** | `Motorola MC74F157AN XXAA9306` | MC74F157AN | 16 | Observed | Quad 2-line to 1-line data selector/multiplexer (Fast TTL) |
| **U9** | `Motorola MC74HC139AN FFGW9304` | MC74HC139AN | 16 | Observed | Dual 1-of-4 decoder/demultiplexer (High-speed CMOS) |
| **U10** | `TI 3156 0BK SN74F253N` | SN74F253N | 16 | Observed | Dual 4-to-1 data selector/multiplexer with 3-state outputs (Fast TTL) |
| **U11** | `Motorola MC74HC374AN FFHR9306` | MC74HC374AN | 20 | Observed | Octal D-type flip-flop with 3-state outputs (High-speed CMOS) |
| **U12** | `TI 237 2 OR K SN74HC157N` | SN74HC157N | 16 | Observed | Quad 2-line to 1-line data selector/multiplexer (High-speed CMOS) |
| **U13** | `Motorola MC74HC74AN FFMA9321` | MC74HC74AN | 14 | Observed | Dual D-type positive-edge-triggered flip-flop with preset/clear |
| **U14** | `TI 3156 0BK SN74F253N` | SN74F253N | 16 | Observed | Dual 4-to-1 data selector/multiplexer with 3-state outputs (Fast TTL) |
| **U15** | `Motorola MC74HC374AN FFHR9306` | MC74HC374AN | 20 | Observed | Octal D-type flip-flop with 3-state outputs (High-speed CMOS) |
| **U16** | `Motorola MC74HC161AN FFNT9327` | MC74HC161AN | 16 | Observed | 4-bit synchronous binary counter with asynchronous clear |
| **U17** | `Motorola MC74HC161AN FFNT9327` | MC74HC161AN | 16 | Observed | 4-bit synchronous binary counter with asynchronous clear |
| **U18** | `TI 3156 0BK SN74F253N` | SN74F253N | 16 | Observed | Dual 4-to-1 data selector/multiplexer with 3-state outputs (Fast TTL) |
| **U19** | `ASR 1.50B 648C LOWER` | 27C010 / 27C020 EPROM | 32 | Observed | Boot ROM Lower Byte (Bits 7..0); checksum/version label `648C` |
| **U20** | `DUART KOREA S SCN2681AC1N40 KNX1943 9322KH` | SCN2681AC1N40 | 40 | Observed | Dual Asynchronous Receiver/Transmitter (Signetics/Philips DUART) |
| **U21** | `Motorola MC74HC161AN FFNT9327` | MC74HC161AN | 16 | Observed | 4-bit synchronous binary counter with asynchronous clear |
| **U22** | `ASR 1.50B 65E0 UPPER` | 27C010 / 27C020 EPROM | 32 | Observed | Boot ROM Upper Byte (Bits 15..8); checksum/version label `65E0` |
| **U23** | `Motorola MC74HC139AN FFGW9304` | MC74HC139AN | 16 | Observed | Dual 1-of-4 decoder/demultiplexer (High-speed CMOS) |
| **U24** | `Motorola MC74HC374AN FFHR9306` | MC74HC374AN | 20 | Observed | Octal D-type flip-flop with 3-state outputs (High-speed CMOS) |
| **U25** | `Motorola MC74HC373AN FFIO9310` | MC74HC373AN | 20 | Observed | Octal transparent latch with 3-state outputs (High-speed CMOS) |
| **U26** | `Motorola MC74HC138AN FFOS9313` | MC74HC138AN | 16 | Observed | 1-of-8 decoder/demultiplexer (High-speed CMOS) |
| **U27** | `Motorola SN74LS14N XXAB9306` | SN74LS14N | 14 | Observed | Hex Schmitt-trigger inverter (Low-power Schottky TTL) |
| **U28** | `MPU Motorola MC68302FC16C 1C65T QETY9307` | MC68302FC16C | 1?? | Observed | Integrated Multiprotocol Processor (16 MHz 68000 core; recorded as `1?? Pin`; standard package is 132-pin PQFP) |
| **U29** | `ENSONIQ OTTOR2 ES5506000102 0390258 FR08978 N9322` | ES5506 (OTTO) | ?? | Observed | 32-channel digital sound generator ASIC (recorded as `?? pin`; standard package is 100-pin QFP) |
| **U30** | `Motorola MC74HC4053N FFJY9319` | MC74HC4053N | 16 | Observed | Triple 2-channel analog multiplexer/demultiplexer |
| **U31** | `TI 252 3 4 YK SN74HC153N` | SN74HC153N | 16 | Observed | Dual 4-to-1 data selector/multiplexer (High-speed CMOS) |
| **U32** | `TI 251 2 3CK SN74HC174N` | SN74HC174N | 16 | Observed | Hex D-type flip-flop with master reset (High-speed CMOS) |
| **U33** | `Motorola MC74HC4053N FFJY9319` | MC74HC4053N | 16 | Observed | Triple 2-channel analog multiplexer/demultiplexer |
| **U34** | `FDC NEC JAPAN D72069GF NEC ’889322EP003` | uPD72069GF | ?? | Observed | Floppy Disk Controller with analog data separator (recorded as `?? pin`; standard package is 64-pin QFP) |
| **U35** | `TI 3040 8YK SN74HC153N` | SN74HC153N | 16 | Observed | Dual 4-to-1 data selector/multiplexer (High-speed CMOS) |
| **U36** | `TI 3113 HSK SN7406N` | SN7406N | 14 | Observed | Hex inverter buffer/driver with open-collector high-voltage outputs |
| **U37** | `Motorola MC74HC00AN FF0S9316` | MC74HC00AN | 14 | Observed | Quad 2-input NAND gate (High-speed CMOS) |
| **U38** | `Motorola MC74HC74AN FFMA9321` | MC74HC74AN | 14 | Observed | Dual D-type positive-edge-triggered flip-flop with preset/clear |
| **U39** | `Motorola MC74HC74AN FFMA9321` | MC74HC74AN | 14 | Observed | Dual D-type positive-edge-triggered flip-flop with preset/clear |
| **U40** | `TI 321 5 H OK SN7407N` | SN7407N | 14 | Observed | Hex buffer/driver with open-collector high-voltage outputs |
| **U41** | `SGLU USA ENSONIQ 5701000101 9321KVB50` | ES5701 (Super-GLU) | 1?? | Observed | Ensoniq Super-GLU bus and memory interface ASIC (recorded as `1?? pin`; standard package is 100-pin QFP) |
| **U42** | `HP 6N138 9307` | 6N138 | 8 | Observed | High-gain optocoupler (MIDI In optoisolator) |
| **U43** | `ESP ES5510` | ES5510 (ESP) | ?? | Observed | Ensoniq Signal Processor DSP ASIC (recorded as `?? pin`; standard package is 44-pin PLCC) |
| **U44** | `H 7555 IPA H 9314` | ICM7555IPA | 8 | Observed | CMOS general-purpose timer |
| **U45** | `Motorola MC74HC373AN FFHG9316` | MC74HC373AN | 20 | Observed | Octal transparent latch with 3-state outputs (High-speed CMOS) |
| **U46** | `Motorola MC74F157AN XXAA9306` | MC74F157AN | 16 | Observed | Quad 2-line to 1-line data selector/multiplexer (Fast TTL) |
| **U47** | `TI 3086 E 5K SN74F161AN` | SN74F161AN | 16 | Observed | 4-bit synchronous binary counter (Fast TTL) |
| **U48** | `TI 3115 4 2K SN74F02N` | SN74F02N | 14 | Observed | Quad 2-input NOR gate (Fast TTL) |
| **U49** | `AD P9318 MM74HCT34N MC74HCT34N` | 74HCT34 | 14 | Observed | Hex non-inverter buffer (TTL-compatible CMOS) |
| **U50** | `TI 3110 JNK SN74F74N` | SN74F74N | 14 | Observed | Dual D-type positive-edge-triggered flip-flop (Fast TTL) |
| **U51** | `NEC USA 41464C-10 9323A V A 30` | uPD41464C-10 | 18 | Observed | 64K x 4 dynamic RAM (100 ns, 18-pin DIP) |
| **U52** | `NEC USA 41464C-10 9323A V A 30` | uPD41464C-10 | 18 | Observed | 64K x 4 dynamic RAM (100 ns, 18-pin DIP) |
| **U53** | `NEC USA 41464C-10 9322A V B 23` | uPD41464C-10 | 18 | Observed | 64K x 4 dynamic RAM (100 ns, 18-pin DIP) |
| **U54** | `NEC USA 41464C-10 9323A V A 30` | uPD41464C-10 | 18 | Observed | 64K x 4 dynamic RAM (100 ns, 18-pin DIP) |
| **U55** | `Motorola MC74HC4051N FFPM9301` | MC74HC4051N | 16 | Observed | 8-channel analog multiplexer/demultiplexer (POT/PAR input mux) |
| **U56** | `TI 3130ZBT LM358P` | LM358P | 8 | Observed | Dual operational amplifier |
| **U57** | `S LM393N FNF1101 9318VI` | LM393N | 8 | Observed | Dual voltage comparator |
| **U58** | `TI 321 5 H OK SN7407N` | SN7407N | 14 | Observed | Hex buffer/driver with open-collector high-voltage outputs |
| **U59** | `N/A` | *none* | — | Unpopulated / Absent | Recorded as N/A in physical inspection |
| **U60** | `N/A` | *none* | — | Unpopulated / Absent | Recorded as N/A in physical inspection |
| **U61** | `N/A` | *none* | — | Unpopulated / Absent | Recorded as N/A in physical inspection |
| **U62** | `N/A` | *none* | — | Unpopulated / Absent | Recorded as N/A in physical inspection |
| **U63** | `N/A` | *none* | — | Unpopulated / Absent | Recorded as N/A in physical inspection |
| **U64** | `TI 3110 JNK SN74F74N` | SN74F74N | 14 | Observed | Dual D-type positive-edge-triggered flip-flop (Fast TTL) |
| **U65** | `TI 3115 4 2K SN74F02N` | SN74F02N | 14 | Observed | Quad 2-input NOR gate (Fast TTL) |
| **U66** | *Absent from observation list* | *unidentified* | — | Not recorded | Not present in supplied observations; do not invent part |
| **U67** | `MC74HC27N FFNY9323` | MC74HC27N | 14 | Observed | Triple 3-input NOR gate (High-speed CMOS, week 23 1993) |

### 2.2 Crystals and Oscillators (Y1 – Y3)

| Ref | Raw marking | Nominal Frequency | Package / Type | Observation status | Functional Consumer & Routing Status |
|---|---|---:|---|:---:|---|
| **Y1** | `RALTRON 16.000 MHz SERIES 93L06` | 16.000000 MHz | HC-49/U or metal can | Observed | Feeds MC68302 IMP system clock (`EXTAL` pin). MAME `[Verified code]`. |
| **Y2** | `ECLIPTEK ECX-1278 30.47618 MHz` | 30.476180 MHz | Metal can oscillator / crystal | Observed | Audio master clock candidate; exact physical divider/mux path `[OPEN]`. Direct ES5506 master clock in MAME `[Verified code]`. |
| **Y3** | `ECLIPTEK ECX-964 33.8688 MHz` | 33.868800 MHz | Metal can oscillator / crystal | Observed | 44.1 kHz-domain master clock candidate (33.8688 MHz / 768 = 44.1 kHz); exact physical divider/mux path `[OPEN]`. |

---

## 3. SCSI Board / SCSI Module

* **PCB Description:** Ensoniq SP-1 / SP-3 SCSI Interface Option Board.
* **Observed Units:** Production units with 1995–1996 date codes.
* **Observation Status:** `[OBSERVED — owner-supplied physical inspection]` for all raw markings.
* **Scope Note:** Reference designators below are strictly local to the SCSI PCB assembly.

### 3.1 Integrated Circuits (U1 – U24)

| Ref | Raw marking | Identified part | Pins | Observation status | Notes |
|---|---|---|:---:|:---:|---|
| **U1** | `Motorola MC74HC139AN FEA9609` | MC74HC139AN | 16 | Observed | Dual 1-of-4 decoder/demultiplexer (High-speed CMOS, week 9 1996) |
| **U2** | `AM33C93A - 16JC 9607GBA E 1989 AMD` | AM33C93A-16JC | 44 | Observed | SCSI-bus Interface Controller (16 MHz, PLCC-44, week 7 1996) |
| **U3** | `TI 67A32NM SN74HC74N` | SN74HC74N | 14 | Observed | Dual D-type positive-edge-triggered flip-flop |
| **U4** | `Motorola MC74HC161AN FRT9550` | MC74HC161AN | 16 | Observed | 4-bit synchronous binary counter (week 50 1995) |
| **U5** | `Motorola MC74HC161AN FRT9550` | MC74HC161AN | 16 | Observed | 4-bit synchronous binary counter (week 50 1995) |
| **U6** | `Motorola MC74HC374AN FIY9614` | MC74HC374AN | 20 | Observed | Octal D-type flip-flop with 3-state outputs (week 14 1996) |
| **U7** | `Motorola MC74HCT04AN FVP9548` | MC74HCT04AN | 14 | Observed | Hex inverter (TTL-compatible CMOS, week 48 1995) |
| **U8** | `HP 2490 9548` | HCPL-2490 / HP 2490 | 8 | Observed | Optocoupler / logic-gate output optoisolator (HP, week 48 1995) |
| **U9** | `TI 64AGCLM SN74HC00N` | SN74HC00N | 14 | Observed | Quad 2-input NAND gate (High-speed CMOS) |
| **U10** | `Motorola MC74AC74N XAA9652` | MC74AC74N | 14 | Observed | Dual D-type flip-flop (Advanced CMOS, week 52 1996) |
| **U11** | `TI 67A32NM SN74HC74N` | SN74HC74N | 14 | Observed | Dual D-type positive-edge-triggered flip-flop |
| **U12** | `TI 67A32NM SN74HC74N` | SN74HC74N | 14 | Observed | Dual D-type positive-edge-triggered flip-flop |
| **U13** | `HCPL2631 TI9623 KOREA` | HCPL-2631 | 8 | Observed | Dual-channel high-speed 10 MBd optocoupler |
| **U14** | `HP 6N138 9618` | 6N138 | 8 | Observed | High-gain optocoupler (HP, week 18 1996) |
| **U15** | `HP 2601 9549` | HCPL-2601 / HP 2601 | 8 | Observed | High-speed optocoupler (HP, week 49 1995) |
| **U16** | `Motorola MC74HCT244AN` | MC74HCT244AN | 20 | Observed | Octal buffer/line driver with 3-state outputs (TTL-compatible CMOS) |
| **U17** | `???` | *unidentified* | — | Unidentified | Unreadable or unrecorded marking; do not guess part |
| **U18** | `??` | *unidentified* | — | Unidentified | Unreadable or unrecorded marking; do not guess part |
| **U19** | `Motorola MC74HCT244AN FLL9626` | MC74HCT244AN | 20 | Observed | Octal buffer/line driver with 3-state outputs (week 26 1996) |
| **U20** | `???` | *unidentified* | — | Unidentified | Unreadable or unrecorded marking; do not guess part |
| **U21** | `HCPL2631 TI9623 KOREA` | HCPL-2631 | 8 | Observed | Dual-channel high-speed 10 MBd optocoupler |
| **U22** | `Motorola MC74HC04AN FXK9546` | MC74HC04AN | 14 | Observed | Hex inverter (High-speed CMOS, week 46 1995) |
| **U23** | `HCPL2631 TI9623 KOREA` | HCPL-2631 | 8 | Observed | Dual-channel high-speed 10 MBd optocoupler |
| **U24** | `HCPL2631 TI9623 KOREA` | HCPL-2631 | 8 | Observed | Dual-channel high-speed 10 MBd optocoupler |

---

## 4. Known Project-Relevant Devices (Cross-Reference)

For convenient lookup, the primary LSI/VLSI devices modelled in OpenASR and their corresponding physical locations are:

| Subsystem / Function | Modelled Device Class | Physical Board | Designator | Raw Markings |
|---|---|---|:---:|---|
| **CPU / System MPU** | Motorola MC68302 IMP | Main Board | **U28** | `MPU Motorola MC68302FC16C 1C65T QETY9307` |
| **Sound Generator** | Ensoniq ES5506 (OTTO) | Main Board | **U29** | `ENSONIQ OTTOR2 ES5506000102 0390258 FR08978 N9322` |
| **Effects Processor** | Ensoniq ES5510 (ESP) | Main Board | **U43** | `ESP ES5510` |
| **System ASIC / GLU** | Ensoniq ES5701 (Super-GLU) | Main Board | **U41** | `SGLU USA ENSONIQ 5701000101 9321KVB50` |
| **Floppy Controller** | NEC uPD72069 | Main Board | **U34** | `FDC NEC JAPAN D72069GF NEC ’889322EP003` |
| **DUART / Serial** | Signetics SCN2681 | Main Board | **U20** | `DUART KOREA S SCN2681AC1N40 KNX1943 9322KH` |
| **Address Decode PAL** | Custom 20-pin PAL | Main Board | **U5** | `ASR-10 V1.1 6457` |
| **Boot ROM (Low)** | 27C010 / 27C020 EPROM | Main Board | **U19** | `ASR 1.50B 648C LOWER` |
| **Boot ROM (High)** | 27C010 / 27C020 EPROM | Main Board | **U22** | `ASR 1.50B 65E0 UPPER` |
| **Static RAM** | 2K x 8 SRAM (6116) | Main Board | **U2** | `6116 SA25TP delta 9322P` |
| **Dynamic RAM** | 64K x 4 DRAM (41464) | Main Board | **U51–U54** | `NEC USA 41464C-10 ...` (4 chips = 256K x 4 = 128 KB lowmem/scratch) |
| **SCSI Controller** | AMD AM33C93A-16JC | SCSI Module | **U2** | `AM33C93A - 16JC 9607GBA E 1989 AMD` |

---

## 5. Architectural Non-Inference Rules

To preserve epistemic rigor, the following boundaries must be strictly respected:
1. **No Netlist from Proximity:**
   The presence of `6116` SRAM (Main Board U2), `SN74F253` data selectors (U1, U3, U6, U10, U14, U18), `MC74HC374` latches (U7, U11, U15, U24), and `MC74HC161` counters (U16, U17, U21) on the Main Board does **NOT** by itself prove the internal topology of the CS1 per-voice banking mechanism (`$FF7F00..$FF7FFF`). Architectural hypotheses regarding CS1 must remain explicitly qualified as `[INFERRED]` or `[OPEN]`.
2. **No Routing from ES5701 Presence:**
   The presence of ES5701 at Main Board U41 confirms that the physical chip is populated. It does **NOT** prove whether unused functional blocks (e.g. static RAM address latches `LA19..LA4`, nybble multiplexers, or 10 MHz/8 MHz clock dividers) are tied to VCC, grounded, or floating on this specific PCB.
3. **No Bus Assumptions from SCSI Optocouplers:**
   The presence of multiple optocouplers (HCPL-2631, 6N138, HCPL-2601) on the SCSI board indicates signal isolation, but their exact circuit connection (e.g. MIDI thru/out auxiliary paths, SCSI differential transceiver control, or power sensing) must not be assumed without a board trace or schematic.

---

## 6. Known Gaps and Incomplete Observations

The physical component inventory has the following acknowledged evidentiary gaps:
* **Main Board U59 – U63:** Physically recorded as `N/A` (unpopulated or unassigned footprints).
* **Main Board U66:** Absent from the supplied observation list. No component is assigned to U66.
* **SCSI Board U17, U18, U20:** Unidentified (`???` or `??`). These footprints have not yet been successfully transcribed.
* **Main Board U28, U29, U34, U41, U43 Pin Counts:** Recorded with question marks in raw inspection (`1?? Pin`, `?? pin`), although package industry standards are known (132-pin PQFP for MC68302, 100-pin QFP for ES5506/ES5701, 64-pin QFP for uPD72069, 44-pin PLCC for ES5510).
* **Discrete Passives & Connectors:** Resistor networks, capacitors, diodes, transistors, jumpers, and test points are omitted from this inventory.
* **Trace Routing & Layer Stackup:** No continuous trace paths between IC pins have been verified by multimeter continuity testing.
