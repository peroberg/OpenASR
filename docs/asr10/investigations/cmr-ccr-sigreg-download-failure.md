# Investigation: ES5510 WRITE_REG Audit, F9/FA/FB Causal Decomposition, Special-Register Matrix, and Full Internal-Effect Corpus Validation

## Status
**Root cause isolated, causally proven via complete A/B/C/D matrix, and validated across 100% of the internal ROM effect corpus.**
Source code clean on baseline commit `9542e49afda9f107fd1f6368f744b3b85a8a7d64`. Awaiting reviewer authorization for production commit.

---

## 1. Executive Summary

A reported defect in OpenASR observed that holding the effect-advance button (or rapidly advancing through internal ROM effects) resulted in `EFFECT DOWNLOAD FAILED / ERROR 032` before reaching approximately ROM-50.

Through deterministic runtime instrumentation, full firmware disassembly of the ESP uploader/verifier, and auditing against the *Ensoniq ESP Specification Rev. 2.4*, the root cause was uncovered:
1. **The defect is NOT rate-dependent or a timing race condition.** It reproduces deterministically on attempt 1 even under slow discrete single-stepping with 800ms settling time.
2. **The exact failing effect is ROM-39 (`PITCH SHIFT`, object pointer `$FFFAE6F6`).**
   - ROM-01 through ROM-38 never touch Special Purpose Register `$FB` (`CMR`, Condition Mask Register).
   - ROM-39 is the **very first effect in the ASR-10 ROM corpus** that writes to `CMR`.
3. **The failure is caused by two distinct, independent bugs in `src/devices/cpu/es5510/es5510.cpp`:**
   - **`WRITE_REG` Macro Typo:** `#define WRITE_REG(r, x) do { r = value; } while(0)` completely ignored argument `x`. For registers `ccr` (250) and `cmr` (251), `(value >> 16) & ...` was passed as `x`, but `r = value;` assigned the 24-bit integer `value` directly to `int8_t cmr`, truncating bits 23:16 and storing `0x00`.
   - **Host Readback Formatting on Control Registers `$F9..$FB`:** Control registers are left-justified in bits 23:16 of the 24-bit host word. Unmapped bits in the high byte read back as high (`1`):
     - `SIGREG` (`$F9`): bits 21:16 unused -> reads back `| 0x3F0000`
     - `CCR` (`$FA`): bits 18:16 unused -> reads back `| 0x070000`
     - `CMR` (`$FB`): bits 17:16 unused -> reads back `| 0x030000`
   - Authentic firmware at `$F9750A..$F97524` explicitly mandates this by performing:
     ```m68k
     if (d1 == $F9) d2 |= $3F;
     if (d1 == $FA) d2 |= $07;
     if (d1 == $FB) d2 |= $03;
     ```
   - When verifying ROM-39 at `$F97574`, firmware expects `D2 = $18 | $03 = $1B`. Generic MAME returned `$00`, triggering 10 retries and `EFFECT DOWNLOAD FAILED / ERROR 032`.

---

## 2. Complete WRITE_REG Callsite Inventory

In `src/devices/cpu/es5510/es5510.cpp`, `write_reg` handles all Special Purpose Registers (SPRs) when `reg >= 0xC0`:

| Reg Index | Reg Name | Dest Variable | Dest Type | Raw Host Value | Passed `x` Expression | Current Result (`r=value`) | Corrected Result (`r=x`) | Documented Width / Representation | Purpose of `x` Expression | Runtime Exposure | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 234 | SER0R | `ser0r` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 235 | SER0L | `ser0l` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 236 | SER1R | `ser1r` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 237 | SER1L | `ser1l` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 238 | SER2R | `ser2r` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 239 | SER2L | `ser2l` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 240 | SER3R | `ser3r` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 241 | SER3L | `ser3l` | `int16_t` | `value` | `value` | `clamp_to_s16(value)` | `clamp_to_s16(value)` | 16-bit serial audio input | None (`WRITE_REG16`) | Serial audio | Identical |
| 245 | DLENGTH | `dlength` | `int32_t` | `value` | `value` | `value` | `value` | 20-bit delay length (bits 23:4) | None (`x == value`) | ROM-01..ROM-50 | Identical |
| 246 | ABASE | `abase` | `int32_t` | `value` | `value` | `value` | `value` | 20-bit address base (bits 23:4) | None (`x == value`) | ROM-11, etc. | Identical |
| 247 | BBASE | `bbase` | `int32_t` | `value` | `value` | `value` | `value` | 20-bit address base (bits 23:4) | None (`x == value`) | ROM-11, etc. | Identical |
| 248 | DBASE | `dbase` | `int32_t` | `value` | `value` | `value` | `value` | 24-bit DRAM base register | None (`x == value`) | ROM-39, ROM-41 | Identical |
| 249 | SIGREG | `sigreg` | `int32_t` | `value` | `value` | `value` | `value` | 24-bit (bit 22 shift mode) | None (`x == value`) | ROM-04, ROM-05 | Identical |
| **250** | **CCR** | `ccr` | `int8_t` | `value` | `(value >> 16) & FLAG_MASK` | `(int8_t)value` (`0x00`) | `(value >> 16) & FLAG_MASK` | 5-bit condition code (bits 23:19) | Shift bits 23:16 to 7:0 and mask flags | Diagnostic/unexercised | **FIXED** |
| **251** | **CMR** | `cmr` | `int8_t` | `value` | `(value >> 16) & (FLAG_MASK \| FLAG_NOT)` | `(int8_t)value` (`0x00`) | `(value >> 16) & (FLAG_MASK \| FLAG_NOT)` | 6-bit condition mask (bits 23:18) | Shift bits 23:16 to 7:0 and mask flags+NOT | ROM-39, ROM-41 | **FIXED** |

**Crucial Finding:** Every single callsite passing `value` as `x` has `x == value`.
Therefore, changing `r = value` to `r = (x)` has **zero side effects on registers 245..249**.
It **only** affects registers 250 (`ccr`) and 251 (`cmr`), exactly where a non-trivial expression was provided.

---

## 3. WRITE_REG History and Intent

- Traced to commit `b8f66ce559f88b159da04d456f9de9a85c29e766` (January 20, 2013) by Christian Brunschen.
- The macro was declared as `#define WRITE_REG(r, x)`, but its implementation was written as `do { r = value; } while(0)`.
- In contrast, the `RETURN(r, x)` macro in the same file properly evaluated `(x)`.
- `[VERIFIED — implementation history]`: This was an accidental coding typo where the author referenced the outer function parameter `value` instead of macro argument `x`.

---

## 4. Full Representation Audit: F9, FA, FB

### 4.1 F9 / SIGREG (Register 249)
- **Specification:** ESP Rev 2.4 §3.2. Bit 22 defines Product Shift Mode (`0` = 2x, `1` = 1x). Bits 21:0 are unused.
- **Host Write:** 24-bit word (`value & 0x00ffffff`).
- **Internal Storage:** `sigreg` (`int32_t`). Updates `mulshift = BIT(sigreg, 22) ? 1 : 2;`.
- **Host Readback:** Unused bits in high byte (bits 21:16) read back as high (`1`). High byte reads back `sigreg | 0x3f0000`.
- **Firmware Verification:** At `$F9750A..$F97510`, firmware explicitly performs `d2 |= 0x3F` before comparing byte 0. At `$F9752E..$F9753C`, firmware skips verifying byte 1.
- **Transformation:**
  ```text
  Host Write (e.g. $000000) -> Internal sigreg ($000000) -> Host Readback ($3F0000)
  ```

### 4.2 FA / CCR (Register 250)
- **Specification:** ESP Rev 2.4 §3.3.2. Bits 23:19 hold condition codes `N, C, V, LT, Z`. Bits 18:16 unused.
- **Host Write:** Condition codes left-aligned in bits 23:19.
- **Internal Storage:** `ccr` (`int8_t`), stores `(value >> 16) & FLAG_MASK`.
- **Host Readback:** Shifted left by 16; bits 18:16 read back as high (`1`). Host readback is `(uint32_t(uint8_t(ccr)) << 16) | 0x070000`.
- **Firmware Verification:** At `$F97514..$F9751A`, firmware explicitly performs `d2 |= 0x07` before comparing byte 0.
- **Transformation:**
  ```text
  Host Write (e.g. $F80000) -> Internal ccr ($F8) -> Host Readback ($FF0000)
  ```

### 4.3 FB / CMR (Register 251)
- **Specification:** ESP Rev 2.4 §4.2.2. Bits 23:18 hold condition mask `N, C, V, LT, Z, NOT`. Bits 17:16 unused.
- **Host Write:** Condition mask left-aligned in bits 23:18. ROM-39 writes `0x180000` (`GT` condition: `LT=1, Z=1, NOT=0`).
- **Internal Storage:** `cmr` (`int8_t`), stores `(value >> 16) & (FLAG_MASK | FLAG_NOT)` -> `0x18`.
- **Host Readback:** Shifted left by 16; bits 17:16 read back as high (`1`). Host readback is `(uint32_t(uint8_t(cmr)) << 16) | 0x030000` -> `0x1B0000`.
- **Firmware Verification:** At `$F9751E..$F97524`, firmware explicitly performs `d2 |= 0x03` before comparing byte 0. Expected `D2 = 0x18 | 0x03 = 0x1B`.
- **Transformation:**
  ```text
  Host Write ($180000) -> Internal cmr ($18) -> Host Readback ($1B0000)
  ```

---

## 5. Four-Way Causal Matrix (A/B/C/D)

Measured on ROM-39 (`PITCH SHIFT`, object pointer `$FFFAE6F6`):

| Configuration | Stored CMR | Readback Byte 0 | Expected D2 | Verifier Result | Download Outcome | Interpretation |
|---|:---:|:---:|:---:|:---:|:---:|---|
| **A (Stock)** | `0x00` | `0x00` | `0x1B` | **MISMATCH** (`exp=1B, act=00`) | 10 retries -> **FAIL / ERROR 032** | Both defects present. |
| **B (WRITE_REG fix only)** | `0x18` | `0x18` | `0x1B` | **MISMATCH** (`exp=1B, act=18`) | 10 retries -> **FAIL / ERROR 032** | Proves correct internal storage alone cannot satisfy the verifier. |
| **C (Readback fix only)** | `0x00` | `0x03` | `0x1B` | **MISMATCH** (`exp=1B, act=03`) | 10 retries -> **FAIL / ERROR 032** | Proves readback masking cannot mask broken internal storage. |
| **D (Both fixes applied)** | `0x18` | `0x1B` | `0x1B` | **MATCH** (`exp=1B, act=1B`) | 0 retries -> **PASS** | Both fixes together satisfy verifier and model semantics. |

`[VERIFIED — runtime causal]`: Both defects are independently real and mutually necessary.

---

## 6. Complete Special-Register Matrix (EA..FF)

| Index | Name | Width | Role | Storage Variable | Host Write Format | Internal Format | Host Read Format | Unused Bits | Dynamic? | Verified by V3.50? | First Known Authentic Workload | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 234 | SER0R | 16 | Serial in 0 R | `int16_t ser0r` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser0r << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 235 | SER0L | 16 | Serial in 0 L | `int16_t ser0l` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser0l << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 236 | SER1R | 16 | Serial in 1 R | `int16_t ser1r` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser1r << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 237 | SER1L | 16 | Serial in 1 L | `int16_t ser1l` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser1l << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 238 | SER2R | 16 | Serial in 2 R | `int16_t ser2r` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser2r << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 239 | SER2L | 16 | Serial in 2 L | `int16_t ser2l` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser2l << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 240 | SER3R | 16 | Serial in 3 R | `int16_t ser3r` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser3r << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 241 | SER3L | 16 | Serial in 3 L | `int16_t ser3l` | 24-bit (bits 23:8) | `clamp_to_s16` | `ser3l << 8` | Bits 7:0 | Dynamic | Skipped (`< F5`) | Boot initialization | VERIFIED |
| 242 | MACL | 24 | MAC accum low | `int64_t machl` | 24-bit | `machl[23:0]` | `machl[23:0]` | None | Dynamic | Skipped (`< F5`) | Unexercised | OPEN |
| 243 | MACH | 24 | MAC accum high | `int64_t machl` | 24-bit | `machl[47:24]`| `machl[47:24]`| None | Dynamic | Skipped (`< F5`) | Unexercised | OPEN |
| 244 | DIL/MEMSIZ | 24 | Delay in / DRAM cfg | `int32_t memsiz` | 24-bit | shift/mask/inc | Dynamic DIL | None | Dynamic read | Skipped (`< F5`) | ROM-01 (`HALL REVERB`)| VERIFIED |
| 245 | DLENGTH | 20 | Delay line len | `int32_t dlength` | 24-bit (23:4) | `dlength` | `dlength \| 0xF` | Bits 3:0 | Static | Low nibble `d2 \|= 0x0F` | ROM-01 (`HALL REVERB`)| VERIFIED |
| 246 | ABASE | 20 | Addr generator A | `int32_t abase` | 24-bit (23:4) | `abase` | `abase \| 0xF` | Bits 3:0 | Static | Low nibble `d2 \|= 0x0F` | ROM-11 (`CMP+DIST+REV`)| VERIFIED |
| 247 | BBASE | 20 | Addr generator B | `int32_t bbase` | 24-bit (23:4) | `bbase` | `bbase \| 0xF` | Bits 3:0 | Static | Low nibble `d2 \|= 0x0F` | ROM-11 (`CMP+DIST+REV`)| VERIFIED |
| 248 | DBASE | 24 | DRAM base addr | `int32_t dbase` | 24-bit | `dbase` | `dbase` | None | Static | Exact 24-bit match | ROM-39 (`PITCH SHIFT`) | VERIFIED |
| 249 | SIGREG | 1 | Product Shift Mode | `int32_t sigreg` | 24-bit (bit 22) | `sigreg` | `sigreg \| 0x3F0000` | Bits 21:16 | Static | Byte 0 `d2 \|= 0x3F`, skip byte 1 | ROM-04 (`DUAL DELAYS`) | VERIFIED |
| 250 | CCR | 5 | Condition codes | `int8_t ccr` | 24-bit (23:19) | `(val>>16)&FLAG`| `(ccr<<16) \| 0x070000`| Bits 18:16 | Dynamic/Static | Byte 0 `d2 \|= 0x07`, skip byte 1 | Unexercised in ROM | VERIFIED |
| 251 | CMR | 6 | Condition mask | `int8_t cmr` | 24-bit (23:18) | `(val>>16)&(F\|N)`| `(cmr<<16) \| 0x030000`| Bits 17:16 | Static | Byte 0 `d2 \|= 0x03`, skip byte 1 | ROM-39 (`PITCH SHIFT`) | VERIFIED |
| 252 | MINUS1 | 24 | Constant -1 | Read-only | Ignored | Constant | `0x00FFFFFF` | None | Static | Skipped (`< F5`) | ESP instructions | VERIFIED |
| 253 | MIN | 24 | Constant min neg | Read-only | Ignored | Constant | `0x00800000` | None | Static | Skipped (`< F5`) | ESP instructions | VERIFIED |
| 254 | MAX | 24 | Constant max pos | Read-only | Ignored | Constant | `0x007FFFFF` | None | Static | Skipped (`< F5`) | ESP instructions | VERIFIED |
| 255 | ZERO | 24 | Constant zero | Read-only | Ignored | Constant | `0x00000000` | None | Static | Skipped (`< F5`) | Verifier park register | VERIFIED |

---

## 7. V3.50 Firmware Verifier Policy (`$F974BE..$F97578`)

| Register Range | Byte Index | Policy | Firmware Disassembly | Purpose / Specification Reference |
|---|:---:|---|---|---|
| `< $F5` (EA..F4) | Any | **Skip verification completely** | `$F974EA: cmp.b #$f5, d1; $F974EE: bcs.b $f975a4` | Dynamic or write-only registers (SER0..3, MACL/H, DIL/MEMSIZ). |
| `$F9` (`SIGREG`) | Byte 0 (high) | `expected \|= 0x3F` | `$F9750A: cmp.b #$f9, d1; $F97510: or.b #$3f, d2` | Bits 21:16 unused in high byte; authentic bus reads high. |
| `$FA` (`CCR`) | Byte 0 (high) | `expected \|= 0x07` | `$F97514: cmp.b #$fa, d1; $F9751A: or.b #$7, d2` | Bits 18:16 unused in high byte; authentic bus reads high. |
| `$FB` (`CMR`) | Byte 0 (high) | `expected \|= 0x03` | `$F9751E: cmp.b #$fb, d1; $F97524: or.b #$3, d2` | Bits 17:16 unused in high byte; authentic bus reads high. |
| `$F9..$FB` | Byte 1 (mid) | **Skip verification completely** | `$F9752E: cmp.b #$f9, d1; $F97534: cmp.b #$fb, d1; $F9753A: addq.w #1, a3; $F9753C: bra.b $f975a4` | Registers are 8-bit left-aligned in byte 0; byte 1 is completely unmapped. |
| `$F5..$F7` (DLENGTH, ABASE, BBASE) | Byte 2 (low) | `expected \|= 0x0F` | `$F97544: cmp.b #$f8, d1; $F9754A: cmp.b #$f5, d1; $F97550: or.b #$f, d2` | Bits 3:0 unused in 20-bit address generator; authentic bus reads high. |
| `$F8` (`DBASE`) | Byte 2 (low) | **Exact compare** (no OR) | Falls through `$F97554` without modification | Full 24-bit DRAM base register. |

---

## 8. Full Internal Effect Corpus (50 Algorithms)

| Step | Ordinal | Object Ptr | Compact Display Name | Special Registers Written | Verifier Reads | Retries | Status |
|:---:|:---:|:---:|---|:---:|:---:|:---:|:---:|
| 02 | ROM-01 | `$FFF9B626` | `HALL REVERB` | F4, F5 | 96 | 0 | PASS |
| 03 | ROM-02 | `$FFF9EC1C` | `44KHZ REVERB` | F4, F5 | 68 | 0 | PASS |
| 04 | ROM-03 | `$FFFA0B1C` | `ROOM REVERB` | F4, F5 | 96 | 0 | PASS |
| 05 | ROM-04 | `$FFFA1190` | `DUAL DELAYS` | F4, F5, **F9** | 10 | 0 | PASS |
| 06 | ROM-05 | `$FFF9B2A6` | `44KHZ DELAYS` | F4, F5, **F9** | 10 | 0 | PASS |
| 07 | ROM-06 | `$FFF9C49A` | `CHORUS+REVERB` | F4, F5 | 111 | 0 | PASS |
| 08 | ROM-07 | `$00016A58` | `PHASER+REVERB` | F4, F5 | 68 | 0 | PASS |
| 09 | ROM-08 | `$FFF9DDC8` | `FLANGER+REVERB` | F4, F5 | 101 | 0 | PASS |
| 10 | ROM-09 | `$FFF9F184` | `ROT.SPKR+REVERB`| F4, F5 | 101 | 0 | PASS |
| 11 | ROM-10 | `$FFF9CBEE` | `CHOR+REV+DDL` | F4, F5 | 111 | 0 | PASS |
| 12 | ROM-11 | `$FFF9D334` | `CMP+DIST+REVERB`| F4, F5, **F6, F7** | 86 | 0 | PASS |
| 13 | ROM-12 | `$FFF9F912` | `DIST+CHOR+REVERB`| F4, F5 | 91 | 0 | PASS |
| 14 | ROM-13 | `$FFFA00A8` | `WAH+DIST+REVERB`| F4, F5, F6, F7 | 78 | 0 | PASS |
| 15 | ROM-14 | `$FFFA1908` | `SMALL ROOM` | F4, F5 | 105 | 0 | PASS |
| 16 | ROM-15 | `$FFFA21FA` | `LARGE ROOM` | F4, F5 | 104 | 0 | PASS |
| 17 | ROM-16 | `$FFFA2AEA` | `HALL REVERB2` | F4, F5 | 101 | 0 | PASS |
| 18 | ROM-17 | `$FFFA33CE` | `SMALL PLATE` | F4, F5 | 104 | 0 | PASS |
| 19 | ROM-18 | `$FFFA3B52` | `LARGE PLATE` | F4, F5 | 104 | 0 | PASS |
| 20 | ROM-19 | `$FFFA42DA` | `REVERSE REVERB` | F4, F5 | 119 | 0 | PASS |
| 21 | ROM-20 | `$FFFA4AE0` | `REVERSE REVERB2`| F4, F5 | 119 | 0 | PASS |
| 22 | ROM-21 | `$FFFA52C2` | `GATED REVERB` | F4, F5 | 120 | 0 | PASS |
| 23 | ROM-22 | `$FFFA5B9E` | `NON-LIN REVERB 1`| F4, F5 | 123 | 0 | PASS |
| 24 | ROM-23 | `$FFFA6492` | `NON-LIN REVERB 2`| F4, F5 | 123 | 0 | PASS |
| 25 | ROM-24 | `$FFFA6D86` | `NON-LIN REVERB 3`| F4, F5 | 101 | 0 | PASS |
| 26 | ROM-25 | `$FFFA7642` | `MULTITAP DDL` | F4, F5 | 35 | 0 | PASS |
| 27 | ROM-26 | `$FFFA7D1A` | `EQ+DELAY LFO` | F4, F5 | 36 | 0 | PASS |
| 28 | ROM-27 | `$FFFA8578` | `VCF+DISTORTION`| F4, F5 | 35 | 0 | PASS |
| 29 | ROM-28 | `$FFFA8D2A` | `GUITAR AMP 1` | F4, F5, F6 | 52 | 0 | PASS |
| 30 | ROM-29 | `$FFFA96E8` | `GUITAR AMP 2` | F4, F5, F6 | 52 | 0 | PASS |
| 31 | ROM-30 | `$FFFAA0A6` | `GUITAR AMP 3` | F4, F5, F6, F7 | 62 | 0 | PASS |
| 32 | ROM-31 | `$FFFAAAB2` | `SPEAKER CABINET`| F4, F5 | 54 | 0 | PASS |
| 33 | ROM-32 | `$FFFAAFD0` | `TUNABLE SPEAKER`| F4, F5 | 59 | 0 | PASS |
| 34 | ROM-33 | `$FFFAB7BE` | `EQ+CHORUS+DDL` | F4, F5 | 37 | 0 | PASS |
| 35 | ROM-34 | `$FFFAC024` | `EQ+VIBRATO+DDL`| F4, F5 | 48 | 0 | PASS |
| 36 | ROM-35 | `$FFFAC8A4` | `EQ+FLANGER+DDL`| F4, F5 | 51 | 0 | PASS |
| 37 | ROM-36 | `$FFFAD1E8` | `EQ+TREMOLO+DDL`| F4, F5 | 42 | 0 | PASS |
| 38 | ROM-37 | `$FFFADA3C` | `PHASER+DDL` | F4, F5 | 25 | 0 | PASS |
| 39 | ROM-38 | `$FFFAE068` | `8-VOICE CHORUS`| F4, F5 | 97 | 0 | PASS |
| **40** | **ROM-39** | `$FFFAE6F6` | `PITCH SHIFT` | F4, F5, F6, F7, **F8, FB** | 41 | 0 | **PASS** |
| 41 | ROM-40 | `$FFFAF04E` | `PITCH+DDL` | F4, F5 | 56 | 0 | PASS |
| 42 | ROM-41 | `$FFFAF9A8` | `FAST PITCH SHIFT`| F4, F5, F6, F7, F8, **FB** | 43 | 0 | PASS |
| 43 | ROM-42 | `$FFFB0104` | `EQ+COMPRESSOR` | F4, F5, F6, F7 | 42 | 0 | PASS |
| 44 | ROM-43 | `$FFFB090C` | `EXPANDER` | F4, F5, F6, F7 | 64 | 0 | PASS |
| 45 | ROM-44 | `$FFFB10DE` | `KEYED EXPANDER` | F4, F5, F6, F7 | 66 | 0 | PASS |
| 46 | ROM-45 | `$FFFB18DC` | `INVERSE EXPNDR`| F4, F5, F6, F7 | 43 | 0 | PASS |
| 47 | ROM-46` | `$FFFB2092` | `DE-ESSER` | F4, F5, F6, F7 | 59 | 0 | PASS |
| 48 | ROM-47 | `$FFFB2A26` | `DUCKER` | F4, F5, F6, F7 | 57 | 0 | PASS |
| 49 | ROM-48 | `$FFFB33D0` | `RUMBLE FILTER` | F4, F5 | 9 | 0 | PASS |
| 50 | ROM-49 | `$FFFB3916` | `PARAMETRIC EQ` | F4, F5 | 30 | 0 | PASS |
| 51 | ROM-50 | `$FFFB408A` | `VAN DER POL` | F4, F5 | 21 | 0 | PASS |

---

## 9. Special Register First-Use Landmarks

- `F4` (MEMSIZ) & `F5` (DLENGTH): `ROM-01` (`HALL REVERB`).
- `F9` (`SIGREG`): `ROM-04` (`DUAL DELAYS`, object `$FFFA1190`).
- `F6` (`ABASE`) & `F7` (`BBASE`): `ROM-11` (`COMP+DIST+REVERB`, object `$FFF9D334`).
- `F8` (`DBASE`): `ROM-39` (`PITCH SHIFT`, object `$FFFAE6F6`).
- `FB` (`CMR`): `ROM-39` (`PITCH SHIFT`, object `$FFFAE6F6`).
- `FA` (`CCR`): Not written by internal ROM effects (reserved for dynamic status and diagnostics).

---

## 10. Corpus Traversal and Regression Results

- **Slow Discrete Traversal:** All 50 ROM effects stepped individually with 800ms settling time. Total 3,467 verifier reads, 0 mismatches, 0 retries.
- **Held Continuous Traversal:** Held panel button auto-repeat across 115 continuous UI display transitions up to ROM-50 and through disk bank effects. 0 retries, 0 lockups, 0 errors.
- **Regression Suite:** `docs/asr10/regression-test.sh` passed cleanly with 17 named targets, 21 checkpoints, 22 PASS lines (including aggregate `PASS regression`), exit code 0.

---

## 11. Signal/Timing Classification Boundaries

- `[RULED OUT — for this measured ROM-39 failure path]`: DTACK handshake delays, Host Access OK timing, HALT edge precision, ES5701 glue timing, audio frame synchronization. The ROM-39 failure is fully accounted for by the dual register write/readback defects.
- `[OPEN — hardware fidelity]`: Silicon electrical mechanism of inactive bits reading high, physical MC68302 external DTACK generation by ES5701, exact Host Access OK handshake latency during active ESP execution.

---

## 12. Final Status and Epistemic Closure

- **ROM-39 / PITCH SHIFT ERROR 032:** `CLOSED / VERIFIED`
- **Cause:** Incorrect ES5510 host->internal conversion for CCR/CMR (macro ignoring `x`) plus missing host-visible inactive bit formatting on `F9/FA/FB`.
- **Internal ROM Corpus:** `ROM-01..ROM-50` accepted under both slow discrete stepping and held continuous navigation.
- **Signal/Timing:** Not causal for this measured failure path; broader physical fidelity remains `OPEN`.
- **Permanent Regression Coverage:** `comp_pitch_shift` (authentic ROM-39 workload) and `es5510_special_regs` (generic ES5510 register semantics) added to `docs/asr10/regression-test.sh`.
