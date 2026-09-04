# Bank loading as sequencer context; transport still open (2026-08-24)

Follow-on to `panel-button-and-transport-map.md`. That task's own framing
for why Record/Stop•Continue/Play stayed `[OPEN]`: Play may need a loaded
sequence to produce any visible response, and none existed in that
session. This task builds that context first, then re-runs the transport
hunt inside it — with substantially more method than a plain sweep.

## Del 1 — the OS disk catalog and the demo sequence

`strings -a -n 6 floppies/asr10booth/V350.img` enumerates filenames
without reverse-engineering the on-disk directory format:
`ATRK TUT BNK`, `ATRK TUT SNG`, `TUTORIAL BNK`, `TUTORIAL SEQ`,
`TUTORIAL SNG`, and `JM DIGI SYN` (a bank; no `BNK` suffix visible as
contiguous text, likely because its type byte is binary, not ASCII in
that position).

Manual's own file-type definitions (`ASR10_manual.pdf`, pdftotext):
- **Bank file**: a snapshot of Internal Memory's instrument/song
  configuration. Doesn't contain the actual instrument/song data, only
  references to them — loading a bank may prompt for other disks.
- **Song file**: a song and all its related sequences; loading one
  replaces sequencer memory.
- **Single Sequence file**: one sequence; loading one does **not** erase
  existing sequencer data, it's just added.

`TUTORIAL SEQ` is a genuine single-sequence file (confirmed on-device as
`"FILE 9  TUT0RIAL 5EQ  "`), and is the demo sequence Del 1 asked for —
no need to build an empty one from scratch.

**Verbatim load procedure** (reliable across repeated runs once the
settle time is generous — see the timing note below):

| step | press | display after |
|---|---|---|
| 1 | `$15` (Seq•Song category) | `"FILE 9  TUT0RIAL 5EQ  "` |
| 2 | `$23` (Enter•Yes) | `"DI5K C0MMAND C0MPLETED"` |

**Timing note, worth recording so it isn't silently rediscovered**: the
transition to `"DI5K C0MMAND C0MPLETED"` is not instantaneous. A settle
of ~250-300ms after `$23` sometimes still shows the pre-load screen; a
~1000ms+ settle (or explicitly polling with `wait_for_text`/
`wait_until_changed` instead of a fixed sleep) reliably catches it. Not
a bug in the emulation — a real disk-command completion has to actually
finish — but a real trap for any future probe that uses a fixed short
sleep after a disk-affecting button press.

`$15` from idle lands directly on the sequence-file **listing**
(`"FILE 9  TUT0RIAL 5EQ  "`), not on a parameter page with `TEMPO`/
`CLICK`/`COUNTOFF`. The manual's "Edit/Seq•Song page" (reached
automatically by pressing Play while holding Record, per Del 6) appears
to be a different UI layer than this file-listing view; Left/Right
Arrow (`$10`/`$11`, both confirmed in the prior task) were not
separately swept from inside the listing view this round — noted as a
real, bounded gap, not silently skipped.

## Del 2 — correlated probe from the Seq•Song context, bank loaded

Re-ran `panel_correlated_probe.lua` (`docs/asr10/lua/lib/`, reusable,
unmodified) across all 64 codes (`$00`-`$3F`) from the Seq•Song page
with `TUTORIAL SEQ` loaded and instrument slot 1 (`$02`) selected.
Every code returns to the scheduler idle loop cleanly
(`returned_idle=true`, all 64 rows); most produce real, distinct
COMMAND-mode menu navigation (each raw code in `$00`-`$25` selects a
*different*, largely unrelated command-list entry — `EDIT PITCH TABLE`,
`CREATE NEW INSTRUMENT`, `SAVE BANK EFFECT`, `QUANTIZE TRACK`,
`CREATE NEW SEQUENCE`, `FORMAT FLOPPY DISK`, etc. — not a "next" scroll
through one list).

**The two previously-tried candidate state variables never move.**
`$016F` ("mode") and `$0D04` ("state"), both sampled before/after every
press by the existing probe tool, stayed `00`/`0000` across all 64
presses in this context. They are not useful discriminators for
whatever internal state the guard message below depends on — recorded
so a future task doesn't re-try the same two addresses.

**`$26`-`$3F` (26 codes) produce zero display change in every context
tried this round** — idle, deep in the `$00`-`$25` command-menu chain,
and paired with each other in hold-combinations (Del 3). This is a real
negative finding, not silence: either these raw codes are genuinely
unwired button-matrix positions (`~28` unused codes is roughly what the
prior task's own physical-control inventory predicted), or their
function requires a context this task's chosen navigation paths never
reached.

## Del 3 — hunting the transport, honestly not found

**A real, reproducible, non-transport state change was isolated.**
Sequentially pressing `$00`-`$1F` after loading `TUTORIAL SEQ` reliably
lands on `"CREATE NEW 5EQUENCE   "` and sets an internal guard: `$20`
(Sample•Source Select) responds `"5T0P 5EQUENCER FIR5T  "` instead of
its normal `"REC 5RC?INPUTDRY LEFT "`. Binary-style prefix isolation
(pressing only the first *k* buttons of that chain, `k=1..32`, fresh
boot each time, then probing with `$20`) pins the trigger exactly:
guard is **off** for `k≤23` (last code pressed `$16`) and **on** for
`k≥24` (last code pressed `$17`), reproducible at every one of the 32
tested prefix lengths. **`$17`, in this specific 23-button preceding
navigation context, is what sets the guard** — not a guess, a measured
single-button isolation with verified necessary context.

This is very likely the manual's own "Create New Sequence" **command**
(matches `button-routine-sweep-v350.csv`'s independent finding: `$17`
from idle shows `"CREATE NEW 5EQUENCE   "` too) having the side effect
of blocking Sample•Source Select while an unsaved sequence is open —
not a dedicated transport button. A full memory diff (`$000000`-
`$01FFFF`, snapshotted immediately before/after the isolated `$17`
press) shows **211 changed bytes**, consistent with a real sequence
object being allocated, not a single flag flip. Two candidate
boolean-looking bytes inside that diff (`$000D0C`, `$000D0F`, both
`00`→`01`) were tested directly across repeated fresh-boot trials and
**ruled out**: their value drifts (`02`→`01` etc.) even when no
candidate button is pressed at all — free-running counters, not the
guard's flag.

**Systematic search for Record/Stop•Continue/Play, this round's
methods, all negative:**

1. **Isolated per-candidate sweep** (fresh boot per candidate, avoiding
   the confound of an unverified "back out" button contaminating later
   trials in a shared-boot sweep): all 64 codes tried as a
   guard-clearing candidate from the confirmed active state. Only
   `$03`, `$17`, `$20`, `$23` change the guard result; none produce the
   manual's own confirmation text `"XXX BARS - KEEP TRACK?"` (the
   task's explicit, unambiguous answer key for Stop•Continue). `$26`-
   `$3F`: zero effect, all 26.
2. **Hold-combination state machine**, exactly as instructed (A down, B
   down while A held, B up, A up): all 650 ordered pairs within
   `$26`-`$3F` (every code with zero single-press effect in every
   context tried — the strongest "silent until combined" candidates)
   from a loaded-sequence, instrument-selected context. **Zero display
   changes** across all 650 trials.
3. A second, narrower hold-combo sweep (110 ordered pairs) among
   `$00`-`$1F` codes that showed no single-press display change from
   idle (excluding the already-identified `$02`/`$22`/`$23`): one
   incidental navigational artifact (`$13`+`$01` → `"N0 5UCH FILE"`),
   no transport signature.
4. Correlated PC/state probe (Del 2, above): the two tracked state
   variables never move; not useful.
5. Memory diff across the one confirmed real state transition found
   this round (`$17`'s guard-setting write): 211 bytes, matches
   "allocated a sequence object," not a flag; the two flag-shaped bytes
   inside it are unrelated counters.

**Verdict: Record, Stop•Continue, and Play remain `[OPEN]`.** This is a
materially wider negative than the prior task's — that one tried
single-press sweeps and one hold-combo pair set from idle; this one
adds bank-loaded context, prefix isolation, a 650-pair hold-combo
sweep, a second 110-pair sweep, correlated state-variable tracking, and
a full memory diff. No code is named as a transport button without its
effect being measured, and none of these methods measured that effect
for any candidate. Two live possibilities, neither confirmed: the
combination this task tried is still not the right one (a mode button
— Load/Command/Edit, all three still `[OPEN]` — may need to be held
too, or a specific screen context neither this nor the prior task
reached), or Record/Play require analog audio/MIDI input this
`-sound none -video none` harness cannot produce (the manual ties
recording to actually playing notes or receiving MIDI clock, which this
investigation's button-only sweeps never supply).

## Del 4 — host keybindings, mnemonics only where measured

`$10`/`$11` were already bound to `KEYCODE_LEFT`/`KEYCODE_RIGHT` by the
prior task (`partial-update-position-probe.md` Del 5) — nothing to
redo there. This task adds mnemonic letters, per the proposed scheme
(`l`=Load, `c`=Command, `e`=Edit, `s`=Sample, `p`=Play, `f`=Effects,
`q`=Sequence), **only** to codes with a measured function:

- `$15` → `KEYCODE_Q` ("Sequence" — the Seq•Song category button,
  measured this task: reliably shows the sequence-file listing).
- `$20` → `KEYCODE_S` ("Sample" — Sample•Source Select, measured prior
  task).

No other letter is assigned: Load/Command/Edit mode buttons, FX
Select•FX Bypass (`$07`, only `[Likely]` in the prior task, not fully
confirmed), and Play (transport, `[OPEN]`) all stay click-only.

**Collision, documented rather than silently avoided**: `KEYCODE_S` is
already bound to `KEY_Cs` (the C-sharp note) on the note-typing
keyboard in the same device. Pressing `S` now fires both the C# note
and Sample•Source Select simultaneously — a real ergonomic conflict for
anyone typing notes on the host keyboard while `BTN_20` is also bound.
Implemented as specified (letters are keyed to measured function, not
to a collision-free layout) and called out explicitly here and in the
code comment, rather than quietly picking a different key.
`esqpanel_device`'s own base-class protocol was not touched — the
change is scoped to `asr10panel_device`'s own ioport table, same
pattern as the prior task's Del 3. Build verified clean:
`make -j12 SOURCES=src/mame/ensoniq/asr10_boot.cpp,src/mame/ensoniq/esqpanel.cpp`.

## Del 5 — LOAD's blink, measured at the code level this time

Prior task's finding ("zero display-channel traffic during a 3-second
idle window") was a wire-level, serial-traffic-only measurement. This
task checks the actual device state directly, and checks whether a
blink mechanism exists at all.

**Source-level fact, not a runtime guess**: `asr10panel_device` (the
class `asr10booth` actually uses) has **no blink implementation at
all** — no `m_blink_timer`, no `update_blink()`, no `m_light_states`.
A sibling class in the same file, `esqpanel2x40_vfx_device` (used by
other, non-ASR-10 Ensoniq machines sharing the `esqpanel_device` base),
**does** implement blinking, and its design directly answers two of
Del 5's three questions for that protocol family:

- **Not a two-state on/off bit.** Each light has a 2-bit state
  (`m_light_states[i]`): `2` = solid on, `3` = blinking. Blinking is a
  **separate annunciator attribute**, not a redefinition of "on."
- **Panel-local timer, not firmware retransmission.** `update_blink()`
  runs off a device-owned `emu_timer` (250ms period, so 500ms on/500ms
  off — a 1Hz full cycle) that is armed unconditionally in
  `device_reset()`, not in response to any particular firmware command
  in this implementation. This matches "panel-local" over "firmware
  sends recurring control."

`src/mame/layout/asr10_panel.lay` has no blink/flash/animate logic
either — confirmed by grep, not assumed.

**Live-witnessed runtime check** (not just an absence-of-traffic
argument): polled all 40 `asr10_annbit%u` outputs at 50ms resolution
for 6 seconds (120 samples) at idle `"FILE 1  TUTORIAL BNK"` (LOAD
flashing per the manual). **Zero changes across all 120 samples, all
40 bits.** A live witness through the whole window, per this project's
own tap-reliability rule — not a single all-quiet run trusted blind.

**Measured toggle frequency for ASR-10: 0 Hz.** Not because real
hardware doesn't blink — the manual is explicit that it does, at
roughly the rate this task was asked to expect — but because
`asr10panel_device` implements no blink mechanism whatsoever, at any
level (device state or serial wire). The sibling class's real,
working implementation is the strongest available evidence for *which*
of Del 5's interpretations the actual protocol likely follows (separate
attribute + panel-local timer), but it is evidence from a different
device class, not a direct ASR-10 measurement — reported as such, not
overstated. Not implemented for `asr10panel_device` this task: which
specific annunciator bit(s) LOAD/INST/STOP occupy is still `[OPEN]`,
and building a blink timer without that would be guessing the exact
mechanism, which the task's own rules forbid.

## Del 6 — the manual's procedure, verbatim, where it stops

1. Select instrument: press `$02` (Instrument•Sequence Track 1,
   confirmed). Display: unchanged from whatever screen preceded it —
   selection is silent, consistent with `button-routine-sweep-v350.csv`'s
   `display_changed=false` for `$02`.
2. **While holding Record, press Play.** — **`[OPEN]`.** Despite this
   task's substantially wider search (Del 3), Record and Play remain
   unidentified. The procedure cannot proceed past this exact point,
   same blocker as `panel-button-and-transport-map.md`, now with much
   broader negative evidence behind it rather than an unchanged claim.

## Del 7 — summary

**Extended button-to-function table** (only new/changed rows; see
`panel-keymap.md` for the full 64-code table):

| code | function | evidence |
|---|---|---|
| `$15` | Seq•Song category button | Verified — reliably shows sequence-file listing; given as fact by this task, corroborated live |
| `$17` (in the specific `$00`-`$16`-preceded context) | "Create New Sequence" command; sets a guard blocking Sample•Source Select until resolved | Verified — prefix-isolated to exactly this button, reproducible across 32 trials |
| `$26`-`$3F` | no measured function; possibly unwired | Open — zero effect in every context and combination tried this round |
| Record / Stop•Continue / Play | sequencer transport | `[OPEN]` — see Del 3 for the full negative-evidence list |

**What bank loading required and enabled**: loading `TUTORIAL SEQ`
(`$15` then `$23`, generous settle) is necessary to reach the
`"CREATE NEW 5EQUENCE"` / guard-state context at all — without a loaded
sequence file present, `$15` alone doesn't reach a file worth loading.
It did **not**, by itself, enable Play or make the transport
observable; the additional `$00`-`$16` navigation chain was required
even to reach the one confirmed non-transport state change this task
found.

**Blink frequency**: 0 Hz measured (unimplemented for `asr10panel_device`);
supports the "separate annunciator attribute + panel-local timer"
interpretation by analogy by a sibling class's real implementation, not
by a direct ASR-10 measurement.

**No regression test added this task.** Rule: only add one if the
transport is identified and reproducible. It isn't — adding a test for
an `[OPEN]` finding would be locking in a negative result as if it were
a mechanism, which isn't what the regression suite is for.

**Stays `[OPEN]`, unchanged**: `$74`/`$75`/`$76`, the 39 unmarked
annunciator bits, Record/Stop•Continue/Play, the Load/Command/Edit mode
buttons, and which specific bit(s) drive LOAD/INST/STOP.

**Deletion list**: all of this task's probes (`stop-from-known-active.lua`,
`stop-single-candidate.lua`, `find-trigger-prefix.lua`,
`sweep-after-new-seq.lua`, `holdcombo-sweep.lua`/`-sweep2.lua`,
`seqsong-correlated-sweep.lua`, `find-guard-flag.lua`,
`probe-flag-clear.lua`, `blink-poll.lua`, and earlier exploratory
scripts) lived in the session scratchpad, never the repository — no
in-tree instrumentation to delete. Two in-tree files changed:
`src/mame/ensoniq/esqpanel.cpp` (+11/-2 lines: two `PORT_CODE`
mnemonic bindings and their comments) and
`docs/asr10/investigations/panel-keymap.md` (2 rows updated). Net
addition to the tree this task: this document, plus the two small
keymap edits above — no C++ instrumentation was added or needed to be
removed.
