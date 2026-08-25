# Slot 5 is the missing link; the six slots identified; the type table stays half-confirmed (2026-08-25)

Follow-on to `trap-c-and-the-real-note-path.md`. Both of this round's
cheap circuit-breakers paid off: tracing `$007C7C`'s callers across
states answers Del 1 as a clean `[Verified]` negative for the sequencer
path, and reading all six scheduler slots' identities (not just
occupancy) found the actual connector between note input and voice
programming — inside Slot 5, the already-known "poller."

## Del 1 — `$007C7C`'s callers, traced across states: no third caller, sequencer confirmed silent

Tapped `$007C7C`'s entry with stack inspection, across three states:

```
idle (post FILE LOADED, no input):     0 calls
panel note press (KEY_C):              1 call
$17 sequence-creation chain (no note): 0 calls
```

**`[Verified]` negative**: the sequencer-creation chain does not reach
`$007C7C` in any state this session can produce — zero calls across
the entire `$00`-`$17` navigation, the same result Del 1's own logic
predicted ("om ingen dyker upp är det bevisat att uppspelningen aldrig
kör"). No third caller exists in any state reached. The panel note's
own caller identity via stack inspection was **inconclusive** — a weak
candidate (`sp+2=$000100`) recurred consistently but could not be
confirmed as a genuine `JSR` return address rather than incidental
stack content (the register name issue that blocked this initially —
`cpu.state["A7"]` does not exist in this MAME build; the correct name
is `SP` — is recorded so a future task doesn't lose time on it again).
**Resolved properly via Del 2 instead** (below).

## Del 2 — all six scheduler slots identified; Slot 5 is the connector

Read all six slots' dispatch addresses (base `$23F6`, stride `$16`,
task pointer at `+6`):

| Slot | Task | Identity |
|---|---|---|
| 0 | `$002B4C` | Not fully characterized — a dispatcher touching effect/wave-adjacent constants (`$255A`/`$2562`), calls through several `jsr`/`jmp` branches. `[OPEN]`, not sequence-data-shaped in what was read. |
| 1 | `$FFC8B0` | Not fully characterized — another generic queue/event dispatcher (uses `TRAP #3`/`#4`/`#7`/`#8`, installs into slot `$23F6` itself via `TRAP #9`). `[OPEN]`. |
| 2 | `$0073EA` | **Confirmed** (two rounds ago) — the MIDI-clock mainline poll consumer. |
| 3 | `$F8F2FA` | **Confirmed** (last round) — the tempo/clock producer's own task, self-rearms via `TRAP #C`. |
| 4 | `$0069CC` | Not fully characterized — touches `$DB8`/`$DE4`/`$170` (the same `$170`-area byte family as `$17E`/`$17F`), looks clock/rate-adjacent. `[OPEN]`. |
| 5 | `$00780C` | **The connector — see below.** |

**Slot 5, disassembled and execution-confirmed**:

```asm
00780C  addq.w  #1,$d0b0.w        ; outer tick counter
007810  cmpi.w  #$b,$d0b0.w       ; divide-by-12 gate
007816  ble.b   $77d4             ; not yet due: skip
007818  jsr     $71e6.w
00781C  jsr     $e63c.l
007822  jsr     $e66e.l           ; <- $00E66E itself, MIDI-clock 2nd stage
007828  bra.b   $77ca
00782A  move.l  a5,$22(a4)
00782E  move.l  a5,-(a7)
007830  jsr     $7ca8.w           ; <- $007CA8, INSIDE the voice-programming routine
```

Measured directly:

```
Slot 5 entry ($00780C):  2000.00 Hz (idle) — the outer poll rate
$00782A (the note-check block): ~83 Hz (idle) — runs continuously,
                                              regardless of input
$007830 (the jsr into voice programming):
    idle:               0 calls
    one panel note press: exactly 1 call
```

**This is the missing link, measured, not inferred.** Slot 5 runs a
continuous ~83Hz background check (`$00782A`) — the exact same rate
this and prior rounds already measured for `$00E672`'s own poll,
confirming these share a common underlying cadence — and that check
conditionally calls into the real voice-programming routine
(`$007CA8`, inside the `$007C7C`-`$007CEE` block found last round)
**exactly once per note**, not on every poll. `$D11`/`$D08` (read into
`a4`'s fields just before the call) are the likely note/velocity
source bytes — not traced to their own writer this task, a concrete
next step. **Answers Del 1 more precisely than the stack trace could**:
the caller of voice programming is Slot 5's own periodic note-check,
not a one-off direct call from the key-press interrupt itself.

## Del 3 — the type table: still only one entry cleanly execution-confirmed

Re-read `$8258`'s table fresh (this session's context) — identical raw
values to the static read two rounds ago, confirming the table itself
is stable:

```
type $00: FFF8   type $02: 82C2   type $04: FFF8   type $06: 82C6
type $08: FFF8   type $0A: 82CA   type $0C: FFFC   type $0E: 6014
type $10: FFF8   type $12: 82DA   type $14: FFF8   type $16: 82DA
type $18: FFF8   type $1A: 82DA
```

**Only type `$0E` has an execution-confirmed dispatch** (prior round:
its `jmp (a0)` fires at exactly 144Hz via the single-word,
zero-extend-to-low-RAM interpretation, landing near `$006014`). This
round's attempt to tap the *other* twelve entries (idle + a note press)
found **zero executions for all of them** — but this is reported as
inconclusive, not confirmed-silent: the target-address decoding for
entries whose word value has bit 15 set (`$FFF8`/`$FFFC`, i.e. every
type except `$0E`) is genuinely ambiguous between "zero-extend the
single word" (what worked for `$0E`) and "combine with the adjacent
word as one 32-bit address" (what a prior round's own informal read
assumed, matching the `F88xxx`/`F8Cxxx`-style ROM addresses this
project's disassembly has consistently found elsewhere). **Static read
≠ execution-confirmed, kept explicitly separate here**: only `$0E` is
`[Verified firing]`; the rest are `[Read, target address ambiguous, not
execution-confirmed]`. Type `$02` (seen firing last round, routed to
Slot 2 via `TRAP #9`'s own `A1` parameter) was not shown to go through
*this* table at all — that observation came from a different
mechanism and shouldn't be counted as confirming this table's own
`$02` entry.

## Del 4 — a real, ROM-verified reference to the diagnostic strings, found for the first time; not yet resolved

Searched the static ROM image directly (host-side, not a live tap —
sidesteps the ROM→RAM overlay confound entirely, since this reads the
ROM file, not live emulated memory) for 32-bit absolute references to
the diagnostic strings' own addresses. One real hit: **`$00A304`**
(ROM file offset, matching CPU address in the pre-overlay low region):

```asm
00A2FA  move.l  $4(a0),d0
00A2FE  lsr.w   #4,d0
00A300  swap    d0
00A302  lsr.l   #4,d0
00A304  sub.l   #$101c,d0         ; $101C = GPR MONITOR's own address
00A30A  bmi.b   $a320             ; below range: skip
00A30C  cmp.l   #$20,d0
00A312  bcc.b   $a320             ; above range ($101C+$20=$103C): skip
00A314  lsr.l   #4,d0
00A316  move.l  d1,-(a7)
00A318  move.l  d0,d1
00A31A  jsr     $8c72.w
00A31E  move.l  (a7)+,d1
```

A genuine bounds check against exactly `$101C`-`$103C` — the 32-byte
window containing `GPR MONITOR` and reaching into `INSTRUCTION
MONITOR`'s own start. **Semantic connection to the string content is
not confirmed**: this code computes its comparison value from a
bit-shifted field extracted from `$4(a0)` (some runtime structure, not
identified this task), and the range `$101C`-`$103C` could plausibly be
validating that a *computed value* falls within a reserved memory
region that only *coincidentally* shares its numeric address with
where the ROM string happens to sit — the same kind of coincidence
this project has been burned by before (`trap3_enqueue`'s queue pool
sharing the ROM string addresses post-overlay). Reported as a real,
concrete lead — the first one found in three rounds of trying — not as
a resolution. Not traced further this task (`$8C72`'s own body, and
what `$4(a0)` actually is, remain `[OPEN]`).

## Summary

- **Del 1**: `[Verified]` — the sequencer chain never reaches
  `$007C7C` (0/0/0 across three states). The panel-note caller is
  identified precisely via Del 2 instead: Slot 5's own periodic check.
- **Del 2**: all six slots' identities read. Slot 2/3 already known;
  **Slot 5 is the connector** — a continuous ~83Hz note-check
  (`$00782A`) that calls into voice programming (`$007CA8`) exactly
  once per note, measured directly (0 idle, 1 per press). Slots 0/1/4
  read but not characterized — `[OPEN]`, not claimed as ruled out.
- **Del 3**: type table re-read, stable. Only type `$0E` remains
  execution-confirmed; the other twelve entries' target addresses are
  ambiguous to decode and untested this round — kept explicitly
  separate from confirmed findings per this task's own rule.
- **Del 4**: a real, ROM-verified 32-bit reference to `GPR MONITOR`'s
  address found via static search (not live tapping) for the first
  time in three attempts — a genuine lead, semantic connection to the
  string itself not yet confirmed. `[OPEN]`, narrowed with a concrete
  next address (`$00A304`/`$8C72`) rather than repeating the same
  blocked live-tap method a fourth time.

## Rules check

Observation only (plus one host-side static ROM search, not a machine
change). Every live tap used `asr10_taps.lua`. No firmware variable
was written. No `mem_map` change, no WD33C93, no ADC, no SCC code, no
ES5510 activation. No `-log`. Static reads and execution-confirmed
findings are kept in explicitly separate categories throughout, per
this task's own rule — Del 3 in particular names which single entry is
confirmed and marks the rest ambiguous rather than silently upgrading
them.
