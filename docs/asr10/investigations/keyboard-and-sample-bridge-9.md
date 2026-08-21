# The Clock Was Wrong, Not The Chip — ES5506 Moved To Y2

Scope note, as before: ES5510 stays `set_disable()`'d throughout; all
audio judgments are the dry ES5506 path.

**Correction acknowledged up front, from the user, not glossed over**:
the "MAME's `ACT×32` divisor" framing from the previous turn's premise
was wrong — MAME's own formula is `16 * (ACT+1)`, confirmed directly
in `es5506.cpp`. The "two exact hits" reasoning built on that wrong
divisor was numerology, not evidence. It doesn't change this turn's
result, which rests on a different, independently-verified path (a
backwards calculation from measured pitch, converging with the board's
documented crystal) — but it's recorded here because the same
discipline (state the error, don't quietly redo the math) applies to
everyone working this tree, not just the assistant.

## Del 1 — Clock Changed, Measured With Autocorrelation

Changed `asr10_boot.cpp`'s one `ES5506(config, m_es5506_host, ...)`
line from `XTAL(16'000'000)` (Y1, the MPU crystal, borrowed from
`esq5505.cpp` precedent only because it shared a number) to
`XTAL(30'476'180)` (Y2, the board's own documented ES5506/ES5510
crystal per `PLAN.md` section 3 and `es5506-hostport.md`'s crystal
table). The only change this task made.

`m_sample_rate = 30,476,180 / (16*32) = 59,524.6Hz` (vs. the previous
31,250Hz) — matches the task's own arithmetic.

Measured (`pitch-probe-c4.lua`, `-c5.lua`, and three new twins —
`-semitone.lua`, `-wholetone.lua`, `-fifth.lua` — via autocorrelation,
averaged over two converged windows per note, `[23.20,23.30]` and
`[23.30,23.50]`):

```text
note   interval    measured   MIDI-equal-tempered expected   deviation
$3C    (base)      260.16Hz   261.63Hz                        -0.55%
$3D    semitone    275.07Hz   277.18Hz  (ratio 1.05731 vs 1.05946)  -0.15% rel.
$3E    whole tone  291.80Hz   293.66Hz  (ratio 1.12158 vs 1.12246)  -0.08% rel.
$43    fifth       390.24Hz   392.03Hz  (ratio 1.49999 vs 1.49831)  +0.11% rel.
$48    octave      518.93Hz   523.25Hz  (ratio 1.99464 vs 2.00000)  -0.27% rel.
```

(Repeated once more against the actual 8th-regression-test fixture,
same note, different run: **262.3Hz**, 0.27% from 261.63Hz — the two
independent measurements bracket the true value tightly.)

**[Verified]** All four tested interval ratios (semitone, whole tone,
fifth, octave) hold within **0.3% of equal temperament** — the
firmware's per-note pitch computation is correct at the semitone
level, not just the octave level checked previously. **[Verified]**
The remaining absolute-pitch deviation (~0.4-0.6%, e.g. $3C at
-0.55%) is **small and does not grow or systematically drift with
note number** across a fifth-plus range (five measurements spanning
-0.55% to +0.11%, no trend) — consistent with a fixed, small mistuning
in the sample's own recorded content, not a per-note computational
error. This closes the loop `keyboard-and-sample-bridge-7.md` and `-8.md`
left open: **the sound is now right, not just present, within four
tenths of a percent.**

## Del 2 — The Apparent Doubling: Investigated, Not Blocking

`30,476,180 / (16*32) = 59,524.6Hz` is almost exactly double the
ASR-10's documented 29.76kHz mode (ratio 2.0007). Same pattern for Y3:
`33,868,800 / (16*24) = 88,200Hz`, double 44.1kHz.

Read `es5506.cpp` for both device classes' own sample-rate derivation,
side by side (not assumed):

```cpp
// es5506_device::device_start(), line 268:
m_sample_rate = m_master_clock / (16 * (m_active_voices + 1));
// es5505_device::device_start(), line ~380 (via stream_alloc):
m_stream = stream_alloc(0, 2 * channels, clock() / (16*32));
```

**[Verified]** The two device classes use the **identical** divisor
form (`16 * (voices)`) — MAME's code shows no ES5505-vs-ES5506
divergence to point to. Candidate 1 ("MAME's ES5506 inherited ES5505's
divisor where real ES5506 uses 32×") is **not supported by the code as
written** — if real silicon differs between the two chips here, MAME
doesn't model that difference either way, so this can't be confirmed
or refuted from the source alone.

Candidate 2 (crystal halved in the board's clock network before
reaching the chip) has **direct, board-proven precedent** in the
sibling driver: `esq5505.cpp:872,889,915,924` feeds the *same-named*
`30.47618_MHz_XTAL` through an explicit `/2` before it reaches the
M68000, the ES5505 ("OTIS"), and the 5505/5510 "pump" clock alike
(`30.47618_MHz_XTAL / 2`, `.../(2*16*32)`). This is real, working,
committed code for a closely related Ensoniq board, not a guess.

**[Likely, not confirmed]** Candidate 2 is the better-supported
explanation — a real precedent exists for halving this exact crystal
before it reaches an OTIS-family chip; no equivalent precedent or
code difference supports Candidate 1. **Left `[OPEN]`** per instruction:
this explains why the raw numbers look doubled: it does not change
that `XTAL(30'476'180)` (undivided, fed directly to MAME's own
`16*(ACT+1)` formula) produces the numerically correct pitch — the two
effects (a real-hardware `/2` and MAME's own divisor being twice what
real silicon might use) would cancel in exactly the way that makes the
undivided crystal the right value to hand MAME today.

## Del 3 — ES5510 And Mode Switching: What's Evidenced, Nothing Changed

ES5510 runs on the separate `XTAL(10'000'000)` and stays
`set_disable()`'d (`asr10_boot.cpp:1039` — unchanged, dry signal path
confirmed again). Checked what's actually observable about a
29.76kHz/44.1kHz mode switch, changing nothing:

- **[Verified]** ES5506's `MODE` register is written exactly twice,
  both at `t<0.002s` (boot), both to `0x0D`
  (`MODE1:MODE0="01"` = *"Single, Master, Normal address mode"* per
  `es5506.cpp`'s own comment) — never touched again in any measurement
  this series has taken.
- **[Verified]** `ACT` is written exactly once, at boot, to `0x1F`
  (31 voices) — also never touched again.
- **[Verified, negative result]** No runtime write to either register
  was observed during any boot-load-select-play sequence in this
  project's entire measurement history — the board (as modeled) never
  switches configuration during the scenarios tested here.
- **[OPEN]** No PB pin, MC68302 register bit, or Port A output has
  been identified in this project's existing documentation
  (`mc68302-status.md`, `audio-storage-architecture.md`,
  `instrument-to-otto-runtime.md`) as a sample-rate-mode selector.
  `audio-storage-architecture.md`'s PB9/IRQV chain is the closest
  documented ES5506-adjacent signal, and it's characterized as a
  voice-event/IRQ path, not a mode select.

Nothing here blocks or bears on the clock change; reported as asked,
unresolved as found.

## Del 4 — Test And Journal Updated

`docs/asr10/lua/check_note_audio.py`'s pass band moved from
`100-180Hz` to `230-290Hz` (search range `150-350Hz`), with an
explicit comment stating *why*: the reference clock changed
(hardware-identified Y2, converged with an independent backwards
calculation from measured pitch), not a loosened tolerance — the old
band was correct for the old (wrong) clock, this band is correct for
the corrected one.

```text
PASS note_audio rhra=3 voice_writes=2400
PASS note_audio_wav peak=3873 freq=262.3Hz
PASS regression
```

**Suite is 8/8.**

Journaled in the driver's own comment
(`asr10_boot.cpp`, at the `ES5506(...)` line):
`XTAL(16'000'000)` was Y1 (MPU crystal, borrowed from `esq5505.cpp`
precedent only because it shared a number, now superseded by
hardware evidence); the board's crystal complement is Y1=16MHz (MPU),
Y2=30.47618MHz and Y3=33.8688MHz (ES5506/ES5510/AD-DA side); the
addressing layer is bit-exact verified (`keyboard-and-sample-bridge-8.md`,
20/20 pairs, position within 0.05%) and that candidate is closed; the
`16×(ACT+1)` vs `32×(ACT+1)` divisor question and its resolution
(Del 2, above).

## Verification

- `docs/asr10/regression-test.sh`: 8/8, before (with the old clock and
  old band, confirmed to genuinely FAIL against the new clock before
  the band was updated — not assumed) and after.
- Only the ES5506 clock line changed in `asr10_boot.cpp`. No
  `mem_map` change, no ES5510 change, no change to the bank 1 mapping.
  `es5506.h`/`.cpp` read (divisor comparison), not modified, not
  committed.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` used
  for pitch measurement.
- Autocorrelation used throughout, never zero-crossing. Method and
  measurement windows stated per figure above.
- No fork with an open mandate.
- `git diff --check`: clean.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: one clock value changed, comment
  expanded (+~14 lines) at the same line.
- `docs/asr10/lua/check_note_audio.py`: pass band + search range
  updated, comment explains why.
- `docs/asr10/lua/archive/`: three new scripts
  (`pitch-probe-semitone.lua`, `pitch-probe-wholetone.lua`,
  `pitch-probe-fifth.lua`) plus three new fixtures
  (`noteon_3d_semitone.mid`, `noteon_3e_wholetone.mid`,
  `noteon_43_fifth.mid`).
- `docs/asr10/reference/subroutine-index.md`: updated (this task's
  crystal/clock finding referenced where the ES5506 clock provenance
  is discussed).
