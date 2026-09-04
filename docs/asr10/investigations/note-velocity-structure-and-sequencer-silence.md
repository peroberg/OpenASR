# Slot 5 reads two plain bytes, not a queue; the sequencer never writes them (2026-08-25)

Follow-on to `slot5-connects-notes-to-voice-programming.md`. Del 1's
own disassembly held a trap that would have produced a false
conclusion if not checked empirically: the instruction boundary at
`$782A` looked like a single 4-byte `MOVE.L`, but byte-level tapping
showed its own extension word (`$782C`) firing only once per note
while `$782A` itself fired continuously at 83Hz regardless — a genuine
contradiction that turned out to be a CPU prefetch artifact, not a
second code path. Resolving it precisely is what let this task land on
the real structure: two plain global bytes, not a queue.

## Del 1 — what Slot 5's call chain reads: `$000D08`/`$000D11`, not a queue

**A real trap, caught before being reported as a finding.** Tapping
`$00782A` and `$00782C` separately showed `$782A` firing at a
steady 83.5Hz in *every* state (idle included), while `$782C` — the
extension word of the very same `MOVE.L A5,$22(A4)` instruction per
the static disassembly — fired **zero times at idle and exactly once
per note**. A 4-byte instruction cannot execute its opcode 83 times
without ever fetching its own operand extension unless something is
wrong with the read. Checked directly: `PC()` at the `$782A` tap
consistently equals `$00782A` itself (no pipeline offset), which at
first looked like confirmation that this address genuinely executes
83 times/sec. The resolution: `$782A` sits immediately after
`$007828: bra.b $77ca`, an *unconditional* branch that Slot 5's own
`~166Hz`-gated tick-divider reaches regularly (roughly matching the
`$D0B0` counter's own threshold-of-11 cadence) — the 68000's prefetch
queue fetches the next sequential word (`$782A`) as a matter of course
immediately before the branch redirects, discarding it. **That
prefetch is what the tap was catching, not a genuine 83Hz "note
check."** The real, once-per-note signal is `$782C` (the instruction's
own extension word actually being consumed), which only happens when
this code is genuinely reached via a real call — not by fall-through,
not by prefetch. Recorded plainly so a future task doesn't mistake
CPU prefetch noise for a periodic poll a second time.

**The structure itself, read fresh at rest and mid-note**:

```
at rest:        $000D08 = $00   $000D11 = $00
during a held note (KEY_C, default octave):
                 $000D08 = $64 (100)   $000D11 = $3C (60)
```

`100` is exactly `esqpanel.cpp`'s own fixed `KEY_VELOCITY` constant for
computer-keyboard note input; `60` is MIDI note 60 — Middle C, exactly
what `KEY_C` at the default octave should produce. **This identifies
both fields precisely, by value, not by guessing from their names**:
`$000D08` = velocity, `$000D11` = note number. Slot 5's own code
(disassembled last round) reads exactly these two addresses
(`move.b $d08.w,d0` / `move.b $d11.w,d0`) to build the voice record's
fields before calling `$007CA8`. **This answers Del 1's "queue,
flag, ring buffer, or event register" question directly: it is
neither — it is two plain global bytes**, the simplest possible
representation, holding the single most-recently-received note.

**The exact caller of this code block (who `JSR`s into `$782A` when a
real note arrives) was not conclusively identified.** Two stack-based
attempts were made and both are reported honestly as inconclusive
rather than asserted: reading `(SP)` gave `$00000000`; reading
`(SP+2)` gave a consistent `$00000100`, which on inspection is **not**
a code address but the *contents* of address `$100` (a repeating
`$F882DA` table-default value) read at an offset that doesn't
correspond to a real return address either. The caller remains
`[OPEN]` — a real, named gap, not smoothed over.

## Del 2 — the writers, with return-address PCs

Tapped writes to both fields across a real key press:

```
$000D08/$D09 (word write, velocity+note pair):
  $0171B4  data=$643C (100, 60)  — the key-DOWN handler, the primary writer

$000D08 alone (byte, high lane):
  $0169C0  data=$6060 (0x60)    — fires on key-UP (release)
  $F8C2F6  data=$6464 (0x64)    — fires on key-UP (release)

$000D10/$D11 (word write, note number):
  $017276  data=$003C (60)      — fires on BOTH key-down and key-up,
                                   same value both times
```

**MIDI note-on was not tested this task** (time did not allow setting
up a `-midiin` fixture run) — reported as untested, not silently
assumed to behave the same as the panel path.

## Del 3 — does the sequencer write here? Measured, strictly scoped

With `TUTORIAL SEQ` loaded and `$17` executed, tapped both fields
across the full `$00`-`$17` navigation chain:

```
$000D11 (note number): zero writes
$000D08: three writes, all from PC=$0169C0, value=$60
```

**`$0169C0` is the same PC seen writing `$D08` on key-*release*** in
Del 2 (not the key-down writer, `$0171B4`) — and its value (`$60`)
doesn't match the confirmed real velocity (`$64`). **`[Verified]`: the
sequencer's `$17`-creation chain does not write a genuine note event
into the structure Slot 5's call chain reads** — `$D11` never moves at
all, and `$D08`'s own touches carry the release-path's signature, not
a new note being created. Per the task's own required framing, kept
separate:

- **`[Verified, address corrected — see transport-ab-test-play-stop-continue.md
  and its follow-up]`** — Slot 5 (via its own call chain) relays an
  observed *panel* note to voice programming, using `$000D08`/`$000D11`
  as the note/velocity representation. The call target named here,
  `$007C7C`, never PC-matches in a later, properly PC-correlated
  re-test (§8.10) — the real, confirmed entry point is `$007CA8`, 44
  bytes into the same block. The relay itself, and the note/velocity
  fields, are unaffected by this correction.
- **`[OPEN]`** — whether the sequencer uses this same path under any
  condition not tested this round (only the specific `$17` creation
  chain was checked; no other sequencer state was reachable this
  session).
- **`[OPEN]`** — whether playback starts at all, under any path. This
  task's negative result is scoped exactly to "not via Slot 5's
  `$D08`/`$D11` structure during sequence creation" — it says nothing
  about any other mechanism.

## Del 4 — `$00A304`: real ROM reference, confirmed never executed in any reachable state

Tapped the containing routine's entry (`$00A2FA`) across idle and ten
steps of Command-mode page navigation (`$06` then `$11`×10, covering
multiple command categories):

```
idle:          0 hits
Command nav:   0 hits (across INSTRUMENT category pages, confirmed by
               display text advancing through "DELETE INSTRUMENT" etc.)
```

**The routine containing the `$101C`-`$103C` bounds check never
executes in any state this task could reach.** Additionally, `$8C72`
(the `jsr` target reached only if the bounds check passes) contains,
when read live, a repeating `$0023 $0023 $0023...` pattern — data
shaped, not a plausible instruction stream. Both observations point
the same way: **this code path is real ROM content but appears
unreached/vestigial in this firmware version's normal operation** —
consistent with (not contradicting) the diagnostic menu being
genuinely inaccessible through any state found across four rounds of
trying. `[OPEN]`, narrowed by a real execution-negative rather than
left as an unconfirmed static lead.

## Summary

- **`[Verified]`** Slot 5's `$782A` firing at 83Hz regardless of notes
  is CPU prefetch noise from an adjacent unconditional branch, not a
  genuine per-poll check — caught before being reported as "Slot 5
  polls at 83Hz," which would have been a real but subtly wrong claim.
- **`[Verified]`** The structure Slot 5's call chain reads is
  `$000D08` (velocity) / `$000D11` (note number) — two plain global
  bytes, value-confirmed against known constants (`100`=`KEY_VELOCITY`,
  `60`=Middle C). Not a queue, flag, or ring buffer.
- **Writers identified**: `$0171B4` (key-down, both fields),
  `$017276` (note number, both edges), `$0169C0`/`$F8C2F6` (key-up).
  MIDI note-on untested.
- **`[Verified]`** The sequencer's `$17` chain never writes `$D11` and
  its `$D08` touches don't match the real note-event signature — kept
  strictly to "not via this path," not generalized to "playback never
  starts."
- **`[OPEN]`** `$00A304` confirmed never executing in any reached
  state; `$8C72` looks like data, not code. Real ROM content,
  unreached — the diagnostic menu stays closed by a fourth measured
  angle, not a repeated guess.

## Rules check

Observation only, every tap through `asr10_taps.lua`. No firmware
variable was written. No `mem_map` change, no WD33C93, no ADC, no SCC
code, no ES5510 activation. No `-log`. The `$782A` prefetch-artifact
finding and the `$D08`/`$D11` field identification are both kept to
what was actually measured — no claim that the sequencer path is ruled
out beyond the specific chain tested, and no claim that Slot 5 is "the"
universal note-to-voice bridge beyond the panel-note case verified here.
