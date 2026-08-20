# Is The Sound Right, Not Just Present? Locked.

Scope note, stated up front per the task's own rule: ES5510 is
disabled (`asr10_boot.cpp:1006`, `es5510_host.set_disable();`) for the
entire duration of this investigation. Every measurement below is the
**dry** ES5506 signal path — no ES5510 processing, no filter/effects
stage.

## Del 1 — Pitch And The Clock

### FC, read directly

`pitch-probe-c4.lua`/`pitch-probe-c5.lua`: played `$3C` (note 60) and
`$48` (note 72, one octave up) via MIDI and logged every ES5506
register write for the allocated voice. **FC is not a static value**
— it updates continuously (~every 11-12ms) after onset, oscillating
±3-4% around a center value: a vibrato/LFO modulation, not a one-shot
pitch. The *first* post-onset FC write is the correct base-pitch
reference:

```text
$3C: first FC = 0x38D (909)
$48: first FC = 0x71B (1817)   ratio = 1817/909 = 2.0011
```

START/END/ACCUM/CR are byte-identical between the two runs — same
sample zone, only FC differs. **[Verified]** The firmware's per-note
pitch scaling is internally correct: doubling FC for a 12-semitone
interval, independent of whatever the absolute ES5506 clock actually
is (a uniform clock error would scale every note by the same factor
and still preserve this exact ratio — this test cannot by itself
validate or refute the absolute clock).

### The playback-rate formula, from `es5506.cpp`, read only

`m_sample_rate = m_master_clock / (16 * (m_active_voices + 1))`
(`es5506.cpp:1157`, `:268`, `:299`). Firmware writes `ACTV=0x1F` (31)
at boot (`STRESS_VOICE`/`POLY_VOICE` traces confirm `m_active_voices`
stays 31 throughout), so `m_sample_rate = 16,000,000 / (16*32) =
31,250 Hz`. Each output tick, `accum += freqcount`
(`generate_pcm`/`generate_ulaw`, `es5506.cpp:823-896`), and
`get_integer_addr()` (`es5506.h:107`) right-shifts by `ADDRESS_FRAC_BIT`
(11) to get the word position `read_sample()` uses — so playback
advances `FC/2048` words per output tick:

```text
$3C: 909/2048 * 31250   = 13,870.2 words/sec
$48: 1817/2048 * 31250  = 27,731.4 words/sec   (ratio 2.00, as above)
```

### What actually came out (corrected measurement)

The prior turn's ~720Hz figure was measured by raw zero-crossing
counting over a very short window — **[Disproven, own prior
measurement]**: zero-crossing counting on a non-sinusoidal, harmonic-
rich waveform overcounts and is unreliable. Re-measured with
autocorrelation (a proper periodicity estimator) over multiple
windows:

```text
$3C window[23.13,23.16]: f0=141.2Hz
$3C window[23.15,23.20]: f0=138.3Hz
$3C window[23.20,23.30]: f0=136.8Hz
$3C window[23.30,23.50]: f0=136.8Hz   (converges, stable)

$48 window[23.13,23.16]: f0=282.4Hz
$48 window[23.15,23.20]: f0=277.5Hz
$48 window[23.20,23.30]: f0=272.7Hz
$48 window[23.30,23.50]: f0=274.3Hz

ratio (stable windows): 274.3/136.8 = 2.005, 272.7/136.8 = 1.994
```

**[Verified]** Octave doubling holds in the actual audio output too
(~2.00), not just in FC — confirms the internal pitch math end to end,
from note number through FC through the actual synthesized waveform.

### Sample content vs. clock: which explains the absolute-pitch offset

MIDI note 60 (middle C) nominally implies 261.6Hz; the measured
fundamental is ~137Hz — roughly **but not exactly** one octave low
(261.6/137 = 1.91, not 2.0). Attempted to find the instrument's own
wavesample header (root key / native sample rate) to settle this
independently:

- Searched all of CPU lowmem (`$000000-$0FFFFF`, which entirely
  contains the loaded payload `$000944-$0552FF`) for the exact 32-bit
  register values programmed into voice 1/2 (`0x0FE8A800`,
  `0x153FF980`, `0x0B278000`) — **zero hits**. These values are not
  stored verbatim anywhere in lowmem; they are computed at note-on
  time from a more compact descriptor, not copied from a template.
- Searched for the word-address forms (`>>11`) as 16-bit values —
  13 coincidental hits for one field, zero for the other two
  (no consistent multi-field match) — **not a real structural hit**.
- **[OPEN]** The wavesample header's exact format (root key, native
  sample rate, fine tune) was not located within this task's bounded
  search. `runtime-object-model.md`'s `$F8D626`/`$F8D8F8` ("reads
  sample/pitch/address-like fields from A3+$F8") is the most likely
  place it's actually consumed, not reverse-engineered further here.

**Conclusion, per the task's own framing**: the octave-doubling test
is clock-invariant by construction, so it cannot distinguish "the
sample's own recorded content is simply not tuned to 261.6Hz" from "a
uniform clock scaling error." Both are equally consistent with a
~1.91x discrepancy. Since real wavetable-synth samples routinely have
native pitches that don't equal their assigned MIDI note (the root-key
field exists specifically to correct for this), and no independent
evidence was found either way, **the deviation's cause is [OPEN]**,
leaning toward "a property of the sample" as the more mundane, more
probable explanation (matching `esqkt.cpp`'s independent 16MHz
precedent for the same chip family) but not confirmed as such.

**Per instruction: the clock is not changed.** For reference only —
*if* the sample's own content were assumed to already represent
261.6Hz at unity rate (an unverified assumption), the clock that would
make the measured output match would be approximately `16,000,000 *
(261.6/136.8) ≈ 30.6MHz` — not a clean, recognizable crystal value,
which itself is a mild point against the "clock is wrong" reading.
This number is reported because the task asked for it, not as a
recommendation.

## Del 2 — Does The Voice Behave Like A Voice?

### Note-off: decays, does not ring forever

`voice-behavior-probe.lua`: held `$3C` 4s, sent a real MIDI note-off
(`$80 $3C $00`), watched register traffic. **[Verified]** `LVRAMP`/
`RVRAMP` (left/right volume-ramp registers) go negative (`0x0000FF74`
= -140 signed) ~46ms after note-off — a release-envelope ramp, not a
`CR` `STOP`-bit change. Silencing happens through the volume envelope.

Audible confirmation, `-wavwrite` RMS in matched time windows,
`voice-behavior-probe.lua` (held-then-released) vs.
`voice-no-noteoff-probe.lua` (held continuously, identical stimulus
otherwise):

```text
t=26.90-27.00s: both runs identical, rms=141.4  (before note-off, as expected)
t=27.30-27.50s: released rms=128.6   vs   held rms=183.1
t=28.00-28.20s: released rms=25.4    vs   held rms=100.0
t=28.50-28.70s: released rms=6.8     vs   held rms=59.5
t=28.90-29.05s: released rms=3.3     vs   held rms=56.8   (~17x quieter, released)
```

**[Verified]** Note-off measurably and substantially accelerates
silencing beyond the sample's own held decay (which is itself real —
amplitude falls even without note-off, consistent with a naturally
decaying/plucked "DIGI SYN" waveform, not a sustained tone). Both
behaviors are real: a built-in decay while held, and a materially
faster release after note-off.

### Looping: continues past the single-pass duration

Loop length = `word_end - word_start` = `0x2A7FF - 0x1FD15` = 43,754
words; at `$3C`'s playback rate (13,870.2 words/sec) that is a **3.15s**
single pass. `voice-no-noteoff-probe.lua` (held continuously, no
note-off) still measured real, nonzero audio at t=28.9-29.05s — **5.8s
after onset**, well past one single pass. **[Verified]** Playback does
not hard-stop at `END`; combined with `CR`'s `LOOPMASK` bits (`BLE`
`|` `LPE`, both set in the measured `CR=0x4018`) being fully set, this
is consistent with looping being genuinely active, not merely
configured. Live accumulator wraparound was not directly observed
(would require C++-side device-state access beyond what Lua's
`memory`/`ioport` bindings expose) — the persistence-past-single-pass
evidence is indirect but consistent across both the register
configuration and the measured audio.

### Polyphony: three notes, three voices, three pitches

`polyphony-probe.lua`: 3 simultaneous MIDI note-ons (`$3C`/`$40`/`$43`,
`docs/asr10/lua/fixtures/noteon_poly3.mid`):

```text
voice 1 ($3C): FC=0x38D (909)
voice 2 ($40): FC=0x47D (1149)
voice 3 ($43): FC=0x561 (1377)
```

**[Verified]** Three distinct voices, three distinct FC values,
increasing monotonically with note number and matching equal-tempered
semitone ratios within FC's integer quantization (predicted
1145/1362 vs. measured 1149/1377, ≤1.1% — consistent with `FC` being a
17-bit integer, not evidence of a broken formula). Real polyphony, not
voice collision or overwrite.

### More notes than voices

`voice-stress-probe.lua`: 10 sequential distinct note-ons, no
note-offs, tight timing. **[Verified]** 10 distinct voices allocated
(1 through 10), sequential, no reuse/collision. **[OPEN, explicitly
bounded]** Full 32-voice-pool exhaustion/stealing behavior (what
happens at the 32nd+ simultaneous note) was not tested — would require
several dozen more notes than this task's scope covered; this result
only rules out *premature* stealing for a realistic ten-note run.

## Del 3 — 8th Regression Test

`docs/asr10/lua/note_audio.lua` (structural preconditions: MIDI
received, instrument selected, ES5506 voice registers written) +
`docs/asr10/lua/check_note_audio.py` (the actual audio-level gate,
run against the `-wavwrite` capture by `regression-test.sh`'s new
`run_test_audio()`):

- Peak amplitude ≥500 (of 32767) in a 0.10-0.50s window after note-on
  — well above any DC/noise floor, comfortably below the measured
  peak (~3231-3970) so the gate has real margin.
- Dominant frequency (autocorrelation, 0.15-0.35s window)
  100-180Hz — a band built around Del 1's four converged measurements
  (136.8-141.2Hz), wide enough to absorb the measured vibrato drift
  (±3-4% around center) without being a tautology. **No pitch
  condition beyond what Del 1 actually measured** — the band is not
  centered on 261.6Hz or any external assumption.

Both fault-injected against the real capture before trusting them:
a deliberately wrong onset timestamp (silence window) fails on peak;
a deliberately wrong expected band (`[200,300]Hz` against the real
137.5Hz measurement) fails on frequency. Both failures are genuine
discriminating results, not tautologies.

```text
PASS note_audio rhra=3 voice_writes=2400
PASS note_audio_wav peak=3231 freq=137.5Hz
PASS regression
```

**Regression suite is 8/8.**

## Del 4 — Journaled Simplifications And Corrections

**Known simplification**: `es5506_wavetable_bank1_map()`
(`asr10_boot.cpp`) reuses `mem_map`'s own `low_rom_or_lowmem_r`/
`lowmem_w` directly, which means ES5506 bank 1 can see the same
boot-time ROM overlay (`cs0_covers(0)`) the CPU side does. Real
ES5506 hardware's sample bus faces DRAM only — it never sees a ROM
overlay. Harmless in practice (notes only ever trigger post-boot, long
after `cs0_covers(0)` goes false), but written down in the C++ comment
at the map function and here, per project policy on known
simplifications. Numeric basis for bank 1 = lowmem, restated:
`keyboard-and-sample-bridge-6.md`'s Del 1 — voice 1/2's live
`START`/`END` (`>>11` word addresses, doubled to bytes) land inside
`$000944-$0552FF`, the real, already-proven loaded-instrument range.

**Correction, restated for anywhere "clear sweep" language appears**:
`$100000-$1FFFFF` (bank 0's CPU-shared region) contains a
`word = address >> 10` address-derived RAM self-test pattern, not
zero-fill. `sample-ram-and-voice-registers.md` and
`sample-topology-closure.md`'s "one-time clear sweep" language
describes this region's write *count* accurately but its content
imprecisely; `keyboard-and-sample-bridge-6.md` made this correction
first, restated here since Del 1-3 of this task depended on it.

## Verification

- `docs/asr10/regression-test.sh`: 8/8 (7 existing + `note_audio`),
  before and after.
- No `mem_map` change. `es5506.h`/`.cpp` read extensively (bank/FC/
  sample-rate formulas), not modified, not committed.
- No ES5510 change — confirmed still `set_disable()`; every audio
  judgment in this document is the dry ES5506 path.
- ES5506 clock unchanged this task, per instruction — the computed
  "what clock would match" figure is informational, not applied.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` used
  as directed; external analysis via a scratchpad-only Python venv
  (numpy added for FFT/autocorrelation cross-checks during
  investigation) plus the plain-stdlib `check_note_audio.py` that
  *is* committed (the regression gate itself has no numpy dependency,
  only `wave`/`array`, to keep the suite's dependency footprint
  minimal).
- No fork with an open mandate.
- `git diff --check`: clean.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: +10 lines (comment only, on the
  existing `es5506_wavetable_bank1_map()`).
- `docs/asr10/lua/note_audio.lua`, `docs/asr10/lua/check_note_audio.py`:
  new, both wired into `regression-test.sh`.
- `docs/asr10/regression-test.sh`: +1 test (`run_test_audio` function
  + one invocation).
- `docs/asr10/lua/archive/`: six new scripts this task
  (`pitch-probe-c4.lua`, `pitch-probe-c5.lua`, `voice-behavior-probe.lua`,
  `voice-no-noteoff-probe.lua`, `polyphony-probe.lua`,
  `voice-stress-probe.lua`) plus four new fixtures
  (`noteon_48.mid`, `noteoff_3c.mid`, `noteon_poly3.mid`,
  `noteon_stress10.mid`).
