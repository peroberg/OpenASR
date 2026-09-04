# The sequencer's own clock, found by tracing backward from allocation; the service menu stays unreached (2026-08-24)

Follow-on to `bank-loading-and-transport-context.md`. That task's closing
recommendation from Codex, taken whole: stop sweeping button codes for
Record/Stop•Continue/Play as a panel-identification problem — the
transport is a **consequence** of the sequencer's runtime state machine
being reached, not a code waiting to be found by press-and-watch. This
task reframes accordingly: find the state, trace backward to its writer.
Del 1 (the service menu) is reported first because it was tried first,
but Del 2 is the task's real result.

## Del 1 — the service menu: real, present in ROM, not reached this task

`strings` on the merged (hi/lo-interleaved) boot ROM surfaces what reads
as a genuine factory diagnostic menu at ROM file offsets ~`$1000`-`$1700`
and ~`$53E0`/`$7C86`-`$7CB4`:

```
ERASE, COPY, SCSI STATUS, GPR MONITOR, INSTRUCTION MONITOR, TEST,
A/D TO D/A, DC OFFSET, MIDI LOOP, PASS, FAIL, COUNT
...
ESP TESTS
LOW VOLTAGE  HIGH VOLTAGE  ESP RAM TEST
RAM TEST1 FAILED XXXXH
RAM TEST2 FAILED XXXXH
RAM FAILED AT XXXXXXH
```

Verbatim, not interpreted: these are the exact ASCII strings in the ROM
image (`roms/asr10booth/asr-648c-lo-1.5b.bin` + `-hi-`, hi/lo-byte
merged). `GPR MONITOR`/`INSTRUCTION MONITOR` name ES5510 GPR (General
Purpose Register) inspection — this is very likely a genuine ESP
(ES5510) factory test suite, distinct from anything in the owner's
manual.

**Corrected mid-task, not left as a false result**: an early read-tap on
this ROM address range showed hits during ordinary boot (no button
held) and was initially read as "the diagnostic string gets touched but
not displayed." That conclusion doesn't hold. `subroutine-index.md`
already documents that `$FC4800`-adjacent low memory ($0000-$0FFFFF) is
ROM only while `cs0_covers(0)` is true (`asr10_boot.cpp`'s
`low_rom_or_lowmem_r`), and switches to RAM (`m_lowmem_shadow`) once the
chip-select is reprogrammed away from address 0 — which happens very
early in boot. `trap3_enqueue`'s queue-node pool at `$14F4`-`$150C` and
the `$00CA` list root at `$14C0` (both already documented, unrelated to
this diagnostic menu) live at the exact same addresses. **Verified
directly**: reading live memory at `$001041`/`$0014F4` post-boot shows
zeros and unrelated linked-list data, not the ROM string — the address
range is genuinely repurposed as RAM almost immediately, and a
read-tap on it after that point cannot distinguish "the diagnostic
string is being read" from "an unrelated RAM structure happens to share
this address." The earlier finding is retracted; recorded here so the
same aliasing trap isn't walked into again.

**Searched, both negative**:
- Single-button held from `t=0` through the whole boot sequence (all 64
  raw codes, one fresh boot per code): zero divergence from the normal
  boot text sequence (`ENSONIQ ASR-1?` → `LOADING SYSTEM` → `TUNING
  KBD` → `FILE 1`) at any of 5 checkpoints.
- 15 structurally-motivated two-button holds (arrow pairs, mode-adjacent
  pairs, first/last-code pairs): same zero divergence.
- The full display-text trace from `t=0` to `FILE 1` (20ms resolution, a
  live witness through the whole window, not an absence-of-traffic
  argument) never shows any of the diagnostic strings.

**The service manual's own "Display Self-test Mode"** (the button-print
chart: LOAD→`8`, COMMAND→`$`, RECORD→`5`, STOP/CONTINUE→`6`, etc.) is a
**different, textually distinct feature** — a fallback in the
keypad/display board's *own* firmware, entered via a hardware jumper
(bridging a point below C83 to a 74F74 pin) when the digital board isn't
communicating. `asr10panel_device` models the panel as a passive serial
peripheral; there is no separate keypad-board CPU device anywhere in
`asr10_boot.cpp`'s machine config. This specific mode is not a
button-hunting problem — the code that would implement it doesn't exist
in this driver at all.

**Verdict: `[OPEN]`.** The GPR MONITOR/ESP TESTS menu is real ROM
content, structurally distinct from the jumper-only keypad self-test,
and not reached by any single button, tested pair, or three prior
rounds' worth of COMMAND-mode navigation sweeps (their transcripts were
checked — none of these strings ever appeared). No hangs to report,
because the menu was never entered. Not chased further with a larger
button-combination search this task, in favor of Del 2's reframed,
substantially more productive approach.

## Del 2 — the sequencer's own clock, traced from allocation to dispatch

**Method, as instructed**: start from `$17`'s ("Create New Sequence",
in the specific `$00`-`$16`-preceded navigation context established
last task) 211-byte memory diff — the allocation — and trace what reads
and writes those addresses afterward, instead of pressing more codes.

### The tempo/clock divider: `$000B6E`/`$000B70`, confirmed by both measurement and disassembly

Live-disassembling `irq6_tick_producer` (`$F88300`, already documented
in `subroutine-index.md` — this task's own disassembly matches it
instruction-for-instruction, corroborating rather than re-deriving)
shows, immediately after the documented "every tenth tick, service the
secondary table" block:

```asm
f88362  tst.b   $17e.w
f88366  bmi.w   $f883b4          ; negative: skip both paths below
f8836a  bne.w   $f883b8          ; nonzero, not negative: alternate path
f8836e  move.w  $b6e.w,d0        ; ACCUMULATOR
f88372  add.w   $b70.w,d0        ; += STEP
f88376  move.w  d0,$b6e.w
f8837a  sub.w   #$271,d0         ; wrap at 625
f8837e  bcs.w   $f883b4          ; hasn't wrapped: done
f88382  move.w  d0,$b6e.w        ; wrapped: store remainder
f88386  jsr     $ffffa0f8.l
f8838c  trap    #3               ; enqueue
f8838e  move.w  #$e,$2(a5)       ; event type = $E (14)
f88394  movea.w $d6.w,a1
f88398  trap    #9               ; install into a scheduler slot
```

This is a textbook software phase-accumulator clock divider: `$B6E`
accumulates `$B70` every main tick (~1000Hz, `boot-sequence.md`'s
measured DUART rate); when the accumulator reaches `$271` (625), it
wraps and fires a type-`$E` event. `$B70` is the natural "tempo
increment" — the task's own predicted "tempodelare."

**Measured, not assumed**: `$B70` is `0` at idle and immediately after
loading `TUTORIAL SEQ` (accumulator inert — confirmed live: 1000 samples
over 2 real seconds at idle show `$B6E` never moving). Prefix-isolating
the `$00`-`$17` chain exactly as `bank-loading-and-transport-context.md`
did for the guard state (`k=1..24`, fresh boot per `k`) gives the
**identical trigger point**: `$B70` stays `0000` through `k=23` (last
code `$16`) and becomes `005A` (90) at `k=24` (last code `$17`) —
reproduced across the full prefix range, not a single sample. After
creation, `$B6E` is observed actively accumulating (grew from `0` to
`$01A4` over the setup window) — the clock is genuinely running, not
just configured. The exact PC of the `$B70` write was not captured: a
wide write-tap over `$B00`-`$BFF` recorded zero hits at the moment of
the `$17` press despite the value changing, meaning the write reaches
`$B70` through the RAM alias window (`system_ram_alias_r/w`,
`asr10_boot.cpp`) rather than the direct low address my tap covered —
reported as a real methodology gap, not silently worked around.

### The dispatch target: jump-table index `$E` → `$F8C588`

`jumptable_dispatch_15entry` (`$00740C`, already named in
`keyboard-and-sample-bridge-3.md`) was live-disassembled this task for
the first time (prior documentation named its existence and behavior in
prose but not its exact indexing arithmetic):

```asm
00740c  movea.w $2(a5),a4        ; index = queue node's type field
007410  cmpa.w  #$e,a4
007414  bls.b   $7418
007416  bra.b   $742c            ; out of range: trap #0
007418  adda.w  a4,a4            ; index *= 2 (not *4)
00741a  adda.l  #$67ac,a4        ; table base
007420  movea.l (a4),a4          ; read a 4-byte target at a 2-byte stride
007422  move.l  $4(a5),d2        ; node's own $4 field -> D2, passed to handler
007426  trap    #4               ; dequeue
007428  jsr     (a4)
```

The `*2` (not `*4`) stride means the table at `$67AC` only produces
valid addresses at **even** indices — confirmed by dumping all 15 slots
and checking which ones decode to plausible ROM/RAM addresses (every
even index does; every odd index doesn't). Type `$E` (14, even) is
exactly the tempo-accumulator's own event type, and decodes to
**`$F8C588`**:

```asm
f8c588  add.w   d2,d2
f8c58a  bmi.w   $f8c594
f8c58e  clr.b   $f58.w
f8c592  rts
f8c594  move.b  $f5a.w,$f58.w
f8c59a  rts
```

A two-branch handler on D2's sign: writes byte `$000F58` either as `0`
(cleared) or as a copy of `$000F5A`, depending on whether the queue
node's `$4(a5)` field is negative. The tick-producer's own enqueue code
(`$F8838E`) doesn't explicitly set `$4(a5)`, so this depends on
whatever `trap3_enqueue` leaves a fresh node's `$4` field as — most
likely zero, taking the `clr.b $f58` branch every time, which would
make `$F58` sit at a constant `0` even while firing repeatedly (checked
live: zero *transitions* in `$F58` over a 2-second high-resolution poll
after sequence creation — consistent with "fires but always writes the
same value," not "never fires"; not fully disambiguated from "never
fires" without also instrumenting the write itself, which the same
alias-window gap above would complicate).

### `$00017E`: a real three-way branch, not proven to be the play/record/stop selector

The `bmi`/`bne` pair gating which of two clock paths runs (the
tempo-accumulator above, or an alternate simple countdown at `$F883B8`
that decrements `$B74` and reloads from `$B78`, tagging its own enqueued
event differently — `move.w #$1,$4(a5)`, which the *other* path never
sets) is a genuine three-state branch on a single signed byte:
negative / zero / positive. Structurally this is exactly the shape the
task predicted ("stoppad, körande, inspelande"). **Measured**: `$17E`
reads `0` at boot and stays `0` through the entire `$00`-`$3F` chain
this task ran (load, create sequence, select instrument, sweep every
remaining code) — the two write events caught by a live tap both happen
during early boot init (`$FB8ABE`, t=5.4s, the same keyboard-tuning-era
init routine documented elsewhere), not from any button press. **Named
honestly as a structural candidate, not a confirmed transport flag**:
this task never observed `$17E` take any value other than `0`, so
neither the "alternate path" nor the "skip both" branch was ever
exercised or attributed to a specific button. Trace-backward stops here,
not because the trail went cold, but because no further write was
found in this session's window.

## Del 3 — `$17` re-characterized

Downgraded per this round's own instruction. `$17` is the **allocation
and preparation step** ("Create New Sequence"), not a "sequencer active
guard": it allocates the ~211-byte object family documented last task
*and* switches the tempo/clock divider on (`$B70`: `0`→`90`). Both
effects are part of *preparing* a sequence to be run, not evidence that
playback or recording has started. The `$20`-blocking guard
("`STOP SEQUENCER FIRST`") most likely reflects "an unsaved sequence
object exists," which is consistent with allocation, not with a
transport state. No claim is made here that `$17` is or isn't adjacent
to the real Play/Record path — only that "guard" overstated what was
measured, and "allocation + clock divider activation" doesn't.

## Del 4 — second-byte classification: real, already documented, not a bug for `$26`-`$3F`

Re-derived from the existing `panel-protocol-state-machine.md` (first
byte, `$FFB392`) and `current-status.md`'s own correction (second byte,
`$FFB20A`), cross-checked against `esqpanel.cpp`'s actual C++, not
assumed: the channel really does carry (at least) three distinct
framings on the same wire, and `esqpanel_device`'s own base class shows
exactly how they differ **by second byte**, not first:

| Sender | First byte | Second byte | `$FFB392` routing |
|---|---|---|---|
| `set_button()` press | `$80\|code` (bit7 set) | `$00` (always) | `$FFB20A`, second byte `0` → button path (`$FFB1E0`) |
| `set_button()` release | `code` (bit7 clear) | `$00` (always) | `$FFB0E0`, second byte `0` → button-release path |
| `key_down()` | `$80\|(key&$3F)` | velocity (1-127) | `$FFB20A`, nonzero second byte → key path (`$FFB258`→`$FFB2DC`→`$FFB43E`) |
| `key_up()` | `key&$3F` (bit7 clear) | `$40` (fixed) | `$FFB0E0`, second byte `$40` → key-up path |
| `key_pressure()` | `$40\|(key&$3F)` (bit7 clear, distinct first-byte range) | pressure (0-127) | `$FFB0E0`, a third first-byte sub-range |

`esqpanel_device::set_button()` sends `$00` as the second byte
**unconditionally**, for every one of the 64 raw codes, both press and
release — verified by reading the function, not inferred. This is the
same function every one of `asr10panel_device`'s `$00`-`$3F` buttons
goes through. **Conclusion: `$26`-`$3F`'s silence is not a
classification/framing bug.** Every one of those codes is correctly
routed as a button-press frame and correctly reaches the button
dispatch table (this round and the prior two rounds both confirm real,
distinct firmware dispatch for every code) — their lack of visible
effect is a real property of those specific button IDs in the contexts
tried, not evidence of misrouting. **Nothing changed in
`asr10panel_device` or `esqpanel_device`** — Del 4's own condition
("hittas något... annars rör inte basklassen") wasn't triggered, because
what was found is that the framing is *already correct*, not broken.

## Del 5 — Cancel/No: `$21`, not `$22`

Prior rounds tentatively tagged `$22` `[Likely]` Cancel/No without a
confirmation-screen signature. Tested directly this task: from
`"CREATE NEW 5EQUENCE"`, `$23` (confirmed Enter/Yes) advances to a
genuine sub-step, `"NEW NAME?5EQUENCE ??"` (a name-entry prompt). From
*that* screen:

| code | effect |
|---|---|
| `$21` | **back to `"CREATE NEW 5EQUENCE"`** — abandons the name-entry step |
| `$22` | no visible change |
| `$24`, `$25` | forward to unrelated next commands (`CREATE NEW WAVESAMPLE`, `CREATE NEW LAYER`) — ordinary list navigation, not a cancel |
| `$02`, `$03` | no visible change |

`$21` is the only candidate that reproduces the task's own signature —
Enter takes the yes-branch forward into a sub-step, Cancel takes the
no-branch back out of it — and it does so uniquely; the two "forward"
neighbors are ruled out as ordinary navigation, and `$22` shows no
effect here at all. **`$21` = Cancel•No, `[Verified]`**, correcting the
prior `$22 [Likely]` tag (which is now unsupported by this specific
test and should be treated as superseded, not merely uncertain). No
keyboard mnemonic assigned — Del 4's letter scheme (`l/c/e/s/p/f/q`) is
for mode/category buttons, not data-entry buttons, so `$21` stays
click-only per that rule.

## Del 6 — summary

**First byte is exhausted for transport identification.** Recorded so a
future task doesn't repeat the same sweeps: single-code isolation from
a fresh sequence-loaded state (32 codes), a 650-pair hold-combination
sweep across every code with zero single-press effect anywhere tried, a
second 110-pair sweep, prefix-isolated binary search on the one real
state transition found (`$17`), correlated PC/state-variable tracking
(two candidate variables, both ruled out), a full 128KB memory diff, and
(this task) live disassembly tracing forward from that diff through two
real subsystems (tempo-accumulator, jump-table dispatch) to a named
byte (`$000F58`) and a named three-way flag (`$00017E`) that was never
observed to move. Between three tasks, this is the exhausted set of
techniques available without either (a) a working Play/Record signal to
correlate against, which doesn't exist yet, or (b) MIDI/audio input,
which this button-only harness cannot supply.

**Service menu**: real ROM content (`Del 1`), not reached by any single
or paired button hold at boot, or by three rounds' worth of COMMAND-mode
navigation. `[OPEN]`.

**Sequencer clock chain, this task's actual result**: `$17` (Create New
Sequence) → `$B70` set to a nonzero tempo step → `$B6E` phase
accumulator actively running → wraps at 625, dispatches jump-table
index `$E` (`$67AC`+`2*14`) → `$F8C588` → writes byte `$000F58`. A real,
disassembly-confirmed, measured chain from allocation to a running
periodic clock — not a guess, not a guard label. `$00017E` is a
genuine three-way flag structurally shaped like a run-state selector but
never observed to change from `0` in this session; named as the
strongest present candidate, not claimed as confirmed.

**`$17` reframed** (Del 3): allocation + clock-divider activation, not
"sequencer active guard."

**Del 4**: second-byte classification is real, already correctly
implemented for every `$00`-`$3F` code; `$26`-`$3F`'s silence is not a
framing bug. No code changed.

**Del 5**: `$21` = Cancel•No, `[Verified]`, superseding the prior
`$22 [Likely]` tag.

**`asr10panel_device` still has no blink mechanism** — noted again,
unchanged from `bank-loading-and-transport-context.md`, not
re-investigated or touched this task.

**Stays `[OPEN]`, unchanged**: Record/Stop•Continue/Play themselves,
`$74`/`$75`/`$76`, the 39 unmarked annunciator bits, the Load/Command/
Edit mode buttons, and the GPR MONITOR/ESP TESTS service menu's entry
condition.

**No `mem_map` change, no WD33C93, no ADC, no SCC code, no ES5510
activation** — this task's own instrumentation stayed entirely in
observation (Lua read/write taps, live memory snapshots, offline
Capstone disassembly of dumped bytes) plus one label correction in
`panel-keymap.md`/`current-status.md`. No C++ was added or modified;
`esqpanel_device`'s base-class protocol was read, not touched.
