# Execution tracing resolves the F58 contradiction and finds a real sequencer stepper (2026-08-24)

Follow-on to `command-pages-and-clock-verdict.md`. Per's own two
corrections, taken whole: the `$00E66E`/transport unification and the
`$00017E`-as-flag promotion were both offered as hypotheses before any
evidence existed for either — the exact "möjlighet till hypotes"
promotion pattern that cost four rounds once already. This task
resolves the `$000F58` contradiction with execution-level measurement
first (read-taps on code addresses reliably catch instruction fetches
in this MAME build — confirmed against a validated 1000Hz positive
control), then uses the same method to find a real, independent
sequencer-stepper chain, and answers Del 3/4 as narrow, measured
questions rather than unifying anything.

## Del 1 — the `$000F58` contradiction, resolved: `$F8C588` is never reached

**Method**: tap reads on `$00F8C588` (candidate handler) and, as a
positive control validating that read-taps catch instruction fetches
at all in this build, on `$00F88300` (`irq6_tick_producer`, already
independently confirmed executing at 1000Hz by direct polling).

```
F88300 (control): 3000 hits / 3.0000s = 1000.0000 Hz — exact
F8C588 (candidate): 0 hits / 3.0000s = 0.0000 Hz
```

The control is exact, so the tap mechanism is trustworthy; `$F8C588`
is **never executed** in this state, full stop. Tracing the producer's
own wrap branch instruction-by-instruction (all reads, all persisted
via `asr10_taps.lua`):

```
F88362 tst.b $17e.w        1100 Hz  (tap-range artifact, see below)
F8836E accumulator start   1000.0 Hz — every main tick
F8837E wrap-check (bcs)    1000.0 Hz — every main tick
F88382 wrapped-store        144.0 Hz — only on wrap, exact
F88386 jsr                  144.0 Hz
F8838C trap #3 (enqueue)    144.0 Hz
F88398 trap #9 (install)    144.0 Hz
F8C588 handler                0.0 Hz — never
```

Everything up to and including `trap #9` fires at exactly the
predicted 144Hz. The break is between "install into a scheduler slot"
and "dispatch to `$F8C588`" — and the reason is a wrong inference from
last round, not a hardware mystery. `trap #9`'s target slot comes from
`movea.w $d6.w,a1`; **`$D6 = $2438`, which is Slot 3 in the primary
six-slot table** (`$23F6` + 3×`$16`), not Slot 2. Dumping all six slots
confirms Slot 3's own resident-task pointer (bytes 6-9, matching the
already-documented slot layout) is **`$F8F2FA`** — a completely
different routine than `$00740C`/`jumptable_dispatch_15entry`, which is
Slot 2's task. **`jumptable_dispatch_15entry`'s `$F8C588` entry
(index `$E`) was never actually in this call chain — it was an
unverified inference, carried over from a different investigation
(`keyboard-and-sample-bridge-3.md`, about key presses through Slot 2)
and applied here without checking which slot `$D6` actually names.**
That's the resolution: two measurements didn't both hold because one
of them rested on an unchecked assumption, not because of address
aliasing this time (Del 1's own aliasing-caution turned out not to be
the cause here, though it remains a real, separate issue elsewhere in
this investigation).

**Traced one level further, not fully resolved**: `$F8F2FA` is
confirmed executing at 144Hz (entry, the `$16`-type-specific check, and
the generic fallback path all reached at exactly 144Hz). For the
tempo-accumulator's type-`$E` node, it takes the generic per-type
dispatch: `movea.w $8258.w,a0 / move.w $2(a5),d0 / movea.w
(a0,d0.w),a0 / jmp (a0)` — and the `jmp (a0)` itself fires at 144Hz
(confirmed by tap). Live-reading `($8258).w = $0020` and indexing it at
byte-offset `$0E` gives target `$6014` (masked/zero-extended to a
plausible low-RAM address `$006014`). A dump at that address didn't
resolve to an obviously-clean instruction boundary in the time this
task had — recorded as a lead, not a confirmed final handler. **The
chain `$000B6E → producer → Slot 3 (`$F8F2FA`) → per-type table
(`$8258`) → `$006014`-ish` is now execution-confirmed through the jump;
`$000B6E → $67AC → $F8C588 → $000F58` is retracted, not merely
"uncertain."**

## Del 2 — a real, independent sequencer-stepper chain, found by execution trace

Filtering `$000C38` (already known: `$F88F60`'s 4kHz hardware-scan
loop) out of the original allocation diff and tapping the remaining
six 0x48-stride "track-shaped" record fields for **reads with PC
breakdown** (not just value changes) surfaced a real, clean signal:

```
$0010B6, $0010FE, $0011D6, $00121E  — all read at exactly 144.00 Hz
  from PC = $F902D8
```

**`$F902D8` runs at exactly the pulse rate**, independent of and
parallel to the abandoned `$F8C588` path. Register capture at a live
hit:

```
A3 = $F8DB18  (a ROM address — a shared constant/table, not per-track)
A4 = $001098  (NOT one of the six 0x48-stride records directly —
               a distinct, nearby address)
A5 = $0014FC / $001504  (a queue-node target, alternating — consistent
               with trap3_enqueue's small node pool)
D7 = varies (0x00, 0x40, 0x60, 0x7C, 0x7E, 0x7F across samples)
```

The instruction `move.b $1d(a4),(a5)+` (already disassembled last
round, now connected to a live, confirmed-executing loop) reads byte
`A4+$1D = $0010B5` — one byte off from where this task's tap was
aimed (`$0010B6`, word-aligned), which explains why the tap fired
correctly at all: MAME's tap ranges catch any byte access overlapping
the tapped word. **`$001098` is the real "current pointer" candidate**
this task is handing off, not the six records themselves — a `trap #9`
install follows (into a slot from an address this task didn't resolve
yet), and the routine ends in **`trap #c`** — a trap number this
investigation has not documented anywhere before.

**Chained to a second handler**: at the same moment, PC `$F91F00`
appears with `A3 = $001098` — the *exact* value `$F902D8`'s own `A4`
held — and `A4 = $FF8414`. This is a genuine two-stage handoff:
`$F902D8` processes something anchored at `$001098` once per pulse,
then control reaches `$F91F00` with that same address passed forward.
**This is the strongest event-consumption lead found across every
round of this investigation** — a chain that runs at the exact
measured pulse rate, independent of the abandoned MIDI-clock path, and
touches a pointer-shaped value once per tick. **Not fully resolved this
task**: `$001098`'s own field layout, what `trap #c` does, and
`$F91F00`'s full body are all `[OPEN]` — named as concrete next steps,
not claimed as the sequence object itself. The original six-record
"track" cluster from `$17`'s allocation is a *neighbor* this loop
touches (offset `+$1D` from `A4`, not the record base), not proven to
be the sequence object's own fields.

## Del 3 — `$00E66E`'s gate: measured to be MIDI-clock-only, not the Del 2 chain

Per's own caution taken literally: tapped **every** reader of
`$000F58` with PC breakdown, over the same window Del 2's stepper chain
was confirmed running.

```
$00E672 (MIDI-clock down-counter, already characterized): 83.33 Hz
$0073B0 (MIDI-clock mainline poll, already characterized): 31.00 Hz
```

**No other PC ever reads `$000F58`** — specifically, neither `$F902D8`
nor `$F91F00` (Del 2's stepper chain) appears here, even though both
were confirmed running in the identical window. **Measured, not
assumed: `$00E66E`'s gate is scoped exclusively to the MIDI-clock
consumer pair; it does not gate, share state with, or otherwise
correlate with the sequencer-stepper chain found in Del 2.** The two
chains are independent siblings hanging off the same 144Hz pulse, not
one gating the other. This directly answers the task's own question —
"styr den endast MIDI-klockutgången, eller också sekvenserarens
dispatch?" — with a clean "only MIDI-clock," measured.

## Del 4 — `$00017E`: writer and reader both now identified; downgrade, not upgrade

Reads tapped with PC breakdown across the full creation chain plus a
3-second steady window:

```
$00017E readers: exactly one PC, $F88366, at 1000.00 Hz
```

`$F88366` is the `tst.b $17e.w` instruction *inside* `$F88300`'s own
branch — already known structurally from the very first disassembly of
this routine, now confirmed as `$17E`'s **only** reader anywhere in the
system. Combined with the writer already on record (`$FB8ABE`,
boot-time only, twice, never again across any button sweep this or
prior tasks have run): **both are now identified, and neither shows
any connection to user input, the sequencer-stepper chain (Del 2), or
the MIDI-clock chain (Del 3) beyond `$F88300`'s own internal branch
selection.** Per the task's own instruction — treat as candidate until
both are known, then let the evidence decide — the resolution here is
a **downgrade**: `$00017E` is a boot-time-static byte that selects
which of `$F88300`'s two internal sub-paths runs, fixed for the entire
session before any button is ever pressed. It is not a live
play/record/stop selector responding to anything a user does. Retired
as a transport-flag candidate, not promoted.

## Del 5 — the diagnostic menu, backward from the strings: blocked by the same overlay this project already documented

The strings (`GPR MONITOR`, `ESP TESTS`, etc.) live at ROM file offsets
`$1000`-`$1700` and `$53E0`-`$5420` (already located, prior rounds).
Tapping reads on those exact CPU addresses from `t=0` through boot
(persisted taps, PC breakdown) shows **80 distinct PCs, 1051 total
hits** — a large, busy signal that on inspection is not usable: this
is the same low-address range `asr10_boot.cpp`'s own `cs0_covers(0)`
switches from ROM to `m_lowmem_shadow` (RAM) essentially immediately at
boot (already established: `trap3_enqueue`'s queue pool and other
unrelated runtime structures live at these exact addresses post-
switch). An address-based read-tap on this range **cannot distinguish
"reading the ROM string" from "reading whatever RAM structure now
occupies the same address"** for any access after the switch, and the
switch happens before this task's earliest reliable observation
window. This is the identical structural trap `sequencer-clock-and-
service-menu.md` already retracted a false lead over — not a new
finding, a confirmation that the same door is still closed by the same
mechanism. **No reliable backward trace from the strings to a caller
was achieved this task.** The menu's entry sequence remains `[OPEN]`;
not chased further given the confound is structural, not a tooling gap
this task's methods can work around.

## Summary

- **`$000F58` contradiction: resolved.** `$F8C588` is never executed
  (0 hits vs. a validated 1000Hz positive control). The real chain is
  producer → Slot 3 (`$F8F2FA`, confirmed at 144Hz) → per-type table at
  `$8258` → a jump that fires at 144Hz, landing near `$006014`. The
  `$67AC`/`$F8C588` path was an unverified inference borrowed from an
  unrelated investigation; retracted, not softened.
- **Sequencer stepper: found, not fully resolved.** `$F902D8` runs at
  the exact pulse rate (144Hz), anchored on `$001098` (not the
  six-record cluster itself), chains to `$F91F00` with that same
  address, and ends in a previously-undocumented `trap #c`. The
  strongest event-consumption lead this investigation has produced —
  named with real PCs and register values, not asserted as complete.
- **`$00E66E` gate: MIDI-clock-only, measured.** Its two readers are
  both already-characterized MIDI-clock consumers; Del 2's stepper
  chain never touches `$000F58`. Not the same gate as anything else.
- **`$00017E`: downgraded.** Writer and reader are both fully
  identified now, and both are boot-time/internal-only — retired as a
  transport-flag candidate rather than promoted.
- **Diagnostic menu**: backward-tracing from the ROM strings is
  blocked by the same ROM→RAM overlay switch already documented in
  this project; `[OPEN]`, not narrowed further this task.

## Rules check

Observation only — no model/machine-behavior change this task beyond
the tap infrastructure already added last round (`asr10_taps.lua`,
used for every tap here). No firmware variable was written — every
investigation this task was read-only. No `mem_map` change, no
WD33C93, no ADC, no SCC code, no ES5510 activation. No `-log`.
Display/menu text verbatim (none newly captured this task beyond
addresses/PCs, per Del 5's own negative result). No unifying hypothesis
was written down without measurement behind it — Del 3 and Del 4 were
both answered as narrow, measured questions and came back negative for
unification, which is reported as the actual finding, not smoothed
into a broader claim.
