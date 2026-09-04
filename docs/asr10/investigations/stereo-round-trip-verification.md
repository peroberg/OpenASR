# Stereo round-trip verification, resumed and completed (2026-08-24)

Resumes a task that was interrupted mid-comparison against the mono WAV
control. This document reports what was actually measured, not what an
earlier, unwritten draft might have claimed. `[Verified runtime]` unless
marked otherwise.

## Inventory before touching anything

`git log -5 --stat` at session start showed the tip commit,
`ecac7babba4` ("asr10: fix off-by-one that left F00000-F7FFFF unbacked"),
already landed: the previous unified wraparound window
(`memory-size-alias-fix.md`) had replaced `map(0xf00000, 0xf7ffff).ram()`
with `map(0x200000, 0xefffff)` — one byte short of `$F00000` — leaving
512 KB, including *both* SCC receive buffers (`$F76600`/`$F74B00`),
unbacked. That commit's own message says it was "found while building a
round-trip verification test that actually compares injected bytes
against what lands in the sample region" — i.e. this task's own
predecessor.

`git status`/`git diff --check` were clean except two pre-existing,
already-known working-tree changes this project never commits
(`es5506.h`, `.project` — untouched this session, per standing rule) and
four untracked, uncommitted Lua probes already sitting in
`lua/archive/`: `stereo-known-pattern-probe.lua`,
`stereo-interleaved-pattern-probe.lua`, `stereo-playback-probe.lua`,
`mono-playback-control-probe.lua`. No investigation doc existed for them
and `handoff-2026-08-23.md` had no resumption note — the interruption
happened after the probes were written but before either was produced.
These four files, and their results below, are the actual state of "how
far the interrupted round got."

Build (`make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j4`) was clean.
`docs/asr10/regression-test.sh` was **10 tests** before any change here
(11 PASS lines + final `PASS regression` = 12 total lines), all green.

## Del 1 — what the four uncommitted probes actually established

### `stereo-known-pattern-probe.lua` — superseded by design, not a bug

Reuses the mono-proven shared runner (`scc_rx_record_probe.lua`:
feed channel, wait for its own completion, feed the next channel). Run
fresh this session: **FAIL**, stopping after feed 2 with
`iack4b=2` already observed before channel 2's own feed even began:

```
FAIL stereo_known_pattern first_stop feed=2 channel=2 pattern=aa_trigger state=0003 iack4a=1 iack4d=1 iack4b=2 witness=805855
```

This is the documented, correctly-diagnosed reason
`stereo-interleaved-pattern-probe.lua`'s own header gives: completing
SCC1's descriptor (vector `$4D`) immediately fires **two** `$37A1` IDMA
completions in the same event, consuming whatever is already in SCC2's
buffer at that instant rather than waiting for SCC2's own independent
completion — matching a real synchronized stereo ADC that delivers L+R
in lockstep, using one channel's completion as the "pair ready" signal.
The sequential mono technique cannot express that. This probe is a
retained negative result, not a live test; the interleaved probe below
is what superseded it, in the same session that wrote it.

### `stereo-interleaved-pattern-probe.lua` — injection + readback: PASS

Feeds SCC2 (RIGHT, constant `$AA` + trigger bytes) and SCC1 (LEFT, a
0-255 ramp + trigger bytes) interleaved, one byte each per iteration, so
channel 2's buffer already holds its own distinct content by the time
channel 1's completion fires the combined transfer. Run fresh:

```
SIP_VERIFY label=LEFT  destination=62C1F0 length=794 copy_matches=794 copy_mismatches=0 own_best_k=6 own_score=64/64 other_best_k=6 other_score=2/64
SIP_VERIFY label=RIGHT destination=70E630 length=794 copy_matches=794 copy_mismatches=0 own_best_k=6 own_score=64/64 other_best_k=6 other_score=2/64
PASS stereo_interleaved left_bytes=794 right_bytes=794 iack4b=2 witness=1482529
```

Both channels: **794/794 exact source==destination copy** (the IDMA
transfer itself is byte-perfect), and each destination's content fits
*only* its own injected pattern (64/64) and not the other channel's
pattern (2/64, chance-level) — the built-in channel-swap/merge check.
This is injection and byte-for-byte readback, both done, both passing,
for two genuinely different channel contents.

### `stereo-playback-probe.lua` / `mono-playback-control-probe.lua` — the interrupted step

Both record the same technique (interleaved stereo pattern / single-channel
mono pattern), STOP, root-key, and a real note-on captured under
`-wavwrite`. Run fresh this session:

```
PASS stereo_playback voice_writes=326
PASS mono_playback_control voice_writes=163
```

Both reach `MODE?FORWARD-NO LOOP` (a real, playable WaveSample) and
produce real ES5506 voice register writes (2 voices for stereo — twice
the mono count, consistent with one voice per channel). This is where
the previous round stopped: the two WAV files existed, but the
comparison between them was never done. That comparison is below.

## Del 2 — the interrupted comparison, completed

Both probes were re-run capturing to `-wavwrite`, then the two WAV files
were analyzed directly (Python `wave`/`struct`, no external tooling).

**A structural fact neither probe's comment anticipated:** both WAV files
have **3 channels**, not 2. `manager.machine.devices` enumeration shows
why: `:fdc:0:35hd:flopsndout` — a mono floppy-drive-noise speaker
automatically added by `FLOPPY_CONNECTOR`'s `"35hd"` drive model — is
counted into `sound_manager::m_outputs_count` (`sound.cpp:920-926`,
sums `inputs()` over *every* `speaker_device` in the tree) ahead of this
driver's own `SPEAKER(config, "speaker", 2)`. Channel 0 is floppy
drive noise (silent throughout both captures, confirmed below); channels
1 and 2 are the driver's own `es5506_host.add_route(0/1, "speaker", ...)`
L/R outputs. A WAV-level comparison that assumed a plain 2-channel file
would have compared the wrong channels.

Per-channel measurement (peak, RMS, nonzero-sample count):

| file | ch0 (floppy) | ch1 | ch2 |
|---|---|---|---|
| mono | peak=0, all zero | peak=8616 rms=69.5 n=347 | peak=8616 rms=69.5 n=347 |
| stereo | peak=0, all zero | peak=15080 rms=123.0 n=379 | peak=10186 rms=150.7 n=343 |

**Mono control: ch1 and ch2 are bit-identical** — every one of the 347
nonzero samples matches exactly, Pearson correlation over the overlap
window = **1.0**. This is the expected "mono in two copies" shape,
confirmed rather than assumed: `add_route(0,...)`/`add_route(1,...)`
both carry the same single voice's output.

**Stereo: ch1 and ch2 genuinely differ.** Different peak (15080 vs
10186), different RMS (123.0 vs 150.7), different nonzero-sample count
(379 vs 343) and different onset sample (1,204,475 vs 1,204,511 — a
~0.75 ms offset), and correlation over the overlap window is **0.059**
— statistically uncorrelated, not a delayed or scaled copy of the same
waveform. First 20 nonzero samples make the shape difference visible
directly: ch1 settles to `665`, ch2 settles to `-9449` — different sign,
different magnitude, not the same signal at different gain.

**Conclusion, quantified rather than asserted:** stereo output at the
final audible stage is genuinely stereo (channel-distinct, essentially
uncorrelated), and the mono control at the same stage produces
literally identical L/R content — the two controls contrast exactly the
way they should if the pipeline is doing its job. This closes the loop
the interrupted round stopped short of: injected pattern -> SCC RX ->
IDMA copy (byte-exact, per Del 1) -> ES5506 playback -> audibly distinct
output channels (this section), with a mono negative control confirming
the distinction isn't a WAV-writer artifact.

**The bearing question, answered:** right data, right channel, right
format — yes, for the tested path. PCM format was already
`[Verified runtime/firmware/current model]` as big-endian signed 16-bit
(`scc-rx-payload-format.md`); this session's byte-exact copy check
reconfirms it without re-deriving it. Channel interleaving was already
`[Verified functional mapping]` as separate LEFT/RIGHT destination
regions, not `L R L R` (`current-status.md`'s "Other corrected narrow
claims"); this session's two distinct, non-overlapping destination
addresses (`$62C1F0`, `$70E630`) reconfirm that too. What this session
adds that wasn't measured before: the *audible* end of the chain is also
channel-distinct, not merely the recorded bytes.

**Is `UNNAMED WS` an object with data, or an empty object with correct
metadata?** Settled empirically, not architecturally: it has data.
Both playback probes produce real ES5506 voice writes (163 mono, 326
stereo — 2x, one voice per channel) *and* non-silent, channel-distinct
audio in the WAV capture. An empty object with merely-correct metadata
would still allocate a voice (voice_writes > 0) but could not produce
the measured, pattern-correlated amplitude/RMS differences between
channels.

## Del 3 — resampling, manual only, hardware not investigated

`docs/ensoniq/sources/../ASR10_manual.pdf` (via `docs/asr10/sources/`),
Section 7 — Sampling/Signal Source Concepts. `Sample•Source Select`'s
RECORD SOURCE parameter has two fields; Field 1 (source) has exactly
four values:

- `INPUTDRY` — Audio Inputs, dry.
- `INPUT+FX` — Audio Inputs, through the ESP effect.
- **`MAIN-OUT`** — "resample any ASR-10-generated audio routed to OUT
  BUS1, 2, or 3 ... instruments can be Selected and Stacked ... playable
  from the keyboard, or via MIDI ... press Play to start the sequencer
  and sample sequences and songs." No accessory required. This is the
  self-sampling function asked about: the machine samples its own
  output.
- `DIGITAL` — "Sample the Digital Input (**requires the optional DI-10
  Digital I/O interface**). If no digital interface is installed, this
  setting is not available."

Field 2 (LEFT / RIGHT / L+R) is the same mono/stereo selector this
session's probes already drive via `$016F`.

**Answer to what was asked:** yes, the function exists, its name is
`REC SRC=MAIN-OUT`, and it requires no accessory (unlike `DIGITAL`,
which needs the DI-10 card). The manual does not say, and this task did
not investigate, what ASR-10 hardware or firmware selector routes
`MAIN-OUT` into the SCC receive path instead of `INPUTDRY`/`INPUT+FX` —
per instruction, the hardware mechanism is out of scope here. What this
does establish: a `MAIN-OUT`-selected recording is a candidate future
byte source that requires no ADC model at all, only the machine's own
already-verified playback path — if the firmware selector for Field 1
is ever identified.

## Del 4 — three queued items, journaled only, not investigated

**1. SCSI is not merely undetected — no device exists in the machine
config at all.** `grep`ing `asr10_boot.cpp` for `WD33C93`/`scsi` finds
no `WD33C93` device instantiation anywhere. What's mapped at
`$FC5000-$FC501F` is `scsi_asr_candidate_r/w`
(`asr10_boot.cpp:774-782`), a plain stub: the read handler
unconditionally `return 0`, the write handler does nothing. This is a
different failure mode from the FDC's `intrq_wr_callback()` situation
(a real device with a genuinely disconnected interrupt line) — there is
no real WD33C93/AM33C93A model here to have a connected or
disconnected line in the first place. `memory-map.md`'s existing entry
already names the address range and the real chip
(`H1`/`$FC5001`/`$FC5003`/AM33C93A) from static disassembly; this
session only confirms the *runtime* side has no corresponding device.
Nothing built.

**2. PB9/PB10/PB11.** Already `[OPEN hardware]` in
`handoff-2026-08-23.md` section 6 and `interrupt-topology-gaps.md`. This
session adds no new evidence, only the observation (already present in
the handoff's own wording) that these three lines have now surfaced as
candidate explanations for two unrelated things — a digital-feedback
(`MAIN-OUT`) source selector (Del 3, freshly read this session) and
SCSI-INTRQ (Del 4.1, freshly confirmed absent-as-a-device this
session). Recorded here as a named future investigation, not folded
into either as a footnote, per instruction. Not started.

**3. Expanded RAM configurations.** Already fully reasoned in
`memory-size-alias-fix.md`'s "What is deferred" section: of the ROM's
own branch table, only 2 MB, 8 MB and 16 MB are expressible as a single
modulus-wrapped region (the current, landed technique); 4 MB and 10 MB
are additive two-bank designs on real hardware and are "not
representable at all by this technique — genuinely needs an
independently-based second bank." That reasoning already sits next to
where `set_extra_options("2M,8M,16M")` is discussed (Del 2 of that
document) — confirmed present and load-bearing this session, not
re-derived. No expansion was built.

## What was NOT done, named explicitly

No ADC implementation, no SCC code change, no expanded RAM
configuration, no `mem_map` change, no touch to factor two, clocks,
bank 1 or ES5510, and `es5506.h`/`.cpp`/`es5510.cpp`/`esqpump.cpp` were
read only, never edited. The physical mechanism for `MAIN-OUT`'s source
selection and PB9/10/11's real identity remain open, as stated above.

## Regression

`docs/asr10/lua/stereo_round_trip.lua`, promoted from
`stereo-interleaved-pattern-probe.lua` (same technique, same
byte-exact + channel-discrimination check) into the permanent suite as
its **11th test**, since Del 2 produced a stable, reproducible result:
without it, nothing in the suite reads back and compares what a stereo
recording actually stores — the exact gap that let the
`F00000-F7FFFF` off-by-one land undetected in the first place.
Fault-injection tested (swapping which pattern is fed to which channel
produced exactly one `FAIL stereo_round_trip` line, then was reverted;
diff against a saved copy confirmed a byte-identical revert). Suite is
now **11 tests, 12 PASS lines, 13 total lines**.

The four original diagnostic probes (`stereo-known-pattern-probe.lua`,
`stereo-interleaved-pattern-probe.lua`, `stereo-playback-probe.lua`,
`mono-playback-control-probe.lua`) are retained under `lua/archive/` as
reproducible provenance for the measurements above, matching this
project's existing convention (`memory-size-alias-fix.md`'s own
diagnostic probes).

## References

- `investigations/memory-size-alias-fix.md` — the category fix that
  made stereo reach `WAITING` at all, and the RAM-configuration
  reasoning cited in Del 4.3.
- `investigations/scc-idma-transfer.md` — the pretrigger-history /
  offset-search technique reused in the readback verification.
  reused unmodified in `stereo_round_trip.lua`'s `best_fit()`.
- `investigations/scc-rx-payload-format.md` — PCM format and
  LEFT/RIGHT destination-separation, reconfirmed not re-derived.
- `docs/asr10/sources/ASR10_manual.pdf`, Section 7 — `MAIN-OUT`
  citation for Del 3.
- `reference/memory-map.md` row H1 — SCSI chip identity from static
  analysis, contrasted with this session's runtime-absence finding.
