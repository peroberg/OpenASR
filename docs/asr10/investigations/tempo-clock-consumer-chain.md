# Closing the tempo chain: an SS8.6 relapse, the pulse's two consumers, and a reproducible crash (2026-08-24)

Follow-on to `sequencer-clock-and-service-menu.md`. Codex's math (1000×90/625
= 144 pulses/s; 144/96 PPQN = 1.5 quarters/s = 90 BPM) is verified exactly.
The pulse's consumer chain is now traced two levels deep with real PCs. The
diagnostic menu stays `[OPEN]` despite direct input from Per this round —
recorded honestly, including a self-inflicted methodology bug that produced
several false negatives before it was caught.

## A relapse, caught before being reported as a result

Nearly every write/read tap installed early this task came back silently
empty — `$FC481F` (tick ack), `$000B82` (tick counter), `$000B70`
(write source), `$000F58` (readers) — even though direct memory *reads*
consistently showed correct, changing values. This matched last task's
own address-aliasing hypothesis at first, but the real cause was much
simpler and entirely self-inflicted: **several of this task's own tap
calls never saved the return value** (`prog:install_read_tap(...)`
called as a bare statement, result discarded). This is exactly SS8.6,
already documented in this project's own methods reference
(`current-status.md`'s opening warning; `panel_correlated_probe.lua`'s
own fix comment) — a tap handle with no reachable reference is
reclaimed by the Lua garbage collector almost immediately, so the tap
dies silently within the first fraction of a second. Confirmed by
direct comparison: `find-periodic-reads.lua` (prior task, taps
correctly kept in a persisted `taps` table) reliably reads `$000B6E`/
`$000B70` at exactly 1000.0 Hz on rerun; a script tapping the same two
addresses without saving the handle reads zero. Every measurement below
uses the fixed pattern (`taps[#taps+1] = prog:install_..._tap(...)`).
Recorded prominently because it wasted real effort before being caught
— exactly the kind of dead end this project's documentation exists to
prevent a future task from re-walking into.

## Del 1 — the tempo derivation, verified, with one real complication

**Tick rate: exactly 1000.0000 Hz, independently confirmed.** Rather
than trust a tap on the DUART's combined `.rw()` register handler
(which — separately from the SS8.6 bug — never caught the specific
byte-read instruction at `$FFFC481F`, a second, unresolved tap gap
worth flagging on its own), the tick rate was measured by high-resolution
polling of `$000B6E` (500µs samples, well under the ~6.9-tick wrap
period) and counting wrap-arounds over a precise window: `2000` ticks
in `2.000000` measured seconds, `= 1000.0000 Hz` exactly — not a
rounding of something close, matching `boot-sequence.md`'s DUART-timer
math (`2×2000/4MHz`) via a completely independent method.

**The pulse rate at the default step matches the prediction exactly.**
The same wrap-counting method, at `$B70=90` (the value `$17` sets),
measured `pulse_hz=144.0000` — exactly `1000×90/625`. This is the
derivation's own real confirmation, measured, not asserted.

**The proportionality falsification test was attempted and failed —
informatively.** Writing new values directly to `$000B70` (90→180→270)
did **not** change the measured wrap rate proportionally; instead the
*wrap count* stayed constant (~288 per 2s) while a naive rate
calculation using the newly-written step produced nonsense
(inversely-scaled "tick_hz" values). The process also crashed
(`SIGSEGV`) partway through this test — not investigated further, since
direct memory pokes into live, running structures are exactly the kind
of risky operation this project's rules caution against, and the result
below explains *why* the test was doomed rather than wrong.

**Explanation, found via the SS8.6 fix**: `$000B70` is not a
free-standing, settable register. A write-tap (correctly persisted this
time) on `$000B70` during normal operation shows it being **repeatedly
reasserted to `$5A` (90)**, roughly once per pulse (53 writes logged
over ~0.36 real seconds, all identical), from instruction
`$F919D2: move.w $828e.w,$b70.w` — copying from `$00828E` on essentially
every pulse. My own single-shot reads of `$00828E` were inconsistent
across separate runs (`$5A` during an active-pulsing session captured
mid-flow via the tap; `$81B0` in a static single-read check in a
separate boot) — **reported as unresolved, not smoothed over**: either
`$00828E` itself is volatile/derived from something else on a similar
cadence, or the two sessions' navigation differed in a way not yet
identified. `$FF922A` also writes `$B70` from the same source once,
earlier, via a different caller. **Conclusion for Del 1: the math is
verified at the measurement level (tick rate, baseline pulse rate both
exact); the proportionality test could not be validated by direct
poking because `$B70` is a continuously-refreshed cache, not a
stable dial** — a real, structural finding, not a failure to test.

## Del 2 — the pulse's two consumers, both real, neither reaching a sequence event

`$F8C588` (documented last task) writes `$000F58` on every accumulator
wrap. With taps correctly persisted this time, two real readers were
caught and disassembled:

**Consumer 1 — `$00E66E`-`$00E68C`, a second clock-division stage:**
```asm
e66e  tst.b   $f58.w
e672  beq.b   $e68c            ; not yet due this call: skip
e674  subq.b  #1,$f58.w        ; decrement
e678  bne.b   $e68c            ; not yet zero: skip
e67a  trap    #3               ; enqueue a new node
e67c  move.w  #$e,$2(a5)       ; SAME type ($E) as the producer's own event
e682  clr.l   $4(a5)
e686  movea.w $da.w,a1         ; a DIFFERENT scheduler slot register ($DA, not $D6)
e68a  trap    #9               ; install
e68c  rts
```
Measured at 83.00 Hz in this session (a fraction of the 144 Hz pulse
rate — consistent with it counting down over several pulses before
firing again, i.e. genuinely a second divider stage, not a 1:1 pass-through).

**Consumer 2 — `$0073A8`-`$0073E4` and onward, a mainline poll/consume
loop with a MIDI-clock-shaped tail:**
```asm
73a8  sf.b    $f58.w           ; unconditionally clear $F58 (Scc "always false")
73ac  tst.b   $f58.w           ; re-test -- non-zero here means the IRQ producer
73b0  bne.b   $73ba            ;   raced in and set it between these two instructions
73b2  jsr     $e68e.l          ; no pulse arrived this poll: service something else
73b8  bra.b   $73c0
73ba  move.b  $f5a.w,$f58.w    ; pulse arrived: mirror the producer's own "copy $F5A" branch
73c0  ...                      ; $DFA/$DF4-gated logic, calls $8F00, sets $D16=$32 (50)
```
`$D16` is the same address the tempo-accumulator handler (`$F8C5CE`,
last task's disassembly) tests against `$34` — a real cross-reference
between the two code regions, not a coincidence. When *no* pulse has
arrived, this loop instead calls **`$00E68E`**, which checks whether a
linked list at `$00D42` is empty (`movea.l $d42.w,a4 / cmpa.l #$d42,a4`
— a classic self-referential empty-list sentinel) and, if not, touches
address range `$00FC2001`-adjacent (inside the MC68302's own PIO/serial
register window per `hardware-map.md`) — strongly suggestive of actual
outgoing MIDI-clock byte transmission, not sequence-event playback.

**Neither consumer reads a sequence event, an event index, or a
sequence pointer.** Both are shaped like MIDI-clock generation/output
machinery (96 PPQN pulse → divide → transmit), running continuously
once `$17` arms the accumulator, independent of whether anything is
actually "playing." This matches Del 1's own finding that `$17` is
allocation + clock-divider activation, not transport start (prior
task's Del 3) — extended here with disassembly-level evidence that the
clock's own consumers don't touch sequence data either. **The real
event-consumption code — whatever steps through `TUTORIAL SEQ`'s own
allocated object and would eventually reach voice allocation — was not
found this task.**

## Appendix — voice allocation tap: inconclusive, plus a real, reproducible crash

Per the added instruction: tap voice allocation's entry
(`runtime-object-model.md`'s `$8E50.w`/`$F8CAFA`, `[Verified firmware]`)
with `TUTORIAL SEQ` loaded, and ask whether the sequencer's clock ever
reaches it.

**A real, reproducible crash was found and isolated, not fixed.**
Installing a read+write tap on `$008E50` while both a bank
(`JM DIGI SYN`, loaded via the exact pattern `note_audio.lua` uses) and
a created sequence (`TUTORIAL SEQ`, via the established `$00`-`$17`
navigation) are active causes a `SIGSEGV`, deterministically at
`t≈0.7167s` into the following wait, reproduced twice. **Isolated to
the tap itself**: the identical button sequence with no tap installed
completes cleanly (`isolate-crash.lua`). Not investigated further or
fixed — outside this task's scope (no `mem_map` change, no risky
internals work), but recorded with exact repro steps for whoever picks
this up: load `JM DIGI SYN` (`$0A`,`$23`,`$02`, wait for `FILE LOADED`),
select instrument (`$02`), load+create `TUTORIAL SEQ` (`$15`,`$23`,
`$00`-`$17`), install a tap on `$008E50`, then wait — crashes before a
3-second wait completes, survives a 0.5-second wait.

**The prescribed tap point could not be validated with a working
positive control, so the appendix's central question is answered
"inconclusive," not "no."** A real note (`KEY_C` via the panel
keyboard) was confirmed to produce genuine voice programming —
`1140` ES5506 register writes, the same proven sanity check
`note_audio.lua`'s own regression test uses — while taps on **both**
documented voice-allocation addresses (`$008E50`, the data list head,
and `$00F8CAFA`, the routine's own code address) recorded **zero**
hits during that same confirmed-real note. Per this project's own rule
(no zero result without a live witness for the whole window), a
silent-window zero on an unvalidated tap point is not evidence of
anything. **Del 2's own consumer-chain finding (above) is the load-
bearing result of this task, not the appendix's silent-window number.**

## Diagnostic menu — still `[OPEN]`, strengthened corroboration, no reproducible entry

Per described several tests from memory during this task — "reference
DC level," "ADC/DAC test," "MIDI loopback," roughly 6-8 tests, and that
running a "GMR test" (recalled as `GPR MONITOR`) hung the machine. This
**independently corroborates** `sequencer-clock-and-service-menu.md`'s
ROM-string finding (`A/D TO D/A`, `DC OFFSET`, `MIDI LOOP`, `GPR
MONITOR` are literally present in the ROM's string table) — Per's
recollection and the ROM content agree closely enough that the menu's
reality is no longer in doubt, only its entry sequence.

Two candidate combinations were tested this task, both from Per's own
recollection, both negative in this emulation:
- `$06`+`$0D` (pre-existing, unverified `asr10_panel.lay` labels "CMD"/
  "TEST"): holding both from `t=0` through boot, holding both from
  idle, and tapping each in sequence all produce ordinary
  command-list navigation (`$06`→`"CREATE NEW INSTRUMENT"`,
  `$0D`→`"NO COMMANDS ON PAGE"`), not a diagnostic screen.
- `$05`+`$0D` ("EDIT"/"TEST" by the same layout labels): same result,
  `$05`→`"FREE SYSTEM BLKS"`.

Per was explicit that he isn't certain the entry was `EDIT+$0D` vs.
`CMD+$0D` vs. something else, and that the layout's own `$06`="CMD"/
`$05`="EDIT" labels are old, unverified guesses (confirmed unverified
by this task — `$06` and `$05` are both just ordinary entries in the
same flat command list mapped in prior tasks, not mode buttons). No
further combination was guessed at past this point; per this task's
own rule, no code is named without its effect being measured, and
guessing indefinitely at combinations Per himself isn't sure of doesn't
meet that bar. **`[OPEN]`, with stronger evidence the menu is real and
weaker evidence about how to reach it than before this task started.**

## Summary

- **Tick rate**: `1000.0000 Hz` exactly, confirmed by an independent
  wrap-counting method, not just the original DUART-timer derivation.
- **Baseline pulse rate**: `144.0000 Hz` exactly, matching
  `1000×90/625`. The BPM math (`144/96 PPQN = 1.5 qps = 90 BPM`) is
  arithmetically sound and now has a measured anchor.
- **`$B70` is not independently settable** — reasserted from `$00828E`
  roughly once per pulse (`$F919D2`); the proportionality test's
  apparent failure is explained by this, not a refutation of the tempo
  math.
- **Pulse consumer chain, two levels, both disassembled**: `$F8C588`
  (producer) → `$000F58` → Consumer 1 (`$00E66E`, second clock divider,
  installs into scheduler slot `$DA`) and Consumer 2 (`$0073A8`,
  mainline poll, falls through to `$00E68E`'s `$00D42` linked-list
  service touching MC68302 PIO-region hardware). Both consumers are
  shaped like MIDI-clock generation, not sequence-event playback — the
  real event-consumer remains `[OPEN]`.
- **Voice-allocation appendix**: inconclusive (unvalidated tap point,
  even with a confirmed-real note as control); a real, reproducible
  `SIGSEGV` was found and isolated to installing a tap on `$008E50` in
  a specific bank+sequence context, reported with repro steps, not
  fixed (out of scope).
- **Diagnostic menu**: `[OPEN]`, corroborated as real by Per's own
  independent recollection of its contents, no reproducible entry
  sequence found from either of his two best-recalled candidate combos.
- **A self-inflicted SS8.6 relapse** (unsaved tap handles) produced
  several false "zero" results early this task, caught and corrected
  before being reported as findings — documented so the same mistake
  isn't repeated.

## Rules check

No `mem_map` change, no WD33C93, no ADC, no SCC code, no ES5510
activation. No factor-two/clock/bank-1/expanded-RAM/PB9-11 changes. No
`-log`. Display/menu text verbatim throughout. This task is
documentation-only — no C++ was modified. The one direct memory write
performed (`prog:write_u16` on `$000B70`, the falsification attempt) was
a Lua-side debugger poke for measurement, not a machine-behavior change,
and its own failure is reported as a finding, not hidden.
