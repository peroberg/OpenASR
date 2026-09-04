# Display protocol: codes, cursor/underline, annunciators (2026-08-24)

> **Correction 2026-08-26:** This journal preserves the original investigation,
> but three conclusions below have been superseded by byte/state capture in
> `display-protocol-state-machine-v350.md`. Channel-B THRB is CPU byte address
> `$FC4817`, not `$FC480D`; the latter came from converting an aligned Lua
> word-tap bucket as a byte offset. `$74-$76` are not verified as a nibble-only
> animation family (`$74 $40`, `$75 $00/$08` and `$76 $00` are observed), so
> their meaning remains `[OPEN]`. Finally, selected-field partial updates do
> require positioning state: `$62` establishes an operational field anchor and
> `$63` restores it before a value-only rewrite. The old full-redraw replay did
> not exercise that transaction class. See the current canonical table in
> `../reference/display-protocol.md`.

The first implementation task in a while, following two measurement-only
rounds (stereo verification, PB9/10/11). Measure first, build second,
lock with test. `[Verified]` unless marked otherwise.

## Del 0 — where the implementation actually lives

Per had no idea where the display was handled. Traced end to end,
without changing anything, before touching code:

```
firmware -> MC68302 -> DUART channel B THRB ($FC480D, found by tapping
  the whole $FC4800-$FC481F window and letting the histogram show which
  register dominates -- 910/1028 writes in one boot+load+note+nav run,
  not the SCN2681 datasheet's usual THRB slot, which would have been
  $FC4817 and was wrong)
  -> scn2681_device -> b_tx_cb -> asr10panel_device::rx_w()
  -> asr10panel_device::rcv_complete() -> send_to_display()
  -> asr10panel_device::send_to_display() [src/mame/ensoniq/esqpanel.cpp]
  -> esq1x22_device::write_char() [src/mame/ensoniq/esqvfd.cpp]
  -> esqvfd_device::update_display() [shared base, esqvfd.cpp]
  -> src/mame/layout/asr10_panel.lay (outputs "vfd0".."vfd43")
```

`asr10panel_device` (ASR-10-specific) intercepts the 2-byte annunciator
opcode ($77-$7b) and never forwards it to the VFD; everything else
reaches `esq1x22_device::write_char()`, a per-subclass override of the
shared `esqvfd_device` base (the same pattern `esq2x40_device` already
uses for its own, different protocol). Before this task,
`esq1x22_device::write_char()` handled exactly two things: `'f'`
($0x66, clear) and printable ASCII ($0x20-$0x5f) — everything else hit
`printf("Unhandled control code %02x\n", data)`, unconditionally, every
single time. That printf is the "Unhandled control code NN" line every
Lua probe's stdout has carried, and been `grep -v`'d out of, all
session.

**What happens to unrecognized codes today, before this task:** silently
discarded after printing to stdout (not `-log`, not aggregated, no state
kept) — visible only if you happened to be reading raw console output,
invisible to every regression test.

## Del 1 — protocol inventory

Tapped the full `$FC4800-$FC481F` window across boot, instrument load, a
note press, and navigation across 4 distinct parameter pages plus REC
SRC Field 2 cycling (`display-protocol-inventory-probe.lua`, archived).
1028 bytes captured; full table in `../reference/display-protocol.md`.

**Validation method, not eyeballing:** rather than trust a byte-by-byte
reading, the entire captured stream was replayed in Python against the
*existing, unmodified* rendering algorithm (clear on `$66`, place-and-
advance on `$20-$5f`, ignore everything else) and split into segments at
each clear. Every one of 18 segments' predicted text matched a real,
independently-known screen string exactly (`   ENSONIQ  ASR-10    `,
`    LOADING SYSTEM    `, `FILE 1  TUTORIAL BNK  `, `REC SRC=INPUTDRY
LEFT `, `FX=INST   CHOR+REV+DDL`, etc.) — proving the *existing* character
placement was already correct and that nothing in the unrecognized-code
set affects which character lands where. That result is what redirected
the investigation from "find a cursor-position code" (there isn't one
needed) to "find an attribute code" (there is one).

**`$74`/`$76` (and `$75`, found alongside them):** always a 2-byte
opcode+nibble pair, forming a wrapping countdown (`$0F`->`$00`), 8 ticks
per burst, individual ticks ~0.7ms apart (execution-speed-dependent,
this run was fast-forwarded ~20x), bursts recurring roughly every
150-800ms. Appears during the boot-time "SHUFFLING"/loading wait *and*
during plain menu navigation with no loading involved — ruling out
"loading progress indicator" as the sole explanation. `$75`/`$76`
appear only at burst boundaries in place of `$74`. No manual text
("spinner", "hourglass", "activity indicator", "blinking cursor")
matches this. Left `[OPEN]`, not guessed at — this is exactly the kind
of code Del 2's alarm exists for.

**`$60`/`$62`/`$72` family — the cursor/underline mechanism (Del 3's
actual subject):** on every screen with an editable value field (REC
SRC, FX Select), the byte-identical structure `$60 $01 <label text> $72
$60 $01 <separator> $62 $60 $03 <value text> $72` appears; on screens
with only a static label (CREATE NEW INSTRUMENT, EDIT PITCH TABLE), it's
entirely absent. The operand differs only between the label run (`$01`)
and the value run (`$03`) — never by position (both `$60 $01` instances
in one screen appear at different physical columns, ruling out "operand
= column"). Cross-checked on a second, independent screen (FX Select:
`$60 $01` for `FX=`, `$62 $60 $03` for `INST`, `$60 $01` again for the
trailing algorithm list) — same structure, same operand-to-role mapping,
different content. This is the manual's own "cursor (underline) beneath
the field" mechanism (Section on renaming/editing: *"Press the
Left/Right Arrow buttons to move the cursor (underline)..."*), and the
field marked `$03` is consistently the one the manual would call
currently selected/editable.

## Del 2 — the unknown is loud now, built first

`asr10panel_device::report_unhandled_display_code()` fires
`osd_printf_error()` once per distinct byte value (a 256-entry seen-set,
reset on `device_reset()`), before any of Del 3/4's rendering was built.
Verified aggregated, not flooding: a full boot+load+REC-SRC-navigation
run produces one line per distinct unrecognized code (26 codes, 26
lines), not one line per occurrence (which would have been in the
thousands given the `$74` animation alone). `esq1x22_device`'s own old
per-byte printf is removed — the new alarm, upstream in
`asr10panel_device`, sees every byte first and is authoritative;
keeping a second, redundant, un-aggregated alarm in the shared VFD class
would defeat the purpose.

**Bug caught and fixed during verification, not left in:** the first
version of the alarm's "is this recognized text" check reused the same
bounds condition (`m_text_position < m_text_chars.size()`) that gates
whether a character gets mirrored into `asr10panel_device`'s own 22-char
buffer. Once that buffer filled (position 22), further legitimate
printable characters (plain ASCII letters, still within `$20-$5f`, still
handled fine by `esq1x22_device`) started tripping the alarm as if they
were unrecognized control codes. Caught by the live verification run
(`R`, `I`, `G`, `H`, `T` from mid-render of the word "RIGHT" showing up
as false "unhandled" alarms), fixed by separating "is this byte
semantically recognized" from "did it fit in our own mirror buffer" —
two different questions the original single condition conflated.

## Del 3 — cursor, underline, field positioning: implemented

`esq1x22_device::write_char()` (`src/mame/ensoniq/esqvfd.cpp`) gained:

- A 2-byte `$60 <attr>` handler, using the same `m_lastchar`-based
  one-byte lookback `esq2x40_device` already uses for its own `$fa`/
  `$ff` pairs (same class hierarchy, same technique, not a new pattern).
  `attr & 0x02` selects `AT_UNDERLINE`; otherwise `AT_NORMAL`.
- `$62` and `$72` reset the current attribute to normal (field
  boundaries).
- Printable-character handling now sets `m_attrs[0][cursx] = m_curattr`
  before advancing (previously never set at all for this VFD variant,
  unlike `esq2x40_device`, which already did this for its own protocol).

No new output wiring was needed: the shared `esqvfd_device::
update_display()` already computed and exposed underline state to
outputs `"vfd22"`-`"vfd43"` (`col + rows*cols`, `rows=1, cols=22` for
this variant) — `src/mame/layout/asr10_panel.lay` was already reading
them (`<repeat count="22">`'s `underline` elements), they had just never
been driven, because nothing ever set `AT_UNDERLINE`.

**Verified against the manual's own description, not assumed:** the
manual explicitly defines "cursor" as "the underline beneath the field",
moved by the Left/Right Arrow buttons, in `Musician's Manual` text found
at multiple points (e.g. *"Press the Left/Right Arrow buttons to move
the cursor (underline) until it is beneath..."*). Live-verified: entering
REC SRC underlines exactly columns 17-21 (`LEFT `, the field's own
5-character width including its trailing pad); pressing Down (changing
Field 2 to RIGHT) and forcing a redraw keeps the underline at the same
17-21 range, now under `RIGHT` — the field's width didn't change, only
its content, and the underline correctly followed.

**Scope discipline, per instruction not to touch `esqpanel_device`'s
base-class protocol:** `esqpanel_device` itself (the byte-receiving,
serial-protocol layer shared by every EPS/ESQ panel variant) was not
touched at all. The edit lives in `esq1x22_device`, a *subclass*
implementing its own, previously-empty protocol surface — architecturally
identical to what `esq2x40_device` already does for its own bytes, not a
change to shared behavior. `esq1x22_device` is also used by
`esqpanel1x22_device` (`esq5505.cpp`); the only change visible to that
consumer is that unrecognized codes no longer print to stdout (net
noise reduction, not a functional change) and that a specific 2-byte
opcode (`$60`) and two single-byte markers (`$62`, `$72`) now set an
already-existing, already-inert attribute field instead of falling
through to the same no-op default. No existing behavior for any other
byte value changed.

## Del 4 — annunciators

Before this task: `m_annunciator_regs[5]` (raw register bytes) was
already captured but never rendered — `asr10_panel.lay` had only a
placeholder rectangle and the literal text `"ANNUNCIATORS OPEN"`.
`m_instrument_lamps[8]` was declared, save-stated, zeroed on reset, and
never written by anything.

**Correlated against known-lit states** (`annunciator-bit-probe.lua`,
archived): captured all 5 registers at fresh-FILE-1, post-load,
post-instrument-1-select, post-reselect (deselect), Level-Detect-entry,
and RECORDING/WAITING. One clean, single-bit, bidirectionally-confirmed
transition: register `$77` bit 0 goes `0->1` when Instrument 1 is
selected (`BTN_02` from idle `FILE LOADED`) and back `1->0` when the
same button is pressed again (deselect) — matching the manual's own
description of the yellow LED toggling. **Correction made during this
same task, not left uncaught:** a broader combined test (this bit
checked *after* first visiting the REC SRC screen) showed the bit was
already `1` before instrument selection — the bit also changes on
entering Sample-Source Select / Level Detect, so "confirmed" is scoped
narrowly to the isolated load->select->reselect sequence, not asserted
as "the general instrument-1-selected flag." The regression test
reproduces exactly that narrow sequence, in that order, for exactly that
reason.

**Implemented:** the confirmed bit mirrors into output `asr10_instlamp0`
(the pre-existing, previously-dead output), rendered as a distinctly
labeled amber lamp ("INST 1") in the layout. The remaining 39 bits (5
registers x 8 bits — "about thirty" candidate indicators, matching the
task's own count) are wired raw to `asr10_annbit0`-`asr10_annbit39` and
rendered as small, unlabeled amber lamps, replacing the dead placeholder
— visible and reactive, not blank, but explicitly not claimed to mean
anything beyond "this bit of this register." Correlating the other 39
bits against more known-lit states (RECORD, PLAY, EDIT, etc.) is future
work, not attempted further here.

## Del 5 — REC SRC Field 1: panel-control gap, not a display gap

The manual states plainly (multiple locations) that the cursor
(underline) moves via the Left/Right Arrow buttons. This project's own
panel pilot keymap (`panel-keymap.md`) assigns `BTN_0C`/`BTN_0D` to
`Left`/`Right` as a *clicking convenience*, explicitly not verified
against real physical semantics.

**Tested directly:** pressing `BTN_0C` from the REC SRC screen does not
move a cursor within that screen at all — it navigates to a completely
unrelated top-level menu (`COPY AUDIO TRACK` -> `ERASE AUDIO TRACK` ->
`FILTER AUDIO EVENTS` -> `SHIFT AUDIO TRACK` on repeated `Down`
presses), already established in the prior PB9/10/11 task's corrected
64-button sweep, which also tested every other button code (including
the numbered 1-8 page buttons the manual's "Press: 1-8" hint might
refer to) with zero effect on Field 1.

**Conclusion: this is a panel-control mapping gap, not a display
rendering gap.** The display now correctly renders whatever attribute/
field structure it's told to (Del 3, verified against two independent
screens); the reason Field 1 can't be reached is that no button in the
currently modeled 64-code matrix produces the firmware event real
Left/Right Arrow presses would produce *on this specific screen*. The
correct button code for real Left/Right Arrow is not established here —
would need either real hardware or ROM button-scan dispatch-table
tracing, out of scope for this task (no guessing semantics).

## Del 6 — regression and journal

`docs/asr10/lua/display_protocol.lua`, 12th regression test: locks in
(a) the instrument-select lamp's isolated 0->1->0 sequence and (b) the
underline range for REC SRC Field 2, both before and after a value
change (LEFT -> RIGHT), asserting the underline correctly stays at
columns 17-21 (the field's fixed 5-character width) while the field's
own content changes. Fault-injection tested: a deliberately wrong
expected underline range (`16-20` instead of `17-21`) produced exactly
one `FAIL display_protocol` line, then was reverted (diff-confirmed
byte-identical). Suite is now **12 tests, 13 PASS lines, 14 total
lines**.

Protocol table: `../reference/display-protocol.md`, evidence level per
row. What remains unknown is exactly what Del 2's alarm now surfaces at
runtime: the `$74`/`$75`/`$76` animation family, the `$E7 $71` pair and
its neighbors (`$7E`/`$FC`/`$FD`/`$FF`/`$D5`/`$15`), the post-
"KEYBOARD TUNED" cluster (`$E0`/`$B0`/`$7F`/`$C0`/`$40`), the volume-
context cluster (`$90`/`$80`/`$3C`/`$64`), and two isolated codes
(`$63`, `$67`) near the FX Select algorithm list. None were guessed at;
all are catalogued with their observed context in the reference table.

## What was not done

No `mem_map` change. No `WD33C93`, no ADC, no SCC code, no ES5510
enable. Factor two, clocks, bank 1, and expanded RAM configurations were
untouched. `es5506.h`/`.cpp`/`es5510.cpp`/`esqpump.cpp` were not read
this task (not relevant to the display). `esqpanel_device`'s own
base-class protocol was not modified — the display-side change is
scoped to `esq1x22_device` (a subclass with its own, previously-empty
protocol surface) and `asr10panel_device` (ASR-10-specific), per
instruction. REC SRC Field 1's real button mapping remains unresolved,
filed as a named follow-up (Del 5), not guessed at.

## References

- `../reference/display-protocol.md` — the protocol code table.
- `docs/asr10/lua/archive/display-protocol-inventory-probe.lua` — the
  Del 1 measurement.
- `docs/asr10/lua/archive/annunciator-bit-probe.lua` — the Del 4
  correlation.
- `docs/asr10/lua/archive/underline-verify-probe.lua`,
  `docs/asr10/lua/archive/instrument-lamp-verify-probe.lua` — live
  verification of the Del 3/Del 4 implementation.
- `docs/asr10/lua/display_protocol.lua` — the permanent 12th regression
  test.
- `panel-keymap.md`, `../investigations/pb9-10-11-and-scsi-probe.md` —
  the prior task's corrected 64-button sweep, reused for Del 5's
  conclusion.
- `docs/asr10/sources/ASR10_manual.pdf` — cursor/underline description
  cited in Del 3.
