# Slot-5 `$007CF0` mode callback — ASR-10 V3.50

## 1. Scope and result

This is a bounded follow-up to
[`effect-mode-consumers-v350.md`](effect-mode-consumers-v350.md).  It follows
only the mode-0-only Slot-5 call of `$007CF0`, stopping at its first
persistent sink.  It does not investigate oscillator routing, rate control,
ES5701, or the rest of the voice manager.

**Result:** `$007CF0` is a voice-record-ring service routine, not generic
input/UI housekeeping.  Its first persistent action rotates the `$0D5A`
current-ring root to the current record's `+$2E` link.  Mode 0 permits eight
additional invocations per completed 12-entry Slot-5 scan; mode 1 suppresses
only those calls.  This establishes a mode-dependent *voice-management work
budget*, but does not establish a clock, sample-rate, allocator-limit, or
board-control path.

Evidence labels follow
[`methods-hypothesis-management.md`](../reference/methods-hypothesis-management.md).

## 2. Starting state

The prior committed-effect investigation established:

| Effect state | `$0CE3` | OTTO ACTV | slots |
|---|---:|---:|---:|
| A: ROM HALL REVERB | `00` | `$1F` | 32 |
| B: Bank 44LUSH PLATE | `01` | `$17` | 24 |
| A2: ROM HALL REVERB again | `00` | `$1F` | 32 |

The ES5510 host-upload/readback and the current-effect transition are already
positive witnesses; this pass does not reinterpret them.

## 3. `$0077E6` callsite and exact gate contract

`$0077C2` is the established Slot-5 periodic poller.  It waits for 12 ticks
(about 12 ms), scans `$FFD0B0 = 0..11`, and yields with `trap #7` after each
index.  The relevant V3.50 code is:

```asm
$0077D4  jsr  $00E68E
$0077DA  jsr  $007CF0              ; unconditional #1
$0077E0  jsr  $007CF0              ; unconditional #2
$0077E6  cmpi.b #$01,$000CE3
$0077EC  beq  $0077FE              ; mode 1 skips optional call
$0077EE  movea.w $00D0B0,A0         ; current scan index
$0077F2  tst.b (-$2f3c,A0)          ; $FFD0C4 + index
$0077F6  beq  $0077FE
$0077F8  jsr  $007CF0              ; mode-0 optional #3
$0077FE  ... trap #7 / increment index
```

Thus the concrete contract is:

```text
call_optional_7cf0 = ($0CE3 != 1) AND (byte[$FFD0C4 + word[$D0B0]] != 0)
```

Mode 1 takes the `$0077EC -> $0077FE` branch before reading the per-index
gate.  Mode 0 reaches `$0077F8` exactly when that byte is nonzero.  The only
live input supplied by the poller is its current `$D0B0` scan index and the
gate byte; `$007CF0` itself obtains its record from `$0D5A`, not from an
argument register.

## 4. `$007CF0` static routine analysis

The routine occupies `$007CF0-$007E22`; `$007E24` is a distinct following
entry.  Its entry is:

```asm
$007CF0  movea.l $000D5A,A4
$007CF4  move.l  ($2E,A4),$000D5A  ; first persistent write
$007CFA  move.w  ($08,A4),D0
$007CFE  cmp.w   #$0008,D0
$007D02  bne     $007D0A
$007D04  jmp     $0000EFBE

$007D0A  cmp.w   #$000C,D0
$007D0E  bne     $007D2E
$007D10  movea.l #$00FC2001,A0
$007D16  move.b  ($15,A4),($7E,A0) ; conditional ES5506 host-window write
$007D1C  jsr     $FFFC6028
...
$007E22  rts
```

The remaining body filters on record fields `+$14` and `+$08`, uses the
record's `+$1E`, `+$18`, `+$CA`, and `+$AE` fields, and calls existing ROM/OS
voice helpers.  It can reach the already documented `$8E44` voice-return/reset
slot after host-status tests.  That later work was not followed: the required
first persistent sink had already been reached at `$007CF4`.

There is no `$0CE3` read in `$007CF0`.  The mode decision is wholly at the
Slot-5 caller; once called, both modes execute the identical routine.

## 5. First persistent sink

| property | evidence | status |
|---|---|---|
| writer instruction | `$007CF4: move.l ($2E,A4),$0D5A` | [Verified static] |
| write-tap PC | `$007CFA` (the instruction after the completed store) | [Verified runtime] |
| sink | longword `$000D5A` | [Verified runtime/static] |
| source | current record's `+$2E` link | [Verified static] |
| structural meaning | ring-root advance through the record link | [Verified static] |

`runtime-object-model.md` independently records that ROM `$F8CD8C-$F8CDAE`
builds the `$0D5A` ring from `+$2E/+32` record links.  It calls `$0D5A` an
active/current voice ring `[Likely]`; the exact manager-list naming remains
open.  The linkage and the fact that this routine advances it are sufficient
to classify the sink as voice-record management, not ordinary background UI
state.

## 6. Runtime A -> B -> A witness

A temporary Lua probe retained a write tap on `$0D5A-$0D5D`; a completed
longword store fires two 16-bit write callbacks.  The tap's `$007CFA` PC is a
completed-store witness for `$007CF4`, not a read/prefetch inference.  Each
phase was a one-second live window after the prior firmware-committed effect
transition.

| phase | `$0CE3` | gates `$FFD0C4..$FFD0CF` | `$0D5A` writes | completed `$007CF0` ring advances | result |
|---|---:|---|---:|---:|---|
| A | `00` | `00,01,01,01,00,01,00,01,01,01,00,01` | 5,328 | 2,664 | optional calls present |
| B | `01` | same 12 bytes | 4,004 | 2,002 | optional calls absent |
| A2 | `00` | same 12 bytes | 5,334 | 2,667 | optional calls restored |

Eight of the twelve gate bytes are nonzero.  Per complete scan that yields
32 calls in mode 0 (24 unconditional + 8 optional) and 24 calls in mode 1.
The measured A/B/A counts match that control-flow difference at the measured
Slot-5 cadence.  `$0D5A` also changed live in all phases, e.g. A
`$000085E8 -> $00008438`, B `$00008510 -> $000085E8`, and A2
`$000095F0 -> $000096C8`; these are ring traversal values, not a stable
rate-setting register.

The B and A2 transitions used the normal front-panel firmware flow:
`Load -> Effects -> Enter` to load 44LUSH PLATE, then the existing FX-select
path back to the Instrument effect.  The probe made no RAM/register writes.

## 7. Relevance and negative evidence

### Voice/audio

`$007CF0` is [Verified] voice-record-ring service: it consumes `$0D5A`,
advances its `+$2E` ring link, and has statically conditional OTTO-window and
voice-helper paths.  The current result does **not** prove that the additional
mode-0 calls program a sample, generate audio, or determine frame timing.

### Device/board

The routine has a statically conditional ES5506 write at `$007D16` to
`$FC207F`, but it is controlled by the selected record's `+$08 == $000C`, not
by `$0CE3`; no mode-specific ES5506 global-register, ES5510, MC68302, CS1 or
other board-facing write occurs in the `$0077E6` gate itself.  No downstream
device path was followed beyond the first sink.

### Allocator and 31/23 versus 32/24

The first sink rotates an already-existing linked ring.  It neither selects a
free record nor establishes a `slots - 1` bound.  Therefore it supplies no
new allocator proof and does not promote the existing interpretation:

```text
31 playable voices from 32 OTTO slots
23 playable voices from 24 OTTO slots
```

That relationship remains `[Likely]`; reserved-slot semantics remain `[OPEN]`.

## 8. Verdict and narrow next step

```text
$007CF0 EXECUTION:
    [VERIFIED] $007CF4 -> $0D5A writes, PC-correlated after the store.

MODE 0:
    Two unconditional calls plus the third call for each nonzero
    $FFD0C4 + $D0B0 gate (eight gates in the measured state).

MODE 1:
    $0077EC branches directly to $0077FE; the optional call is suppressed
    before the gate is read.

FIRST PERSISTENT SINK:
    $0D5A <- (current voice record + $2E), a linked voice-ring advance.

VOICE/AUDIO RELEVANCE:
    [VERIFIED] voice-record service; sample/audio-rate consequence [OPEN].

DEVICE/BOARD RELEVANCE:
    [OPEN] only later record-conditioned ES5506 work exists; no mode-specific
    board write was observed in this bounded path.

ALLOCATOR RELEVANCE:
    [NONE] before the first sink.

31/23 -> 32/24:
    [LIKELY] unchanged.

BRANCH VERDICT:
    [CONTINUE] only as a bounded voice-service workload branch, not as a
    clock-control lead.

NEXT MINIMAL STEP:
    Follow one record-conditioned $007CF0 path only if it is first shown to
    alter a voice-state/allocator field; otherwise return to the separately
    parked $00E4E0 `$320/$200` descriptor consumer.  Do not start a clock
    search from this callback.
```
