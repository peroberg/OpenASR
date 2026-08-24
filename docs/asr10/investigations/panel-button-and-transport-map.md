# Panel controls: matrix vs. real hardware, and why Left/Right don't work (2026-08-24)

Del 5 of the previous task found REC SRC Field 1 unreachable via any
modeled panel control. This task surveys the panel controls broadly:
what the manual describes, what our matrix has, what's confirmed, what's
wrong, and what's still open. `[Verified]` unless marked otherwise.

## Del 0 — manual's physical panel vs. our matrix

The manual (`Front Panel Controls`, `Additional Front Panel Controls`)
enumerates every physical control by name:

| # | Control | Our matrix |
|---|---|---|
| 1 | Volume Slider | analog output, not swept this task |
| 2 | Mode Buttons: Load, Command, Edit | unidentified `BTN_XX` |
| 3 | 14 Page Buttons (10 numbered + Instrument/Seq-Song/System-MIDI/Effects) | unidentified `BTN_XX`, except `$02`=Instrument-1-select (established prior) |
| 4 | Data Entry: Slider, Up/Down Arrow, **Left/Right Arrow**, Enter•Yes, Cancel•No | Slider = `analog_data_entry`; Up=`$0B`, Down=`$0A` (confirmed); Left/Right = **`[OPEN]`, not `$0C`/`$0D`** (this task); Enter•Yes=`$23` (established prior); Cancel•No = `$22`? (`[Likely]`, see Del 1) |
| 5 | Display | fully modeled (prior task) |
| 6 | Input Level LED Meters (L/R Signal+Peak) | not modeled |
| 7 | Sample•Source Select | `$20` (established prior) |
| 8 | FX Select•FX Bypass | `[Likely]` `$07` (this task's sweep: "FX?INST CHOR?RE??DDL") |
| 9 | Instrument•Sequence Track buttons 1-8 | `$02`=slot 1 confirmed; slots 2-8 `[OPEN]`, never individually tested |
| 10 | Audio Track buttons A, B | `[OPEN]` — ruled out `$0A`/`$0B` (Del 2 below) |
| 11 | Sequencer Transport: **Record, Stop•Continue, Play** | `[OPEN]`, not identified despite systematic sweeping (Del 1/4) |

**64 codes exist in the ioport matrix (`$00`-`$3F`); the manual's own
enumeration above accounts for well under 64 physical buttons** (roughly
3 mode + 14 page + 6 data-entry + 8 instrument + 2 audio-track + 3
transport ≈ 36, plus the 61-key keyboard and octave shift handled
separately). The remaining raw codes may be unused, reserved, or mapped
to controls the manual's own diagram doesn't individually enumerate
(e.g. per-page context-sensitive soft functions). Not resolved this
task — noted as a real, bounded gap, not investigated further.

**Wire protocol, clarified (was previously undocumented at this level):**
`esqpanel_device::set_button()` (`esqpanel.cpp`) sends `(pressed ? 0x80 :
0) | (button & 0xff)` then `0x00` — a *two-byte* frame per press or
release, not the raw code alone. THRB (display-out) and RHRB
(button/key-in) share the same DUART register slot, word offset 6
within `$FC4800-$FC481F` (`$FC480D`, matching
`display-protocol-inventory.md`'s own finding for the display side) —
found the same way, by tapping the whole register window and reading
off which word dominates for the relevant direction, not by assuming
the datasheet's usual slot.

## Del 1 — empirical button-to-routine map

Built `button-routine-sweep-probe.lua` (archived): for every code
`$00`-`$3F`, presses it and records the wire frame, the display text
before/after, and the PC trace until the machine settles back into the
scheduler idle loop (`$F87F92`-`$F87FD0`, `subroutine-index.md`'s
`sched_dispatch_scan`/`sched_idle_loop`). Run in two contexts: idle
after `FILE LOADED`, and the REC SRC screen. Full raw output:
`../static/button-routine-sweep-v350.csv` (128 rows).

**Every single code produces real, distinct dispatch activity** — all
64 codes reach a genuine handler and return cleanly to idle
(`returned_idle=true` in all 128 rows); none are dropped or hang. Most
codes change the display to a different (often COMMAND-mode command
list or EDIT-mode page) screen — confirming the wire protocol and
first-byte classification work correctly for the *entire* code space,
not just the handful of previously-confirmed codes.

**`task_writes` (writes to `$0B6A`, the scheduler's "current task"
pointer) turned out to be a poor discriminator**: it sits at ~120-130
for essentially every press, including ones with zero visible effect —
background scheduler/timer/MIDI-poll activity dominates it. Tried,
found not useful, dropped in favor of display-text and (later)
underline/annunciator diffing instead. Recorded here so a future task
doesn't re-try the same dead end.

## Del 2 — why Left/Right do nothing: wrong code, not missing, not silently accepted

The task's own framework has three possibilities. Discriminated
directly:

1. **Code doesn't exist in the ioport matrix** — ruled out. `$0C`/`$0D`
   exist, generate well-formed two-byte frames, and are received.
2. **Code exists but is wrong — firmware receives something other than
   it expects** — **this is what's happening.** `$0C`/`$0D` reach real,
   distinct, working handlers (`button-routine-sweep-v350.csv` rows for
   `$0C`/`$0D`: `returned_idle=true`, real PC trace, real display
   change), but those handlers are for a *different* function
   (navigating to `COPY AUDIO TRACK` -> `ERASE AUDIO TRACK` -> `FILTER
   AUDIO EVENTS` -> `SHIFT AUDIO TRACK` on repeated presses — an
   unrelated top-level menu category, not a cursor move within the
   current screen). This was already established in the prior task's
   corrected 64-button sweep; this task's independent PC-level trace
   confirms it structurally, not just by display text.
3. **Code reaches firmware, firmware responds, but nothing shows on the
   display** — ruled out for `$0C`/`$0D` specifically: they *do* show a
   real display change (to the Audio Track utility menu), so this isn't
   a silent-response case.

**Verdict: case 2.** The pilot keymap's assignment of `KEYCODE_LEFT`/
`KEYCODE_RIGHT` to raw codes `$0C`/`$0D` was simply wrong, not merely
unverified — a real, working button exists at those codes, it just
isn't Left/Right Arrow. **Fixed** (Del 3): the keyboard bindings are
removed; the real Left/Right Arrow codes remain `[OPEN]` despite
systematic sweeping across three contexts (idle, REC SRC, an attempted
EDIT PITCH TABLE sweep — the latter's own baseline turned out to drift
because that page button cycles through COMMAND-mode entries on repeat
press rather than redrawing idempotently, invalidating that specific
sweep's comparison methodology; noted so it isn't silently repeated).

**Record/Stop•Continue/Play were sought using the same method and are
also `[OPEN]`.** A dedicated annunciator-focused sweep
(`transport-annunciator-sweep-probe.lua`, watching all 5 annunciator
registers in addition to display text, since the manual places
Sequencer Status in the indicator-light area, not the 22-character text
line) found annunciator changes correlated with menu navigation
(matching Del 0/1's general finding) but no isolated, stable toggle
resembling a transport-state indicator. Two structural reasons this
sweep may simply be the wrong tool for Record specifically, named
rather than left implicit: Record's own single-press behavior may be
designed to be silent until combined with Play (a hold+combo the sweep,
which presses one code at a time, never exercises), and Play may
require a loaded sequence to produce any visible response, which
none exists in this session. Not resolved — filed as `[OPEN]`, not
guessed at.

## Del 3 — fixed what Del 0/2 established; verified what wasn't broken

**Fixed:** `esqpanel.cpp`'s `asr10panel_device` ioport definition —
`KEYCODE_LEFT`/`KEYCODE_RIGHT` removed from `BTN_0C`/`BTN_0D`, reverted
to plain click-only buttons like the other 60 unidentified codes (was
actively misleading, not just unverified — see Del 2). `panel-keymap.md`
corrected to match. `esqpanel_device`'s own base-class protocol was not
touched; the change is scoped to `asr10panel_device`'s own ioport table.

**Verified, not broken, nothing to fix:** the "hold Record, press Play"
requirement needs two buttons held simultaneously — a mouse can only
click one location at a time, but two computer-keyboard keys can be
held together. Built `simultaneous-hold-probe.lua`: held `BTN_0A` then
`BTN_0B` together (the only two buttons with keyboard bindings, since
the real Record/Play codes are unidentified), then released them, and
read the actual DUART RHRB byte sequence delivered to firmware. Result:
`8A 00` (press A) `8B 00` (press B, while A still held) `0A 00`
(release A) `0B 00` (release B) — four clean, correctly-ordered, fully
distinct events. MAME's own input-holding model and this device's
serial transport already deliver simultaneous holds correctly; no bug
existed here. Regression-locked (`docs/asr10/lua/panel_input.lua`, 13th
test) so this doesn't silently regress. Once real Record/Play codes are
identified, they should get `PORT_CODE` keyboard bindings following
this exact pattern (`BTN_0A`/`BTN_0B`'s own precedent) — not done here,
since the codes themselves are still `[OPEN]`.

## Del 4 — the manual's own procedure, exactly where it stops

Attempted the manual's sequence-recording procedure (Section 1,
paraphrased in the task):

1. **Select a loaded instrument via its Instrument•Sequence Track
   button.** Reached: `BTN_02` from idle `FILE LOADED` selects
   Instrument 1, confirmed by the existing regression test and this
   task's own lamp verification (`asr10_instlamp0` 0->1).
2. **Hold Record, press Play.** **Blocked here.** Record and Play's raw
   button codes are not identified (Del 1/2) — the procedure cannot
   proceed to this step at all, not because a button press failed
   silently, but because there is no established code to press.

Nothing further in the procedure (Right Arrow x3 to TEMPO, adjust
value, Stop•Continue, `XXX BARS - KEEP TRACK?`) was reachable, since it
depends on step 2 completing. Reported precisely per instruction: the
blocker is at step 2, not step 3 as the task's own framing suggested —
Left/Right's failure is real (Del 2) but sequencer recording never gets
far enough to exercise it in this procedure, since Record/Play are the
earlier, more fundamental gap.

## Del 5 — regression and journal

`docs/asr10/lua/panel_input.lua`, 13th regression test: locks in (a)
the simultaneous-hold event sequence (Del 3) and (b) that `BTN_0A` (the
one confirmed-working navigation control) genuinely changes REC SRC
Field 2's underlying state, not just display text. A bug was caught
before landing: the test's own `press_button()` helper initially
hardcoded the `buttons_0` port regardless of code, silently pressing
the wrong button (`BTN_00` instead of `BTN_20`) and producing a false
failure — caught by the first run, fixed by selecting the port from the
code the same way every other probe in this project does. Fault-
injection tested (inverted the navigation-effect comparison, produced
exactly one `FAIL panel_input` line, reverted byte-identical). Suite is
now **13 tests, 14 PASS lines, 15 total lines**.

**Annunciator bits, marked `[OPEN]` per instruction, not just in
prose.** `asr10_panel.lay`'s 39 unconfirmed bits (`annlamp` elements)
were previously rendered in a bright amber close in tone to the one
*confirmed* lamp (`instlamp`, register `$77` bit 0) — a lit lamp with no
known meaning reads as information it isn't. Changed: the 39 unconfirmed
bits now render dim and desaturated even when lit, with an explicit
"OPEN, UNKNOWN MEANING" label in the view itself, not only in a source
comment. `display-protocol.md`'s code table already marks them `[OPEN]`
in text; the layout now says so visually too.

**`$74`/`$75`/`$76` remain `[OPEN]`.** Not touched this task. The
countdown-animation characterization from the previous task stands;
no new evidence changes it, and per instruction it is not guessed at
further here.

## What was not done

No `mem_map` change. No `WD33C93`, no ADC, no SCC code, no ES5510
enable. Factor two, clocks, bank 1, and expanded RAM configurations
were untouched. PB9/10/11 was not investigated (moved to the hardware
queue in the prior task). `es5506.h`/`.cpp`/`es5510.cpp`/`esqpump.cpp`
were not read this task (not relevant). Real Record/Stop•Continue/Play
and Left/Right Arrow codes remain unidentified — not guessed at,
reported as the concrete next blocker.

## References

- `../reference/display-protocol.md` — display-side protocol table,
  cross-referenced for the shared THRB/RHRB register-slot finding.
- `../static/button-routine-sweep-v350.csv` — full 128-row sweep output.
- `docs/asr10/lua/archive/button-routine-sweep-probe.lua`,
  `transport-annunciator-sweep-probe.lua`, `edit-cursor-sweep-probe.lua`,
  `simultaneous-hold-probe.lua` — the four measurement probes this task
  built.
- `docs/asr10/lua/panel_input.lua` — the permanent 13th regression test.
- `panel-keymap.md` — corrected pilot keymap.
- `panel-input-model.md` (2026-08-10) — the original, still-open
  physical-label mapping list this task narrows (Left/Right, Audio
  Track A/B, and Transport remain on it).
