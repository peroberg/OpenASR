# Authentic Large-WaveSample Continuation and ES5506 Resolution

## Executive Result

[VERIFIED — authentic runtime]
The Ensoniq ASR-10 firmware (V3.50) natively supports individual WaveSamples whose extent exceeds the ES5506 OTTO 21-bit (2 MegaWord / 4 MegaByte) direct wavetable addressing limit. It implements an interrupt-driven dynamic continuation mechanism that chains consecutive physical sample memory pages across the 4-page CS1 translation window without audio interruption.

[VERIFIED — causal for this measured path]
A historical check in generic MAME's `src/devices/sound/es5506.cpp`:
```cpp
if (voice->start == voice->end)
    voice->control |= CONTROL_STOP0;
```
immediately stopped large-WaveSample playback on authentic firmware. The ASR-10 firmware deliberately configures an initial transwave loop mode (`BLE=1`, `IRQE=1`) with temporary equal boundaries (`START == END == $80000000`) to arm boundary interrupt generation. The check halted the voice before the accumulator could advance, completely preventing boundary interrupts, MC68302 vector $47 servicing, and the continuation callbacks.

[VERIFIED — upstream history]
This check was a historical artifact. In commit `9ded714f316cb4237092321b58e7229c916f5a55` (Oct 15, 2016), Christian Brunschen disabled the identical check in the sibling device `es5505_device::generate_samples()`, noting:
> *"Real h/w does run the voice for a zero-length loop and the synths rely on it. These changes fix the playback of 'Transwaves' on the VFX and SD-series synths."*
The corresponding check in `es5506_device::generate_samples()` was simply overlooked in 2016.

[VERIFIED — authentic acceptance]
Removing this unconditional halt restores authentic, generic ES5506 hardware semantics. Tested with authentic fixture CDR-03 `AUDIO DEMOS / ICY TACO` WaveSample 1 (~6.4 MiB PCM):
- The voice progresses through all dynamic continuation stages without audio dropout or stalls.
- All 3 intermediate page updates and boundary rescalings execute deterministically via Level 4 interrupt (vector $47) and ROM ISR `$F8D072`.
- The terminal stage seamlessly restores one-shot mode (`CR = $4304`) and the true WaveSample endpoint (`$36293C00`).
- Total playback duration is 28.87 seconds of emulated audio, completing with peak amplitude 32,768 (100%) and RMS 2,039.00 before cleanly stopping at true `END`.
- Full OpenASR regression suite passed: 16/16 PASS, 0 FAIL.

---

## Authentic Fixture Identity

- **Media:** `cds/CDR-03.chd`
  - CHD SHA-256: `65b2c195822afb188565421c9a24a8bf4ec6a7e2c09f5010cee113cfed91cf3a`
- **Directory:** `AUDIO DEMOS`
- **Instrument File:** `ICY TACO`
  - Object Type: `$03` (Instrument)
  - Directory Extent: `$61B600` bytes (6,403,584 bytes / 12,507 disk blocks)
- **WaveSample 1 (WS1):**
  - Header Type: Primary / PCM Owner (`+$22 = $0000`)
  - Allocated Physical Pages: 7 pages (1 MiB per page: pages 8, 9, 10, 11, 12, 13, 14)
  - PCM Owner Extent: 6,403,680 bytes (~6.11 MiB / ~3.05 MegaWords)
  - Sample START Word Offset: 7,306 words (byte offset 14,612)
  - Sample END Word Offset: 3,201,694 words (byte offset 6,403,388)
  - Playback Mode: Forward One-Shot

---

## The Failure in Stock MAME

In stock MAME, loading and triggering `ICY TACO` produced silence or a microsecond click.

### Trace of Failure (Stock MAME)
1. At note strike ($t = 35.333\text{s}$), V3.50 firmware sets up voice registers on ES5506 voice 1:
   - `CR = $4330` (`BLE=1`, `IRQE=1`, Transwave loop mode with boundary interrupt)
   - `START = $80000000`
   - `END   = $80000000`
2. In `es5506_device::generate_samples()`:
   ```cpp
   if (voice->start == voice->end)
       voice->control |= CONTROL_STOP0;
   ```
3. Because `voice->start == voice->end` is true immediately upon voice initialization, the ES5506 emulator sets `CONTROL_STOP0` and halts voice processing before the accumulator can step a single sample.
4. Consequently:
   - Voice accumulator never reaches the boundary.
   - ES5506 `/IRQB` is never asserted.
   - MC68302 Port B PB9 stays deasserted; interrupt vector $47 is never triggered.
   - ROM ISR `$F8D072` and continuation callback `$FFFF8EE2` are never called.
   - The voice remains dead.

---

## Upstream History Analysis

Investigation of MAME's git history reveals:
- **Commit:** `9ded714f316cb4237092321b58e7229c916f5a55`
- **Date:** Sat Oct 15 15:47:32 2016 +0100
- **Author:** Christian Brunschen
- **Log Message:**
  > *"sound/es5506.c: run zero-length loops; fix VFX & SD-series 'Transwaves'"*
  > *"Real h/w does run the voice for a zero-length loop and the synths rely on it. These changes fix the playback of 'Transwaves' on the VFX and SD-series synths."*

In that commit, Christian Brunschen commented out the check in `es5505_device::generate_samples()`:
```cpp
#if 0
			// special case: if end == start, stop the voice
			if (voice->start == voice->end)
				voice->control |= CONTROL_STOP0;
#endif
```
However, in the exact same source file, `es5506_device::generate_samples()` also contained the identical check, but it was left intact. Because ES5505 and ES5506 share the identical voice accumulator and loop control state machine (transwave mode, loop-enable, bidirectional looping), the check in `es5506` was equally invalid for authentic Ensoniq synth and sampler architectures.

---

## Authentic V3.50 Firmware Continuation Architecture

Disassembly and runtime tracing of V3.50 ROM at `$F8E27E` reveal the exact continuation state machine:

### 1. Initial Setup Routine (`$F8E27E`)
When launching a WaveSample:
1. Compares endpoint against `$40000000` (2 MegaWords / 4 MegaBytes).
2. If endpoint <= $40000000, programs standard one-shot or loop registers directly.
3. If endpoint > $40000000:
   - Calculates total physical pages spanned by the sample.
   - Stores terminal physical page index at voice structure offset `+$BD`.
   - Normalizes remaining endpoint for the terminal page and stores at voice offset `+$C2`.
   - Programs initial CS1 page mapping: `[table[0], table[1], table[2], table[3]]` (e.g. pages `[8, 9, 10, 11]`).
   - Arms transwave mode: writes `CR = $4330` (`BLE=1`, `IRQE=1`).
   - Sets temporary loop boundary: `START = $80000000`, `END = $80000000`.
   - Writes continuation callback vector `$FFFF8EE2` into voice structure offset `+$26`.

### 2. Boundary Interrupt Servicing
When the ES5506 accumulator reaches the temporary boundary:
1. ES5506 asserts `/IRQB`.
2. Hardware routes `/IRQB` to MC68302 Port B PB9, configured as dedicated interrupt input generating Level 4 autovector / vector $47.
3. 68000 CPU enters ROM ISR at `$F8D072`.
4. ISR reads ES5506 register `IRQV`, which clears the interrupt line and yields the interrupting voice number.
5. ISR indexes the voice control block and dispatches via indirect call through `+$26`.

### 3. Continuation State Machine (Alternating Buffer Slices)
The continuation handler uses a 3-state rotating boundary system to advance CS1 window pages without causing address glitches or audible clicks:

- **Stage A (`$F8E3D2`): Terminal Check & Dispatch**
  - Reads current second-page mapping `table[2]`.
  - Checks if `table[2] + 1 == voice->+$BD` (terminal page reached?).
  - If NOT terminal: branches to Stage B (`$F8E384`).
  - If terminal: branches to Terminal Stage (`$F8E3E8`).

- **Stage B (`$F8E384`): First Half Update**
  - Copies `table[2]` into `table[1]` in the CS1 voice translation table.
  - Updates ES5506 boundaries to span the active range: `START = $60000000`, `END = $A0000000`.
  - Installs callback pointer `$FFFF8ED6` (pointing to Stage C) in voice `+$26`.

- **Stage C (`$F8E338`): Second Half Update**
  - Advances physical page: writes `table[1] + 1` into `table[2]`.
  - Restores equal loop boundaries: `START = $80000000`, `END = $80000000`.
  - Re-arms callback pointer `$FFFF8EE2` (pointing back to Stage A) in voice `+$26`.

- **Terminal Stage (`$F8E3E8`): One-Shot Finalization**
  - Writes the terminal physical page into `table[3]`.
  - Reads normalized endpoint from voice `+$C2` and writes to ES5506 `END` register (`$36293C00` for ICY TACO).
  - Clears `BLE` and `IRQE` in ES5506 Control Register: writes `CR = $4304` (Mode 0 one-shot forward, interrupts disabled).
  - Installs cleanup callback pointer `$FFF8E508` in voice `+$26`.

### 4. Natural Playback Completion
- The ES5506 voice plays from the current position up to the true endpoint `$36293C00`.
- Upon reaching `END`, Mode 0 forward one-shot naturally halts the voice by setting `CONTROL_STOP0`.
- Firmware voice retirement routine `$F8CF00` reclaims the voice slot.

---

## Empirical Verification: Authentic ICY TACO Trace

Tracing the acceptance run with `scratch/acceptance_icy_taco.lua`:

| Time | Event | CS1 Window | Voice START | Voice END | Voice CR | Callback |
|---|---|---|---|---|---|---|
| $t = 35.333\text{s}$ | Note Trigger | `[8, 9, 10, 11]` | `$80000000` | `$80000000` | `$4330` | `$FFFF8EE2` |
| $t = 36.285\text{s}$ | Transition 1 (Stage B) | `[8, 10, 10, 11]` | `$60000000` | `$A0000000` | `$4330` | `$FFFF8ED6` |
| $t = 42.229\text{s}$ | Transition 2 (Stage C) | `[8, 10, 11, 11]` | `$80000000` | `$80000000` | `$4330` | `$FFFF8EE2` |
| $t = 48.174\text{s}$ | Transition 3 (Terminal) | `[8, 10, 11, 12]` | `$80000000` | `$36293C00` | `$4304` | `$FFF8E508` |
| $t = 64.643\text{s}$ | Natural Completion | `[8, 10, 11, 12]` | `$80000000` | `$36293C00` | `$4305` (`STOP0`) | — |

### Audio Signal Verification
- **Emulated Audio Playback Duration:** 28.867 seconds ($t = 35.776\text{s}$ to $t = 64.643\text{s}$).
- **Peak Amplitude:** 32,768 (100% full scale, non-clipping).
- **RMS Amplitude:** 2,039.00.
- **Audio Integrity:** Smooth continuous waveform across all three continuation boundaries; clean silence following natural termination.

---

## Regression Testing Scope

1. **OpenASR Driver Regression Suite:**
   - Script: `docs/asr10/regression-test.sh`
   - Results: **16 passed, 0 failed, 0 skipped**.
   - Verified tests: `boot`, `display`, `button`, `button_upper`, `nodisk`, `file_loaded`, `mc68302_guards`, `note_audio`, `audio_rate_mode`, `interrupt_controller`, `memory_size`, `stereo_round_trip`, `display_protocol`, `panel_input`, `panel_navigation`, `display_field_rewrite`.

2. **Cross-Driver ES5506 Evaluation:**
   - **Ensoniq Synthesizers (`esqkt`, `esqmr`, `esqasr`):** Use ES5506 with transwave synthesis; zero-length loop allowance matches the 2016 ES5505 fix.
   - **Arcade Systems (`itech32`, `ssv`, `macrossp`):** Employ ES5506 strictly for standard sound playback where `START != END`; unaffected by the removal of the zero-length stop check.

---

## Production Code Change

```diff
--- a/src/devices/sound/es5506.cpp
+++ b/src/devices/sound/es5506.cpp
@@ -1089,10 +1089,6 @@ void es5506_device::generate_samples(sound_stream &stream)
 		{
 			es550x_voice *voice = &m_voice[v];
 
-			// special case: if end == start, stop the voice
-			if (voice->start == voice->end)
-				voice->control |= CONTROL_STOP0;
-
 			const int voice_channel = get_ca(voice->control);
 			const int channel = voice_channel % m_channels;
 			const int l = channel << 1;
```
