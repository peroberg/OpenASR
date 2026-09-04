# ASR-10 V3.50 — effect-mode consumers

## 1. Executive result

This pass follows the already verified effect-selected state, rather than an
oscillator hypothesis:

```text
current effect descriptor +$66 -> $0CE3
```

The three known consumers do not form a second audio-clock path.

* `$00E436` changes two CPU-delay counts in the SCC1/SCC2 receiver-enable
  sequence.  The register values ultimately written are invariant.
* `$00E4E0` is in a separate routine beginning at `$00E4DA`; it selects a
  stride/range value for a four-pass RAM descriptor initializer.
* `$0077E6` is in the established slot-5 periodic background poller; mode 1
  suppresses one optional `$007CF0` callback per scan entry.

None of these proven dataflows reaches an ES5506 register other than the
already separate ACTV path, an ES5510 control, CS1, an external latch, or a
clock control.  No C++ change is justified.

## 2. Starting state and evidence scope

The preceding A -> B -> A commit witnesses establish:

| State | Effect | `+$66` | `$0CE3` | ACTV |
|---|---|---:|---:|---:|
| A | ROM HALL REVERB | `00` | `00` | `$1F` |
| B | Bank 44LUSH PLATE | `01` | `01` | `$17` |
| A2 | ROM HALL REVERB | `00` | `00` | `$1F` |

`+$66 -> $0CE3` and the ACTV chain are `[Verified firmware/runtime]` in
`actv-source-and-rate-control-v350.md`.  The consumer branches below are
`[Verified firmware]`.  Consequently the A/B outcomes in the table are direct
static consequences of a runtime-verified input change, unless explicitly
marked runtime.

The temporary Lua A/B/A driver again reached the live `FILE 1 TUT0RIAL BNK`
witness, committed B and restored A2.  Its program/opcode fetch taps produced
no callbacks, while installed/enabled debugger breakpoints also did not fire
even for the independently known ROM ACTV caller.  This run is consequently
not usable as negative execution evidence for any consumer; it records a
measurement limitation rather than absence of execution.
The temporary probe was removed.  The older slot-5 experiment supplies the
positive execution witness for consumer C: 831 PC-correlated `$0077CE` sleep
entries and 9,964 resumes at `$00780C`, with a stable roughly 12 ms cadence.

## 3. Consumer inventory

| Consumer | Reads `$0CE3` | Role | A/B difference | Final sink | Board-facing | Verdict |
|---|---|---|---|---|---|---|
| `$00E436` | direct `tst.b` | SCC receiver-enable delay selection | D5/D6 `10/7` vs `7/4` | two DBRA delay loops | fixed SCC SCM/IMR sequence | `[Verified]` delay-only branch |
| `$00E4E0` | direct `tst.b` | RAM descriptor-initializer parameter selection | D1 `$320` vs `$200` | headers/ranges rooted at caller-supplied A0 | none in this routine | `[Verified]` descriptor parameter; role beyond that `[OPEN]` |
| `$0077E6` | direct `cmpi.b #1` | slot-5 optional-callback gate | mode 1 skips one `$007CF0` call | control flow; `$007CF0` semantics open | none in the branch | `[Verified]` periodic background gate |

## 4. `$00E436`: SCC receiver-enable prelude

The surrounding V3.50 code establishes the following bounded sequence:

```text
$00E432  D5 = $10; D6 = 7
$00E436  tst.b $0CE3
$00E43A  beq $00E440
$00E43C  D5 = 7; D6 = 4             ; only when $0CE3 != 0
$00E440  critical section / PB3 wait
...       wait for PB3 high then low
          DBRA D5
$00E49A  SCM2 ($FC6894) = $703B
          DBRA D6
$00E4A8  SCM1 ($FC6884) = $703B
$00E4B6  calls slot $8ECA -> V3.50 $00ECB0
...       IMR |= $2400, sets $0D06, returns at $00E4D8
```

The enclosing receiver-enable tail is already independently classified in
`reference/subroutine-index.md` and `reference/mc68302-status.md`: `$703B`
sets ENR on SCC2 then SCC1, and `$2400` unmasks their interrupt sources.  PB3
is the existing LRCLK input.  The entry before `$00E400` remains `[OPEN]`, but
the `$00E436` block is a verified part of that SCC configuration path.

Dataflow is exactly:

```text
$0CE3 = 0 -> D5=$10,D6=7 -> 17/8 DBRA iterations -> fixed $703B writes
$0CE3 = 1 -> D5=7,D6=4   ->  8/5 DBRA iterations -> fixed $703B writes
```

The branch changes delay length only.  It neither changes the SCM values nor
writes a mode-specific PBDAT, CS1, ES5506, ES5510, or external latch value.
This disproves the narrow hypothesis that this consumer selects distinct SCC
configuration bytes as a clock-control mechanism.  The physical meaning of
the delay difference is `[OPEN]`; it must not be called a sample-rate delay.

## 5. `$00E4E0`: separate descriptor initialization

`$00E4D8` is an `rts`; `$00E4DA` begins a separate routine.  It is not a
second phase of the receiver-enable code above:

```text
$00E4DA  D6 = 3
$00E4DC  D1 = $0200
$00E4E0  tst.b $0CE3
$00E4E4  bne $00E4EA
$00E4E6  D1 = $0320                 ; only when $0CE3 == 0
$00E4EA  D0 = 8; D3 = $0200
$00E4F2  jsr $FFF8C09E
$00E4F8  A1 -= $48; A0 = (A1)
$00E500  DBRA D6,$00E4EA
$00E504  rts
```

`$F8C09E` writes the caller's A0-rooted header (`+8 = D0`, `+A = D1`) and
initializes successive `+$28` descriptor records using D3 and D1.  With D0=8,
its decrement-and-branch loop emits eight records; `$00E4DA` repeats the operation four times.
The resulting dataflow is:

```text
$0CE3 = 0 -> D1=$0320 -> four A0-rooted descriptor groups
$0CE3 = 1 -> D1=$0200 -> four A0-rooted descriptor groups
```

The live A0/A1 roots and the routine's caller are not yet recovered, so this
is deliberately a **descriptor initializer**, not a proven allocator, voice
pool, audio buffer, or rate register.  Its verified sinks are ordinary RAM
headers/range records supplied by the caller.  There is no device write in
this routine or in `$F8C09E`'s bounded initializer body.

## 6. `$0077E6`: slot-5 periodic gate

Consumer C belongs to the known infinite `sched_slot5_poller` at `$0077C2`:

```text
$0077CA  D0=$64; trap #8             ; stable wake cadence about 12 ms
$0077D0  $D0B0 = 0
$0077D4  call $00E68E
$0077DA  call $007CF0                ; unconditional
$0077E0  call $007CF0                ; unconditional
$0077E6  cmpi.b #1,$0CE3
$0077EC  beq $0077FE                 ; mode 1: suppress optional call
$0077EE  A0 = word($D0B0)
$0077F2  tst.b (-$2F3C,A0)           ; $FFD0C4 + index
$0077F6  beq $0077FE
$0077F8  call $007CF0                ; mode 0 only, when gate byte != 0
$0077FE  D0 = byte($FFD0B6 + index); trap #7
...       index increments through 0..11 and loop repeats
```

Thus `$0CE3=1` bypasses exactly the third, gate-controlled `$007CF0` call;
`$0CE3=0` permits it for enabled entries.  This is control flow only at the
consumer boundary.  `$007CF0` has not been semantically recovered, so the
consumer cannot yet be named audio scheduling, voice allocation, or UI
maintenance.  The known periodic scheduler provenance supports the narrower
classification: mode-selected background-service gating.

## 7. A/B/A consequences and lifecycle

| Consumer effect | A (`$0CE3=0`) | B (`$0CE3=1`) | A2 (`$0CE3=0`) | Status |
|---|---|---|---|---|
| `$00E436` | longer D5/D6 delays | shorter D5/D6 delays | longer delays | `[Derived]` from verified branch/state |
| `$00E4E0` | D1=`$320` descriptor spacing | D1=`$200` | D1=`$320` | `[Derived]` from verified branch/state |
| `$0077E6` | optional `$007CF0` remains gate-controlled | optional call suppressed | gate-controlled again | `[Derived]`; enclosing poller execution `[Verified runtime]` |

`$00E436` executes when the SCC receiver-enable lifecycle runs; it is
configuration-time code, not a continuous service.  `$00E4E0`'s caller and
runtime lifecycle remain `[OPEN]`.  `$0077E6` is continuous background work,
not effect-load-only code.  The temporary A/B/A run verifies all three input
states and effect commits, but does not upgrade the first two routines to a
new direct V3.50 execution witness.

## 8. Hardware, voice and clock implications

* **Board-facing effect:** only `$00E436` reaches the MC68302 SCC boundary;
  its values are invariant, while only CPU delay-loop counts depend on mode.
* **Additional ES5506 control:** none found.
* **ES5510 relation:** none in these three chains; host upload/commit remains
  the already verified separate path.
* **Voice allocator:** none of the consumers reaches an allocator limit,
  active-voice pool, or reserved-slot state.
* **31/23 -> 32/24:** `[LIKELY]`, unchanged.  These consumers neither prove
  nor disprove it.
* **Clock control:** `[OPEN]`.  No consumer produces a mode-specific clock,
  mux, divider, CS1, ES5701, GPIO or serial-register value.

## 9. Negative evidence and next experiment

The following narrow possibilities are now ruled out for the verified local
paths:

* `$00E436` is not a branch that chooses different SCC mode-register values;
  `$703B` is written in both cases.
* `$00E4E0` is not a direct hardware-control writer.
* `$0077E6` is not itself an ES5506/ES5510/board-register writer.

The single next experiment is to backtrack `$007CF0` only far enough to
identify its first persistent sink for the mode-0 optional call, using the
existing slot-5 execution witness and a focused enabled-entry stimulus.  Stop
if that sink is ordinary background RAM without voice/board provenance; follow
it only if it is a concrete device or voice-allocator boundary.

MODE STATE:
    descriptor +$66 -> $0CE3

ACTV PATH:
    $0CE3 -> $0D5E -> +1 -> $1F/$17 -> ES5506 ACTV

CONSUMER $00E436:
    SCC receiver-enable delay selector; fixed `$703B` SCM writes.

CONSUMER $00E4E0:
    `$320/$200` RAM descriptor-initializer parameter selector.

CONSUMER $0077E6:
    periodic slot-5 gate for one optional `$007CF0` call.

VOICE ALLOCATOR:
    [OPEN]

31/23 -> 32/24:
    [LIKELY]

ADDITIONAL ES5506 CONTROL:
    none found.

BOARD-FACING CONTROL:
    SCC receiver enable is reached, but `$0CE3` changes only its delays, not
    the SCC/IMR values.

CLOCK-CONTROL EVIDENCE:
    [OPEN]

ES5510 RELATION:
    none in these consumers.

COMPLETE RATE PATH:
    [PARTIAL] — descriptor-to-ACTV remains verified; these siblings add no
    physical clock control.

NEXT MINIMAL STEP:
    follow the optional mode-0 `$007CF0` callback to its first persistent sink.
