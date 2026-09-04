# The Keyboard, And What Pressing A Key Actually Does

## Scope

The consolidation (`mc68302-consolidation.md`/`-2.md`) is green, 7/7,
fault-injection tested. This task builds the keyboard — needed to play
the machine, and doubled as the cheapest available probe for the open
sample-bridge question (voice 0 pointing at 256KB of zeros). Two quick
closures first, then the keyboard, then what a key press actually does.

## Del 1 — Two Closures

### 1. Vectors 4 and 11: resolved, in the direction the user predicted

First check: are the table values plausible code addresses at all —
even, in ROM or RAM, instruction-like content?

```text
V411_BYTES vector=4  addr=FC6000 bytes=46 FC 27 00 01 88 00 04 54 AF 00 02 4A 94 01 88
V411_BYTES vector=11 addr=FC6014 bytes=46 FC 27 00 01 C8 00 18 54 AF 00 02 4A 94 01 C8
V411_BYTES vector=3  addr=F882AE bytes=70 81 60 CE 70 83 60 CA 70 82 60 C6 70 84 60 C2  (reference: known-real handler)
```

Both addresses are even. The content is *not* obviously garbage —
`$46FC $2700` decodes as `MOVE #$2700,SR` (supervisor mode, interrupt
mask 7), a completely textbook exception-handler prologue. Content
analysis alone was **ambiguous**, not decisive — real-looking opcodes at
an address don't prove the CPU actually executes from there for the
reason the vector table claims.

**Decisive check**: correlate every tap hit with the CPU's own PC at that
instant.

```text
vector 4:  every hit -> PC=$000010 (vector 4's OWN table slot, not $FC6000)
vector 11: alternates -> PC=$FC6014 (true) / PC=$00002C (vector 11's own table slot)
```

Vector 4's "handler execution" never happens — every single hit shows
PC=`$000010`, the low-RAM table slot itself, reading `$FC6000` as *data*
from unrelated code, not executing there. Vector 11 is subtler: the CPU
genuinely does execute real code at `$FC6014` sometimes (`PC==$FC6014`,
true instruction fetches) — but it's called *from* `$00002C`, vector 11's
own slot, which is itself ordinary low-RAM code doing an unrelated
subroutine call. Neither address is reached via actual CPU exception
dispatch in either case.

**Answer: neither vector 4 nor vector 11 has a dedicated, installed
exception handler.** Their table slots have been overwritten by ordinary
firmware low-RAM code/data reuse — the same phenomenon that made the
original blanket vector-table tap unreliable
(`mc68302-consolidation-2.md`), now localized to exactly these two
slots specifically, with proof rather than suspicion. Consequence, as the
task asked to note: **illegal instruction and line-1111-emulator have no
real destination on this machine as currently booted.** A genuine
exception of either type would jump into whatever transient data or code
firmware last left at `$000010`/`$00002C` — undefined, not merely
"unhandled." `[OPEN]` closes in this direction, not the ambiguous one.

### 2. Catch-all RAM: corrected arithmetic

The previous task's "~680KB untouched" was wrong — an arithmetic slip
composing the summary, not a bad measurement. Shown explicitly this time:

```text
range $FC5020-$FFFFFF = 241,632 bytes = 236.0 KB   (not megabytes)
minus SIB window (4096 bytes)        = 237,536 bytes = 232.0 KB
9 touched 4KB buckets                =  36,864 bytes =  36.0 KB
untouched                            = 200,672 bytes ≈ 196.0 KB
```

**[Verified]** ~196KB untouched, not ~680KB. The qualitative finding
stands unchanged (only the top ~36KB, matching supervisor stack/OS
variables, is ever touched) — only the arithmetic was wrong, corrected
here explicitly per instruction.

## Del 2 — The Keyboard

`asr10panel_device` (`esqpanel.h`/`.cpp`) gains `key_change` and
`octave_change` `INPUT_CHANGED_MEMBER`s, wired to the base class's
already-existing `key_down()`/`key_up()`/`key_pressure()`. No new
protocol invented — reused the established mechanism, per instruction.

- **61 keys, key numbers `$00-$3F`.** One computer-keyboard octave (13
  semitones, `Z`=C through `,`=C of the next octave — the standard
  "music typing" QWERTY layout) plus octave shift on `-`/`=`. Octave
  range 0-4 (5 positions) × 13-note layout spans key numbers 0-60 exactly
  (`octave*12` gives `{0,12,24,36,48}`, each overlapping the next by the
  shared C), covering all 61 keys with no gaps and no key number ever
  exceeding `$3F`.
- **Fixed velocity (100), explicitly a simplification** — a plain
  computer keyboard has no velocity/pressure input to model.
- **Mid-hold octave-shift safety**: `m_key_number_for_offset[13]` records
  which absolute key number `key_down()` actually sent for each held
  computer key, so `key_up()` releases the *same* key even if the octave
  changed while it was held — without this, a mid-hold shift would leave
  the original note stuck on (release computed from the *new* octave
  would never match the key that was actually turned on).
- **Playability prioritized over layout drawing**, per instruction —
  `asr10_boot.lh` (the visual panel layout) was not touched.

### `XMIT_RING_SIZE` overflow: measured, not assumed, then fixed

`xmit_char()` (`esqpanel.cpp`, shared base class) had no overflow check
at all. Measured under a deliberate worst case — all 13 note keys
pressed as close to simultaneously as a scripted test can manage (one
port write setting all 13 bits, matching what fast/chorded playing could
produce):

```text
XROS_RESULT expected_bytes=52 received_bytes=20 lost=32
```

**Real spillage, not theoretical** — 32 of 52 expected bytes lost, before
any fix. Per the consolidation's own principle, made loud rather than
left silent: `xmit_char()` now checks whether the ring is full before
writing, drops (rather than silently overwriting unconsumed data) and
counts the overflow. **Loud required a second fix of its own**:
`logerror()` alone is not observable under this project's own
constraints — its callback is only registered when `-log` is passed
(`src/emu/machine.cpp:289`, checked directly, not assumed), and `-log`
is forbidden here. Added `osd_printf_error()` alongside it, which prints
unconditionally (confirmed: this project's own captured run output
already showed unconditional osd-level messages, e.g. MAME's
`install_read_tap` range error from an earlier task, with no `-log` in
play). Re-measured after the fix:

```text
esqpanel: XMIT_RING_SIZE overflow (count=1), dropping byte a0
...
esqpanel: XMIT_RING_SIZE overflow (count=20), dropping byte ...
XROS_RESULT expected_bytes=52 received_bytes=32 lost=20
```

The C++ overflow counter (20) matches the Lua-measured byte deficit (20)
exactly — cross-validated, not just internally consistent. Fewer bytes
are lost now than before the fix (20 vs. 32) because dropping cleanly
stops one overflow from corrupting multiple already-queued bytes, unlike
the silent overwrite it replaced.

## Del 3 — What A Key Press Actually Does

After `FILE LOADED`, pressed `KEY_C` (default octave 2, key number 24),
measuring panel reception, ES5506 voice writes (all 32 voices, not just
0/1), `$100000-$1FFFFF` writes, and the ROM voice-management table
(`$8000`, `instrument-to-otto-runtime.md`'s own boundary) simultaneously:

```text
KPRP_DELTAS rhrb=4 voice_writes=0 samram_writes=0 witness_writes=75199 voicetable_writes=0 display_changed=false
```

- **[Verified]** 4 RHRB bytes consumed — matches `key_down()` (2 bytes)
  + `key_up()` (2 bytes) exactly. Firmware receives the bytes at the
  hardware/serial level.
- **[Verified]** Zero ES5506 register writes (any voice, any field —
  not just voices 0/1), zero `$100000-$1FFFFF` writes, **zero writes to
  the ROM voice-management table at all**. Firmware's own voice
  allocation logic never runs, not just "runs but doesn't reach the
  chip."
- **`-wavwrite` capture across the press**: complete silence (`peak=0`,
  every sample, matching `sample-topology-closure.md`'s own earlier
  finding) — expected given the above, not new information on its own.

### Why: the wire protocol collides with the button protocol

The task's own three anticipated outcomes (audible, silent-with-zeroed-
sample-RAM, silent-with-filled-sample-RAM) all presuppose firmware
*attempts* to process the event as a note. This run shows it doesn't
attempt to at all — a different, more basic outcome than any of the
three, and investigated rather than left unexplained.

`key_down()`'s inherited encoding sends `0x80|(key&0x3f)` then velocity —
**the identical byte range** `esqpanel_device::set_button()` already uses
for ordinary panel buttons (`sendme = (pressed?0x80:0)|(button&0xff)`,
button codes also `0x00-0x3F`). Default-octave `KEY_C` (key=24, `$18`)
sends the same first byte a direct `BTN_18` press would. **Direct test**:
pressed `BTN_18` (an undefined button number — nothing in this whole
project's history has ever shown a defined behavior for it) through the
panel's own real button ioport:

```text
BVK_BTN18_RESULT rhrb_delta=4 changed=false
```

**Identical signature** — 4 RHRB bytes, zero visible reaction. This is
decisive, not merely suggestive: my `KEY_C` press is wire-protocol-
indistinguishable from pressing an unmapped button number, not a note
event. Firmware's dispatch table (per `current-status.md`'s own
documented panel receive path, `$F82484`/`$F89CEA` lookup) evidently has
no defined action for button code `$18`, so it does nothing — exactly
matching what an unmapped button press should do, and exactly explaining
the complete absence of downstream activity.

**Checked whether the real protocol is documented anywhere available**:
`docs/asr10/sources/ASR10_manual.pdf` is the end-user manual (features
and usage), not a service/technical manual — grepped for
protocol-adjacent terms, nothing relevant, as expected for this class of
document. The real ASR-10 note-event byte encoding (whatever channel or
byte range it actually uses — possibly a separate marker byte, a
different value range, or a distinct mechanism entirely from the button
path) is **`[OPEN]`**, genuinely unknown, not guessed at further this
task. Determining it requires tracing firmware's own panel-byte dispatch
table (disassembly), out of scope for this round.

### Which of the task's three outcomes applies

**None of the three, precisely** — the task's own decision tree
(audible / silent-zeroed / silent-filled) presupposes the stimulus
reaches note-processing logic. Voice 0's `$00000/$20000` boot defaults
remain **exactly what they were before this task** (confirmed
unchanged, `KPRP_VOICE0_BEFORE`/`_AFTER` identical) — not because
firmware examined them and left them alone, but because firmware never
got as far as examining any voice at all. The sample-bridge question
(does `$100000-$1FFFFF` ever feed a voice once a note plays) remains
exactly as open as before this task started — this measurement neither
confirms nor refutes it, because the note never reached the code path
that would exercise it. **[OPEN]**, correctly, per the task's own
"undersök, gissa inte bättre" principle: no `mem_map` change, no ES5506
change, no invented protocol — the real blocker identified precisely
(wire-protocol collision) instead of painted over.

### Bank 1

Not applicable this task — no voice was ever programmed by a key press
(zero ES5506 writes of any kind), so there's nothing to report about
which bank a key press activates. Still `[OPEN]`, untouched, exactly as
instructed.

## Del 4 — No Eighth Test

Per instruction: "bara om del 3 visar något reproducerbart — bygg inte
ett test kring ett beteende som ännu inte är förstått." Del 3 found a
reproducible *negative* (zero voice activity from a key press, confirmed
across two independent runs and cross-checked against a direct `BTN_18`
press) but no reproducible *positive* behavior worth locking in — there
is no "key press causes voice register activity" fact to gate a
regression test on, since it doesn't currently happen. Building a test
around the absence would either assert a bug is intended behavior or
break the moment the real protocol is found and fixed. **No eighth test
added.**

**Confirmed instead, as the task's own fallback requires**: the five
consolidation guards stay green with keyboard activity actually exercised
in the run, not just the plain load sequence — ran `mc68302_guards.lua`
with a `KEY_C` press inserted before its own pass/fail check:

```text
PASS mc68302_guards display="FILE L0ADED           " alarms=0
```

Zero alarms with the keyboard wired in and exercised. The existing 7th
test's guards were not fooled by, nor triggered by, the new panel
traffic.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, checked after the keyboard wiring
  (`esqpanel.h`/`.cpp`) and again after the `xmit_char()` overflow fix —
  two separate green checkpoints.
- `mc68302_guards.lua` (all five guards) confirmed green with a key press
  exercised in the same run, per Del 4's fallback requirement.
- No `mem_map` change. No ES5510 change. `es5506.h` not read, not
  modified, not staged.
- No `-log`; `-wavwrite` used for Del 3, as permitted.
- CMR table untouched, stays `[Likely]`.
- Bank 1 untouched, stays `[OPEN]`.
- `git diff --check`: clean.

## Line Count

- `src/mame/ensoniq/esqpanel.h`: +2 `INPUT_CHANGED_MEMBER` declarations,
  +2 members (`m_octave`, `m_key_number_for_offset[13]`), +1 overflow
  counter (`m_xmit_overflow_count`).
- `src/mame/ensoniq/esqpanel.cpp`: +1 ioport block (`keys_0`, 15 fields),
  +2 handler implementations (`key_change`, `octave_change`), `xmit_char()`
  gains an overflow check (net +~15 lines).
- `docs/asr10/lua/archive/`: seven new scripts this task
  (`vector-4-11-content-probe.lua`, `vector-4-11-pc-correlation-probe.lua`,
  `button-vs-key-collision-probe.lua`, `key-press-response-probe.lua`,
  `xmit-ring-overflow-stress.lua`, `keyboard-wavwrite-capture.lua`) —
  written, run, archived.
- No eighth regression test (Del 4's own conditional instruction).
