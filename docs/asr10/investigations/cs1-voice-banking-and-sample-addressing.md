# CS1 Voice Banking and Dynamic ES5506 Sample Addressing

**Status:** Verified and Resolved  
**Scope:** `src/mame/ensoniq/asr10_boot.cpp` (`asr10booth`)  
**Target Defect:** Intermittent playability / audio outcome defect where certain loaded banks/instruments produced near-silent clicks or leftover phantom audio from prior loads.

---

## 1. Context & Problem Statement

Historically, playback of loaded instruments in the ASR-10 MAME driver exhibited severe inconsistencies depending on prior session history:
- **Path 1 (historically "BAD"):** Fresh boot -> load File 11 `ATRK TUT BNK` -> select Slot 1 `BLUES DRUMS` -> MIDI Note 60 produced a near-silent click (Peak 486 / 32767, RMS 18.8).
- **Path 2 (historically "GOOD" / phantom):** Boot -> load File 1 `TUTORIAL BNK` -> load File 11 `ATRK TUT BNK` -> select Slot 1 `BLUES DRUMS` -> MIDI Note 60 produced audible sound (Peak 5065, RMS 513.2).

Because Path 2 produced sound while Path 1 did not, this was previously categorized as an intermittent playability defect or state retention bug across bank loads.

---

## 2. Firmware Reverse Engineering

Disassembly of the ASR-10 V3.50 OS ROM (`roms/asr10booth/asr-648c-lo-1.5b.bin` and `asr-65e0-hi-1.5b.bin`, mapped at `$F80000-$FFFFFF`) revealed the true architectural mechanism.

### A. Voice Banking Initialization (`$F8CD22-$F8CD56`)

During system boot, firmware initializes 32 voice-channel descriptor structures (`a4 = $8000 + voice * $D8`):

```m68k
0xF8CD22:  move.l   #$ff7f00, d0
0xF8CD28:  moveq    #$0, d7
0xF8CD2A:  lea.l    $8000.l, a4
0xF8CD30:  move.w   d7, $14(a4)
0xF8CD34:  clr.b    $14(a4)
0xF8CD38:  clr.w    $b2(a4)
0xF8CD3C:  clr.l    $ca(a4)
0xF8CD40:  move.l   d0, $2a(a4)       ; Voice banking table pointer
0xF8CD44:  lea.l    $d8(a4), a4
0xF8CD48:  cmp.w    #$0, d7
0xF8CD4C:  beq.b    $f8cd50           ; Skip adding 8 for Voice 0
0xF8CD4E:  addq.l   #$8, d0           ; Advance 8 bytes per voice for V >= 1
0xF8CD50:  addq.w   #$1, d7
0xF8CD52:  cmp.w    #$1f, d7
0xF8CD56:  ble.b    $f8cd30
```

- Voice 0 and Voice 1 share entry 0 (`$FF7F00`).
- Voice 2 uses entry 1 (`$FF7F08`).
- Voice $V$ ($V \ge 1$) uses entry $V - 1$ (`$FF7F00 + (V - 1) * 8`).
- Each entry comprises 8 bytes (4 words of 16 bits).

### B. Sample Playback Setup (`$F8E250-$F8E27C` and `$F8CF1A-$F8CF2C`)

When an instrument sample is triggered for voice playback:
1. The 24-bit sample pointer `a3` is evaluated.
2. The lower 20 bits (`a3 & 0x000FFFFF`, the offset within a 1 MB page) are shifted into ES5506 word address format and loaded into the voice register `START` (`a4+0x22`).
3. The upper 4 bits (`(a3 >> 20) & 0x0F`, the physical megabyte index `d0`) are written to the voice banking table pointed to by `$2a(a4)`:

```m68k
0xF8E270:  movea.l  $2a(a4), a0       ; a0 = $FF7F00 + (voice - 1) * 8
0xF8E274:  move.w   d0, (a0)+         ; Word 0: starting megabyte d0
0xF8E276:  addq.w   #$1, d0
0xF8E278:  move.w   d0, (a0)+         ; Word 1: megabyte d0 + 1
0xF8E27A:  addq.w   #$1, d0
0xF8E27C:  move.w   d0, (a0)+         ; Word 2: megabyte d0 + 2
...
0x007C72:  move.w   d0, (a0)+         ; Word 3: megabyte d0 + 3
```

4. The firmware always sets bit 14 of `CR` (`bset.b #$e, d0` at `$F8CF28`) for all voices $V > 0$, configuring `BS1:BS0 = 01` (Bank 1).

---

## 3. Hardware Architecture & Mathematical Derivation

On the Ensoniq ASR-10 mainboard:
- The MC68302 Chip Select 1 (`CS1`, configured for the 8 KiB window `$FF6000-$FF7FFF`, with the per-voice banking table occupying the upper 256 bytes at `$FF7F00-$FF7FFF`) accesses external voice-banking glue logic.
- The ES5506 provides a 21-bit word-addressed wavetable bus (`A20:A0`), representing a 4 MB window (2M words):
  - **Bits 20:19 (`(offset >> 19) & 3`):** Selects which of four 1 MB pages within the 4 MB window is being addressed (Words 0..3 of the voice's table entry).
  - **Bits 18:0 (`offset & 0x7FFFF`):** Offset within that 1 MB page (512K words).
- The external banking hardware combines the 4-bit megabyte index from the voice table with the 1 MB sub-offset to generate the physical DRAM byte address:
  `phys_byte_address = ((megabyte << 20) | (sub_offset << 1)) % SYSTEM_RAM_BYTES`

In the 2 MB stock system:
- Addresses $< 1 \text{ MB}$ map to `m_lowmem_shadow` (DRAM Chunk 0, CPU `$000000-$0FFFFF`).
- Addresses $\ge 1 \text{ MB}$ map to `m_sample_ram` (DRAM Chunk 1, CPU `$100000-$1FFFFF`).

---

## 4. Root Cause of the Playability Defect

Before this fix, `asr10_boot.cpp` had hardcoded static wavetable maps:
- Bank 0: mapped to `m_sample_ram` (Chunk 1).
- Bank 1: mapped to `m_lowmem_shadow` (Chunk 0).
- Bank 2 & 3: unpopulated (`noprw`).

Because firmware always programs playing voices to Bank 1 (`CR = 0x4300`), the ES5506 was **always** reading sample data from Chunk 0 (`m_lowmem_shadow`), completely ignoring the heap allocator's actual physical placement:

1. **`JM DIGI SYN` (single instrument):**
   - Heap address: `a3 = 0x0062C8C0` -> d0 = 6.
   - 6 % 2 = 0 -> DRAM Chunk 0.
   - Because Chunk 0 was mapped to Bank 1, `JM DIGI SYN` coincidentally worked.
2. **`BLUES DRUMS` (inside `ATRK TUT BNK`):**
   - Heap address: `a3 = 0x007E14F0` -> d0 = 7.
   - 7 % 2 = 1 -> DRAM Chunk 1 (`m_sample_ram`).
   - But ES5506 read from Bank 1 (Chunk 0) at `0x0E14F0`!
   - **Path 1 (fresh boot):** Chunk 0 at `0x0E14F0` held uninitialized boot memory -> near-silent click (Peak 486).
   - **Path 2 (after `TUTORIAL BNK`):** `TUTORIAL BNK` had previously loaded `JM DRUMS` into Chunk 0 around `0x0E14F0`. When `ATRK TUT BNK` loaded, Chunk 0 retained leftover data -> ES5506 read from Chunk 0 and played phantom drums from `JM DRUMS` (Peak 5065).

The defect was not memory corruption or state retention: it was an architectural omission in the driver's wavetable address decoding.

---

## 5. Implementation

In `src/mame/ensoniq/asr10_boot.cpp`:
1. Added state-saved `std::array<std::array<u16, 4>, 32> m_voice_bank{}`.
2. Installed `voice_bank_r` and `voice_bank_w` at `$FF7F00-$FF7FFF` in `mem_map`.
3. Implemented dynamic voice-bank translation in `es5506_wavetable_r(offs_t offset)`:
   ```cpp
   u16 asr10_boot_state::es5506_wavetable_r(offs_t offset)
   {
       const u32 voice = m_es5506_host ? m_es5506_host->get_voice_index() : 0;
       const u32 entry = (voice > 0) ? (voice - 1) : 0;
       const u32 bank_idx = (offset >> 19) & 3;
       const u32 sub_offset = offset & 0x7ffff;
       const u32 megabyte = m_voice_bank[entry][bank_idx];
       const u32 phys_byte_address = ((megabyte << 20) | (sub_offset << 1)) % SYSTEM_RAM_BYTES;
       if (phys_byte_address < LOWMEM_WORDS * 2)
           return m_lowmem_shadow[phys_byte_address >> 1];
       return m_sample_ram[(phys_byte_address - LOWMEM_WORDS * 2) >> 1];
   }
   ```
4. Unified all 4 ES5506 bank address spaces (`0..3`) to `es5506_wavetable_map`.
5. Net harness delta: 47 additions, 51 deletions ($\Delta = -4$ lines, harness shrunk).

---

## 6. Empirical Verification

### A. Controlled Audio Output Measurements (`BLUES DRUMS`)

| Metric | Path 1 (Pre-Fix) | Path 1 (Post-Fix) | Path 2 (Post-Fix) |
| :--- | :--- | :--- | :--- |
| **Peak Amplitude** | 486 (1.5%) | **14536 (44.4%)** | **14559 (44.4%)** |
| **RMS Amplitude** | 18.80 | **521.75** | **520.86** |
| **Non-Zero Samples (>100)**| 174 | **2748** | **2743** |

- **Waveform Cross-Correlation (Path 1 vs Path 2):** **0.998892 (99.89% correlation)**.
- History-dependent divergence is completely eliminated. Both paths produce identical, authentic drum audio.

### B. 8-Iteration Repeated Bank-Load Stress Test (`bank11_reproducer.lua`)

Tested across alternating histories:
- Iteration 1 (`BOOT -> ATRK TUT BNK`): PASS (Baseline established)
- Iteration 2 (`TUTORIAL BNK -> ATRK TUT BNK`): PASS (Identical)
- Iteration 3 (`ATRK TUT BNK -> ATRK TUT BNK`): PASS (Identical)
- Iteration 4 (`TUTORIAL BNK -> ATRK TUT BNK`): PASS (Identical)
- Iteration 5 (`ATRK TUT BNK -> ATRK TUT BNK`): PASS (Identical)
- Iteration 6 (`TUTORIAL BNK -> ATRK TUT BNK`): PASS (Identical)
- Iteration 7 (`TUTORIAL BNK -> ATRK TUT BNK`): PASS (Identical)
- Iteration 8 (`ATRK TUT BNK -> ATRK TUT BNK`): PASS (Identical)

Every iteration produced the bit-exact identical PCM memory hash `E1F17C2E` and identical voice parameters.

### C. Full Regression Suite (`docs/asr10/regression-test.sh`)

All 21 regression checks pass cleanly:
- 16 named tests, 21 PASS output lines.
- `note_audio_wav`: `peak=3586 freq=130.8Hz`.
- `audio_rate_mode`: A, B, A2 all passing.

---

## 7. Board-Level Physical Implementation Frontier & MAME Modeling Notes

1. **Physical Attribution Frontier:** While the functional contract of CS1 per-voice banking is verified by ROM firmware and empirical audio output, the physical IC(s) on the 4-layer ASR-10 Digital Board that store the table, track active voice execution, and generate external DTACK remain strictly `[OPEN]`. Digital Board schematics were omitted from service documentation. See `cs1-board-level-implementation-frontier.md` for the dedicated board-level audit.
2. **ES5701 (SuperGLU) Exclusion:** The ES5701 silicon specification (Bob Yannes Rev. 2) confirms zero internal registers/RAM, no CS1 connectivity, and address outputs limited to LA19. ES5701 is definitively `[DISPROVEN]` as the storage receiver for the per-voice banking table.
3. **MAME Write-Only Modeling Note:** MC68302 CS1 hardware is configured write-only (`BR1=$1FEF`, `OR1=$FFFE`, `RW=1, MRW=1`). In `asr10_boot.cpp`, `voice_bank_r` is installed alongside `voice_bank_w`. Firmware never reads from this window, so read availability is runtime-inert, but does not reflect hardware write-only behavior.
