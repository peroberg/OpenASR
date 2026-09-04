# The display as oracle: a real rendering bug, then Left/Right/Up/Down confirmed (2026-08-24)

Principle for this round: effect -> internal state -> routine -> button,
not button code -> guessed function. But the display had to be trusted
first. It wasn't. `[Verified]` unless marked otherwise.

## Del 1 — partial-field-update positioning: a real bug, found and fixed

The prior round's character-placement validation replayed the full
captured byte stream against 18 **full-screen redraws** and matched
every one exactly. That result is downgraded in
`../reference/display-protocol.md` to `[Verified, coverage:
full-redraw]` — it says nothing about partial updates, and partial
updates turned out to be broken.

**Measured symptom, reproduced directly**: selecting an instrument
shows `"JM DIGI SYN  VOLUME=99"`. Pressing the (at the time still
mislabeled) Down/Up buttons to change the value did not overwrite the
digits — before this task's fix, the same bug the retrospective
described would have appended a new two-digit run after the old one.

**Diffed three cases** (`partial-update-full-vs-partial-diff-probe.lua`,
archived): (a) a full-screen redraw (entering REC SRC), (b) a single
value change on the same page (Up/Down on the VOLUME screen), (c) an
attempted field switch (a static-table candidate for Left Arrow). Case
(a) never emits a byte below `$20` other than as part of an already-
understood pair. Cases (b) and (c) both do: a **standalone byte in
`$00-$1F`, immediately preceding the changed content**, present in every
partial update and absent from every full redraw.

Captured directly (`volume-append-bug-repro-probe.lua`): pressing the
value-decrement button three times on the VOLUME screen emitted
`14 39 39`, `14 39 39`, `14 39 38` — `$14` (=20 decimal) every time,
followed by the two ASCII digits of the new value. Column 20 is exactly
where `"VOLUME="`'s two digits start in the 22-character line (counted
directly: `J-M-sp-D-I-G-I-sp-S-Y-N-sp-sp-V-O-L-U-M-E-=` is 20
characters, columns 0-19; the value occupies columns 20-21). Not a
coincidence read into the data — an exact numeric match, repeated
identically on every press regardless of which value it precedes.

**Implemented** (`esq1x22_device::write_char()`, `esqvfd.cpp`): a
standalone byte `$00`-`$1F` sets `m_cursx` to that value directly,
unless it's the operand of `$74`/`$75`/`$76` (disambiguated by the same
one-byte lookback already used for `$60`'s operand — both live in the
same low byte range, and the animation family's own operand never
exceeds `$0F`, so the ambiguity is real only for `$00`-`$0F` and is
resolved correctly by tracking the preceding byte, not by range alone).

**Fix verified two ways.** First, the VOLUME screen: re-ran the exact
repro, now shows `?0LUME?98` (correctly overwritten) instead of
appending. Second, and more structurally telling: the same fix also
explained a **second, previously-misread** symptom. The Left-Arrow-
candidate test (`$10`) had produced a full-line redraw with no visible
effect on the display's underline output, even though the raw byte
trace clearly carried a `$60 $03` (underline) attribute on `"INPUTDRY"`
instead of `"LEFT "`. Before this fix, that redraw silently wrote into
buffer columns 22+ (invisible — outputs only cover columns 0-21),
because nothing had reset the cursor back to column 0 and the previous
redraw had left it stuck past the visible range. The redraw's own
leading `$00` byte — previously dismissed as background-animation
spillover — turned out to be exactly this fix's target: column-0 reset.
After the fix, the same sequence correctly moves the underline to
columns 8-15 (`"INPUTDRY"`). One fix, two previously-separate symptoms,
both explained and both resolved.

A follow-up sweep (Del 4) independently observed standalone
position-opcode values `$00`-`$0A`, `$0C`-`$0F`, `$15` across many
different screens, every one within the valid 0-21 column range and
none exceeding it — corroborating evidence beyond the original `$14`/
`$00` pair, not a single-point guess generalized.

## Del 2 — LOAD's blink: panel-local, not host-driven

The manual: LOAD blinks while INST and STOP are shown solid — three
states, not two. Tapped the full display byte stream
(`load-blink-traffic-probe.lua`) for a sustained 3-second window at
idle `FILE 1  TUTORIAL BNK` (LOAD mode, flashing). **Zero bytes**
emitted in that window, on any register (text or annunciator).

**Verdict: panel-local blink.** No periodic re-transmission drives the
blink from the host side within this window; if LOAD is genuinely
blinking on real hardware, the panel's own microcontroller must be
doing it locally against its own clock, not because the host keeps
resending anything. Per the task's own predicted consequence: **the
39-bit "simple on/off" annunciator interpretation is not confirmed and
should not be treated as settled** — a blink-capable indicator plausibly
needs a blink-mask bit or a two-bit encoding this project hasn't
identified, not a single on/off bit. Not implemented further: no
specific blink-mask bit has been measured, and building a blink timer
without knowing which bits it should watch would be guessing the exact
mechanism this task's own rules forbid. The 39 unlabeled annunciator
bits remain `[OPEN]` in the layout and in `display-protocol.md`,
unchanged from the prior task — this task adds *why* their current
"simple on/off" reading is specifically suspect, without resolving it.

## Del 3 — the correlated probe

Built `panel_correlated_probe.lua` (`docs/asr10/lua/lib/`, reusable
across scripts): one function, `press_and_correlate(code, context)`,
producing one `PANEL_EVENT` line per press with all six required
fields — timestamp, wire frame, `$016F`/`$0D04` state before and after,
the PC chain collected until the machine settles back into
`$F87F92`-`$F87FD0` (`subroutine-index.md`'s `sched_dispatch_scan`/
`sched_idle_loop`), the emitted display bytes, and display text
before/after. Observation only; no C++ instrumentation.

**A real SS8.6 bug, caught and fixed before trusting the tool's own
output.** The first version's tap-handle table (`taps`) was a bare
top-level local, never referenced by the returned `press_and_correlate`
closure — once the module's own top-level chunk finished executing, the
tap became unreachable and eligible for garbage collection, exactly the
failure mode this project's own methods doc (§8.6) warns about. Caught
because `disp_bytes` came back empty for all 114 rows of the first full
sweep, despite the display visibly changing in dozens of them. Fixed by
returning `taps` from the module table too, keeping it reachable for as
long as the caller holds the returned `probe` object.

**Residual flakiness, reported honestly rather than hidden.** Even
after the fix, the `disp_bytes` column has come back empty for an
entire run on roughly 1 of 3 attempts in testing, while every other
column (PC chain, state variables, display text) has been reliable
every time. Documented as a known caveat in the tool's own header
(§8.7: a zero result needs a live witness for the whole window, and
this specific column's witness has been observed to die silently on
some runs) rather than presented as unconditionally trustworthy. The
findings in this document do not depend on this tool's `disp_bytes`
column specifically — the position-opcode and Left/Right findings were
each independently confirmed with dedicated, simpler probes first.

## Del 4 — the sweep, with the display as oracle

Ran the correlated probe across all 38 statically-valid raw codes
(`$00`-`$25` — see the `panel-raw-map.csv` note under Del 5) in three
contexts: idle after `FILE LOADED`, REC SRC (a parameter page), and Load
mode (deselected, file-browser). 114 rows total:
`../static/panel-correlated-sweep-v350.csv`.

**Every code reaches a real handler and returns cleanly to idle** —
consistent with the prior task's own full-64-code sweep. Most codes
navigate between screens (matching Command/Edit-mode "successive
presses scroll through pages" from the manual); none hang or produce
unrecognized garbage beyond the already-catalogued `[OPEN]` display
codes.

## Del 5 — the controls that matter most

### Left and Right Arrow: confirmed, not guessed

Static grounding first, then measurement. `../static/panel-raw-map.csv`
(pre-existing, from ROM `$F82484`'s raw->mapped key-code table, `[0-9A-F]00`-`$25`
verified bound) groups raw codes by their **mapped** value into clean,
contiguous blocks: mapped `$00`-`$07` = the 8 Instrument•Sequence Track
buttons (mapped `$00`=raw `$02`, already confirmed Instrument 1); mapped
`$20`-`$25` = a 6-slot block matching the manual's own "Data Entry
Controls" cluster (Up/Down/Left/Right/Cancel/Enter — six controls).
Within that block, mapped `$20`=raw `$0A` and mapped `$21`=raw `$0B`
were already confirmed as Up/Down; mapped `$25`=raw `$23` was already
confirmed as Enter-Yes. That left two unconfirmed slots and two
candidates (`$10`, `$11`, `$21`) — enough to test directly instead of
sweeping blind.

**Measured, using the display's own underline output as ground truth**
(`left-arrow-candidate-probe.lua`, `left-right-candidate-underline-probe.lua`):
from REC SRC with Field 2 (`"LEFT "`) underlined (columns 17-21),
pressing raw `$10` moves the underline to Field 1 (`"INPUTDRY"`,
columns 8-15) — an exact column-range match, not an approximation.
From there, pressing raw `$11` moves the underline back to columns
17-21, staying on the same screen. Pressing raw `$21` from the same
state instead navigates away entirely (`"CREATE NEW INSTRUMENT"`) — not
Right Arrow behavior, ruled out.

**Confirmed: raw `$10` = Left Arrow, raw `$11` = Right Arrow.** Both
directions round-trip correctly. Implemented in
`asr10panel_device`'s ioport table (`esqpanel.cpp`): `BTN_10` gets
`KEYCODE_LEFT`, `BTN_11` gets `KEYCODE_RIGHT` — the codes freed up when
the previous task removed them from the wrong buttons (`$0C`/`$0D`).
Regression-locked (`panel_navigation.lua`, 14th test).

### Up and Down: the pilot keymap had them backwards

The manual: Up/Down and the Data Entry Slider change the *value* of the
current parameter. Measured on the VOLUME screen (Del 1's own repro,
before the position-opcode fix was even needed to see this): pressing
raw `$0A` twice left the value unchanged (`99`, `99`) — consistent with
already being at a ceiling, i.e. genuinely **Up**. Pressing raw `$0B`
decreased it by one (`99` -> `98`) — genuinely **Down**. Cross-checked
against the already-established REC SRC Field 2 behavior: raw `$0A`
increments the field index (`LEFT`=0 -> `RIGHT`=1 -> `L+R`=2), raw `$0B`
decrements — the same increment/decrement direction on both a numeric
value and an enumerated choice, consistent with one button always
meaning "increase" and the other "decrease."

**The pilot keymap had `$0A`=`KEYCODE_DOWN`/`$0B`=`KEYCODE_UP` — swapped
relative to the measured effect.** Verified against effect, not label,
per instruction, before concluding a firmware/mapping error — there
wasn't one; the keyboard shortcut was simply pointed at the wrong wire
code. **Fixed**: `esqpanel.cpp` now binds `$0A`=`KEYCODE_UP`,
`$0B`=`KEYCODE_DOWN`. The wire-level codes and their effect on REC SRC
Field 2 are unchanged; only which physical computer key triggers which
code changed. No existing regression test depended on the keyboard
binding specifically (all drive the ioport field directly by mask), so
none needed updating for this swap.

### Record, Play, Stop•Continue: tried as a state machine, still open

Per instruction, tried as a state machine, not single clicks
(`transport-hold-combo-probe.lua`): held candidate A, held candidate B
while A stayed held, released B, released A, watching display text and
all 5 annunciator registers for a combined effect neither single press
alone produces. Candidates were the codes showing **no visible
single-press effect from idle** in the prior task's own 64-code sweep,
restricted to the statically-valid `$00`-`$25` range and excluding
already-identified Instrument-select and Enter-Yes codes: `$00`, `$01`,
`$1D`, `$22`. All twelve ordered pairs among these four: no combined
effect on display text or any annunciator register.

**One useful side finding**: raw `$15` navigates to a genuine sequence
file (`"FILE 9  TUTORIAL SEQ"`) from idle — a strong candidate for the
manual's "Seq•Song" category button (`seq-context-hold-combo-probe.lua`).
Retried the same hold-combo search from that context, plus `$0C`/`$0D`/
`$21` as additional candidates: every observed change was consistent
with the *already-known* "successive page-button presses scroll through
files" behavior (`"FILE 11 ATRK TUT BNK"`, `"FILE 1? TUTORIAL SNG"`,
etc. — a single button's own file-browsing effect, not a two-button
combination), not a new transport state.

**Not identified.** Two structural reasons a single-session hold-combo
search may simply be the wrong tool for Record specifically, named
rather than left implicit: Record's own down-edge may be designed to be
silent until Play is *also* pressed while held (this task's 12-pair
search covers ordered holds among 4 candidates, not the full space of
raw codes, since the candidate list itself was narrowed by "silent
alone" — a real Record button's press could still be silent alone and
simply not be in this narrowed set); and Play may need a loaded
sequence or an active recording context to respond at all, which this
session's file browser detour did not establish. The sequencer status
area the manual describes (Stop/Play/Record indicator lights) was not
localized to any specific annunciator bit this session — the state
variable to trace (per instruction: "hitta sekvenserartillståndet och
spåra vem som skriver det") was not found. Filed as the concrete next
step, not guessed at.

## Del 6 — regression and journal

`docs/asr10/lua/panel_navigation.lua`, 14th regression test: locks in
Left Arrow moving the underline from REC SRC Field 2 to Field 1 and
Right Arrow moving it back, using the exact column ranges measured in
Del 5. Fault-injection tested: a deliberately wrong expected underline
range produced exactly one `FAIL panel_navigation` line, then was
reverted (diff-confirmed byte-identical). Suite is now **14 tests, 15
PASS lines, 16 total lines**.

**Explicitly marked `[OPEN]`, not resolved this task:**

- The 40 annunciator bits (Del 2: the "simple on/off" reading is now
  specifically suspect, not just unconfirmed, given the panel-local
  blink finding — but no blink-mask encoding was identified, so no
  reinterpretation was implemented).
- `$74`/`$75`/`$76`'s countdown animation (unchanged from the prior
  task; not touched, not guessed at).
- Record, Stop•Continue, Play (Del 5: tried as a state machine per
  instruction, not found; the sequencer status variable to trace
  remains unidentified).
- Roughly a dozen other single/paired display codes cataloged in
  `../reference/display-protocol.md` (`$90`/`$80`/`$3C`/`$64` near the
  VOLUME context, `$63`/`$67` near the FX algorithm list, etc.) —
  unchanged, not investigated further this task.

## What was not done

No `mem_map` change. No `WD33C93`, no ADC, no SCC code, no ES5510
enable. Factor two, clocks, bank 1, expanded RAM configurations, and
PB9/10/11 were not touched. `esqpanel_device`'s own base-class protocol
was not modified — both C++ changes (the cursor-position opcode, the
Left/Right/Up/Down keymap corrections) are scoped to `esq1x22_device`
(a subclass with its own previously-empty protocol surface, same
pattern as the prior task's underline opcode) and `asr10panel_device`'s
own ioport table respectively.
`es5506.h`/`.cpp`/`es5510.cpp`/`esqpump.cpp` were not read this task.

## References

- `../reference/display-protocol.md` — updated code table: the new
  cursor-position opcode, the downgraded full-redraw-only coverage
  note, and corrections to three previously-miscategorized `[OPEN]`
  bytes.
- `../static/panel-raw-map.csv` — pre-existing ROM raw->mapped table
  that supplied the Left/Right/Cancel candidate shortlist.
- `../static/panel-correlated-sweep-v350.csv` — the 114-row Del 4 sweep.
- `docs/asr10/lua/lib/panel_correlated_probe.lua` — the reusable
  correlated-probe library (Del 3).
- `docs/asr10/lua/archive/` — `partial-update-full-vs-partial-diff-probe.lua`,
  `volume-append-bug-repro-probe.lua`, `left-arrow-candidate-probe.lua`,
  `left-right-candidate-underline-probe.lua`, `load-blink-traffic-probe.lua`,
  `transport-hold-combo-probe.lua`, `seq-context-hold-combo-probe.lua`,
  `correlated-panel-sweep-probe.lua` — the measurement probes this task
  built.
- `docs/asr10/lua/panel_navigation.lua` — the permanent 14th regression
  test.
- `../investigations/panel-button-and-transport-map.md` — the prior
  task's 64-code sweep and Del 0 manual-vs-matrix comparison, reused
  rather than repeated here.
- `panel-keymap.md` — corrected again (Up/Down swap, Left/Right added).
