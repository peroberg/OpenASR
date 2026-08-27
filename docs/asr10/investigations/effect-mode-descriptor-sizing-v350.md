# Effect-mode descriptor sizing — ASR-10 V3.50

## 1. Result and scope

This pass follows only the mode-dependent `$00E4DA/$00E4E0` descriptor path.
It starts at the already verified effect-mode byte `$0CE3`; it does not start
from clocks, oscillators, ES5701, or audio symptoms.

**Result:** `$0320/$0200` is a verified per-range byte stride in four
eight-entry, object-relative range tables.  It is neither a timeout nor an
allocator request, and `$F8C09E` performs no allocation.  The four objects
are two fixed voice-control objects and two SCC receive-control objects.
The latter two are consumed both by SCC ring setup and by the established
ES5506 voice callback family.  This makes the branch audio/voice relevant,
but it is still not a sample-clock control path and does not explain the
31/23-versus-32/24 question.

Evidence labels follow
[`methods-hypothesis-management.md`](../reference/methods-hypothesis-management.md).

## 2. Starting evidence

| committed effect state | `$0CE3` | ACTV | documented class |
|---|---:|---:|---|
| A: ROM HALL REVERB | `00` | `$1F` | low class |
| B: 44LUSH PLATE | `01` | `$17` | 44.1 kHz / 23 voices |
| A2: ROM HALL REVERB | `00` | `$1F` | low class restored |

The commit and `$0CE3 -> ACTV` transition are established in
[`actv-source-and-rate-control-v350.md`](actv-source-and-rate-control-v350.md).
This pass establishes the separate `$0CE3 -> range-table stride` sibling.

## 3. `$00E4DA` routine and calling convention

`$00E4D8` is an `rts`; `$00E4DA` is a distinct entry.  Its complete bounded
control flow is:

```asm
$00E4DA  moveq   #3,D6
$00E4DC  move.w  #$0200,D1
$00E4E0  tst.b   $0CE3
$00E4E4  bne     $00E4EA
$00E4E6  move.w  #$0320,D1

$00E4EA  moveq   #8,D0
$00E4EC  move.l  #$00000200,D3
$00E4F2  jsr     $FFF8C09E
$00E4F8  suba.l  #$48,A1
$00E4FE  movea.l (A1),A0
$00E500  dbra    D6,$00E4EA
$00E504  rts
```

The entry contract is a first object root in `A0` and its pointer-slot address
in `A1`.  It calls the helper, then walks the pointer-slot table backward in
`$48`-byte steps.  `D6=3` makes exactly four calls.  No value is returned;
the persistent result is the four initialized objects.

The mode contract is exact:

```text
$0CE3 = 0 -> D1 = $0320 = 800
$0CE3 = 1 -> D1 = $0200 = 512
```

## 4. `$F8C09E` contract

`$F8C09E` is a range-table initializer, not an allocator.  Its inputs are
`A0` (object base), `D0` (entry count), `D1` (per-entry stride), and `D3`
(initial range value).  It writes:

```asm
$F8C09E  move.w D0,$08(A0)          ; count
$F8C0A2  move.w D1,$0A(A0)          ; stride
$F8C0A6  lea    $28(A0),A2
loop:
          move.l D3,$00(A2)
          move.l D3,$04(A2)
          move.l D3,$0C(A2)
          add.l  D1,D3
          move.l D3,$08(A2)
          move.l D3,$10(A2)
          lea    $14(A2),A2
          subq.l #1,D0
          bne    loop
          rts
```

With the caller values, it writes eight records of `$14` bytes.  The table
therefore carries a lower value (fields `+0/+4/+C`) and a lower-plus-stride
value (fields `+8/+10`) for each entry.  The exact semantic names of duplicate
fields are not needed here; the monotonically contiguous range construction
is verified static dataflow:

```text
entry n lower = $0200 + n * D1
entry n upper = $0200 + (n + 1) * D1
```

This proves that `D1` is the object-relative **range stride/capacity in
bytes**.  It does not reserve memory, return a base, alter `A0`, or advance
`A1`.

## 5. Four structures and layout

The A/B/A write witness observed the exact `A1` pointer slots and `A0` object
roots, in caller order:

| pointer slot / object class | A0 root | header `+8/+A` |
|---|---:|---|
| `$13B0`, fixed voice-control object | `$007F1300` | `8 / $320` or `8 / $200` |
| `$1368`, fixed voice-control object | `$007F2E00` | `8 / $320` or `8 / $200` |
| `$1320`, SCC2/voice-control object | `$007F4900` | `8 / $320` or `8 / $200` |
| `$12D8`, SCC1/voice-control object | `$007F6400` | `8 / $320` or `8 / $200` |

The four slots are exactly `$48` bytes apart in reverse order, matching
`$00E4F8`.  Runtime values at all four bases were identical within a mode.

```text
object +$08: count = 8
object +$0A: stride = $0320 (mode 0) or $0200 (mode 1)
object +$28: first of eight $14-byte range records

mode 0: [$0200,$0520), [$0520,$0840), ... [$17E0,$1B00)
mode 1: [$0200,$0400), [$0400,$0600), ... [$1000,$1200)
```

Consequently the covered object-relative extent is `$1900` bytes in mode 0
and `$1000` bytes in mode 1.  These are derived byte extents (`8 * D1`), not
an independently named audio block duration.

## 6. A -> B -> A runtime witness

A temporary Lua probe retained write taps for the `$F8C09E` header stores and
read the fully initialized tables after each firmware-committed effect state.
The post-store PCs `$F8C0A2` and `$F8C0A6` are completed-write witnesses; they
are not instruction-fetch/read-tap claims.

| state | all four `+8` | all four `+A` | first upper | final upper |
|---|---:|---:|---:|---:|
| A, `$0CE3=00` | `0008` | `0320` | `0520` | `1B00` |
| B, `$0CE3=01` | `0008` | `0200` | `0400` | `1200` |
| A2, `$0CE3=00` | `0008` | `0320` | `0520` | `1B00` |

Each transition produced all eight header writes (count and stride for all
four roots).  The B values and restored A2 values are therefore direct
reinitialization evidence, not a display-derived inference.

## 7. First meaningful consumers

### `$12D8/$1320`: SCC receive descriptors

ROM `$F8C1BE` consumes the objects' `+$08`, `+$0A`, and `+$28` range table.
It builds standard eight-entry SCC buffer descriptors, increments the
payload pointer by the header stride, and writes the same stride into each
channel's MRBLR field.  `$F8C16C` invokes this for `$12D8`/SCC1 and `$F8C188`
for `$1320`/SCC2.

Thus, for these two objects, `D1` is directly the SCC receive-buffer capacity
and the spacing of the eight object-relative payload ranges.  This is the
same descriptor contract independently documented in
[`scc-cp-rx-contract.md`](scc-cp-rx-contract.md).  It reaches the MC68302 SCC
parameter/device boundary, but is not a mode-specific clock-register write.

### `$12D8/$1320`: ES5506 voice callback

The same two control objects are mapped to fixed voice records by
`$F8DF3E-$F8DFF8`; it installs `$F8E160` as their callback.  `$F8E160` reads
the range table at `object + $30 + index*$14` and
`object + $2C + index*$14`, transforms the values, adds the voice base, and
writes ES5506 host offsets `$10` and `$08`.

This is a verified firmware consumer of the D1-derived boundaries at an
ES5506 voice-programming boundary.  It proves voice/audio relevance of this
descriptor family.  It does not prove those entries are output frames,
effect-DSP buffers, or a physical sample clock selector.

### `$1368/$13B0`: fixed voice-control objects

`$F8DD94-$F8DDF2` maps `$1368` and `$13B0` to fixed voice records `$8288` and
`$8360`, based on object state and backpointer fields.  This establishes their
voice-control ownership.  No direct post-initialization reader of their
`+$08/+A/+28` range fields was needed to classify the four-object group, and
none is promoted here.  Their table-specific consumer remains `[OPEN]`.

## 8. Ownership, lifecycle, and relations

The routine reinitializes all four range tables during every observed
effect-mode transition A -> B -> A.  It is configuration-time work, not a
periodic service.  The consumer split is:

```text
$1368/$13B0       -> fixed voice-control objects -> fixed voice records
$1320/$12D8       -> SCC2/SCC1 descriptor rings -> MC68302 SCC
                    \-> fixed voice records -> $F8E160 -> ES5506
```

There is no verified shared field with the `$00E436` SCC-enable delay loop or
the Slot-5 `$007CF0` ring root.  They share only the upstream `$0CE3` mode
class.  The table is also not an ES5510 host-upload/program buffer: no
ES5510 access occurs in `$00E4DA`, `$F8C09E`, or the direct consumers above.

## 9. What this does and does not explain

* **Audio/voice relevance:** `[VERIFIED firmware]` through the `$12D8/$1320`
  -> fixed-voice -> `$F8E160` -> ES5506 boundary.
* **Allocator relevance:** `[NONE]` in this bounded path.  It configures
  range tables; it does not select voices, set an active-voice bound, or show
  a reserved slot.
* **ES5510 relation:** `[NONE]` in the measured/static direct path.
* **Device/board relation:** `[VERIFIED firmware]` to SCC parameter RAM and
  to ES5506 voice registers, but no mode-specific board clock/mux/divider
  control was reached.
* **Rate-mode relevance:** `[LIKELY]` as a genuine effect-mode-selected
  resource/buffer capacity: A/B/A reinitializes it with `$320/$200`.  Its
  relation to physical 29.7619/44.1 kHz timing is `[OPEN]`; interpreting its
  numerical values as sample counts or milliseconds would be arithmetic only.
* **31/23 -> 32/24:** `[LIKELY]` unchanged.  The path does not reach the
  voice allocator or a `slots - 1` rule.

## 10. Verdict

```text
MODE 0:
    D1=$320

MODE 1:
    D1=$200

$F8C09E CONTRACT:
    initializes eight $14-byte object-relative range records from D3=$200;
    D1 is the byte stride and is stored at object +$0A.

STRUCTURES:
    $7F1300, $7F2E00, $7F4900, $7F6400; pointer slots
    $13B0, $1368, $1320, $12D8 respectively.

STORED D1-DERIVED STATE:
    header +$0A and the contiguous range boundaries through $1B00/$1200.

PRIMARY CONSUMERS:
    SCC1/SCC2 descriptor setup for $12D8/$1320; $F8E160 ES5506 voice callback
    consumes the same two objects' range entries.

SUBSYSTEM:
    fixed voice-control plus SCC receive-buffer configuration.

AUDIO/VOICE RELEVANCE:
    [VERIFIED]

ALLOCATOR RELEVANCE:
    [NONE]

ES5510 RELATION:
    [NONE]

DEVICE/BOARD RELATION:
    [VERIFIED] SCC and ES5506 consumers; no clock-control write.

$320/$200 MEANING:
    per-range byte stride/capacity, eight ranges per object.

RATE-MODE RELEVANCE:
    [LIKELY] mode-selected resource sizing; physical-rate meaning [OPEN].

BRANCH VERDICT:
    [CLOSE] as a clock-control lead.  Retain it as verified mode-dependent
    SCC/voice range configuration.

NEXT MINIMAL STEP:
    Do not extend this descriptor branch.  The next rate-path experiment must
    start from a separate concrete board-facing control candidate, not from
    $320/$200 arithmetic.
```
