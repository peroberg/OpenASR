# Closing The Sample Topology Numerically, Then Wiring It Up

## Scope

`sample-ram-and-voice-registers.md` found `$100000-$1FFFFF` gets a
one-time, full-range clear before `FILE 1` and nothing afterward, and
that the ES5506 reads samples through its own disconnected per-device
address space. This task closes the remaining numeric gap (does voice 0's
own pointer land inside that specific range?), searches the tree for the
real CPU-RAM-to-ES5506 sharing idiom, and — only because Del 1 confirmed
the topology — makes one named change: `.share()` the two allocations and
wire an output. Then measures the result objectively via `-wavwrite`.

## Del 1 — Voice 0's Pointers, Numerically

`docs/asr10/lua/archive/sample-topology-and-payload-probe.lua` re-measures
voice 0's `START`/`END`/`ACCUM` (same decoder as
`sample-ram-and-voice-registers-probe.lua`, reused not re-derived) and
converts the resulting word address to the CPU byte address it would
correspond to under a `$100000`-based alias (`cpu_byte = 0x100000 +
word_addr * 2`).

```text
STP_VOICE0 cr=0308 bank=0 start_raw=00000000 end_raw=10000000 accum_raw=10000000
STP_VOICE0_WORDS start_word=00000 end_word=20000 accum_word=20000 in_range_0_7FFFF: start=true end=true accum=true
STP_VOICE0_CPU_BYTES start=100000 end=140000 accum=140000
```

- **[Verified]** `START`, `END`, and `ACCUM` all resolve to word addresses
  inside `$00000-$7FFFF` (`0x00000`, `0x20000`, `0x20000`) — the exact
  range the task asked to test. Under the `$100000`-base conversion:
  `START=$100000`, `END`/`ACCUM=$140000`. **The shared-RAM hypothesis is
  confirmed numerically, not just by architectural reasoning.**
- **[Verified]** Same result on a fresh run (measured twice, identical
  both times — `start_word/end_word/accum_word` unchanged), and consistent
  with the previous task's measurement of the same voice.
- **Caveat, stated plainly:** at measurement time this specific address
  range holds only the boot-time clear's zeros (`sample-ram-and-voice-
  registers.md`'s Del 1) — the numeric membership test passes, but nothing
  audible could come from voice 0 *yet*, because nothing has been written
  to `$140000` specifically. This is exactly why Del 3 below closes the
  loop by sharing the allocation, not by asserting the hypothesis was
  already sufficient on its own.

### Payload content check

Read every IDMA-arm destination range from live memory (already proven
byte-identical to the source disk image) and computed basic content
statistics, to rule out "this file barely contains sample data" before
trusting the rest of the measurement:

```text
STP_PAYLOAD arms=21 total_bytes=172544 zero_bytes=6055 zero_pct=3.51 distinct_values=256/256 mean_abs_delta=115.02
```

- **[Verified]** Low zero-byte fraction (3.51%) and full byte-value range
  used (256/256) — not a sparse, mostly-empty parameter table.
- **[OPEN]** `mean_abs_delta=115.02` is higher than uniformly-random bytes
  would average (~85), i.e. *less* sample-to-sample smoothness than plain
  linear PCM audio typically shows. Two explanations remain open, neither
  ruled out: the payload is genuinely sample data in a non-linear/companded
  format (`es5506_device` has a real `generate_ulaw()` path, so this is
  not far-fetched), or it is largely structured parameter/patch data
  consistent with "DIGI SYN" naming a synthesized rather than sampled
  patch. Not resolved — the ASR-10 instrument file format's internal
  layout was not available to decode definitively this task.
- **Decision:** the low zero-fraction and full byte-range rule out the
  "barely any sample data, pick a different instrument" branch the task
  asked to check for — this is not a degenerate/empty file by either
  measure, so the existing instrument was kept rather than switched.

### Full root directory (all 40 slots, non-empty entries)

Dumped for completeness and to have a record of what else is on this
disk, in case a future task needs a different instrument for comparison:
29 named entries (`ASR-10 OS`, `TUTORIAL BNK`, `JM DIGI SYN`, `JM DRUMS`,
`DEMO PERCS`, `MOOG POP 1`, `HIGH STRINGS`, `JM CLAV`, `OB-8*`, three
`TUTORIAL`/`ATRK` sequence/song files, several drum/bass/organ patches,
and a block of `!44`-prefixed effects presets) plus one further entry at
index 39. Per-entry field semantics beyond the name remain undecoded
(`disk-read-path.md`'s scope, not extended here) — raw bytes only,
reported in the script's own output, not reproduced field-by-field here.

## Del 2 — The Real Idiom (Searched, Reported Honestly)

Searched `esq5505.cpp`, `esqkt.cpp` (both explicitly named), and, since
the search widened beyond just those two: `esqasr.cpp` — **upstream
MAME's own ASR-10 skeleton driver**, and the whole tree for any
ES5506/ES5505 driver using `.share()` between CPU RAM and a chip's own
bank address space.

**Result: no such idiom exists anywhere in this tree, including in
upstream's own ASR-10 driver.**

- `esq5505.cpp` / `esqkt.cpp`: `set_region0()`..`set_region3()` with real
  `ROM_REGION` sample ROM data (`waverom`..`waverom4`) — these are
  ROM-resident factory-sound instruments, not RAM samplers; their
  `.share()` calls are for `osram`/`seqram` (CPU-only OS/sequencer RAM),
  never for the ES5506/5505's own bank space.
- **`esqasr.cpp`** (`grep`-confirmed: no `.share()`, no `set_addrmap()`
  for any ES5506 bank) uses `ROM_REGION(0x200000, "waverom",
  ROMREGION_ERASE00)` — a **static, zero-initialized placeholder region**,
  for all four banks, on both `asr10` and `asrx`. This is upstream's own
  incomplete answer to "how does ASR-10 sample RAM work": it doesn't
  implement it either, it substitutes an erased ROM stand-in. This
  matches the same private, unshared shape `asr10_boot.cpp` already had
  before this task, not the `.share()` idiom the task set out to find.
- Tree-wide search: no other MAME driver (checked via grep across
  `src/mame/*/*.cpp`) shares CPU RAM with an ES5506/ES5505 bank. The one
  precedent found for the *mechanism* (a root-relative `.share(":tag")`
  connecting two different devices' address maps) is `sega/fd1089.cpp`,
  an unrelated CPU-decryption use case — proof the syntax and MAME's
  underlying `memory_share` machinery support cross-device sharing in
  general, not a chip-specific precedent.

**This changes what Del 3 is:** not "apply the idiom used elsewhere,"
since none exists, but a deliberate, source-verified new construction —
justified by Del 1's numeric confirmation and by reading MAME's own
`memory_share` size/width/endianness validation
(`src/emu/emumem_aspace.cpp`, `prepare_map_generic()`:
`share->compare()` `fatalerror`s on any mismatch) before writing the
change, not by copying a pattern that turned out not to exist.

## Del 3 — Wired, As One Named Unit

Both changes landed together in one commit, since neither is testable
without the other (silence with no route proves nothing about the RAM
link; a route with unshared RAM proves nothing about the sound).

**1. `.share()` — the named, narrow `mem_map` exception.**

```cpp
// mem_map():
map(0x100000, 0x1fffff).ram().share(":asr10_sample_ram");

// es5506_wavetable_map():
map(0x000000, 0x07ffff).ram().share(":asr10_sample_ram"); // matches mem_map's 1MB exactly
map(0x080000, 0x1fffff).ram();                            // unchanged, private, unshared
```

Verified mechanically sound before writing, not just architecturally
plausible: `es5506_device`'s `m_bank0_config` is `(ENDIANNESS_BIG, 16, 21,
-1)` (`es5506.cpp:185`) — word-addressed, so word range `$000000-$07FFFF`
is exactly `0x100000` bytes, matching `mem_map`'s own declared byte range
in width, size, and endianness (68000-family `AS_PROGRAM` is big-endian,
16-bit). A root-relative tag (`":asr10_sample_ram"`, not a plain
`"asr10_sample_ram"`) is required: `device_t::subtag()`
(`src/emu/device.cpp:892`) prefixes a plain tag with the *owning device's*
own path, so the same plain string in `mem_map` (owned by `m_maincpu`) and
`es5506_wavetable_map` (owned by `m_es5506_host`) would resolve to two
different tags and silently allocate two disconnected memories instead of
one shared block — caught by reading the resolution code before writing
this, not discovered by a failed test.

`es5506_wavetable_map`'s single line had to become two (shared prefix +
private remainder) for the sizes to match — not an unrelated decoding
change; every address that was `.ram()` before is still `.ram()` after,
just split so the shared portion's size is declarable. `mem_map`'s own
line changed by exactly the `.share()` tag, nothing else.

**2. Output routing.**

```cpp
SPEAKER(config, "speaker", 2).front();
es5506_host.add_route(0, "speaker", 1.0, 0);
es5506_host.add_route(1, "speaker", 1.0, 1);
```

`set_channels()` left unset (falls back to 1 in `device_start()`,
producing one L/R pair) — sufficient to answer "is anything audible at
all," per instruction not to touch bank switching or anything beyond this
one named unit. Clock (`XTAL(16'000'000)`) and bank routing untouched, per
instruction.

Build succeeded; `docs/asr10/regression-test.sh`: 6/6 both before and
after landing this change.

## Del 4 — Measured, Not Listened To

`docs/asr10/lua/archive/wavwrite-capture.lua`: boots, loads `JM DIGI SYN`
(`0A 23 02`), then idles for 8 further seconds with **no button/key
press** (per instruction — no keyboard model exists to press one with),
captured via `-wavwrite`.

```text
channels=3 sampwidth=2 framerate=48000 nframes=1429441 duration=29.780s
total_samples=4288323 peak=0 rms=0.00 nonzero_samples=0 nonzero_pct=0.0000
```

Per-second breakdown (`t=0s` through `t=28s`): `peak=0 rms=0.00` at
**every single second**, from before `FILE 1` through 8 seconds past
`FILE LOADED`.

- **[Verified]** Complete digital silence — not just quiet, exactly zero
  amplitude in every sample, the whole capture.
- **Per instruction, this is not treated as a failure.** It is the
  expected result of two already-established, unresolved facts, not a
  new problem: (1) no note is ever triggered — `asr10panel_device` has no
  key-press ioport, so nothing ever gates a voice on; (2) the periodic
  voice-register sweep (`sample-ram-and-voice-registers.md`) leaves all
  32 voices in their default/idle configuration throughout — it was never
  expected to produce sound on its own, only to be ready to if a note
  arrived. The `.share()`/`add_route` wiring from Del 3 cannot be judged
  audibly without a note-trigger to test it with.
- **[OPEN, minor]** The capture reports 3 channels, not the 2 this driver
  routes — likely `-wavwrite` mixing in another (silent) sound device's
  channel count from elsewhere in the system. Did not affect the
  all-zero finding (every channel, every sample, zero) and was not
  investigated further — out of scope for this task's question.
- **Conclusion:** this run does not confirm or refute whether the
  `.share()`/output wiring will produce correct audio once a note is
  triggered — it confirms the *absence of spurious noise* (no garbage,
  no artifacts, no crash), which is consistent with the wiring being
  structurally correct and simply never being asked to play anything.
  The next real test of this wiring requires keyboard input to exist
  first, exactly as sketched in `sample-ram-and-voice-registers.md`'s Del
  4.

## Verification

- `docs/asr10/regression-test.sh`: 6/6, before and after Del 3's C++
  change (the only code change this task).
- **`.share()` is the named, narrow `mem_map` exception** this task's
  standing rule allows: it adds and removes no decoding, only names an
  existing allocation so a second address space can reference it. No
  other `mem_map` line changed.
- Clock, bank switching (`get_bank()`/bank 1-3 wiring), and ES5510
  untouched, per instruction.
- `src/devices/sound/es5506.h`/`.cpp` read only, not modified, not
  staged, not committed (pre-existing unrelated dirty file excluded from
  every commit this session).
- No `-log`. `-wavwrite` used for Del 4, as explicitly permitted.
- `git diff --check`: clean.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: `mem_map()` +1 line changed (tag
  added), `es5506_wavetable_map()` 1 line → 2 lines (shared/private
  split), machine_config +12 lines (`SPEAKER` + 2 `add_route` + comment),
  +1 `#include "speaker.h"`. No lines removed — nothing in this driver
  became dead code as a result.
- `docs/asr10/lua/archive/sample-topology-and-payload-probe.lua`: written,
  run, archived. Hit and fixed the same BAR-reinstall tap-drop trap
  (`methods-static-analysis.md` #8.5) this project has already documented
  once — installing the IDMA tap before `FILE 1` silently zeroed it;
  moved to install after, matching `file-loaded-verification-probe.lua`'s
  own established fix.
- `docs/asr10/lua/archive/wavwrite-capture.lua`: written, run, archived.
