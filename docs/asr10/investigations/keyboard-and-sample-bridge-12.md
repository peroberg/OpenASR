# The Pump: A Real Missing Block, But It Doesn't Explain Factor Two

Scope note: Del 3/4's scratch experiments enabled ES5510 briefly,
measured, and reverted via `git checkout` — the committed tree has
ES5510 `set_disable()`'d throughout, unchanged.

## Del 1 — The Real Chain, Mapped

### `esqpump.cpp`/`.h`, read only

`esq_5505_5510_pump_device : public device_t, public
device_sound_interface` (`esqpump.h:16`) — **[Verified]** it *is* a
`device_sound_interface`, with its own `sound_stream *m_stream`
(`esqpump.h:79`) and a `required_device<es5510_device> m_esp`
(`esqpump.h:82`). The hypothesis is confirmed directly from this file,
not from ES5510's own absence of a sound interface elsewhere:

```cpp
// device_start():
m_stream = stream_alloc(8, 4, clock(), STREAM_SYNCHRONOUS);
// device_clock_changed():
m_stream->set_sample_rate(clock());
// sound_stream_update(), every sample:
stream.put(2, 0, stream.get(0, 0));           // Aux bypass, L
stream.put(3, 0, stream.get(1, 0));           // Aux bypass, R
m_esp->ser_w(0, ...); ... m_esp->ser_w(5, ...); // FX1/FX2/DRY -> ESP serial in
if (!m_esp_halted) { m_esp->run_once(); }       // one ESP microprogram cycle
sound_stream::sample_t l = m_esp->ser_r(6) * output_scale;
sound_stream::sample_t r = m_esp->ser_r(7) * output_scale;
stream.put(0, 0, l); stream.put(1, 0, r);
```

**[Verified]** 8 input channels, 4 output channels — **hardcoded** in
`device_start()`, not derived from any attached device or exposed as
configurable. Comment in the same function names the mapping exactly:
*"The VFX only has a single pair of stereo outputs, 'Main'... VFX-SD
and later have a separate 'Aux' stereo output that bypasses ESP effect
processing."* Channels 0/1 = Aux (dry passthrough), 2-7 = three stereo
effect-send pairs (FX1/FX2/DRY) routed through the ESP. **This is
exactly 4 stereo pairs — ES5505's own pair count, not ES5506's 6.**
`run_once()` (`es5510.cpp:1051`) has no reference to any MAME
`disabled()`/scheduler-gate check anywhere in `es5510.cpp`/`.h` — it
is a plain method call, independent of `set_disable()`, which only
withholds the device from MAME's own automatic scheduler dispatch. A
*separate*, driver-controlled gate exists: `m_esp_halted`, defaulting
to `true` in the constructor (`esqpump.cpp:24`) and only cleared via
`set_esp_halted(false)`.

### The full `esq5505.cpp` chain, both patterns

**Pattern A** (`common()`, VFX family — `esq5505.cpp:760-803`):

| Device | Clock expression | Value |
|---|---|---|
| M68000 | `10_MHz_XTAL` | 10,000,000 Hz |
| ES5510 (`m_esp`) | `10_MHz_XTAL` | 10,000,000 Hz, `set_disable()`'d (line 767) |
| Pump | `10_MHz_XTAL / (16 * 21)` | **29,761.9 Hz** |
| ES5505 (`m_otis`) | `10_MHz_XTAL` | 10,000,000 Hz |

ES5505 routes all 8 of its own channels to `"pump"` (0-7, one call
each, `esq5505.cpp:796-803`); the pump routes 0/1 to `"speaker"`.

**Pattern B** (`common32()`, SD-1/32-bit family — `esq5505.cpp:887-938`):

| Device | Clock expression | Value |
|---|---|---|
| M68000 | `30.47618_MHz_XTAL / 2` | 15,238,090 Hz |
| ES5510 (`m_esp`) | `10_MHz_XTAL` | 10,000,000 Hz — a **separate**, undivided clock, unrelated to the 30.47618MHz crystal; `set_disable()`'d (line 894) |
| Pump | `30.47618_MHz_XTAL / (2 * 16 * 32)` | **29,761.9 Hz** |
| ES5505 (`m_otis`) | `30.47618_MHz_XTAL / 2` | 15,238,090 Hz |

**[Verified]** Both patterns exist in the *same file*, for *different*
board configs (`vfx`/`vfxsd`/`sq1`/`eps` use pattern A via `common()`;
`sd132`/`sd1`/`ks32` use pattern B via `common32()`). **Both patterns'
pump ends up at the identical 29,761.9Hz** — matching ASR-10's own
documented 29.76kHz mode — despite using completely different crystal
derivations. **[Verified]** ES5510 is `set_disable()`'d in *both*
patterns, in upstream MAME's own committed driver for this exact chip
family — our own driver's choice to disable it is not a shortcut
unique to this project, it matches established practice for the same
device.

### Comparison table

| Element | `esq5505.cpp` | `asr10_boot.cpp` |
|---|---|---|
| Oscillator → speaker | Never direct — always via pump | Direct (`es5506_host.add_route(.... "speaker" ...)`) |
| Pump present | Yes, both patterns | No |
| ES5510 `set_disable()` | Yes, both patterns | Yes |
| ES5510 clock | `10_MHz_XTAL` in both (even when M68000/oscillator use the 30.47618MHz crystal) | `XTAL(10'000'000)`, matches |
| Oscillator clock | 10MHz flat (A) or crystal/2 (B) | Crystal, undivided (Y2, 30.47618MHz) |
| Pump clock | Derived to land on 29,761.9Hz in both patterns | N/A — no pump |
| Channel count into pump | 8 (ES5505's 4 pairs) | N/A |

## Del 2 — Does This Explain Factor Two? Reasoned Answer: No

**[Likely, architectural reasoning — not a fresh audio re-measurement
with an actual pump wired in]**: it does not.

`device_clock_changed()` sets the pump's *own* stream sample rate
directly from its *own* `clock()` — a parameter entirely independent
of whatever rate the oscillator device upstream runs at. MAME's own
sound core documents that connected streams at differing rates are
automatically resampled (`src/emu/sound.h:50`, *"the inputs will have
been resampled to match the output"*, backed by a real
`audio_resampler` class and `create_resamplers()`). Resampling changes
sample-rate *representation*, not pitch — a correctly-resampled signal
sounds the same. **The oscillator's own pitch is set entirely by its
own clock, via the same `clock/(16*(voices+1))` formula this project
has used throughout for ES5506** — nothing the pump does after the
fact reaches back into that.

So: if ES5506 were clocked at the halved Y2 (15,238,090Hz, matching
pattern B's precedent for ES5505), its own internal generation rate
becomes `15,238,090/(16*32) = 29,761.9Hz` — exactly half of what this
project's currently-correct-sounding configuration uses
(`59,524.6Hz`). For firmware's fixed `FC` value, playback words/sec
would **halve**, and the note would sound **one octave low** — a
pump inserted afterward, at any rate, only resamples that
already-an-octave-low signal to a new sample-rate representation. It
does not restore the octave.

**Answer to the specific question**: the chain crystal → halved-clock
ES5506 → pump *would* land on the pump's own output rate of
29,761.9Hz (matching ASR-10's documented mode) — but that number is
the *final mixdown rate*, not the pitch of what ES5506 generated. Pitch
would still be wrong (an octave low) under a halved ES5506 clock,
pump or no pump. **This does not resolve the factor of two.** The
open statement stands exactly as parked in the prior turn, unchanged:

> Vid verkliga hårdvaruvärden — CLKIN 15,238 MHz, utgångstakt
> 29 762 Hz — producerar MAME:s ES5506 halva tonhöjden mot vad riktig
> hårdvara producerar för samma FC. FC- och ACCUM-bredderna matchar
> databladet. Vår dubblade klocka kompenserar exakt. Orsaken är
> okänd.

## Del 3 — What Wiring Would Require, Without Building It

- The pump does not reference any specific oscillator device type in
  `sound_stream_update()` — it only reads generic `stream.get(n, 0)`
  inputs, so it could in principle accept any `device_sound_interface`
  source via `add_route`. It is *not* bound to ES5505 specifically at
  the routing level.
- **Channel count is the real obstacle**: `stream_alloc(8, 4, ...)` is
  hardcoded in `esqpump.cpp`'s own `device_start()` — not a
  constructor parameter, not derived from the attached oscillator.
  ES5506's 6 stereo pairs (12 channels) cannot all be fed through the
  *existing* 8-input pump without modifying `esqpump.cpp` itself — a
  shared device used by multiple drivers, outside this project's
  mandate to change. Only 4 of ES5506's 6 pairs could be connected as-is
  (matching the VFX's own Aux+FX1+FX2+DRY scheme); the remaining 2
  pairs would have nowhere to route.
- ES5510 does **not** need to be enabled (un-`set_disable()`'d) for
  `run_once()` to execute — that call bypasses MAME's scheduler gate
  entirely. What **would** be required: clearing the pump's own
  `m_esp_halted` flag (`true` by default), which `esq5505.cpp` does via
  a real hardware bit (PB6, "ESPHALT") that firmware toggles after
  downloading the program. This project's driver has not identified or
  wired an equivalent bit.
- Pump clock: no ASR-10-specific value is established. Replicating
  either `esq5505.cpp` pattern (`crystal/(16*21)` or
  `crystal/(2*16*32)`, tuned to whichever crystal ASR-10 actually
  wires) would reproduce 29,761.9Hz; this is a design choice, not yet
  evidenced for this board specifically.
- **8th regression test's pitch reference**: per Del 2's reasoning, a
  pump alone (without also changing ES5506's own clock) only affects
  final output sample-rate representation, not the measured pitch —
  the current `230-290Hz` band would likely remain valid. This is not
  verified by building the pump; flagged as expected, not confirmed.

## Del 4 — `ERROR 032`: Original Hypothesis Corrected By Measurement

**Hypothesis tested**: the retry-eligibility compare
`filesystem-browser-map.md` documented (`ffc896: cmpi.l
#$fff9bca0,$e8e.w`) is what fails when ES5510 is enabled.

**[Disproven, by direct measurement]** Reproduced `button.lua`'s exact
failing stimulus (single `BTN_0A` press) with ES5510 scratch-enabled,
tapping `$e8e.w` and the `$FFC896` compare instruction directly:

```text
E032B_RESULT ok=false final_display="EFFECT D0WNL0AD FAILED"
E032B_FFC896_HIT_COUNT count=0        <- never executed
E032B_E8E_READ ... data=FFF9 (four times, from t=16.331s onward)
```

**The hypothesized compare instruction never executes at all in this
failing run.** `$e8e.w`'s high word **does** reach `0xFFF9` — matching
the expected sentinel's high word (`0xfff9bca0`) — meaning that
specific gate is not what blocks progress; if anything, this shows the
transfer *reaching* the point the old documentation described as
successful, not failing to reach it. (Baseline, ES5510 disabled, same
button.lua stimulus for comparison: `$e8e.w` never shows `0xFFF9` at
all across the same window — but that run also *succeeds*, so absence
of the sentinel there is not itself evidence of anything broken; it
simply means that code path isn't exercised the same way when the
device is a stub.)

A broader tap on the message-dispatcher addresses `filesystem-browser-
map.md` also names (`$F884B6`, `$F884BC`, `$F89C48`) fires 20 times
across both the boot-time and load-time windows — but these addresses
are **generic, frequently-executed dispatch code**, not
error-specific; their firing doesn't by itself identify the failing
condition.

**Honest result: the specific mechanism that produces `EFFECT
DOWNLOAD FAILED` when ES5510 is enabled was not found within this
task's bounded effort.** The originally-carried-over hypothesis (built
on a different investigation's documentation, possibly for a
different code path or ROM context than the one this exact stimulus
exercises) is corrected, not confirmed. What is established: enabling
the device changes *something* observable (`$e8e` reaching the
sentinel value where it didn't before) without hanging MAME, and the
failure is a real, distinct, reproducible outcome — but its precise
trigger remains open for a future, more targeted task (a full-
resolution instruction trace from the moment `$e8e` is set through to
the display-text write, not attempted here).

## Del 5 — Journal

- **Chain comparison** (Del 1's table) and both `esq5505.cpp` clock
  patterns recorded — not just the one cited two turns ago.
- **The missing block**: `asr10_boot.cpp` routes ES5506 straight to
  `SPEAKER`; the real architecture (both `esq5505.cpp` patterns)
  always inserts the pump. Linked to the factor-of-two entry: a real,
  named gap, but **not its explanation** — Del 2 shows the pump cannot
  retroactively fix an upstream oscillator-clock-driven octave error.
- **Del 2 result**: reasoned "no," backed by `sound.h`'s own resampler
  documentation and the pump's `device_clock_changed()`/
  `sound_stream_update()` code. The open factor-of-two statement is
  **unchanged** (Del 2 did not resolve it, so its wording stays exactly
  as parked).
- **Del 4 result**: the specific `ffc896`/`$e8e` hypothesis is
  corrected by direct measurement — that compare never fires in the
  failing run. The actual failing mechanism stays genuinely `[OPEN]`,
  stated sharply enough to be attacked later (measure the exact
  instruction sequence between `$e8e` reaching `0xFFF9` and the
  display text changing).
- **ESP program + no audio path**: restated from the prior turn — a
  real, complete 160-instruction program is downloaded
  (`keyboard-and-sample-bridge-11.md`), and `es5510_device` has no
  `device_sound_interface` of its own; the *pump* is the missing piece
  that would give it one, confirmed by reading `esqpump.h` directly
  this task.
- **Per's architecture argument, acknowledged as-is**: the ES5505/5506
  + ES5510-with-pump layout is real, proven across multiple committed
  Ensoniq drivers in this exact codebase (not merely "known from other
  models" in the abstract) — a deviation in this project's own chain
  (direct ES5506→SPEAKER) is measurably more likely to be the
  project's own gap than an error in that shared, multiply-reused
  design.
- **Method note, recorded plainly**: this is the third time an
  `esq5505.cpp` pattern was cited as *the* pattern when the file
  contains several. A partial reading of a reference file is the same
  failure class as a tap without a live witness — it looks complete
  and isn't. Going forward: when citing `esq5505.cpp` (or any
  multi-config reference driver) as precedent, grep for *all* machine
  configs in the file before citing one as representative.

## Verification

- `docs/asr10/regression-test.sh`: 8/8, unaffected — no permanent code
  change. Both scratch experiments (Del 3's channel/halt check
  needed no build; Del 4's `error032_probe`/`error032_probe2`/
  `error032_probe3` used the same one-line `set_disable()` scratch
  edit as the prior turn) were built, measured, and reverted via `git
  checkout -- src/mame/ensoniq/asr10_boot.cpp`, confirmed clean
  (`git status`/`git diff --stat` empty) before the final regression
  run.
- No `mem_map` change, no clock change, no bank 1 change, no permanent
  ES5510 activation. `esqpump.cpp`/`.h`, `es5506.cpp`/`.h`,
  `es5510.cpp`, `esq5505.cpp` read extensively, not modified, not
  committed.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` not
  needed this task (register/instruction-level measurement only).
- Factor of two: not investigated further beyond Del 2's reasoned
  (not re-measured) architectural answer; wording unchanged from the
  prior turn's parked statement.
- No fork with an open mandate.
- `git diff --check`: clean.

## Line Count

- No permanent C++ change (two scratch experiments, both built,
  measured, and reverted).
- No new archived Lua scripts this task (four scratchpad-only probes
  used for measurement, findings fully captured here).
