# ASR-10 Panel Protocol — Channel B Output Ring and ACK Loop

**Date:** 2026-07-13 · Companion to `architecture.md` (see there for register map, channel model, evidence levels)

---

## 1. Main model

The panel lives on **DUART Channel B**. Display output is an ack-driven byte ring;
panel input (keys/ACK/status) arrives on RHRB and is routed through the OS RX parser.

```text
slot0/output code enqueues bytes into ring $0378-$03B7   (f89a72)   [STAT]
$03b8 = write pointer, $03ba = read pointer                          [STAT]
byte[$03bc] = number of bytes/outstanding units in ring              [STAT inc/dec; MODEL exact unit]
$03c5 = TX idle / kick flag                                          [STAT usage; MODEL meaning]
$03bd = unknown (flag/state; NOT the high half of a counter)         [open]
panel ACK/response on Channel B RX drives f89a9a/f89ab8 progress     [DYN via FF probes]
byte[$03bc] reaching zero -> f89ac2 clears slot0 bit1 (wake)         [STAT+DYN]
```

## 2. The ring routines [STAT]

```asm
; enqueue byte D2 (IRQ wrapper at f89a6e: bsr.s f89a72; rte)
f89a72: ori #$0700,SR
f89a76: movea.w $03b8.w,A0        ; write ptr
f89a7a: move.b D2,(A0)+           ; store byte
f89a7c: cmpa.w #$03b8,A0          ; wrap at ring top
f89a82:   movea.w #$0378,A0
f89a86: move.w A0,$03b8.w
f89a8a: addq.b #1,$03bc.w         ; count++
f89a8e: tst.b $03c5.w             ; TX idle?
f89a92: beq  f89a9a               ; idle -> kick send
f89a94: andi #$f8ff,SR; rts

; send/complete (drain step)
f89a9a: tst.b $03bc.w / beq done
        move.b (A0)+,$fffc4817.l  ; -> THRB (Channel B!)
f89ab8: subq.b #1,$03bc.w
f89abc: bne  f89ac8
f89abe: movea.w $00d8.w,A0
f89ac2: bclr #1,$0002(A0)         ; wake slot0
f89ac6: st  $03c5.w
```

Flow-control thresholds: `cmpi.b #$1e/$0e,$03bc` at `f89a20/f89a46/f89a60`
(ring space checks). OS drain barriers: two `tst.b $03bc; bne self` spins in V161
(`img 0xeae8`, `img 0x127a4`). [STAT]

## 3. RX side: parser and completion [STAT]+[DYN]

- `$03c0` = parser state pointer; first byte via `ffb3ba`, second-byte states
  `ffb24e`/`ffb2f6`; injected `71,xx` → event class `0x55` → object → trap #9 →
  slot3 → `f8f37a` node type `0006`. [DYN]
- **Completion call sites live in the RX parser** (zero in boot ROM):
  `ffb286`/`ffb32e` → `jsr f89aec`; `ffb3e4`/`ffb424` → `jsr f89a9a`. [STAT]
  Incoming panel bytes drive ring progress: ack → send next byte → `count--`.
- Panel raw-byte mapping table at `fff82484` (raw→mapped key codes). [DYN dump]
- Early boot uses the **polled** primitive `f89cb0` (write `FC4817`, fixed delay,
  read-back) — why "ENSONIQ ASR-10"/"LOADING SYSTEM" appear with no DUART model. [STAT]

## 4. Diagnostic evidence and its boundary

- `71,00` stimulus produced firmware TX `A0 55 00` — **reclassified**: previously
  read as panel TX; after the ring/FC4817 analysis this is Channel A service TX
  (likely MIDI), not the main panel-output path.
- 14 diagnostic `FF` probes drained `byte[$03bc] 0e → 00`, each probe exactly one
  `f89a9a → f89ab8` completion; zero-crossing reached `f89ac2`, slot0
  `0202 → 0002`, dispatcher selected slot0. [DYN]
- **Boundary:** the FF probes *simulated* panel ACK/completion events. It is not
  proven that `FF` is the authentic panel ACK byte, that the natural panel sends
  `71,00`, or what real response format/timing the 80C52 panel uses (single ACK,
  `FC xx`, echo, multi-byte status — all open).
- What built the initial `0e` outstanding bytes has not been traced. [open]

## 5. What an emulated panel must do [MODEL]

A stub that only receives THRB bytes is insufficient. The device must:
consume THRB output, maintain protocol state, and return the ACK/status bytes on
RHRB (raising RxRDYB → IRQ6) that steer the parser into the `f89a9a`/`f89aec`
branches — one completion per outstanding unit — with plausible ordering/timing.

**esqpanel reuse [HYP]:** MAME's `esqpanel` devices implement exactly this pattern
for VFX/SD/EPS. ASR-10's display format matches `ESQPANEL1X22` (1×22 VFD + fixed
LEDs). Compare THRB byte stream vs esqpanel commands, and esqpanel responses vs
the `$03c0` parser states (`ffb3ba..ffb4xx`). Same electrical style is likely;
command set and sizes may differ — treat as hypothesis, verify against parser.

## 6. Open protocol questions

1. Authentic boot-time panel handshake (who speaks first, what bytes).
2. Real ACK/status vocabulary (`FF`? `FC xx`? echo?) and per-unit granularity.
3. Meaning of `$03bd` (seen `01`; possibly affected by `FC 01`).
4. TX message framing: for node `1504` (`+02=0006, +04=0055, +06=0000` → TX
   `A0 55 00`): source of the `A0` lead byte, meaning of `+08=1514/+10=151c`. [open]
5. Whether the natural boot enqueues display text (e.g. disk/bank prompt) into
   the same ring — likely, given the two OS drain barriers.

## 7. TRAP #$A marker+payload encoder (2026-07-21) [STAT]+[DYN]

A second, distinct producer feeds the same ring/THRB path from §2. It is reached
by a **software** exception, not a hardware interrupt:

```text
producer loads serialized byte into D2
  -> opcode 0x4e4a, TRAP #$A (trap immediate 10, exception vector 42, table addr 0x00a8)
  -> runtime-installed vector value 0xffff88e8
  -> trampoline at 0xff88e8 -> jmp 0xfff89a5a
  -> f89a5a flow-controlled enqueue handler (expects D2 preloaded; not itself a producer)
  -> f89a72 TX-ring enqueue (§2)
  -> f89a9a-f89ac8 dequeue/service -> f89aa4 THRB write -> RTE
```

Do not call TRAP #$A "Line-A" — Line-A is a separate, pre-existing mechanism
(opcode family `0xAxxx`, exception vector 10, service at `f882ca`).

**Encoder loop** (`f8a7dc-f8a808`, boot ROM): reads a source byte from `(A2)+`,
separates its bit 7, and emits it as **two** transmitted bytes — marker, then
masked payload:

```text
payload = source_byte & 0x7f
marker  = (0x77 if bit7 clear, 0x7a if bit7 set) + D0
```

**Three D0 classes**, statically verified at the dispatch ladder:

```text
0x0089b0: moveq #2,D0; bra 0x0089ba   -> class 2
0x0089b4: moveq #1,D0; bra 0x0089ba   -> class 1
0x0089b8: moveq #0,D0                 -> class 0
0x0089ba: jmp 0xfff8a7dc
```

Full marker matrix:

| D0 | bit7=0 | bit7=1 |
|----|--------|--------|
| 0  | 0x77   | 0x7a   |
| 1  | 0x78   | 0x7b   |
| 2  | 0x79   | 0x7c   |

This proves three serialization classes exist. **It does not prove their
physical meaning** — do not label them active/inactive, left/right,
normal/highlight, etc. without further evidence.

### 7.1 Explanation of the observed `7b,0b,7a,0b` and the visible "Z" [DYN]

Both halves come from the **same** static record, `f824e8` = `"8b 00"`,
selected via pointer-table index 0 (`f824c8 -> f824e8`):

| invocation | source byte | bit7 | D0 | marker | payload |
|---|---|---|---|---|---|
| 1st (`89b4`, fixed) | 0x8b | set | 1 | 0x7b | 0x0b |
| 2nd (`89b8`/`89b0`, selected) | 0x8b | set | 0 | 0x7a | 0x0b |

The current MAME parser treats `0x7a` as printable `z`→`Z` (14-segment renders
both cases identically) and `0x0b` as a text-flush delimiter, leaving a visible
"Z". **This is a parser artifact, not raw firmware text**, and not yet a proven
statement of the correct physical panel effect.

### 7.2 Short-record library and selector loop [STAT]+[DYN]

`f824c8` is an 8-entry pointer table into a library of short null-terminated
records living at `f824e8` onward. Some entries alias to the same record:

```text
idx 0 f824c8 -> f824e8 "8b 00"
idx 1 f824cc -> f824ea "8c 00"
idx 2 f824d0 -> f824ec "83 00"
idx 3 f824d4 -> f824f0 "8e 00"
idx 4 f824d8 -> f824ea "8c 00"
idx 5 f824dc -> f824f0 "8e 00"
idx 6 f824e0 -> f824ec "83 00"
idx 7 f824e4 -> f824f2 "83 03 00"
```

`f824ee` = `"82 00"` is **direct-only** (addressed by `$3afe`, not via this
pointer table) — this is not simply an eight-field display table.

Live disk-overlay routine (0x003b32-0x003b70; absent from the static 256KB boot
ROM, same as `f89a5a`):

```asm
3b32: movea.l #$fff824c8,A3
3b38: move.w  $cbb8.w,D2        ; "previous" selection (see caveat below)
3b3c: movea.l (A3,D2.w),A2
3b40: jsr     $89b4.w           ; redraw previous selection, class 1
3b44: moveq   #0,D2
3b46: cmp.w   $c98.w,D2         ; $c98 = current-selection index (observed 0)
3b4a: bne     $3b68
3b4c: lsl.w   #2,D2
3b4e: move.w  D2,$cbb8.w        ; persist new selection as "previous" for next call
3b52: movea.l (A3,D2.w),A2
3b56: lsr.w   #2,D2
3b58: cmp.w   #4,D2
3b5c: bcs     $3b64
3b5e: jsr     $89b0.w           ; index 4-7 -> class 2
3b62: bra     $3b68
3b64: jsr     $89b8.w           ; index 0-3 -> class 0
3b68: addq.w  #1,D2
3b6a: cmp.w   #8,D2
3b6e: bcs     $3b46
3b70: bsr     $3afe             ; then always calls the $3afe direct-record step
```

`$3afe` (called from `3b70`, i.e. **after** `3b32`'s loop, not before/independently)
sends the direct record `f824ee` ("82 00") conditionally on `$838e` and `$cbbc`,
and also updates `$8a72`.

**Working interpretation (STRONG INFERENCE, not proven):** `$c98` is a
currently-selected-item index (0-7); `$cbb8` persists the previously-selected
offset so the next call can redraw it; the class-0/class-2 split at index 4
plausibly corresponds to which half of an 8-item display the selection falls
in. In the observed boot ("NO INST OR BANK FILES"), `$c98` is computed to 0 by
a call at `0x003c7c` (invoked with `D0=7` from a small dispatcher at `0x3ae0`,
whose own algorithm was not traced) — i.e. "nothing to select, default to slot
0" — which is why *both* transmissions in this run happen to hit the same
record.

**Open addressing caveat:** `$cbb8`, `$cbbc` and `$838e` all have bit 15 set,
so `.w`-absolute references sign-extend to `0xFFFFxxxx` (landing in the plain
`fc6900-ffffff` RAM, not the `0x0000xxxx` region); `$c98` (bit 15 clear) is
unaffected. Live capture confirmed `$838e` (true address `0xff838e`) carries
real, frequently-read/written state (0 -> 1 -> 0x52 -> 1) during the scan, and
a direct one-off read of `0xffcbb8` confirmed value `0x0000` at the moment
`$cbb8` should read as index 0 — consistent with the model above — but a
dedicated read/write tap on that same address never fired for this specific
instruction, an unexplained anomaly noted for future investigation rather than
silently resolved.

### 7.3 Inverse-decoder search [STAT — negative]

No coherent subtract/compare/range-decode structure for the `0x77-0x7c` marker
range, and no marker-then-payload reconstruction state machine, has been found
in the searched host-side artifacts (static 256KB boot ROM; disk-image search
for `0x77`-`0x7c`-relative compares was ROM-only, not yet extended to the full
disk overlay). Result: **search coverage incomplete** — not "no decoder
exists," and not grounds to assert the consumer's identity.

### 7.4 Panel-controller hardware evidence [open]

`docs/asr10/hardware-map.md` lists "ENS5702000102 + 80C52" only as a *possible*
candidate for the frontpanel/keyboard/display controller, with no cited chip
marking, photograph, schematic, or service-manual reference backing it up in
this repository. Treat panel-controller identity as **external panel
controller, firmware unavailable** — not "proven 80C52" — until a sourced
hardware reference is added.

### 7.5 External hardware observation: real panel boot sequence [external physical evidence]

External physical observation of a real ASR-10 during boot:

- all 8 instrument indicators illuminate in both red and yellow channels
  (16 lamps total);
- at "TUNING KEYBOARD - HANDS OFF", the instrument indicators turn off;
- later, LOAD flashes;
- INST remains steadily illuminated;
- STOP remains steadily illuminated.

This is external physical evidence, not host-ROM/OS evidence. The structural
match between an 8-entry selector and the three D0-selected marker classes
(`0x77`-`0x7c`) is suggestive, but no class/color/state/index mapping is
proven. In particular, do not assign meanings such as class=color,
class=state, payload=lamp, index=instrument, cursor, display half, blink,
selected, or unselected without panel-controller firmware, service
documentation, or direct hardware capture tying those meanings to the
host-side byte stream.

### 7.6 Direct transmit byte `0x66` text-frame prefix evidence (2026-07-21) [STAT]+[DYN]

A separate direct transmit path, distinct from the TRAP #$A encoder, uses
DUART Channel B.

Static local routine `f89c94` has a verified text-frame shape:

```text
f89c94: D2 = 0x66
  -> f89c48 THRB transmit of 0x66
  -> transmit bytes from A2 through f89cb0 until NUL
```

That proves `0x66` is a local prefix before a NUL-terminated direct-path text
payload in this routine. It does not prove a global meaning for byte `0x66`.

Dynamic ring-drained observations also show `0x66` before direct-path text
payloads, including the frame that previously rendered `FNO INST OR BANK FILES`.
Those bytes enter the TX ring via `f89a7a` and drain at `f89aa4`; the observed
prefix enqueue path returns through `f89a70`, while following payload bytes
return through `f89a56` and related flow-control sites. This is trusted local
runtime context for the observed text-prefix frame, but the original high-level
producer or selection condition is not yet proven.

Before the parser correction, host-side parser behavior rendered byte `0x66` as
printable ASCII `'f'`, producing visible `FNO INST OR BANK FILES` after the
marker/payload `Z` artifact was removed. Treat `0x66` as a probable
control/frame prefix, not as proven text and not as a proven control command.
Parser handling must be bound to the verified direct-text-frame context, not to
byte value alone.

### 7.7 Superseded conclusions

- TRAP #$A is Line-A / exception vector 10 (it is vector 42; Line-A remains a
  separate, pre-existing mechanism at `f882ca`).
- `f89a5a` is a semantic byte producer (it is a flow-controlled enqueue
  wrapper; D2 is preloaded by the caller).
- `f89a72` is "the drain" (it is the enqueue side; `f89a9a-f89ac8` dequeues).
- `0x0b` is a general transport delimiter (it is an encoder payload byte in
  this path).
- The two halves of `7b,0b,7a,0b` come from two different source records
  (both come from `f824e8`, `"8b 00"`, via two different D0 classes).
- A decimal-digit formatter (`f8a772-f8a78e`) produces the `f824e8` record
  (proven unrelated for this specific object).
- The visible "Z" is raw firmware text (it is a marker byte, parser-rendered).
- A full 8-entry table redraw happens on every invocation (only the fixed
  first entry plus the one matching `$c98` are actually transmitted).
