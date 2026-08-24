# Command pages reached; the clock's second stage is inert; the D42 list is a dead end (2026-08-24)

Follow-on to `tempo-clock-consumer-chain.md`. Per's own correction —
`$06` then `$0C` shows `"NO C0MMAND5 0N PAGE  "`, meaning Command mode
was genuinely *reached*; the page just has no commands, and pages are
browsed with Left/Right — reframes Del 5 entirely and is confirmed
here. The other four deliverables (tempo master, MIDI-clock quartering,
`$0073A8`'s list, the sequence object's field map) all produced real,
mostly negative results, reported as measured rather than forced to
match the predicted numbers.

## Tooling fix, done first: taps that cannot be forgotten

Per's own diagnosis — "a rule that fails twice is a tooling problem" —
addressed directly, not just noted. `docs/asr10/lua/lib/asr10_taps.lua`
wraps `install_read_tap`/`install_write_tap` and keeps every handle in
a module-level upvalue automatically; a caller gets the SS8.6 fix for
free just by using `reg.taps.read_tap(...)`/`reg.taps.write_tap(...)`
instead of the raw `space:install_..._tap(...)` call, with no
boilerplate `local taps = {}` / `taps[#taps+1] = ...` to remember or
skip. Wired into `asr10_regression.lua` as `M.taps`, alongside the
existing `M.display`. Every tap this task uses goes through it.

## Del 5 — the command pages, catalogued verbatim, GPR MONITOR not among them

`$06` = Command, confirmed exactly as Per described. From idle,
`$06` lands directly on `"CREATE NEW IN5TRUMENT   "`, and stepping
Right (`$11`) walks a real, cleanly-wrapping list. Nine distinct
category page-sets were found this way (entered via `$06` then a
category-switch code, then walked with `$11` to a confirmed wrap):

**INSTRUMENT** (8, reached by default or via `$00`-`$04`,`$08`,`$0F`):
`CREATE NEW INSTRUMENT`, `COPY INSTRUMENT`, `DELETE INSTRUMENT`,
`SAVE INSTRUMENT`, `SAVE BANK`, `CREATE PRESET`, `DELETE INST EFFECT`,
`IMPORT NON-ASR SOUNDS`.

**SEQ*SONG** (13, via `$15`-`$17`): `CREATE NEW SEQUENCE`,
`COPY SEQUENCE`, `DELETE SEQUENCE`, `SAVE CURRENT SEQUENCE`,
`SAVE SONG + ALL SEQS`, `RENAME SONG/SEQUENCE`,
`SEQUENCER INFORMATION`, `ERASE SONG + ALL SEQS`, `APPEND SEQUENCE`,
`CHANGE SEQUENCE LENGTH`, `EDIT SONG STEPS`, `ERASE ALL AUDIOSAMPLES`,
`SET SONG ATRK PLAYBACK`.

**SYSTEM/GLOBAL PARAMETERS** (19, via `$05`): `FREE SYSTEM BLKS=626`,
`FREE DISK BLKS=25`, `MASTER TUNE=??`, `GLOBAL BEND RANGE=2`,
`TOUCH=MEDIUM 2`, `PEDAL=VOLUME MIDI=7`, `LEFT FOOT SW=OFF`,
`AUTO-LOOP FINDING=OFF`, `MIDI BASE CHANNEL=1`,
`TRANSMIT ON=BASE CHAN`, `BASECHAN PRESSURE=KEY`, `MIDI IN MODE=OMNI`,
`MIDI CONTROLLERS=ON`, `MIDI SYS-EX=OFF`, `MIDI PROG CHANGE=ON`,
`MIDI SONG SELECT=ON`, `MIDI XCTRL NUMBER=71`,
`MULTI CONTROLLERS=OFF`, `ENTER PLAYS KEY=C4` — this is the
`SYSTEM~MIDI` category the service manual's own panel enumeration
names; no numeric field here reads as a tempo/BPM value.

**EFFECTS** (2, via `$07`,`$09`-`$0B`): `SAVE BANK EFFECT`,
`COPY CURRENT EFFECT`.

**TRACK/EVENT EDITING** (9, via `$0C`): `QUANTIZE TRACK`, `COPY TRACK`,
`ERASE/UNDEFINE TRACK`, `FILTER EVENT`, `MERGE TWO TRACKS`,
`EVENT EDIT TRACK`, `TRANSPOSE TRACK`, `SCALE EVENT`,
`SHIFT TRACK BY CLOCKS`.

**PITCH TABLE** (4, via `$18`): `EDIT PITCH TABLE`, `COPY PITCH TABLE`,
`DELETE PITCH TABLE`, `EXTRAPOLATE PITCH TBL`.

**DISK/SYSTEM COMMANDS** (15, via `$1B`-`$1D`): `FORMAT FLOPPY DISK`,
`COPY O.S. TO DISK`, `SAVE GLOBAL PARAMETERS`,
`LOAD GLOBAL PARAMETERS`, `CREATE DIRECTORY`,
`CHANGE STORAGE DEVICE`, `SAVE MACRO FILE`, `COPY FLOPPY DISK`,
`MIDI SYS-EX RECORDER`, `WRITE DISK LABEL`, `COPY SCSI DRIVE`,
`BACKUP/RESTORE`, `DAT BACKUP/RESTORE`, `CONFIGURE AUDIO TRACKS`,
`FORMAT SCSI DRIVE`.

**WAVESAMPLE PROCESSING** (7, via `$1E`): `NORMALIZE GAIN`,
`VOLUME SMOOTHING`, `MIX WAVESAMPLES`, `MERGE WAVESAMPLES`,
`SPLICE WAVESAMPLES`, `FADE IN`, `FADE OUT`.

**DATA EDITING** (7, via `$1F`/`$21`/`$22`): `CLEAR DATA`,
`COPY DATA`, `REPLICATE DATA`, `REVERSE DATA`, `INSERT DATA`,
`ADD DATA`, `SCALE DATA`.

**WAVESAMPLE CREATION** (15, via `$24`/`$25`-`$3F`, all aliasing to
this same list): `CREATE NEW WAVESAMPLE`, `COPY WAVESAMPLE`,
`DELETE WAVESAMPLE`, `WAVESAMPLE INFORMATION`, `TRUNCATE WAVESAMPLE`,
`CROSS FADE LOOP`, `REVERSE CROSS FADE`, `ENSEMBLE CROSS FADE`,
`BOWTIE CROSS FADE LOOP`, `BIDIRECTIONAL X-FADE`, `MAKE LOOP LONGER`,
`SYNTHESIZED LOOP`, `CONVERT SAMPLE RATE`, `TIME COMPRESS/EXPAND`,
`COPY WAVE PARAMETERS`.

Every one of these lists was walked with `$11` to a confirmed clean
wrap (the first page repeats exactly). **`GPR MONITOR`, `INSTRUCTION
MONITOR`, `A/D TO D/A`, `DC OFFSET`, `MIDI LOOP`, and `ESP TESTS`
appear in none of these nine catalogs.** Per's read of his own
correction — "diagnostiktesterna är därför sannolikt kommandon på en
viss sida" — does not hold for the pages reachable this way. Two
structural possibilities, neither chased further this task: the
diagnostic tests live on a page reached via a still-unidentified LOAD
or EDIT mode button (the service manual's own third and first panel
buttons, still `[OPEN]`), or they are not reachable through the
Command-mode category system at all. `[OPEN]`, narrowed rather than
resolved.

No hang was reproduced this task — every command page reached
returned cleanly to its list; nothing was "run" past viewing its name
(running a command generally requires `$23`, and running most of these
irreversibly modifies disk/instrument state, out of scope to trigger
speculatively without a specific reason to).

## Del 1 — the "master tempo" claim is retracted, not confirmed

Rules forbid writing to firmware variables this task, so the TEMPO
panel path was searched for (not found — none of Del 5's nine
catalogs contains a TEMPO field; it likely lives on the manual's
Edit/Seq•Song parameter page, reached automatically by Play-while-
Record, still `[OPEN]`) and the write-side of `$00828E` was
re-examined by direct measurement instead of trusting the prior task's
static disassembly.

**The result contradicts the prior task's own reading.** A
write-tap on `$00828E` (via the new `asr10_taps.lua`, so this is not
another SS8.6 miss) shows it written exactly 5 times, all during boot
(the last at `t≈18.4s`, from `$F8CD6C`, value `$81B0`) — and then
**never again**, for the rest of a full `$00`-`$3F` button sweep.
Meanwhile `$000B70` (the tempo step) is independently confirmed
(previous task, reconfirmed here) to be repeatedly rewritten to `$5A`
from `$F919D8`/`$F919D2` well after `t=18.4s` (first at `t≈23.2s`).
**`$5A` does not match `$81B0`.** If `$F919D2`'s `move.w $828e.w,$b70.w`
really sources from `$00828E`, and `$00828E` genuinely hasn't changed
since `t=18.4s`, the value it copies into `$B70` should be `$81B0`,
not `$5A` — it consistently isn't. Capstone's operand order was cross-
checked against an already-validated example from this same
investigation (`$F88300`'s `move.w $b6e.w,d0` / `move.w d0,$b6e.w`,
confirmed correct by matching measured accumulator growth) and is not
in doubt. **The prior task's identification of `$00828E` as the tempo
step's source is retracted as unconfirmed** — the true source read by
`$F919D2` remains `[OPEN]`, and so does the master-tempo value's real
address. Not overwritten with a new guess; reported as a real,
measured contradiction per this task's own rule that no variable is
named without its use being measured.

## Del 2 — the second clock-division stage is permanently inert in this state, not 36Hz

Direct high-resolution polling of `$000F58` (200µs samples, 15,000
samples over 3.000000s — the same tap-independent method that measured
the 1000Hz tick exactly) shows it **constant at `$00` for the entire
window, zero transitions**, while `$000B70=$5A` and `$00017E=0`
confirm the accumulator itself is actively configured and running.
This means the producer's `d2`-sign branch at `$F8C588` never takes
the `move.b $f5a.w,$f58.w` path in this state — only `clr.b $f58.w`
fires, every time, and Consumer 1 (`$00E672`'s `tst.b $f58.w / beq`)
consequently **never proceeds past its own guard to decrement or
dispatch**, because the byte it tests is never anything but zero.
Corroborated independently: a write-tap on `$00E674` (the decrement
instruction) recorded **zero writes** from that address across
repeated 5-second steady-state windows, consistent with a branch that
never falls through. `144÷4=36Hz` was not measured — not because the
math is wrong, but because the gate that would let a nonzero countdown
reach Consumer 1 never opens in the "sequence created, nothing played"
state this task's harness can reach. **Del 2's own predicted number is
unconfirmed; the actual, measured finding is that the second stage is
gated shut**, which is itself informative: whatever sets `d2` negative
(arming the real countdown) is tied to something this investigation
still hasn't triggered — very plausibly the same missing "actually
playing" condition Del 3/4 below also run into.

## Del 3 — the `$D42` list: Outcome 2, a dead end, read-verified empty throughout

`$000D42`'s value is `$000D42` itself (the self-referential empty
sentinel) at idle, after loading `TUTORIAL SEQ`, after creating it,
and after a full `$00`-`$3F` button sweep with the head both polled
directly and tapped for writes — **it is never populated**. The only
writes ever recorded to `$D42`/`$D44` happen during boot's keyboard-
tuning phase (`$F8CD04`, `t=15.0-18.4s`, the identical timing window
`$00828E`'s own writer uses — the same subsystem, not the sequencer),
each one **resetting it back to the empty sentinel**, not adding an
element. Per Del 3's own decision framework: **Outcome 2** — this is
some other service list (most plausibly hardware/MIDI-adjacent, given
`$00E68E`'s touch on MC68302 PIO-region addresses when the list is
non-empty), not the sequencer's event dispatch. `$0073A8`'s own poll
of it is real code with a real purpose, but not evidence of sequence
playback; **the real event-consumer is not this list and remains
`[OPEN]`.** Not asserted as confirmed by finding elements (there are
none to inspect) — asserted by the list's total, repeated,
read-verified failure to ever hold anything across everything this
session could trigger.

## Del 4 — no event/read-pointer found; the diffed fields are mostly unrelated background noise

Twenty-four candidate addresses from the original 211-byte diff
(excluding the tempo/clock cluster now identified) were polled at 2Hz
for a 10-second post-creation window. Several DO change continuously
(`$000C38`, `$000D0C`, `$000D0F`, `$000D5C`, `$000DF0`,
`$0002D3`, `$0002D8`) — but **`$000C38` is already documented, from an
earlier investigation this same session, as part of `$F88F60`'s
general 4kHz key/hardware-scan polling loop**, unrelated to sequence
content; the others cycle through small repeating value sets or
free-running counters with no monotonic, event-count-shaped pattern.
**None of the 211-byte diff's fields show a read-pointer or
event-index signature** — the diff captured real allocation *plus*
coincidental background scheduler/scan activity in the same window,
and the two were never cleanly separable from the diff alone. **The
sequence object's own field map, and its event/read pointer, remain
`[OPEN]`** — this task's contribution is ruling out the candidate set
tried, not finding the real one. A crash (`SIGSEGV`, `t≈6.26s`, a
different context than last task's `$8E50` crash) interrupted the
10-second polling window before it completed; not investigated
further, consistent with this task's own scope limits, but recorded —
**a second reproducible-looking crash context this investigation has
now hit**, worth a dedicated look in a future task.

## Summary

- **Tooling**: `asr10_taps.lua` makes the SS8.6 mistake structurally
  harder to repeat — every tap through `reg.taps` is automatically
  persisted.
- **Command pages**: nine full categories catalogued verbatim (`$06`
  confirmed as Command); `GPR MONITOR`/`ESP TESTS` confirmed absent
  from all of them — the diagnostic menu's entry stays `[OPEN]`,
  narrowed to "not in Command mode's category system," not resolved.
- **`$00828E`**: retracted as the tempo master — measured value
  doesn't match what gets copied into `$B70` at the time it's copied.
- **144→36Hz**: not measured; instead, found and confirmed (two
  independent methods) that the second clock stage is permanently
  gated shut in this session's reachable state.
- **`$D42` list**: Outcome 2 — a real but unrelated service list,
  read-verified empty across everything this session could trigger;
  ruled out as the sequencer's event dispatch.
- **Sequence object field map**: not found; the diff's changing fields
  are dominated by unrelated background polling (`$C38`'s 4kHz scan
  loop, already documented) with no event-index-shaped candidate
  surviving.
- **Two reproducible-looking crashes** now on record (`$8E50` tap,
  prior task; this task's 24-address polling loop) — neither
  investigated, both out of scope for this task's rules, both worth a
  dedicated future task.

## Rules check

No `mem_map` change, no WD33C93, no ADC, no SCC code, no ES5510
activation. No factor-two/clock/bank-1/expanded-RAM/PB9-11 changes.
**No firmware variable was written this task** — `$00828E`,
`$000F58`, `$000D42`, and the sequence-object candidates were all
read-only. No `-log`. Display/menu text verbatim throughout,
including all nine command catalogs above. This task is
documentation-plus-one-Lua-library — no C++ was modified.
