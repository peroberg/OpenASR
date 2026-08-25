# Slot 5 re-checked against §8.10; the $001098 table explained as normal behavior; effects-table writer partially open; voice lifetimes measured but ambiguous

Follow-up to `transport-ab-test-play-stop-continue.md`, prompted directly
by that task's own §8.10 audit gap: the Slot 5 → voice-programming
finding rested on the same un-PC-correlated address family as the
now-corrected `$007C7C`, and had to be checked before anything more was
built on it.

## Del 1 — Slot 5 re-tested, PC-correlated: the connection holds, the address was wrong

Re-tapped the whole chain (`$00780C`, `$00782A`, `$007830`, `$007C7C`,
`$007CA8`, `$007E24`) simultaneously, PC-correlated, first with a raw
`:panel:keys_0` press (no result — see the dead end below) and then with
`note_audio.lua`'s own proven stimulus (load `JM DIGI SYN`, an explicit
**separate** `"select Instrument 1"` press after `FILE LOADED` — missed
in the first attempt, which produced zero results everywhere despite the
note fields changing — then a real MIDI note-on injection).

**Dead end, reported so it isn't repeated**: a raw `:panel:keys_0` press
writes `$000D08`/`$000D09` (confirmed: `$00→$60`/`$00→$3C`, i.e. release
signature + Middle C) but produces **zero** ES5506 writes and zero hits
anywhere in the chain, even held 2 seconds and observed for 5 more. This
looks like a stimulus that reaches the note-field bytes without driving
the rest of the real panel-protocol pipeline — not investigated further,
flagged as a trap for future scripts reusing this port.

**With a real, MIDI-confirmed note** (`rhra_delta=3`, `voice_writes_delta=3174`,
matching `note_audio.lua`'s own passing thresholds):

```
$007830 (jsr $7ca8.w, inside Slot 5's own code): 1 hit, PC==$007830  -- MATCHED
$007CA8 (the jsr's target):                      2 hits, one PC==$007CA8 -- MATCHED
$007C7C (previously called "the entry"):          1 hit, PC==$007E24     -- NOT matched
$007E24:                                          173 hits, PC==$007E24 (172x) -- a real, distinct, frequently-executing routine
```

**Conclusion: the Slot 5 → voice-programming connection holds, properly
PC-correlated now.** `$007830` and `$007CA8` are both confirmed
executing, exactly once for one note, real evidence per §8.10 (an actual
instruction fetch at the tapped address, not a bare read hit). What was
wrong was the specific address cited as "the routine": **`$007CA8`, not
`$007C7C`** — the two are 44 bytes apart inside the same block
(`$007C7C-$007CEE`) prior rounds described as one unit. `$007C7C` itself
is never executed in any test this task ran; it is read as data, once
per note, by `$007E24` — a real, previously undocumented routine in its
own right, `[OPEN]` for future work.

**Corrected in place, not deleted** (per this project's own convention):
`current-status.md` (three separate spots — the original
`trap-c-and-the-real-note-path.md` summary, the
`slot5-connects-notes-to-voice-programming.md` summary, and last round's
own `$007C7C` bullet), `slot5-connects-notes-to-voice-programming.md`
(Del 1's own tap, which used a bare read on `$007C7C`'s entry with no
PC-correlation — the 0/1/0-calls shape is not retracted, since Del 2's
independent, now-PC-correlated measurement reconfirms a real
once-per-note event; only the address it was attributed to is wrong),
and `note-velocity-structure-and-sequencer-silence.md` (one bullet
naming `$007C7C` as the relay target).

**No other project conclusion depends on `$007C7C` executing** — checked
directly: every other file mentioning the address either already treats
it as a block/range identifier (not a single instruction) or is one of
the four files just corrected.

## Del 2 — the $001098 table's cycling in case B: normal, not a bug

**The bank files' own on-disk content, read directly (not inferred):**
`ATRK TUT BNK` (disk offset `0x153000`) references exactly three
instrument-catalog entries (`O` markers `$0C`/`$0D`/`$0E` = catalog
indices 12/13/14 = `BLUES DRUMS`/`BLUES BASS`/`BLUES ORGAN`) plus one
more (`$0F` = index 15 = `ATRK TUT SNG`, the bundled song — not another
instrument). `TUTORIAL BNK` (disk offset `0x32C00`) references **six**
instrument entries (`O` markers `$03`-`$08` = catalog indices 3-8 =
`JM DRUMS`/`DEMO PERCS`/`MOOG POP 1`/`HIGH STRINGS`/`JM CLAV`/`OB-8`).
This matches the slot-table population counts from last round exactly:
6 distinct values for A, 3 for B.

**The manual's own physical control layout** (`panel-manual.md`,
Section 1 "Additional Front Panel Controls"): eight
**`Instrument/sequence track`** buttons, numbered `1`-`8` — one fixed
physical control group serving double duty as instrument-slot selector
during loading and as sequence-track selector during sequencing. This
directly explains the table's fixed 8-slot size, independent of how many
real instruments a bank provides.

**Conclusion: B's 3-values-cycling-across-8-slots pattern is the direct,
mechanical, predictable consequence of a 3-instrument bank populating a
fixed 8-track table — not a loader defect.** A 3-instrument bank driving
an up-to-8-track song has no other correct way to fill all 8 slots than
by repeating instrument references across tracks. Chasing this as a bug
would be chasing correct behavior.

**Writers, tapped and compared (full `$001098-$0012F0` span, PC per
write, both cases)**: a shared boot-time init pass (`$F87DDA`, ~301
words of `$0004` filler, then `$FB8ABE`, ~602 byte-writes of mostly
zero) runs identically in both, well before any file loads. The
**real per-slot population differs**: case A's load touches `$F8A00E`
(102 writes), `$F9256E` (24), `$FFAF36/48/4E/52` (6 each),
`$F8A018/01E` (6 each), and others — case B's load touches the **same**
family at different counts (`$F8A00E`=51, `$F9256E`=70, `$FFAF*`=9 each,
`$F8A018/01E`=3 each) **plus three PCs A's load never reaches at all**:
`$00DE16` (170 writes), `$00DE10` (5), `$00DE0A` (5), and `$F95470` (8).
The extra writers match exactly what's expected: B's load also
processes a bundled Song (this specific A run loaded the bank alone,
without a separate sequence step) — consistent with, not a coincidence
alongside, the manual's own track/instrument-slot framing above. Full
disassembly of `$00DE16` etc. was not attempted this task — the PC
identities and counts are the measured fact; what each one specifically
does is `[OPEN]`.

## Del 3 — the effects-preset table: content confirmed, writer mechanism not found; the control run's navigation problem described, not solved

**Content reconfirmed**: `$02CA80` onward holds `"HALL REVERB"`,
`"JUST REVERB"`, `"MORE REVERB"`, `"ALSO REVERB"` after a fresh-boot
`ATRK TUT BNK` load — read directly from CPU memory at the end of the
load, byte for byte matching last round's finding.

**Writer not found — a genuine method gap, reported precisely rather
than papered over.** A write-tap on the full `$02CA00-$02CB80` span
during the load captures exactly one thing: a boot-time filler pass
(`$F87DDA`, 193 writes of `$00B2`) at `t≈1.1s`, **and nothing else**
through the rest of the load and 3 further seconds. The final memory
content is nonetheless the real reverb strings, not the filler value —
meaning whatever writes the real text does **not** go through a normal
CPU `MOVE`-style write that `install_write_tap` on this address range
would catch. This matches the project's own established blind spot
(`methods-static-analysis.md` §7: register-relative and, by extension,
DMA/bulk-copy mechanisms are invisible to address-keyed instrumentation)
— not chased further this task; the identity of the real writer is
`[OPEN]`.

**The control run (`ATRK`→measure→`TUTORIAL BNK`→measure→`ATRK`→measure):
attempted properly this time, still blocked — described exactly, not
glossed over.** Confirmed directly: after a Song-bundling bank load
(`ATRK TUT BNK`), the file browser becomes **permanently locked** to a
3-entry cycle (`FILE 9 TUTORIAL SEQ` / `FILE 15 ATRK TUT SNG` / a
synthesized `"TUT0RIAL SNG"` screen with no matching catalog entry) —
tested with `$0A`, `$0B`, `$10`, and `$11` in every combination, 20+
steps each, never reaching `TUTORIAL BNK` or any other bank/instrument
entry again. The manual documents a dedicated **`Instrument`**
object-page button, distinct from `Load`/`Seq-Song`, that should return
to instrument browsing (`panel-manual.md`, "Object pages") — but its
button code is not established anywhere in this project
(`panel-button-and-transport-map.md` already lists it `[OPEN]`:
*"14 Page Buttons... unidentified `BTN_XX`"*). A bounded sweep of
plausible neighboring codes (`$00`,`$01`,`$03`-`$05`,`$12`-`$14`,`$16`,
`$1A`) found `$04` opens an **Instrument edit/parameter page**
(`"BLUE5 BA55 ?0LUME?99"`) — a new, real data point for that `[OPEN]`
gap — but not the file browser, so it does not solve the navigation
problem. **The control run as specified was not completed.** Finding the
actual return-to-Instrument-browsing button is a bounded, concrete task
for a future round, not attempted exhaustively here.

**Fallback used instead**: last round's own imperfectly-controlled run
(`ATRK`→[`"TUTORIAL SNG"`-labeled screen, not `TUTORIAL BNK`]→`ATRK`)
remains the only data point: the effects table holds real strings after
the first, fresh-boot `ATRK` load and reads as fully zeroed after the
second. Repeated here as still-standing evidence, not newly confirmed —
the middle step was not `TUTORIAL BNK`, so it cannot yet be tied cleanly
to the task's own "B after A improves" hypothesis, exactly as flagged
last round.

## Del 4 — voice lifetimes measured as numbers, but the metric turns out ambiguous

Tapped every ES5506 `CR` (voice control register) write during a 6-second
Play window in both cases, with each write's paired `START`/`END` values
captured at that instant, and computed the interval between successive
`CR` writes to the same voice.

```
A: 26 voices touched, 900 CR writes, 874 measured intervals
   min=0ms  median=0ms  mean=142.8ms  max=2313.6ms
   intervals <10ms: 459/874 (52.5%)   <5ms: 451/874 (51.6%)

B: 21 voices touched, 368 CR writes, 347 measured intervals
   min=0ms  median=0ms  mean=200.6ms  max=2981.9ms
   intervals <10ms: 192/347 (55.3%)   <5ms: 186/347 (53.6%)
```

**This does not cleanly discriminate A from B.** Medians are both ~0ms
(most `CR` writes arrive in back-to-back register-write bursts, not
spread over time); means and the <10ms proportions are close between the
two cases; B's mean interval is *longer*, not shorter. **Every single
measured interval, in both cases, paired with an identical `START`/`END`
as the previous write to that voice** (874/874 for A, 347/347 for B) —
meaning most `CR` writes are **not** full retriggers with a new sample
address; they are repeat writes to an already-programmed voice, most
plausibly envelope/gate-state bits inside `CR` toggling (key-on/off or
similar) without reassigning the sample. This project has not
established which `CR` bit(s) correspond to that gating, so a raw
inter-write interval **cannot be safely read as "how long the note plays
before dying"** without that decode — reported as measured, with this
limitation stated plainly rather than presented as a clean answer. The
strongest evidence for what causes the reported clicking remains Del 3's
prior-round finding (case B's active voices redundantly sharing only 4
sample regions across ~18 voices) — this task's own lifetime metric
neither confirms nor refutes that; it measures something adjacent
(register-touch frequency) that turned out not to be the right number.

## Del 5 — journal (this file) plus the transport recap

- `$1D`=Play and `$17`=Stop/Continue are dynamically verified
  (`transport-ab-test-play-stop-continue.md`), and `$17` is **genuinely
  context-dependent**: inert at idle, `"CREATE NEW SEQUENCE"` in its own
  deep Command-menu prefix context, Stop/Continue once a sequence is
  actively playing. Both prior attributions were correct in their own
  contexts — neither was wrong, and nothing here changes that.
- Slot 5's connection to voice programming holds, PC-correlated; the
  entry address is corrected from `$007C7C` to `$007CA8`, spread through
  every prior conclusion resting on it (Del 1 above).
- `ATRK TUT BNK`'s slot-table cycling is normal behavior for a
  3-instrument bank filling a fixed 8-track table, confirmed against
  both the bank file's own on-disk content and the manual's physical
  control layout (Del 2 above) — not a bug to chase.
- `$001098`'s writers differ between A and B by the addition of
  Song-specific code (`$00DE16`/`$00DE10`/`$00DE0A`/`$F95470`) that a
  plain bank-only load never reaches (Del 2 above).
- The effects-preset table's content is confirmed but its writer
  mechanism evades address-keyed write-tapping — a real, reported
  method gap, not a solved question (Del 3 above).
- Voice lifetimes were measured as real numbers in both cases, but the
  metric does not discriminate A from B without a `CR`-bit decode this
  task didn't have time for — reported honestly as inconclusive, not
  forced into a clean story (Del 4 above).

## Rules check

No firmware variable written, read-only throughout. No model change —
measurement only. No `mem_map` change, no WD33C93, no ADC, no SCC code,
no ES5510 activation. No `-log`. No forks (§11) — every experiment and
edit in this file was run directly by the active main agent. Display
text verbatim throughout. Every positive execution claim in Del 1 is
PC-correlated per §8.10; Del 3 and Del 4's negative/ambiguous results are
reported as such, not dressed up as resolutions.

## Deletion accounting

No C++ changed. No Lua committed — every script this task used lived in
the session scratchpad. Documentation: this file plus five correction
edits to existing files (`current-status.md` ×3 spots,
`slot5-connects-notes-to-voice-programming.md`,
`note-velocity-structure-and-sequencer-silence.md`) and a
`DOCUMENTATION-MANIFEST.md` update.
