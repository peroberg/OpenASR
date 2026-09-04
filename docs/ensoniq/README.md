# Ensoniq Source and Reference Index

This directory serves as a bibliographic reference index for Ensoniq hardware and audio silicon documentation.
**Third-party reference PDFs and proprietary transcriptions are excluded from repository distribution under `.gitignore`.**

Treat these references as chip-level or family-level evidence only: they describe what each device or subsystem can do in general, not how a specific ASR-10 revision wires the devices.

## Document Inventory & Cryptographic Hashes

| Document Reference | Title / Description | Author / Revision | SHA-256 Hash | Role in ASR-10 Work |
|---|---|---|---|---|
| `ES5506.pdf` | ES5506 (OTTO) Synthesizer Chip Specification | Ensoniq Corp., Rev 2.3 | `c83f986a68c32c0c9c62263c66a68db7953a2bf3e275fc53071ed2a4791f08b9` | Primary reference for OTTO wavetable/sample playback synthesizer: host registers, voice state, sound-memory interface, PAR, PAGE, IRQV/IRQB, clock/voice divisor ($F_s = \text{CLK} / (16 \times \text{voices})$). |
| `ES5510.pdf` | ES5510 (ESP) Signal Processor Specification | Ensoniq Corp. | `822edf5f79b54a2e20c6b9b4b10be572a56eba212c5bc49835decb6b06ba74a0` | Primary reference for ESP digital signal processor: host interface, GPR and microinstruction memory, external RAM/IO interface, execution and halt semantics. |
| `ES5510-programming.pdf` | ES5510 Programming Guide / Notes | Ensoniq Corp. | `997f40169912eb7ad6a4fec34e04e50e6339842205ecd576d5b33fa5c67101b4` | Supplemental ESP programming and instruction-format notes. |
| `ES5511.pdf` | ES5511 Signal Processor Reference | Ensoniq Corp. | `9b6c67915e26506942970dd62f0a520deeb5072338b7551c63619053766ba92c` | Related ESP-family reference; comparative context for effects architecture. |
| `ES5701.pdf` | 5701 Super-GLU Gate Array Technical Specification | Bob Yannes, Rev. 2 (1988-09-14) | `1cadf7a8345ced262cc469780781b22f232738fbe25d784dbe95c96122732b44` | Super-GLU gate array: 68000-to-ESP, 68000-to-OTIS, OTIS-to-static-memory interface, and clock generation/dividers. (`es5701.md` is a local translation). |
| `EPS.pdf` | EPS Service Manual / Architecture Reference | Ensoniq Corp. | `b5d0494ef7761125105347385b2deaceb51fd342c9ed20016a36c2e1f1f9b1d0` | Architectural predecessor reference for EPS-family comparison and design conventions. |
| `EPS-ES5700.pdf` | EPS / ES5700-family source material | Ensoniq Corp. | `0bd3db5d9967be5379834adbebdbe9fc800ca39ab57a503812f27b800937385d` | ES5700-family logic gate array comparative context. |
| `Ensoniq_EPS_16_Service_Manual.pdf` | Ensoniq EPS-16 Plus Service Manual | Ensoniq Corp. (Model # EPS-16 Plus) | `5b95c60687c590b1ca5f785b7b933ea62ae405c2670c6f30ac96d1dbbb973e7f` | Service procedures, signal names, power routing, and diagnostic routines. |
| `Ensoniq_EPS-16_Plus_Manual.pdf` | Ensoniq EPS-16 Plus Musician's Manual | Ensoniq Corp. | `68ba0838847ff3b3819fd530052bf19dfb85c0aa4bbd18ed0911c38eff64552f` | User-facing operating model, parameter definitions, and disk structure conventions. |

## Evidence and Verification Rules

- A chip specification supports `[Verified silicon spec]`.
- ASR-10 ROM/OS disassembly and execution support `[Verified firmware]`.
- A reproduced MAME test run supports `[Verified runtime]`.
- Physical ASR-10 PCB routing remains `[OPEN]` unless confirmed by an authentic schematic or hardware netlist trace.
