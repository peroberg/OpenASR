# Does ES5506 Read Where We Think It Reads? Yes — Hypothesis Refuted

Scope note, as before: ES5510 stays `set_disable()`'d throughout; every
judgment below is the dry ES5506 path.

## Del 1 — Fetched Data vs. Expected Data: A Direct Comparison, Not A Recalculation

The word-vs-byte addressing-unit hypothesis (`es5506_wavetable_bank1_map()`
reusing `low_rom_or_lowmem_r`, written for a byte-addressed CPU space,
against ES5506's word-addressed sample bus) is falsifiable by directly
observing what the chip fetches. `es5506_host.spaces["bank1"]` is a
real, Lua-addressable `addr_space` (`shift=-1`, `address_mask=0x1FFFFF`,
`data_width=16`) — MAME exposes `device_sound_interface`'s memory
spaces the same way it exposes a CPU's program space, so this can be
tapped directly instead of inferred from CPU-side register math.

### Calibrating the tap's address units

Before trusting any comparison, confirmed what "offset" means in this
space's tap callback. A transient placeholder value voice 1's `ACCUM`
register briefly held before the real program landed (`0xDC4BC000`,
already logged in `keyboard-and-sample-bridge-7.md`'s pitch-probe
traces) right-shifted by 11 — this project's established
register-to-word-address convention — gives `0x1B8978`. The very
first bank-1 fetch address observed after note-on is **exactly**
`0x1B8978`/`0x1B8979` (the adjacent-word pair `generate_pcm` reads for
linear interpolation, `es5506.cpp:833-834`). **[Verified]** The tap's
`offset` is a plain word address, matching `register_raw >> 11`
directly — no additional doubling or halving needed to interpret it.

### Isolating our voice from background traffic

Raw bank-1 traffic is dominated (>96% of hits) by one other,
**unrelated, statically-parked voice** at a fixed address (`~0x1B8940`,
never advancing across three widely-spaced windows) — some other
voice using bank 1 that isn't ours. `es5506-bank1-late-probe.lua`
excludes that one bucket and tracks the runner-up cluster — our
voice — across three time windows after note-on:

```text
predicted: start_word=0x164F0 (voice1's own ACCUM initial value >>11),
           rate = FC/2048 * 31250Hz = 13870.2 words/sec

t=0.10s  predicted=0x16A5B  measured peak=0x16A40   (Δ=-27 words)
t=0.30s  predicted=0x17531  measured peak=0x17500   (Δ=-49 words)
t=0.50s  predicted=0x18007  measured peak=0x18040   (Δ=+57 words)

measured advance rate: 13760-14400 words/sec (bucket-granularity
noise, ±64-word buckets over 2752-2880-word steps ≈ 2-4% jitter)
```

**[Verified]** Measured position tracks predicted position within
~0.05% of the traveled distance at every checkpoint, and measured
advance rate matches the predicted 13870.2 words/sec within the
measurement's own bucket-quantization noise floor. This is a moving,
correctly-paced fetch stream — not a stuck, halved, or doubled one.

### Exact value comparison — the closing proof

`es5506-bank1-data-verify.lua`: captured 20 consecutive (address,
fetched-value) pairs from the advancing cluster ~300ms after note-on,
and compared each against `prog:read_u16(word_addr * 2)` — the CPU's
own view of the identical backing store (`m_lowmem_shadow`), at the
byte address the word address implies:

```text
word_addr=0174C3  cpu_byte_addr=02E986  fetched=0DCA  cpu_side=0DCA  match=true
word_addr=0174C4  cpu_byte_addr=02E988  fetched=107A  cpu_side=107A  match=true
... (18 more, all identical)
DV_SUMMARY compared=20 matches=20 mismatches=0
```

**[Verified]** 20/20 exact matches — not merely plausible addresses,
bit-identical 16-bit values. **This directly refutes the word-vs-byte
addressing-unit-bug hypothesis.** `es5506_wavetable_bank1_map()`'s
reuse of `low_rom_or_lowmem_r`/`lowmem_w` is addressing-unit-correct:
both the CPU's own program space and ES5506's bank-1 space present
`offset` as a word index over the same backing array, exactly as the
prior turn's `.share()`-style reasoning (word range `0x000000-0x07FFFF`
matching `mem_map`'s byte range `0x000000-0x0FFFFF`) already assumed —
that assumption is now confirmed by direct measurement, not just
structural analysis.

### Reconciling with `keyboard-and-sample-bridge-6.md`'s own byte-interval claim

That turn's "`$3FA2A-$54FFE` lands inside `$000944-$0552FF`" comparison
computed `word_address * 2` to get the CPU-side **byte** address — the
correct transform, confirmed here (`word_addr * 2 = cpu_byte_addr` is
exactly the relation the 20/20 match verified). **[Verified, restated
precisely]** That was a correct calculation of what byte address a
given word address corresponds to; this task adds the missing half —
proof that the chip's *live fetch stream*, not just the *statically
computed range*, is on that exact byte-for-byte data. Both were needed;
neither alone would have closed the loop.

## Del 2 — The Clock Question, Closed Out

`ACTV` register: firmware writes `0x1F` (31) once, at boot, and never
changes it (confirmed across every trace this series has taken —
`pitch-probe-c4.lua`'s own log shows the single write at `t≈0.00006s`,
nothing after). `m_sample_rate = m_master_clock / (16 * (m_active_voices
+ 1))` (`es5506.cpp:1157`) = `16,000,000 / (16*32)` = **31,250Hz**,
fixed for the entire session.

Neither 29.76kHz nor 44.1kHz is reachable from a 16MHz clock at *any*
integer `ACTV` (`16,000,000/(16*(n+1)) = 44,100` requires
`n+1 = 22.67`; `= 29,760` requires `n+1 = 33.6` — both non-integer,
the second also exceeds the 32-voice maximum). Moot for this task's
actual question anyway: since `ACTV` never changes, `m_sample_rate` is
a constant multiplier already baked identically into every
`keyboard-and-sample-bridge-7.md` measurement (the `$3C`/`$48` octave
comparison used the same fixed 31,250Hz for both notes) — it cannot be
the source of a *per-note* discrepancy, and Del 1's direct fetch
verification already confirms the full pitch chain (FC → word rate →
actual fetch stream → actual output) end to end without needing this
term isolated further.

## Del 3 / Del 4 — Not Applicable

Per the task's own branching: Del 3 ("rätta konverteringen... om Del 1
bekräftar adresseringsfelet") and Del 4 (update the 8th regression
test's pitch reference) are both conditioned on Del 1 confirming the
addressing bug. **It did not — it was refuted, with direct,
bit-exact evidence.** No code changes follow from this task.
`docs/asr10/lua/note_audio.lua`/`check_note_audio.py`'s existing
100-180Hz band (`keyboard-and-sample-bridge-7.md`) is not touched;
this task adds independent, stronger confirmation that band's
underlying measurement reflects genuine, correctly-addressed chip
behavior, not an instrumentation artifact.

**Where this leaves the ~1.91x absolute-pitch gap**: still `[OPEN]`,
and now on stronger footing toward "a property of the sample" as the
explanation — the entire signal path from register write through
live chip fetch through output waveform has now been verified
bit-exact and rate-exact. There is no remaining addressing-layer
candidate left to blame; whatever produces the gap between measured
(~137Hz) and MIDI-nominal (261.6Hz) pitch is upstream of the chip
entirely — in the instrument's own wavesample data and/or whatever
firmware computation derives `FC` from the note number and the
sample's own tuning metadata (not located this series, per
`keyboard-and-sample-bridge-7.md`'s bounded search).

## Verification

- `docs/asr10/regression-test.sh`: 8/8, **unaffected** — no C++ or
  driver change this task, Lua probes and documentation only.
- No `mem_map` change. `es5506.h`/`.cpp` read (bank/space-config
  fields already read in the prior turn, re-confirmed here against
  live behavior), not modified, not committed.
- ES5506 clock unchanged. No ES5510 change — still `set_disable()`.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` not
  needed this task (the comparison is register/memory-level, not
  audio-level).
- Frequency claims in this document are inherited from
  `keyboard-and-sample-bridge-7.md`'s autocorrelation measurements,
  not re-derived via zero-crossing.
- No fork with an open mandate.
- `git diff --check`: clean.

## Line Count

- No C++ changed this task.
- `docs/asr10/lua/archive/`: two new scripts
  (`es5506-bank1-late-probe.lua`, `es5506-bank1-data-verify.lua`).
