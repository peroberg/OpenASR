# ASR-10 V3.50 — ACTV source and rate-control backtrack

## Executive summary

The ES5506 ACTV value is no longer an unclassified caller input.  A targeted
V3.50 A -> B -> A trace and static backtrack establish this firmware chain:

```text
current effect descriptor $0E92
  -> descriptor byte +$66
  -> runtime class byte $0CE3
  -> ROM $F8CC9A chooses $0D5E = $1E or $16
  -> $F8CDB2 adds one into D3 ($1F or $17)
  -> $F8CDBC calls OS binding $8E7A
  -> binding target $00E7B4 saves/restores D3 as D4
  -> $00E826 stores D4 to ES5506 ACTV
```

This proves an effect-descriptor-controlled OTTO active-slot transition.  It
does **not** prove a physical clock selector: the same targeted branch has no
reversible CS1, MC68302 GPIO, ES5701, clock-latch, or ES5506 MODE write.
No C++ change is warranted.

### Evidence labels

* `[Verified firmware]`: bounded V3.50 static disassembly/dataflow.
* `[Verified runtime]`: retained temporary Lua taps with a live V3.50 boot
  witness and A -> B -> A restoration.
* `[Verified source]`: local original Ensoniq documentation.
* `[Likely]` and `[OPEN]`: as defined by the ASR-10 methods reference.

## 1. A/B/A baseline

| Stable effect | `$0E92` descriptor | `+$66` | `$0CE3` | `$0D5E` | `$0D60` | D4 / ACTV |
|---|---:|---:|---:|---:|---:|---:|
| A: ROM HALL REVERB | `FFF9B626` | `00` | `00` | `001E` | `001F` | `1F` |
| B: Bank 44LUSH PLATE | `0062B600` | `01` | `01` | `0016` | `0017` | `17` |
| A2: ROM HALL REVERB | `FFF9B626` | `00` | `00` | `001E` | `001F` | `1F` |

The existing effect-upload investigation supplies the positive commit oracle:
B completed 549 ES5510 verification reads with zero mismatch, retry and
error; restored A completed 822 with the same result.  Thus the table is not
a display-only comparison.  `44LUSH PLATE` is documented as a 44.1-kHz,
23-voice algorithm `[Verified source]`; ROM HALL is the existing low-class
comparison, with its explicit 30-kHz pairing still `[Likely]`.

## 2. Exact ACTV routine and writer contract

The OS binding table maps RAM slot `$008E7A` to `$00E7B4`.  The active caller
at `$F8CDBC` is independently witnessed by the user-stack return `$F8CDC2`
when the ACTV tap fires.  Relevant decoded instructions are:

```text
$00E7B4  move.w #$28,$0F72.w
...       prepares OTTO state and preserves D3 on the stack
$00E7DE  move.w (A7)+,D4
...       status/interrupt waits using $FC6829 and $FC6818
$00E826  move.b D4,($005E,A0)       ; A0 = $00FC2001
$00E82A  jsr $00E838
```

The byte store targets `$FC205F`; the Lua write tap reports its aligned word
address `$FC205E`, with post-store PC `$00E82A`.  Earlier provenance notes
called this instruction `$00E824`; `$00E826` is the corrected instruction
address.  The routine does not derive D4: it restores the caller's D3.

## 3. D4 producer and transformation

The active ROM path is:

```text
$F8CC9A  test $0CE3
          zero branch:     D0 = $001E
          non-zero branch: D0 = $0016
...       writes D0 to $0D5E
$F8CDB2  move.w $0D5E,D3
$F8CDB6  addq.w #1,D3
$F8CDB8  move.w D3,$0D60
$F8CDBC  jsr $FFFF8E7A
```

The runtime values in the table prove the reversible transformation
`$1E + 1 = $1F` and `$16 + 1 = $17`.  `$00E7B4` preserves this D3 and loads
it into D4 immediately before the ACTV write.  Therefore D4 is calculated,
not stored directly in the descriptor.

## 4. Descriptor source and conservative semantics

Runtime RAM code at `$013060` performs:

```text
move.l A1,$0E92.w
move.b ($66,A1),$0CE3.w
```

The source-pointer and byte reversibility in the A/B/A table, plus the write
tap's post-write PC `$013066`, establish this relation `[Verified firmware /
Verified runtime]`.  Descriptor byte `+$66` is therefore a **verified ACTV
class selector**: zero produces 32 ES5506 slots and non-zero produces 24.

It is `[Likely]` a rate/voice-class field because the B descriptor belongs to
the independently documented 44.1-kHz/23-voice algorithm.  It is not yet a
verified physical sample-clock selector, and it must not be named an ES5701
register, divider bit, or oscillator mux.

## 5. Sibling consumers and writes

The same `$0CE3` branch has other firmware consumers, but none is yet a
clock-control witness:

| Consumer | Verified action | Status |
|---|---|---|
| `$F8CC9A` | chooses `$0D5E` and reaches ACTV through the chain above | `[Verified firmware]` |
| `$00E436` | selects delay/count values (`D5=$10,D6=7` versus `D5=7,D6=4`) before fixed SCC configuration writes | `[Verified firmware]`; timing meaning `[OPEN]` |
| `$00E4E0` | selects `D1=$320` versus `$200` before a service call | `[Verified firmware]`; resource meaning `[OPEN]` |
| background slot near `$0077E6` | branches on `$0CE3` | consumer identity `[Verified firmware]`; semantics `[OPEN]` |

The direct `$00E7B4` A/B/A hardware trace is also negative evidence.  Each
state performs the same preparatory writes to `$FC207F` and `$FC205F` with
zero, polls `$FC6829`, and performs the same OTTO interrupt/control write.
Only the final `$FC205F` value differs: `$1F -> $17 -> $1F`.

No reversible write in that branch reached CS1, MC68302 GPIO/PBDAT,
ES5701, a board latch, or a clock-selection target.  Existing evidence also
has no A/B change of ES5506 MODE (boot value `$0D`); the targeted committed
reconfiguration difference is ACTV only.

## 6. ES5510 relation and event order

The descriptor/source state is established as part of the successfully
committed effect object, and both A and B use the verified ES5510 host
upload/readback path.  The ACTV branch is then called from ROM `$F8CDBC` with
the resolved class-derived D3.  No ES5510 host/control register is written by
the `$0CE3` -> ACTV branch, so this establishes provenance, not ESP execution
or ESP frame-clock behavior.

The bounded ordering is consequently:

```text
effect object resolution -> $0E92 / $0CE3 -> class-derived $0D5E/$0D60
-> $F8CDBC -> $00E7B4 -> ACTV -> completed UI state
```

Host upload/readback succeeds for the committed object; the exact instruction
interleaving of the full ESP upload with descriptor resolution is not needed
for, and was not promoted to, a clock-control claim.

## 7. 31/23 versus 32/24

ES5506 ACTV is zero-based: `$1F` means 32 active slots and `$17` 24
`[Verified source]`.  The descriptor class drives ACTV values numerically
matching the documented 31/23 voice labels.  This supports, but does not
prove, the familiar reserved-slot interpretation:

```text
31 playable voices + one OTTO slot = 32  [Likely]
23 playable voices + one OTTO slot = 24  [Likely]
```

The missing evidence is a consumer in the voice allocator showing whether a
slot is reserved.  The field itself is an ACTV-class selector, not allocator
proof.

## 8. Clock control verdict and minimal next step

The arithmetic model remains attractive:

```text
Y2 / 2 / (16 * 32) = 29.7619 kHz
Y3 / 2 / (16 * 24) = 44.1000 kHz
```

This investigation adds firmware evidence for the 32/24 half of that model,
but no firmware or hardware evidence for Y2/Y3 selection or `/2`.  ES5701 is
therefore `[OPEN]` for this path.  The minimum next experiment is a focused
backtrack of the non-ACTV `$0CE3` siblings (especially `$00E436/$00E4E0`) to
their concrete service targets, followed only by an A/B/A tap on a resulting
board-facing candidate.  Do not alter clocks before such a candidate exists.

## Remaining OPEN

* physical Y2/Y3 routing, `/2` stage, and any clock mux/divider control;
* whether one `$0CE3` sibling reaches board/serial/ESP timing state;
* ES5510 execution and frame-clock relation to this class;
* voice allocator evidence for a genuinely reserved slot;
* exact physical role of ES5701/U41 in the audio clock path.

EFFECT:
    ROM HALL REVERB (`+$66=$00`) <-> Bank 44LUSH PLATE (`+$66=$01`).

COMMITTED STATE:
    `$0E92` current-effect descriptor; successful ES5510 host verification
    establishes A -> B -> A commit.

RATE/VOICE SOURCE:
    descriptor byte `+$66` -> `$0CE3` [Verified ACTV-class selector].

D4 TRANSFORMATION:
    `$0CE3` selects `$0D5E=$1E/$16`; `$F8CDB2` adds one to D3,
    `$00E7B4` restores it as D4 `$1F/$17`.

ACTV WRITER:
    `$00E826` (the former `$00E824` label is corrected; post-write PC `$00E82A`).

ACTV:
    `$1F <-> $17` [Verified runtime].

SIBLING CONTROL:
    firmware delay/resource branches only; no reversible board-facing write
    identified.

CLOCK CONTROL:
    [OPEN] — no Y2/Y3, divider, ES5701, CS1, or GPIO control reached.

31/23 -> 32/24:
    [LIKELY] reserved-slot model; allocator proof remains open.

COMPLETE RATE PATH:
    [PARTIAL] effect descriptor -> ACTV is verified; physical clock control
    is open.
