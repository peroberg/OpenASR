# Diagnostic menu found by testimony; the runtime sequence object found and read-audited; a note-field address correction

Six-part task: audit prior execution findings for a CPU-prefetch confound
(§8.10), re-investigate `$00A304` as data instead of code, follow
`TUTORIAL SEQ`'s IDMA load to a runtime object and characterize it, find
who reads that object, verify `$D08`/`$D11` across multiple points, and
resolve the diagnostic menu by asking Per directly instead of measuring a
fifth time.

## Del 1 — prefetch audit

`$782A`'s self-corrected 83Hz false positive
(`note-velocity-structure-and-sequencer-silence.md`) is now §8.10 in
`reference/methods-static-analysis.md`, with `asr10_taps.lua`'s
`pc_correlated_read_tap()`/`M.pc()` making PC-correlation the default
path (this tooling was already in place going into this task).

Audited every positive execution claim in the project for documented
PC-correlation. Two findings, both in `tempo-clock-consumer-chain.md`,
have no PC-correlation on record (checked directly: zero `PC`/`CURPC`
references in that file) and are downgraded in `current-status.md`
to `[OPEN, prefetch-osäkert]`:

- `$00E66E`'s `~83Hz` clock-division-stage figure and `$0073A8`'s
  positive-execution claim. Note: `$00E66E` is a `jsr` target
  (`$007822: jsr $e66e.l`), not reached by falling through past an
  unconditional branch the way `$782A` was — structurally less exposed
  to the specific adjacent-branch pattern, but that is a plausibility
  argument, not a measurement, and SS8.10's criterion is documented
  correlation.
- Type `$0E`'s dispatch table entry: the `$F8F2FA`/Slot 3 leg stays
  `[Verified]` (trap-based, register-captured, not exposed to
  instruction-prefetch false positives), but the "`jmp (a0)` lands near
  `$006014`" leg (already self-flagged "not fully resolved" in
  `execution-traced-clock-and-sequencer-stepper.md`) has no
  PC-correlation and is downgraded.

Not touched, per the task's explicit protection: `$F8C588` and
`$00A304`'s existing zero-result findings (both live-witnessed
negatives, not positive execution claims).

## Del 2 — `$00A304` re-examined as data

The premise this task was handed ("$00A304 is a data record, not an
instruction") does not survive contact with the disassembly already on
record in `slot5-connects-notes-to-voice-programming.md`: `$00A304` is
the opcode address of `sub.l #$101c,d0`, one instruction inside a
coherent, sensible bounds-check routine (`$00A2FA`-`$00A31E`) — a
`move.l`/`lsr.w`/`swap`/`lsr.l`/`sub.l`/`bmi.b`/`cmp.l`/`bcc.b`
sequence that decodes cleanly as real code, not data misread as
instructions. The actual "32-bit reference" the earlier round found was
a byte-pattern match for the numeric value `$0000101C` (which equals
`GPR MONITOR`'s own ROM string address) embedded as that `sub.l`'s
immediate operand — coincidental placement inside code, not a data
table entry.

Tested the question the task actually wants regardless: does *any* code
read `$00A304` (or its `jsr` target `$8C72`, already flagged in
`current-status.md` as "reads as repeating table data, not code") as a
plain 32-bit **data** value, independent of `$00A2FA` ever executing as
code? PC-correlated read-taps on both `$00A304-$00A307` and
`$008C72-$008C75`, with a continuously-growing write-tap control on
`$000B6E` (1227→7857 across the run, proving the tap mechanism was live
through the whole window, not just at start) — **zero hits on both**
across boot, a 12-step Command-mode category sweep, and navigating into
and entering `GPR MONITOR`. `[Verified, negative]`: nobody reads
`$00A304` or `$8C72` as data in any state reached. Independent of, and
consistent with, the pre-existing zero-result for `$00A2FA` never
executing as code.

## Del 3 — the runtime sequence object, found

Loading `TUTORIAL SEQ` (`$15` then `$23`) does **not** use the MC68302
IDMA path the instrument-bank load uses. First attempt taps the
`$FC6800-$FC681F` SIB register window from script start and finds
literal zero writes through the whole load — this is §8.5's own trap
(`install_internal_window()` tears down and reinstalls that window on
every BAR write, silently killing a tap installed before boot).
Reinstalling the identical tap *after* boot settles (the
already-documented fix) immediately catches real traffic: two IDMA
arms.

- Arm 1: `SAPR=$FFFC5803`, `DAPR=$00000944`, `BCR=$0201` (513 bytes) —
  the same staging-buffer address already measured for the instrument
  bank load. Content here is a regular incrementing 24-bit sequence
  (`$0009F7, $0009F8, ... $000A16`), not sequence-shaped — almost
  certainly the reused scratch buffer already flagged in
  `file-loaded-verification-probe.md`, not real payload.
- Arm 2: `SAPR=$FFFC5803` (unchanged — consistent with an FDC data-port
  source register, not an incrementing memory pointer), `DAPR=$0062B242`,
  `BCR=$1A01` (6657 bytes).

`$0062B242` is real, backed memory: it reads back byte-identical whether
addressed as `$62B242` or `$02B242` (this model's RAM decode aliases the
two — only the low ~21 bits matter here), and the sample-RAM alias
(`$12B242`) instead shows the ES5506 wavetable's constant `$04AC`
filler, ruling out a sample-RAM coincidence.

**The object itself, dumped 2048+ bytes from `$02B242`:**

```
+0000  90 00 00 10 [T.U.T.O.R.I.A.L. .S.E.Q. each char + $FF pad]
+001E  00 04 00 22  00 5A 06 60  00 00 28 60  00 00 08 C0
       00 00 4E 40  00 00 7F 20  00 00 5F E0  00 10 96 00
+0040  (zero padding to +0066)
```

Type byte `$90`, then the sequence's own name `"TUTORIAL SEQ"` stored
as char+`$FF` pairs, then header words including a byte `$5A`
(=90=`$000B70`'s own tempo constant, the value `$17`/"Create New
Sequence" is separately known to set — the same number recurring in an
unrelated field is exactly the kind of coincidence this project has
been burned by before, so this is a lead, not a claim). Past the header,
the body is organized into repeated 4-byte records grouped into
sections, each terminated by a distinctive `80/81/82 E9 xx E0 00 00 00
00 00 00` marker (`xx` varies: `1F`, `25`, `30` across three sections
seen in the first 2048 bytes) — consistent with per-track event
streams, though which byte in each 4-byte record is delta-time vs.
status vs. note vs. velocity is **not decoded this task** — no semantic
label is claimed for any specific byte position without direct support,
per this project's own rule. Note-like byte values observed cluster
mostly in `$15`-`$8F`, overlapping but not confined to either the
`$00`-`$3F` or MIDI `$00`-`$7F` hypothesis — `[OPEN]`.

`[Verified]`: runtime object exists at `$0062B242`/`$02B242` (alias),
size ≥ 2048 bytes (no terminator found within that window — true extent
unmeasured beyond it), contains the sequence's own name and
header-shaped fields including a tempo-matching byte, followed by a
long structured event-shaped body. `[OPEN]`: exact event-field
semantics, full object size, and whether `$5A`'s recurrence is
meaningful or coincidental.

## Del 4 — who reads the object

Tapped `$02B242`/`$62B242`, 4096 bytes each (both alias forms, so
either addressing convention would be caught), PC-correlated, with a
continuously-growing `$000B6E` write-tap control (2973→24313 across the
run) proving the tap stayed live through every state tested, not just
at the start.

**77 reads total, by 13 distinct PCs** (`F906D6`×6, `F8A592`×4,
`F92306`×2, `F8A47C`×4, **`F92464`×40**, `F924C8`×1, `F87DF6`×4,
`F8A544`×4, `011E46`×2, `F924B0`×1, `F91708`×2, `F924EE`×1,
`F907B4`×6) — **all 77 occur during the load itself**, between pressing
`$23` and `"DISK COMMAND COMPLETED"` appearing. `$F92464` dominates
(40 of 77 hits), suggestive of a header-parsing or checksum loop, not
chased further this task.

**Zero further reads** across every post-load state tried: 3 seconds
idle, a 40-step Command-mode category sweep, entering `GPR MONITOR`,
15 steps of Seq•Song category paging, and pressing each of the three
untested transport-candidate codes (`$22`, `$24`, `$25`).
`[Verified, negative, scoped]`: no ongoing/playback-shaped consumption
of the object was found in any of these reached states. This does not
rule out a state not reached (Record/Stop•Continue/Play remain `[OPEN]`
project-wide), and the 13 load-time PCs are a concrete, unchased lead
for a future task — the object *is* read, just only by the loader
finishing its own job, not by anything that looks like an ongoing
player.

## Del 5 — `$D08`/`$D11` multi-point verification finds an address error

Panel-key velocity cannot be varied at all: `esqpanel.cpp`'s
`key_change()` hardcodes `KEY_VELOCITY=100` for every press
unconditionally (a documented simplification — a plain computer
keyboard has no pressure sensor). Testing real multiple velocities
needs MIDI input through the driver's own `mdin`/DUART-A path, not
attempted (no C++ change was in scope this task).

Note number **could** be swept, across five points (`KEY_C`, `KEY_C2`,
`KEY_A` at the default octave, `KEY_C` after one octave-up, `KEY_C2`
after two octave-ups). A first pass, delayed byte-polling (~15-150ms
after press), read `$D08=100` correctly every time but `$D11=0` every
time regardless of key or octave — including combinations where the
prior round's own formula predicts a nonzero value. A word-granular,
PC-correlated write-tap over the entire `$D00`-`$D1E` region resolved
this immediately: `$0171B4` writes velocity and note as **one 16-bit
word to `$000D08`** (high byte = velocity, low byte = note) —
`$000D09` is the note byte, and `$000D11` **is never written by any key
press in this session.** A second copy of the same note value lands
separately at `$000D0C` (word, written by `$0171BE`, not `$017276` as
previously recorded).

| key | octave | data@$D08 (word) | velocity | note ($D09) |
|---|---|---|---|---|
| `KEY_C` | 2 (default) | `$643C` | 100 | 60 |
| `KEY_C2` | 2 | `$6448` | 100 | 72 |
| `KEY_A` | 2 | `$6445` | 100 | 69 |
| `KEY_C` | 3 | `$6448` | 100 | 72 |
| `KEY_C2` | 4 | `$6460` | 100 | 96 |

All five points fit exactly: `note = min(m_octave*12+note_offset, 60) +
36` (`m_octave` defaults to `2`, `esqpanel.h:177`; the `min(...,60)`
pre-clamp and the `+36` are both `esqpanel.cpp`'s own arithmetic, read
directly, not inferred). Velocity stays exactly `100` at all five
points, confirming that half of the original claim.

`[DISPROVEN → corrected]`: the prior round's `$D11=$3C, writer $017276`
claim. `[Verified, multi-point]`: `$D08` (velocity, always 100 through
this input path) and `$D09` (note, formula-exact across 5 points), both
written by `$0171B4`; `$D0C` carries a redundant copy of the note value,
written by `$0171BE`. `current-status.md` and
`DOCUMENTATION-MANIFEST.md` both corrected in place, with the original
claim kept and struck through per this project's own revision-trail
convention rather than silently rewritten.

## Del 6 — the diagnostic menu, found by asking

Four prior rounds' worth of measurement (button sweeps, prefix
isolation, hold-combinations, correlated PC/state tracking) never
reached the diagnostic menu, while Per had personally seen it run.
Asked directly instead of measuring a fifth time. His answer —
"testa 0D eller 0B efter CMD (06)?" — was exactly it: `$06` then `$0D`
(or `$0B`, a second valid entry point into the same category) reveals
an 11-entry category, confirmed cycling cleanly in both directions:

```
NO COMMANDS ON PAGE   CALIBRATE KEYBOARD    SOFTWARE INFORMATION
EXAMINE DOS STATUS    EXAMINE ANALOG INPUTS GPR MONITOR
INSTRUCTION MONITOR   ESP TESTS             A/D TO D/A
DC OFFSET             MIDI LOOP             (wraps)
```

Every entry verbatim-matches the ROM string table found via static
search two rounds ago. All 11 were entered (Enter/`$23`, with a
60-second background-process hang guard on each — none hung):

| entry | display after Enter |
|---|---|
| CALIBRATE KEYBOARD | `KEYBOARD TUNED` |
| SOFTWARE INFORMATION | `RAM VERSION??5?` |
| EXAMINE DOS STATUS | `DOS STATUS??` |
| EXAMINE ANALOG INPUTS | `PITCHWHL 64` |
| GPR MONITOR | `READ GPR??? ??????` (parameter-entry prompt) |
| INSTRUCTION MONITOR | `READ ?? ????????????` |
| ESP TESTS | `COUNT?????` |
| A/D TO D/A | `HIT CANCEL TO ABORT` |
| DC OFFSET | `L??????? R???????` |
| MIDI LOOP | `PASS?? FAIL?2` → `PASS?? FAIL?7` (live counter, still counting at settle) |

`?` glyphs are this project's established transcription convention for
VFD control codes the decoder doesn't map, not literal question marks.
`MIDI LOOP`'s counter changing between the two captures is itself a
finding — it is a live, running test loop, not a static screen. No
sub-navigation past each entry's first screen was attempted this task
(e.g. `GPR MONITOR`'s own register-number entry) — a concrete next
step.

## Summary

- **Del 1**: §8.10 audited against the project's own positive-execution
  findings; two (`$00E66E`/`$0073A8`, and type `$0E`'s `$006014`
  landing leg) downgraded to `[OPEN, prefetch-osäkert]`. Zero-result
  findings untouched, as required.
- **Del 2**: `$00A304` confirmed a real instruction, not data — the
  task's own premise corrected. Live-witnessed negative: nobody reads
  it, or `$8C72`, as data either.
- **Del 3**: the runtime sequence object found — `$0062B242`/`$02B242`
  (alias), name+header+event-shaped body, ≥2048 bytes, exact event
  format `[OPEN]`.
- **Del 4**: object is read 77 times by 13 PCs, all during the load
  itself; zero reads in every post-load state tried, live-witnessed.
- **Del 5**: `$D08`(velocity)/`$D09`(note, not `$D11`) verified exactly
  across 5 points; the earlier `$D11` claim corrected project-wide.
- **Del 6**: diagnostic menu found (`$06`+`$0D`/`$0B`), all 11 entries
  catalogued, none hang.

## Deletion accounting

No C++ changed. No scratch Lua scripts committed (all exploratory
scripts for this task lived in the session scratchpad, per the
project's own instrumentation-is-deleted-when-done rule — nothing to
delete from the tree because nothing exploratory was added to it).
Documentation additions: this file, a `current-status.md` summary
section plus one correction block, a `DOCUMENTATION-MANIFEST.md`
correction note, and `reference/methods-static-analysis.md`'s §8.10
(already present from tooling work earlier in this task, deduplicated
here).
