# V3.50 ROM-11 COMP+DIST+REVERB Download Failure and ES5510 Address-Register Readback Resolution

## Status

**Resolved.** Completed investigation and verified production correction in `src/devices/cpu/es5510/es5510.cpp`.

---

## 1. Workload Identity and Symptom

* **[VERIFIED — runtime/static]** The reported internal algorithm is ROM-11. Its ROM effect object is `$FFF9D334`; its compact embedded display label is `CMP+DIST+REV`, the firmware's abbreviated name for **COMP+DIST+REVERB**.
* **Neighboring algorithms:** Preceding algorithm is ROM-10, `CHOR+REV+DDL`, at `$FFF9CBEE`. Succeeding algorithm is ROM-12, `DIST+CHO+REV`, at `$FFF9F912`.
* **Original symptom:** Under stock MAME, selecting ROM-11 exhausted 10 host upload retries and halted with:
  ```text
  EFFECT DOWNLOAD FAILED
  ```
  triggering the firmware's `ERROR 032` / `$FFC88E` error path.

---

## 2. Canonical Reproducer and Control

* **[VERIFIED — runtime]** Boot ASR-10 V3.50 with `floppies/asr10booth/V350.img` to `FILE 1  TUTORIAL BNK`.
* **Selection path:** Press `FX Select` (`$07`), then issue discrete advance edges (key `$0A`, 1.2s intervals) to cycle through `INST`, `BANK`, `ROM-01` through `ROM-10`.
* **GOOD Control:** ROM-10 `CHOR+REV+DDL`. Completes 962 verifier reads with 0 mismatches, 0 retries, and cleanly commits to `$0E92 = $FFF9CBEE`.
* **BAD Target (Stock):** Issue one further advance edge to ROM-11 `CMP+DIST+REV`. ROM-11 deterministically failed at verifier PC `$F97576` with 10 identical mismatches, exhausted retries (`$0E8C = $0A`), and displayed `EFFECT DOWNLOAD FAILED`.

---

## 3. Firmware Uploader and Verifier Path

* **[VERIFIED — firmware]** Selected effect objects enter the firmware host uploader at `$F973F0`:
  * Upload cursor is stored at `$0E7E`.
  * Current transfer object base is `$0E8E`.
  * Current effect pointer is `$0E92`.
  * Retry counter is `$0E8C`.
* **Verification loop:** The uploader writes record streams to the ES5510 host interface at `$FC3000-$FC31FF`, then executes a verification pass (`$F97496..$F975BC`).
* **Comparison:** At `$F97574..$F9757A`, the firmware reads host register `$FC3004` (host latch offset `$02`, GPR low byte) and compares it with expected byte `D2`. Mismatch calls retry path at `$F97580`; exhausting 10 retries sets Carry at `$F97596` and exits to error handling at `$FFC88E/$FFC896`.

---

## 4. First Semantic Divergence: F6 / ABASE

For ROM-11, the first failing verifier read under stock MAME was stable across all 10 attempts:

| Property | Value |
|---|---|
| Verifier PC | `$F97576` |
| Host read address | `$FC3004` (host latch offset `$02`, GPR low byte) |
| Read select index | `$80` select index `$F6` (`ABASE`) |
| Expected `D2` | `$0F` |
| Actual readback | `$00` |
| Written source value | `$3EFF00` |
| Cursor at mismatch | `$0E7E = $FFF9DA78`, `A3 = $F9DDC3` |
| Retries | `$00` through `$09`, then `$0E8C = $0A` |

Under stock MAME, `read_reg(0xF6)` returned `abase` unchanged (`$3EFF00`), producing `$00` at host latch offset `$02`, whereas firmware expected `$0F`.

---

## 5. F6-Only Discriminator and Next Divergence (F7 / BBASE)

* **[VERIFIED — runtime causal]** Modifying only `read_reg(0xF6)` to return `abase | 0x00000F` completely eliminated the mismatch on register `F6`. The compare at `$F97576` evaluated `$0F == $0F` and passed.
* **Next divergence:** Execution advanced immediately to the next Address Generator register in the ROM-11 stream:
  * **Register:** `$F7` (`BBASE`)
  * **Written source value:** `$3DFE00`
  * **Expected `D2`:** `$0F`
  * **Actual readback:** `$00`
  * **Verifier PC:** `$F97576`
  * Total verifier reads increased from 854 to 864 (exactly 1 additional read per retry attempt).
* This confirmed that `F6` was not an isolated scalar anomaly, but part of a register-family semantic.

---

## 6. Firmware Rule Encoding: F5 <= reg < F8

Disassembly of the V3.50 OS ROM uploader revealed the exact architectural rule:

### A. Verification Skips (`$F974BE..$F974E6`)
```m68k
$F974C0:  cmp.b    #$f4, d1       ; MEMSIZ
$F974C4:  beq.b    $f974e4        ; SKIP verification
$F974C6:  cmp.b    #$f8, d1       ; DBASE
$F974CA:  beq.b    $f974e4        ; SKIP verification
$F974C8:  cmp.b    #$e8, d1
$F974D0:  beq.b    $f974e4        ; SKIP verification
$F974D2:  cmp.b    #$e9, d1
$F974D6:  beq.b    $f974e4        ; SKIP verification
$F974D8:  cmp.b    #$ea, d1       ; SER0R..SER3L, MACL, MACH ($EA..$F3)
...
$F974E4:  addq.l   #$3, a3
$F974E6:  bra.w    $f975a4        ; Skip compare; advance to next record
```

### B. Expected Low-Nibble Masking (`$F9753E..$F97554`)
```m68k
$F9753E:  cmpa.w   #$2, a0        ; Verifying host latch offset $02 (low byte)?
$F97542:  bne.b    $f97572
$F97544:  cmp.b    #$f8, d1       ; If d1 >= $F8, skip OR mask
$F97548:  bcc.b    $f97554
$F9754A:  cmp.b    #$f5, d1       ; If d1 < $F5, skip OR mask
$F9754E:  bcs.b    $f97554
$F97550:  or.b     #$f, d2        ; For F5, F6, F7: EXPECT LOW NIBBLE = $F
$F97554:  ...
$F97574:  cmp.b    (a6), d2       ; Compare host readback against D2
$F97576:  beq.b    $f9759c
```

* **[VERIFIED — firmware]** Firmware explicitly expects bits 3:0 of the readback low byte to be high (`$F`) for registers `$F5 \le \text{reg} < $F8`:
  * `F5`: `DLENGTH` (Length of Delay Memory)
  * `F6`: `ABASE` (Table "A" Base Address Register)
  * `F7`: `BBASE` (Table "B" Base Address Register)
* Register `F8` (`DBASE`) is explicitly excluded from the mask (`bcc.b $f97554`) and is separately skipped from verification entirely (`$F974C6`).

---

## 7. F5–F7 Family Discriminator and A/B/A Causality

Applying the discriminator across the full verified range:
* `F5` (`DLENGTH`): `dlength | 0x00000F`
* `F6` (`ABASE`): `abase | 0x00000F`
* `F7` (`BBASE`): `bbase | 0x00000F`

### Empirical A/B/A Result Matrix

| Stage | Model Configuration | ROM-10 Result | ROM-11 Result | First Mismatch | Total Verifier Reads | Final State |
|---|---|---|---|---|---:|---|
| **A1 (Stock)** | Stock `read_reg` | **PASS** (962 reads, 0 mismatches) | **FAIL** | PC `$F97576`, reg `F6`, exp `$0F`, act `$00` | 854 | `EFFECT DOWNLOAD FAILED` |
| **B (Fix)** | `F5`--`F7` readback `\| 0x0F` | **PASS** (962 reads, 0 mismatches) | **PASS** | **None** (0 mismatches, 0 retries) | 86 | `FX?R0M-11 CMP?DI5T?RE?` |
| **A2 (Revert)**| Stock `read_reg` | **PASS** (962 reads, 0 mismatches) | **FAIL** | PC `$F97576`, reg `F6`, exp `$0F`, act `$00` | 854 | `EFFECT DOWNLOAD FAILED` |

* **[VERIFIED — runtime causal]** The missing inactive-low-nibble readback behavior on `F5`--`F7` was the sole causal blocker preventing ROM-11 upload completion.

---

## 8. Specification Reconciliation

* **[VERIFIED — specification]** *ENSONIQ ESP Specification Rev. 2.4* §3.2 (p. 5) and §3.5.2 (p. 13) confirms:
  * Registers `F5` (`DLENGTH`), `F6` (`ABASE`), `F7` (`BBASE`), and `F8` (`DBASE`) are 20-bit wide.
  * All Address Generator registers and internal data paths are 20-bit wide and **left-justified** within the standard 24-bit host word (bits 23:4).
  * Bits 3:0 are unused by the Address Generator.
* **[OPEN — physical precision]** The specification does not explicitly document the electrical pull-up or drive implementation of unmapped bus lines during host readback. Firmware demonstrably expects ones; the physical silicon mechanism is preserved as open.

---

## 9. Production Implementation

In generic MAME source `src/devices/cpu/es5510/es5510.cpp`:

```cpp
		case 244: RETURN(dil, dil); // DIL when reading
		// Address-generator registers are 20-bit values left-justified in the 24-bit host word; unused low bits read back high.
		case 245: RETURN(dlength, dlength | 0x00000f);
		case 246: RETURN(abase, abase | 0x00000f);
		case 247: RETURN(bbase, bbase | 0x00000f);
		case 248: RETURN(dbase, dbase);
```

* **Scope:** Confined entirely to generic `es5510.cpp`.
* **Preservation:** Register writes, internal 20-bit address calculations, MEMSIZ masking, DRAM addressing, and `F8` (`DBASE`) remain untouched. No ASR-10 driver workarounds.

---

## 10. Acceptance and Regression Verification

1. **Acceptance Cycle:**
   $$\text{ROM-10 } (\$FFF9CBEE) \to \text{ROM-11 } (\$FFF9D334) \to \text{ROM-12 } (\$FFF9F912) \to \text{ROM-11} \to \text{ROM-10} \to \text{ROM-11}$$
   * Total verifier reads across sequence: 1,422.
   * Total mismatches: 0. Retries: 0.
2. **Rapid / Held Scrolling Stress:**
   * 15 consecutive fast transitions (150 ms intervals) traversing ROM-11 through ROM-26 (`EQ?DELAY LF0`, `$FFFA7D1A`).
   * Total mismatches: 0. Retries: 0. No UI lockup or error dialogs.
3. **Full Regression Suite (`docs/asr10/regression-test.sh`):**
   * Extended with permanent focused target `comp_dist_reverb`.
   * **17 named targets** (17/17 passed).
   * **21 checkpoints** (21/21 passed).
   * **22 `^PASS ` lines emitted** (aggregate script verdict `PASS regression`, exit 0).

---

## 11. Final Epistemic Classification

* **`[VERIFIED — firmware]`** Authentic V3.50 firmware explicitly verifies `d2 |= $0F` for registers `F5`, `F6`, and `F7` at `$F97544..$F97550`, and explicitly skips verification for `F8`.
* **`[VERIFIED — runtime causal]`** Missing `F5`--`F7` readback low-nibble semantics caused the `COMP+DIST+REVERB` `ERROR 032` failure in the emulated model.
* **`[VERIFIED — functional model]`** Corrected generic ES5510 readback semantics allow ROM-11, neighboring ROM effects, and rapid UI navigation to succeed deterministically.
* **`[OPEN — physical precision]`** The exact physical silicon/electrical mechanism causing unused low bits to read high remains unmeasured.
