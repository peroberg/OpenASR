# Finding The Note Protocol: Three Gates, And A Prior Sweep Already Closes The Question

## Scope

`keyboard-and-sample-bridge.md`'s "byte-for-byte identical to a button
press" conclusion was a jumped-to elimination argument, not a verified
fact — `$C0-$FF` being the only non-colliding range doesn't mean notes
live there. This task follows three sequential gates (cheapest first),
each capable of ending the investigation on its own. Del 1 falls through
immediately; Del 2 turns out to be the whole answer, but not the one
either the prior task or this task's own `[HYPOTHESIS]` expected.

## Del 1 — The Velocity Gate

`KEY_VELOCITY = 100` (`esqpanel.cpp:1026`) — **already ≠ `$00`**, so the
"byte-for-byte identical to a button frame" explanation from the
previous task **falls immediately**, exactly as this task's own gate
predicted it might. That explanation was wrong in its precise mechanism:
button frames are `($80|btn, $00)`; my key frames are `($80|key, $64)` —
the second byte genuinely differs. Straight to Del 2, per instruction.

## Del 2 — Tracing The Dispatch Table

### Setup: a live-memory disassembler, since these addresses are RAM, not ROM

`$FFB20A` and everything reachable from it live in the `$FC5020-$FFFFFF`
catch-all RAM range (OS-loaded at runtime), not the static boot ROM
`.bin` files — a static disassembler on the ROM image cannot see this
code. Built a small pipeline instead: dump raw bytes from a live,
booted machine via Lua (`prog:read_u8()`), disassemble offline with
Capstone (installed in an isolated venv under the scratchpad, not
touching the system Python — reversible, no repo changes). This is the
same general method `panel-protocol-state-machine.md` must have used to
produce its own `$FFB392` listing (that document doesn't name its tool,
but the output shape matches).

### `$FFB20A`, disassembled fresh, at the actual runtime `$CCD1` value

`panel-protocol-state-machine.md`'s own `$FFB392` transcription explicitly
assumed `$CCD1==0`. Measured directly this task, at the same point
(`FILE 1`, before any panel interaction): `$CCD1=$F8` — **nonzero**, the
opposite case from that document's own worked example. This matters:
`$FFB392`'s `tst.b $ccd1.w / beq ...` branches differently depending on
which case is live, and `$CCD1=$F8` is what a real key press actually
runs under, not the zero case. Noted as a correction, not a re-derivation
— `panel-protocol-state-machine.md`'s own disassembly of `$FFB392` stands
unchanged; only which branch of it applies to a live key press differs
from what its own worked example showed.

```asm
ffb20a  tst.b   $ccd1.w
ffb20e  beq.b   $ffb266          ; $CCD1==0 path (not this task's live case)
ffb210  move.w  #$b392,$3c0.w    ; $CCD1!=0 path: reset $3C0 for the NEXT frame
ffb216  tst.b   d1               ; test the SECOND byte
ffb218  bne.b   $ffb258          ; D1 != 0  ->  NOT a button-shaped frame
ffb21a  bsr.b   $ffb1e0          ; D1 == 0  ->  button path: call $FFB1E0
ffb21c  cmp.b   #$80,d2          ; ...then classify D2 against $80/$3A/$40/$25
...
ffb258  moveq   #$0,d2           ; D1 != 0 path (our case: velocity=$64)
ffb25a  bsr.w   $ffb2dc
ffb25e  bmi.b   $ffb2d6
ffb260  bsr.b   $ffb1fe
ffb262  bra.w   $ffb43e          ; -> $FFB43E, the same "completion path" panel-completion-consumer-v350.md already documented uses trap #3/#4
```

**The wire protocol does distinguish a nonzero second byte from a zero
one, at exactly this dispatch point.** A real key press (velocity=$64)
and a real button press (second byte=$00) take **genuinely different
code paths** from `$FFB20A` onward — `$FFB258`/`$FFB2DC`/`$FFB43E` for
the key, `$FFB1E0`+the `$80`/`$3A`/`$40`/`$25` comparison chain for the
button. **Correction to `keyboard-and-sample-bridge.md`**: "wire-protocol-
indistinguishable from an unmapped button" was wrong. The `BTN_18`/`KEY_18`
comparison that seemed to confirm it measured only the *observable
effect* (RHRB count, display, ES5506 silence) — which happened to match
for both paths, for unrelated reasons on each side — not the *code path*,
which this task's disassembly now shows genuinely differs.

**Verified empirically, not just from the listing**: tapped the actual
key press's execution with PC correlation (the same proven technique
from `mc68302-consolidation-2.md`). `$FFB43E` is genuinely executed
(`PC==$FFB43E`, a real instruction fetch, not a coincidental data read):

```text
T34_CHECKPOINT_HIT name=B43E t=22.300373 pc=FFB43E
```

### `$C0-$FF`: the task's own `[HYPOTHESIS]` is refuted, not confirmed

`$FFB392`'s own disassembly (already in `panel-protocol-state-machine.md`,
re-verified this task): bytes `>= $C0` (or `>=$E0` under the `$CCD1!=0`
threshold this task measured live) branch to **`$FFB3C0`, the control
branch** — the same handler `$FF`/`$FC`/`$F7` special commands already
use, per that document's own clean-byte probe table (`$CC`/`$DD`/`$EE`
all route there too). **`$C0-$FF` is not available for notes at all — it
is already claimed by the control-command path.** The task's own framing
("den starkaste nuvarande kandidaten... men det är `[HYPOTHESIS]` tills
dispatch-tabellen verifierat det") asked this to be treated as a question
for the table, not an assumption — the table's answer is no.

### `$FFB43E`'s trap calls: generic queue primitives, not note-specific logic

Disassembled `$FFB43E` (previously only summarized as "trap #3 ... trap
#4" in `panel-completion-consumer-v350.md`). It packs `D1`/`D3` into an
`A5`-relative record, then calls **`TRAP #3`** and later **`TRAP #4`**.
Read those vectors live (masked to 24 bits; reading them too early —
right after `FILE 1`, before any panel traffic — gave the sentinel
`$FFFFFF` and crashed a tap install outright, fixed by deferring the
read to just before the actual key press) and disassembled both
handlers:

```asm
; TRAP #3 ($F88078): generic enqueue
f88078  ori.w   #$700,sr           ; mask interrupts, critical section
f8807c  movea.w $b6c.w,a5          ; $B6C: a queue-head pointer
f88082  bne.b   $f88088
f88084  moveq   #$90,d0
f88086  trap    #0
f88088  move.w  (a5),$b6c.w        ; advance the queue pointer
f8808c  clr.w   (a5)
f8808e  addq.b  #1,$b7f.w          ; $B7F: element count, capped by $B80

; TRAP #4 ($F880A2): generic dequeue / release
f880a2  ori.w   #$700,sr
f880a6  movea.w $b6c.w,a0
f880aa  move.w  a5,$b6c.w
f880ae  move.w  a0,(a5)
f880b0  subq.b  #1,$b7f.w
f880b4  rte
```

**`TRAP #3`/`#4` are a generic ring-buffer enqueue/dequeue primitive**
(interrupt-masked critical section, head-pointer advance, capped element
counter) — not note-dispatch logic, and not obviously panel-specific
either. `$FFB43E` uses them to post the received byte pair into *some*
shared queue for a separate consumer to process later, exactly matching
`panel-completion-consumer-v350.md`'s own unresolved `[OPEN]`: *"What
consumes the decoded value."* **Verified empirically that a real key
press reaches this far**: `trap3_hits=8`, `trap4_hits=16` in the same run
that showed `PC==$FFB43E`. **Still zero ES5506 writes** in that same run.
The wire encoding is correctly classified as key-shaped, is correctly
queued — and whatever drains that queue either never runs in the
measured window, or runs and still doesn't produce a voice write for
this payload.

### The question was already answered once, comprehensively — found, not re-run

`panel-completion-consumer-v350.md` (same investigation family, read
this task, not previously connected to the keyboard question):

> "192 frames over branches `$00-$3F`, `$40-$7F`, and `$80-$BF` gave
> zero display changes, zero ready scheduler slots, and zero ES5506
> writes."

Combined with this task's `$C0-$FF` finding (control branch, not a note
candidate), **the entire `$00-$FF` first-byte space has now effectively
been exhausted**: `$00-$BF` by that earlier comprehensive sweep, `$C0-$FF`
by this task's disassembly showing it can't be a note candidate at all.
This is independently corroborated, not just cited: this task's own
fresh measurement (velocity=$64, a value that earlier sweep didn't
specifically list as tested, since it swept *first* bytes, not second
ones) also produced zero ES5506 writes, consistent with the same
conclusion holding for second-byte variation too, at least for this one
value.

**Del 2 identifies the actual blocker, closing the investigation without
needing Del 3**: the wire protocol is correctly received, correctly
classified as key-shaped (not button-shaped), correctly queued via a
generic OS primitive — and the *consumer* of that queue, wherever and
whatever it is, does not result in an ES5506 write for any tested
payload, first-byte value, or (this task's one new data point)
second-byte value. This is not a wrong guess at an encoding; the
encoding classification is confirmed working. The open question is a
different, deeper one: what drains this queue, and under what
precondition does it actually program a voice.

## Del 3 — Not Run, And Here Is Why

Per instruction: report why Del 2 was sufficient before starting a
sweep, then don't run it if it wasn't needed. Del 2 already exhausted the
byte space Del 3 would have swept (`$00-$BF` via the prior session's
192-frame sweep, `$C0-$FF` via this task's disassembly proving it's the
control branch, not reachable as a note candidate under any first-byte
value). A bounded sweep of `$C0-$FF` specifically — the task's own
priority order — would sweep a range already shown to be structurally
unavailable for notes. Running it would not add information; it would
reconfirm what the disassembly already proves for a reason a black-box
sweep can't see (a branch target, not a per-value behavior).

**What actually remains open** is not "which byte value plays a note" —
it's "what consumes the generic TRAP-#3-queued record, and why does a
correctly-classified, correctly-queued key event not result in a voice
write." That is a different, deeper investigation (tracing the queue
consumer, likely a scheduler-driven task this project's `runtime-service-
model.md`/`architecture-handoff.md` may already have partial context on)
than a byte-value sweep can resolve — consistent with the task's own
fallback: *"Ger hela svepet ingenting är det också ett resultat...
frågan blir en annan."* The question already became a different one,
found rather than swept into.

**A structural possibility worth naming, not chased further this task**:
`asr10_boot.cpp`'s machine config has exactly one panel-adjacent device,
`asr10panel_device`, modeling the front-panel button/display serial
channel. There is no separate keyboard-scan device anywhere in the
config. On real ASR-10 hardware, the musical keybed may not share this
same serial channel with the front panel at all — a dedicated scan
matrix wired independently to the MC68302 (or a separate scanner IC)
would explain a byte-space sweep finding nothing *by construction*, not
because of a still-undiscovered magic value. Not investigated further
this task — flagged as the next thing to check before spending more time
inside this specific serial channel.

## Del 4 — Method Points, Journaled

Added `methods-static-analysis.md` §8.9: `logerror()` is confirmed dead
in this project's own run conditions (`src/emu/machine.cpp:289`: its
callback is only registered when `-log` is passed, and `-log` is
forbidden here) — every loud mechanism, present and future, must go
through `osd_printf_error()` (C++) or a Lua `print()` instead. Cites the
`xmit_char()` overflow as the concrete case this was learned from.

Overflow re-confirmed as the verified fact it already was
(`keyboard-and-sample-bridge.md`): 32 of 52 bytes lost in a 13-key
stress test before the fix, ring-full check added, counter matches the
measured loss exactly after.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, unaffected — no C++ or driver
  change this task, Lua probes and documentation only.
- No `mem_map` change. No ES5510 change. `es5506.h` not read.
- `esqpanel_device`'s base-class protocol untouched — no ASR-10-specific
  override added, since Del 2 closed the investigation before any
  encoding change was warranted.
- No `-log`. Capstone installed in an isolated venv under the scratchpad
  only, not the system Python, not the repo — reversible, no trace left
  in the tree.
- Bank 1 untouched, still `[OPEN]`. CMR table untouched, stays `[Likely]`.
- `git diff --check`: clean.

## Line Count

- `docs/asr10/reference/methods-static-analysis.md`: +1 section (§8.9).
- `docs/asr10/lua/archive/`: three new scripts this task
  (`panel-dispatch-memory-dump.lua`, `trap-3-4-key-press-verify.lua`,
  `trap-handler-memory-dump.lua`) — written, run, archived. (The
  Capstone disassembler wrapper and its venv live only in the
  scratchpad, not the repo — not part of this project's Lua library.)
- No C++ changed this task.
