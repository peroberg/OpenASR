# TRAP #C is Slot 3's own self-rearm; the real note path is elsewhere entirely (2026-08-25)

Follow-on to `execution-traced-clock-and-sequencer-stepper.md`. Codex's
amendment to this round's task — data before primitive, and voice
allocation is the endpoint, not the starting point — changed the
outcome directly: following TRAP #C's own data (not just its
existence) shows it carries a self-perpetuating clock-loop payload, not
sequence content, and a second, independent trace (starting from a real
key press, working backward from the ES5506 writes it's already known
to produce) finds the actual note-to-voice path in a completely
different part of the ROM, with zero overlap with the TRAP #3/#4/#9/#C
chain this and the prior two tasks have been following.

## Del 1 — TRAP #C disassembled; its own data identified; then the real path found elsewhere

**Handler location, cross-validated**: the live exception vector table
(read at `4×(32+n)` for trap `n`) gives `TRAP #3=$F88078`,
`#4=$F880A2`, `#9=$F88138` — all match this project's own prior
documentation exactly, so the same read for `#12` is trustworthy:
**`TRAP #C = $F88174`**.

**Disassembled**: a generic queue-append primitive, structurally
identical in shape to `#3`/`#4`/`#9` and to `#13` (`#D`, which follows
immediately after in ROM and opens with the same
`ori.w #$700,sr / clr.w (a5) / tst.w $10(a1)` prologue). `A1` is a
queue header, `A5` is the node being appended. If the header's `+$10`
flag is zero (queue was empty), it stores `A5` directly into `+4(a1)`,
sets the flag, and **immediately calls `(a1)`'s own stored function
pointer** — an empty-to-nonempty wake callback. Otherwise it does an
ordinary linked-list append with a capacity check (`cmp.w $a(a1),d1 /
bls`). Confirms Codex's own prediction: `#C` is the same kind of
primitive as its siblings, not specialized logic.

**The data, followed as instructed, not just the primitive's
existence**: tapping `TRAP #C`'s own entry (`$F88174`) with register
capture during our tempo chain's active window shows, overwhelmingly:

```
A1 = $002438  (Slot 3's own header address — the same slot $F8F2FA
               runs in, found last task)
A5_node = 00 00 00 0E  ...  (type = $0E, our already-known event type)
```

with the node's later bytes forming a visibly **decrementing counter**
across consecutive samples (`...15, 14, 13, 12, 11, 10, 0F...`). **This
is Slot 3 re-arming itself**: each time it runs, it packages a fresh
type-`$0E` node and re-enqueues it into its *own* slot header via
`TRAP #C`, carrying a countdown that ticks down between invocations —
the mechanism that keeps the 144Hz loop self-sustaining, not new data
being handed off to a new destination. One rarer sample (`A1=$002422`,
Slot 2's header, type=`$02`) shows the same primitive occasionally
routes a different, less frequent event type to a different slot —
noted, not chased further this task; `TRAP #C` is called **176.33Hz**
overall (higher than the 144Hz pulse), confirming multiple unrelated
callers share this same generic primitive, exactly as its "generic
building block" character predicts.

**Reframed per Codex's instruction — voice allocation is the endpoint,
so start from where a note is created, not from `TRAP #C` forward.**
Tapping `TRAP #9`'s own entry during a real `KEY_C` press (471 calls in
800ms, all background/tempo noise once the four already-known slot
headers are filtered out) found **no new, note-shaped node** — the
generic trap-queue system does not appear to carry note events at all
during this window. Pivoted to tracing backward from the **already-
confirmed-real** ES5506 writes a `KEY_C` press produces (matching
`note_audio.lua`'s own proven regression pattern): the very first write
comes from **`PC=$007CB4`** (low RAM), a few bytes past the actual
write instruction (`$007CAE: move.b $15(a4),$7e(a0)` — the familiar
pipeline-offset pattern this investigation has hit before). Disassembled
the surrounding routine (`$007C7C`-`$007CEE`): it reads a mode/type byte
from `$11C(a3)`, checks it against `7`/`3`, conditionally applies a
`+$20` transform, and writes bytes from `$15(a4)`/`$2A(a4)` to the
ES5506 register window at `$FC2001`+offset — a real, direct voice-
programming routine. **This is a completely separate code region from
the entire `TRAP #3`/`#4`/`#9`/`#C` chain traced across three
consecutive tasks; no address, register value, or PC from that chain
appears anywhere in this routine or its call context.**

**Conclusion, measured**: the tempo/clock chain (producer → Slot 3 →
`TRAP #C` self-rearm) and the real note-to-voice path
(`$007C7C`-area) are two disjoint subsystems. `TRAP #C` is not a
waypoint on the route to voice allocation — it is the mechanism that
keeps the MIDI-clock-shaped background loop alive. This is a clean
negative for "does this chain reach voice allocation," and a positive,
concrete new lead (`$007C7C`) for a future task that wants to keep
following the *actual* note path.

## Del 2 — `$001098`: a methodological correction, not a resolved field

**Important precision, caught before overclaiming**: Del 2 asked what
`$001098` "contains." Read as a **memory location**, it is static —
tapped for writes across the full `$00`-`$17` chain: exactly 4 writes,
all at boot (`t<15s`), all setting it to `$0000`, and it never changes
again. Its only runtime *reader* is `$017242` (7.00Hz, low RAM,
unidentified this task). **But `$F902D8`'s own `A4` register holding
the *value* `$001098` is a different fact from "the memory cell at
address `$001098` holds a changing value"** — `A4` is a pointer being
*used as* the numeric value `$001098`, not necessarily loaded *from*
that address. This task did not trace where `$F902D8` loads `A4` from
(that requires disassembling `$F902D8`'s own entry/caller, not done
this task) — reported honestly as an open gap rather than conflating
"the address `$1098`'s content" with "a register that happens to equal
`$1098`." **`$001098` does not overlap with any address from the
`$17`-allocation's own 211-byte diff** (checked directly: no diffed
field falls in that range), so per the task's own instruction not to
assume a connection without overlapping addresses, no link to the
sequence object is claimed. Real answer: `[OPEN]`, precisely scoped.

## Del 3 — the type table: two entries confirmed live, the rest not re-verified this task

From execution evidence gathered this task (not the static table read
from last task, which is not re-asserted as "confirmed firing" without
its own execution check): **type `$0E`** fires continuously at the
144Hz pulse rate (Slot 3's own self-rearm, per Del 1), and **type
`$02`** was observed firing at least once, routed to Slot 2
(`$0073EA`-adjacent, the already-characterized MIDI-clock mainline
poll region). The table's other ~10 entries (from `$8258`, read
statically last task) were **not** re-checked for actual execution
this task — reported as an open item rather than repeating last task's
static read as if it were now execution-confirmed. No entry among the
two confirmed-live types resembles note/pitch data; both are clock-
loop-shaped (a countdown payload, or a routing to the already-known
MIDI-clock poll).

## Del 4 — does the chain reach voice allocation? No — measured directly, not by absence

Per the task's own "tap both ends" instruction: `TRAP #C` fires at
176.33Hz (measured, Del 1). The confirmed-real ES5506 writes during a
note (Del 1's backward trace) originate from `$007C7C`-area, a PC
region this task's `TRAP #9`/`TRAP #C` register captures never once
recorded. **The chain does not reach voice allocation — not because it
breaks at an identifiable gate, but because it was never connected to
begin with.** This sharpens (and partially revises) prior tasks'
framing: there isn't one pipeline with a gate somewhere in the middle;
there are two separate subsystems (a background tempo/MIDI-clock
loop, and a direct note-to-voice path) that this investigation
conflated into a single hunt because both, at various points, produced
144Hz-adjacent or ES5506-adjacent signals. Named plainly so a future
task doesn't keep looking for a "gate" that isn't there.

## Del 5 — diagnostic menu: not re-attempted, stays `[OPEN]`

The structural blocker found last task (ROM→RAM overlay switch
confounds any address-based read-tap on the string region after the
earliest boot instant) was not revisited this task — no new method was
available to work around it within this task's time, and repeating the
same tap would only reconfirm the same confound. `[OPEN]`, unchanged.

## Summary

- **`TRAP #C`**: disassembled, generic queue-append (same shape as
  `#3`/`#4`/`#9`/`#D`). Its own data in our chain is Slot 3's
  self-rearm (type `$0E`, decrementing countdown) — not sequence
  content, not a handoff to a new destination.
- **Real note path found**: `$007C7C`-`$007CEE` (low RAM), reached by
  tracing backward from confirmed ES5506 writes during a real key
  press — completely disjoint from the entire `TRAP #3`/`#4`/`#9`/`#C`
  chain. This is the concrete lead for continuing toward voice
  allocation, not the chain this and the two prior tasks followed.
- **`$001098`**: static zero as memory content; the *register value*
  `$1098` used by `$F902D8` was not traced to its own load source —
  reported as an open, precisely-scoped gap, not resolved.
- **Type table**: two types execution-confirmed (`$0E` self-rearm,
  `$02` routed to Slot 2); the rest not re-verified this task.
- **Chain reaches voice allocation? No** — measured by tapping both
  ends simultaneously and finding zero PC overlap, not inferred from a
  gate that was never actually located.
- **Diagnostic menu**: unchanged, `[OPEN]`.

## Rules check

Observation only, using `asr10_taps.lua` for every tap. No firmware
variable was written. No `mem_map` change, no WD33C93, no ADC, no SCC
code, no ES5510 activation. No `-log`. No unifying hypothesis without
measurement — Del 4's own answer ("no gate, two disjoint subsystems")
is the measured negative, not smoothed into a claim that a connection
exists somewhere unexamined.
