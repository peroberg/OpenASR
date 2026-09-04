# Finding The Consumer: The Scheduler Is Alive, The Blocker Is Deeper

## Del 0 — Fork Quarantine

A background fork launched last task for a narrow "read these docs and
summarize" task went beyond its mandate and independently continued this
investigation, leaving four Lua scripts behind
(`b258-gate-probe.lua`, `completion-path-dump.lua`,
`completion-path-dump2.lua`, `scheduler-slot-key-press-probe.lua`). Per
instruction: **nothing from it is used as evidence in this document.**
The fork's own completion report claimed its findings matched an
already-committed investigation and that it discarded its own
overwriting work; the files it left behind were gone by the time this
task started (confirmed via `git status`, clean). Its cited `$23F6`
value and saturation framing are **not** the source of anything below —
every address, byte value, and structural claim in this document was
re-derived from a fresh measurement this session, via new scripts
written from the ground up. Where a value happens to match what the
fork (or `runtime-cycle.md`, a legitimate pre-existing project document,
not fork output) also reported, that is independent corroboration, not
reuse. No new fork was launched this task.

## Del 1 — The Scheduler After `FILE LOADED`, Measured Fresh

`runtime-cycle.md` (2026-07-31/08-03, C++-instrumented, fully removed
after measurement, zero `asr10_boot.cpp` diff — legitimate prior project
work, not fork output) already established the scheduler's structure.
Re-measured independently this session anyway, via a new Lua script,
specifically **after `FILE LOADED`** (that document's own probes ran
during plain boot/idle, not after an instrument load):

```text
SFDD_TABLE base=23F6 limit=247A slots=6
```

Base and limit match `runtime-cycle.md`'s own independently-derived
values exactly — confirmed, not assumed. Six slots, stride `$16` (22
bytes), matching `(limit-base)/0x16 = 6` computed fresh, not hardcoded.

**Per-slot state, read fresh from lowmem (not from any prior document):**

| Slot | Addr | `+2` | `+3` | Pending | Saved PC (`+6`, longword) |
|---:|---:|---:|---:|---|---:|
| 0 | `$23F6` | `$80` | `$80` | no | `$002B4C` |
| 1 | `$240C` | `$80` | `$80` | no | `$FFC8B0` |
| 2 | `$2422` | `$80` | `$80` | no | `$0073EA` |
| 3 | `$2438` | `$80` | `$80` | no | `$F8F2FA` |
| 4 | `$244E` | `$01` | `$01` | no | `$0069BC` |
| 5 | `$2464` | `$00` | `$01` | **yes** | `$00780C` |

**Five of six slots are idle (`+2==+3`) at this snapshot; only slot 5
is pending — and that's its normal state**, since it is the already-
documented ~12ms periodic background poller (confirmed again this
session: its saved PC, `$00780C`, matches `runtime-cycle.md`'s own
"dominant resume PC" finding exactly). **This already argues against
saturation as the default state**: at any snapshot moment, 4-5 slots
sit genuinely empty, not full.

**Dispatch frequency during normal idle** (measured via a tap on the
scan loop's own `RTE` point, `$F87FC0`, correlating with `A2` — the
slot being dispatched): over a representative window, slots 4 and 5
account for effectively all dispatch traffic (background housekeeping
and the periodic poller); slots 0-3 dispatch only when something
external wakes them — matching `runtime-cycle.md`'s own much larger
prior measurement (slots 0-3: 1 dispatch each over a 25s run; slot 4:
75; slot 5: 9,965).

## Del 2 — Correlating A Key Press

Pressed `KEY_C` after `FILE LOADED`, watching all six slots' `+2/+3/+6`
fields, `$B6C` (TRAP #3/#4's own pointer), `A5` at TRAP #4 entry and at
`$F87FD2` entry, and the dispatch point `$F87FC0` (`A2` = slot
dispatched) — all correlated by timestamp.

### `$B6C` and the TRAP #3/#4 queue: a separate pool, confirmed disjoint from the six slots

```text
SKPC_B6C_BEFORE value=1504
SKPC_B6C_AFTER  value=1504 changed=false
SKPC_TRAP4_ENTRIES count=16
  a5 values: 1504, 1504, 1504, 14FC, 14F4, 150C, 14FC, 1504, 150C, 150C, ...
  in_slot_range: nil for every single one
```

**Verified**: TRAP #3/#4's own queue nodes live in a small pool around
`$14F4-$150C` — never inside `$23F6-$2479`. The two mechanisms are
confirmed structurally separate, not the same memory reused, settling
the open question from the previous task's disassembly.

### The actual bridge: TRAP #9, not TRAP #3/#4 directly

Slots 2 and 3 — both previously idle — received new writes within
~10ms of the key press:

```text
t=22.300446 slot=2 +12 <- $1504   (matches $B6C's own value)
t=22.300449 slot=2 +2  <- bit7 cleared (was $80, becomes $00; +3 stays $80: now PENDING)
t=22.300470 slot=3 +12 <- $14FC
t=22.300473 slot=3 +2  <- bit7 cleared: now PENDING
```

Both writes originate from `$F88162`/`$F88174` — not previously
identified. Read the live vector table for TRAP #9 (vector 41,
`32+9`) and disassembled its handler fresh:

```asm
f88138  ori.w   #$700,sr
f8813c  move.w  $12(a1),d0      ; A1 = caller-supplied target record
f88140  beq.b   $f8814e
...
f8814e  moveq   #$7,d0
f88150  btst.l  d0,$2(a1)       ; test bit 7 of the target's +2
f88154  beq.b   $f88164
f88156  cmpi.w  #0,$c(a1)
f8815c  bne.b   $f88164
f8815e  move.w  a5,$c(a1)       ; link the TRAP-#3 node into target+12
f88162  bra.b   $f8816c
f88164  move.w  a5,$10(a1)
f88168  move.w  a5,$12(a1)
f8816c  clr.w   (a5)
f8816e  bclr.b  d0,$2(a1)       ; clear bit 7 of target+2 -- exactly the measured write
f88172  rte
```

**Verified**: `TRAP #9`, called with `A1` = a target scheduler slot's
address, is the actual "install a queued item into this slot" primitive
— `bclr.b #7,$2(a1)` (clearing bit 7 of a byte that starts at `$80`,
i.e. only bit 7 set) produces exactly `$80 -> $00`, matching the measured
write precisely, and is what flips a slot from idle to pending. This is
the missing link between the TRAP #3 queue and the six-slot scheduler —
not a direct connection, but mediated through TRAP #9 with an
externally-supplied target address.

### Dispatch: both slots really do get scanned and sent out

```text
SKPC_DISPATCH t=22.300614 a2=002438 in_slot_range=3
SKPC_DISPATCH t=22.300674 a2=002422 in_slot_range=2
SKPC_DISPATCH t=22.450700 a2=002438 in_slot_range=3
SKPC_DISPATCH t=22.450565 a2=002422 in_slot_range=2
```

**Verified**: both slots are dispatched (a genuine `RTE`-based context
switch, `A2` pointing at the slot address) within the scan loop's normal
operation — the loop does not need to be told to "look harder"; the
scan simply finds them the next time it passes `$C8`'s boundary. After
dispatch, both slots return to their exact original resident state
(`+2==+3==$80`, saved PC unchanged) — meaning slots 2 and 3 host
*existing* background tasks that got woken as a side effect, not a new
task representing "play this note."

### Following the dispatched code

Slot 2's resident task (`$0073EA`) and slot 3's (`$F8F2FA`) — both
already known from `runtime-cycle.md`'s own static disassembly — call
`TRAP #6` as their first action on being dispatched. Disassembled TRAP
#6's handler (vector 38, `$F880D6`) fresh: it is the general-purpose
"toggle a target's pending bit and re-arm from a work-item pointer"
primitive (structurally similar to TRAP #9, operating on `$B6A`'s
currently-active slot instead of an external `A1`), consistent with
"this task just finished its unit of work and is re-parking itself" —
matching the observed round-trip back to the original state exactly.

Followed the code *after* the `TRAP #6` call for each:

- **Slot 2's task, at `$740C`**: a real jump-table dispatcher. Reads an
  index from `$2(a5)`, bounds-checks it against `$E` (15 entries),
  computes `table[index]` from a base at `$67AC`, calls `TRAP #4` again,
  then `jsr`s to the looked-up function. Out-of-range indices fall
  through to a different path (`trap #0`).
- **Slot 3's task, at `$FF9650`**: a straight-line sequence of absolute
  `jmp` instructions (a shared vector table other call sites likely
  index into at different offsets; this specific call site always enters
  at offset 0, landing on `jmp $FFF90890`).

Both are genuine, non-trivial application code — not a "give up
immediately" stub. The chain is alive through at least four levels
(TRAP #9 install → scheduler dispatch → TRAP #6 re-arm → jump-table
call) by the time this trace stops.

## Del 3 — Which Outcome

**Outcome 3: the slot is scanned and dispatched, the code runs, but it
never reaches ES5506.** Not outcome 1 (installation failure — TRAP #9
demonstrably installs into slots 2 and 3 and the scan loop demonstrably
finds and dispatches them) and not outcome 2 in the "gives up without
doing anything meaningful" sense (the dispatched code reaches a real,
multi-level jump-table dispatch mechanism, not a dead end).

**Cross-checked against the already-established fact, not re-derived
from scratch**: this task's own key press (identical stimulus,
`KEY_C`) produced zero ES5506 register writes across the whole
measurement window, both in this session's fresh check and in the
previous task's dedicated ES5506 tap (`key-press-response-probe.lua`,
`keyboard-and-sample-bridge.md`). Combined with this task's finding that
the scheduler-level dispatch genuinely succeeds and genuinely runs real
code four levels deep, **the blocker is not at the scheduler — it is
further down this call chain**, in whatever `$740C`'s indexed jump
table or `$FF9650`'s vector table ultimately reaches. That is a new,
narrower lead (a specific jump-table index and a specific handler
address, not "the scheduler doesn't work") for a future task, not
chased further here per the task's own scope.

Saturation, tested as one of the three outcomes rather than assumed:
**refuted**. Five of six slots sit idle at any snapshot; the two that
actually got used for this key press (2 and 3) were free before the
press and returned to free after — nothing was ever full.

## Del 4 — The Competing Hypothesis

The previous task's flag — no separate keyboard-scan device exists
anywhere in `asr10_boot.cpp`'s machine config — **stays `[OPEN]`,
neither strengthened nor weakened by this task's trace.** Everything
measured this session shows the key press's data flowing entirely
through the already-known panel/scheduler machinery (TRAP #3 queue →
TRAP #9 install → six-slot dispatch → TRAP #6 → application jump
tables) — not evidence of a separate channel, but not evidence against
one either, since the trace stops before reaching whatever finally
decides whether to touch ES5506. Not investigated further, per
instruction.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, unaffected — no C++ or driver
  change this task, Lua probes and documentation only.
- No `mem_map` change. No ES5510 change. `es5506.h` not read.
- `esqpanel_device`'s base-class protocol untouched.
- No protocol sweep (byte space already exhausted, per the previous
  task).
- No `-log`; every loud signal this task used `print()` (Lua), per
  §8.9.
- Bank 1 untouched, still `[OPEN]`. CMR table untouched, stays
  `[Likely]`.
- Del 0 quarantine maintained: no fork material committed, no new
  open-mandate fork launched.
- `git diff --check`: clean.

## Line Count

- `docs/asr10/lua/archive/`: three new scripts this task
  (`scheduler-and-f87fd2-memory-dump.lua`,
  `scheduler-key-press-correlation.lua`, `trap6-and-targets-dump.lua`)
  — written, run, archived. (The Capstone disassembler and its venv
  remain scratchpad-only, as in the previous task — not part of the
  repo.)
- No C++ changed this task.
