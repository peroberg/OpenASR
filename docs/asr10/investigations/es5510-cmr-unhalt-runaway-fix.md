# Investigation: ES5510 CMR Unhalt State, ROM 36 -> 37 Divergence, and Pipeline Sanitization Resolution

## Status

**Resolved.** Root cause isolated, causally proven, and verified with 100% clean regression suite pass (25/25 tests).

---

## 1. Symptom and Problem Statement

When navigating the internal ROM effects in ASR-10 V3.50, transitioning from **ROM-36 (`EQ+TREMOLO+DDL`)** to **ROM-37 (`PHASER+DDL`)** resulted in explosive self-oscillation and total audio breakdown:
* The allpass filter feedback pole register `GPR_92` collapsed from its expected stable value of $\approx +0.90$ (`$E50000` / `$733333`) to $-1.0$ (`$800001`).
* Filter state registers `GPR_27` and `GPR_28` saturated to full scale (`$800000`).
* Once collapsed, the Phaser effect remained trapped in runaway self-oscillation even with no audio input.
* In contrast, booting directly into ROM-37 or transitioning from **ROM-38 (`CHORUS+REVERB`)** to ROM-37 was completely stable.

---

## 2. Disproven Hypotheses

During the initial investigation, several intuitive explanations were tested and definitively falsified:

1. **Audio Scaling / Excessive Loop Gain Hypothesis — [DISPROVEN]:**
   - *Hypothesis:* ROM-37 (Phaser) feedback math is fundamentally unstable or the ES5510 multiplier accumulator scaling injects too much gain under active audio input.
   - *Test:* Boot directly into ROM-37 (`$FFFADA3C`) and play Middle C via MIDI note trigger.
   - *Measurement:* Peak output remained within normal dynamic range; `GPR_92` remained stable at `$E50000` throughout note onset, sustain, and decay. The algorithm is mathematically stable under audio.

2. **Dirty High GPR ($0x90..0xBF$) Hypothesis — [DISPROVEN]:**
   - *Hypothesis:* ROM-36 leaves uninitialized state in registers $0x90..0xBF$ (since firmware Tag 04 only clears $0x00..0x8F$), which corrupts ROM-37 upon unhalt.
   - *Test:* In a custom harness at unhalt PA4, forcefully zeroed `gpr[0x90]` through `gpr[0xBF]`.
   - *Measurement:* `GPR_92` still collapsed to `$800001` within 50 ms. Zeroing high GPRs had zero effect on the runaway.

---

## 3. Causal Decomposition: Microcode Execution of ROM-37

Disassembly and cycle-by-cycle instruction tracing of ROM-37 (`$FFFADA3C`) revealed a two-phase loop structure:

### Phase A and Phase B Structure
* **Phase A (Instructions 0..12 and 78..88):**
  - Instruction 78: Writes `GPR_09` (`$240000`) into register `$FB` (`CMR`).
    - Bitfield decode: `FLAG_V` ($0x20$) with `FLAG_NOT = 1` ($0x04$) $\rightarrow$ Condition: `NOT Overflow`.
* **Phase B (Instructions 13..77):**
  - Instruction 13: Writes `GPR_06` (`$0C0000`) into register `$FB` (`CMR`).
    - Bitfield decode: `FLAG_Z` ($0x08$) with `FLAG_NOT = 1` ($0x04$) $\rightarrow$ Condition: `NOT Zero` / `NEQ`.

### The LFO Phase Accumulator and Overflow Handler
Around PC 10–11:
* **Instruction 10:** `ADDU GPR_26, GPR_99 > GPR_26`
  - Increments the LFO phase accumulator.
  - Updates CCR (`alu.flag_v` is set only on true arithmetic overflow).
* **Instruction 11 (`0xFFFF002790C0`):** `MOV GPR_00 > GPR_27`, skippable (`skip = 1`).
  - `GPR_00` is initialized by the firmware Tag 04 table to `$7FFFFE` (+1.0 maximum amplitude).
  - This instruction is the overflow handler: when the phase accumulator overflows, it resets `GPR_27`.
* **Normal Steady-State Operation (Frame 2+):**
  - Prior to PC 10, Instruction 78 executed at the end of the previous frame, setting `CMR = 0x24` (`NOT Overflow`).
  - On ordinary frames where no overflow occurs, `CCR & FLAG_V == 0`.
  - Because `FLAG_NOT = 1`, the skip condition is satisfied:
    $$\text{skipConditionSatisfied} = (0 == 0) \oplus 1 = \text{true}$$
  - Instruction 11 is **skipped**, leaving `GPR_27` undisturbed at `$000000`.

---

## 4. The Defect: Stale Condition Mask Across HALT

In MAME's `es5510_device::set_HALT(true)`, halting the DSP for an effect download halted instruction execution but **did not sanitize `cmr` or `ccr`**.

1. **ROM-36 State at Exit:**
   - ROM-36 (`EQ+TREMOLO+DDL`) terminates its loop with `CMR = 0x08` (`FLAG_Z` set, `FLAG_NOT = 0`).
2. **Transition to ROM-37:**
   - Host CPU asserts HALT, uploads ROM-37 microcode and coefficients, and deasserts HALT.
   - On **Frame 1** of ROM-37, execution begins at PC 0. Instruction 78 has not yet run.
   - Therefore, `cmr` on Frame 1 retained the value `$08` left behind by ROM-36.
3. **The Fatal Frame 1 Execution:**
   - At Instruction 10: `ADDU` executes. `CCR` has `FLAG_Z = 0`.
   - At Instruction 11: MAME evaluates `skip` against the stale `CMR = 0x08`:
     $$\text{mask} = 0x08, \quad \text{ccr} \& \text{mask} = 0, \quad \text{FLAG\_NOT} = 0$$
     $$\text{skipConditionSatisfied} = \text{false}$$
   - Instruction 11 was **NOT skipped**!
   - `GPR_00` (`$7FFFFE` / +1.0) was copied directly into `GPR_27`.
4. **Runaway Cascade:**
   - `GPR_27` injected maximum amplitude into `GPR_28`.
   - Instructions 17–18 executed `SUB MACH` from `GPR_92` (`MACH = GPR_28 * GPR_8D`).
   - Because `GPR_91` was $0$, instructions 17–20 were not skipped by the coefficient ramp.
   - `GPR_92` collapsed from $+0.90$ to $-1.0$ (`$800001`), locking the allpass filter into permanent oscillation.

---

## 5. Specification Analysis & The Fix

### Ensoniq ESP Specification Rev 2.4 (§4.2.2)
The condition mask register bits are defined as:
* Bits 23:19: Condition select masks (`N, C, V, LT, Z`)
* Bit 18: `NOT` invert bit
* Bit 17:16: Unused

When condition mask bits are 0 and `NOT = 1` ($0x04$), the condition evaluates to:
$$\text{Condition} = (\text{CCR} \& 0) \ne 0 \iff \text{false}$$
$$\text{With NOT} = 1 \implies \text{true (Always Skip)}$$

Conversely, $0x00$ (`NOT = 0`) means `Never Skip`.

### Implementation
1. **HALT Sanitization (`src/devices/cpu/es5510/es5510.cpp`):**
   When `set_HALT(true)` is called or `device_reset()` executes:
   ```cpp
   ccr = 0;
   cmr = 0x04; // Always TRUE / Always Skip uninitialized conditions
   ```
   If an effect explicitly configures CMR during HALT via host writes (such as ROM-39 `PITCH SHIFT` writing `$180000`), the host write updates `cmr` normally.
   If an effect does not configure CMR via host register writes (such as ROM-37), `cmr` defaults to safe $0x04$, preventing conditional initialization instructions from firing errantly on Frame 1.

2. **Pipeline and Latch Flush on Unhalt:**
   When transitioning from HALT to RUN (`unhalting`):
   ```cpp
   void es5510_device::reset_pipeline()
   {
       alu.write_result = false;
       mulacc.write_result = false;
       machl = 0;
       mac_overflow = false;
       memset(&ram, 0, sizeof(ram_t));
       memset(&ram_p, 0, sizeof(ram_t));
       memset(&ram_pp, 0, sizeof(ram_t));
       dol_count = 0;
       dol[0] = dol[1] = 0;
   }
   ```

3. **Serial Input Inhibit during HALT (`src/devices/sound/esqpump.cpp`):**
   In `sound_stream_update`, check if ESP is halted before pushing serial samples to prevent pre-charging DSP inputs while halted:
   ```cpp
   const bool esp_halted = m_esp_halted || m_esp->get_HALT();
   if (!esp_halted) {
       // push serial input samples
   }
   ```

---

## 6. Verification and Regression Results

1. **Isolated Transition Test (ROM-36 -> ROM-37):**
   - Result: `GPR_27 = 000000`, `GPR_28 = 000000`, `GPR_92 = E50000`.
   - Outcome: **CLEAN / STABLE**.

2. **Active Audio Note Test in Phaser:**
   - Result: `GPR_92` remained stable at `E50000` at +100ms, +300ms, and under active MIDI note playback.
   - Outcome: **CLEAN / STABLE**.

3. **Full Regression Suite (`docs/asr10/regression-test.sh`):**
   All 25 tests passed:
   - `PASS boot`
   - `PASS display`
   - `PASS button`
   - `PASS button_upper`
   - `PASS nodisk`
   - `PASS file_loaded`
   - `PASS mc68302_guards`
   - `PASS note_audio`
   - `PASS audio_rate_mode` (A, B, A2)
   - `PASS interrupt_controller`
   - `PASS memory_size`
   - `PASS stereo_round_trip`
   - `PASS display_protocol`
   - `PASS panel_input`
   - `PASS panel_navigation`
   - `PASS display_field_rewrite`
   - `PASS comp_dist_reverb`
   - `PASS comp_pitch_shift`
   - `PASS es5510_special_regs`
   - `PASS disk_label_text_edit`
   - `PASS vfd_dp_and_inst_lamps`
   - `PASS regression`
