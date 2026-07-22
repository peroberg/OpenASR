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

### 7.1 Explanation of the observed `7b,0b,7a,0b` and the historical visible "Z" [DYN]

Both halves come from the **same** static record, `f824e8` = `"8b 00"`,
selected via pointer-table index 0 (`f824c8 -> f824e8`):

| invocation | source byte | bit7 | D0 | marker | payload |
|---|---|---|---|---|---|
| 1st (`89b4`, fixed) | 0x8b | set | 1 | 0x7b | 0x0b |
| 2nd (`89b8`/`89b0`, selected) | 0x8b | set | 0 | 0x7a | 0x0b |

Before the parser correction, the MAME host-side parser treated `0x7a` as
printable `z`->`Z` (14-segment renders both cases identically) and `0x0b` as a
text-flush delimiter, leaving a visible "Z". **This was a parser artifact, not
raw firmware text**, and not a proven statement of the correct physical panel
effect. The current parser consumes `0x77`-`0x7c` as neutral marker bytes and
consumes the immediately following byte as the marker payload.

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

## 8. Current parser and architecture snapshot (2026-07-22)

This section is the stable reference point after the marker/payload parser
correction, the bounded Path B direct-text-prefix correction, and the
`panel_receive_byte()` boundary refactor. Earlier sections preserve the
historical observations that led here; when they conflict with this section,
treat this section as the current understanding.

### 8.1 Current architecture

The ASR-10 driver still uses the hand-written DUART shadow model. A real MAME
`mc68681_device`/`scn2681_device` is not yet instantiated for this path.

```text
68k firmware
    |
    v
hand-written DUART shadow
    |
    v
Channel B THRB write at FC4817
    |
    v
temporary upstream bridge
    |
    v
panel_receive_byte(u8 data)
    |
    v
panel protocol parser
    |
    v
visible display state
```

The temporary upstream bridge lives in `asr10_boot.cpp` immediately before the
call to `panel_receive_byte()`, with supporting ring provenance tracked by
`note_panel_direct_text_prefix_ring_store()` and consumed by
`consume_panel_direct_text_prefix()`. It exists only to preserve the verified
local Path B behavior while the driver is still attached to a firmware/register
shadow boundary instead of a real DUART TXB byte/serial boundary. It is not a
protocol semantic rule and must be removed or replaced when byte-stream framing
or real DUART wiring can provide the boundary directly.

`panel_receive_byte(u8 data)` is the parser-facing entry point for Channel B
panel bytes. It accepts only the transmitted byte. It no longer accepts a
firmware PC, return PC, ring index, ring address, or firmware routine identity.
The parser still records the current PC internally for existing diagnostics and
milestone attribution, but protocol branching is not based on PC in
`panel_receive_byte()`.

### 8.2 Panel transport

#### PATH A: marker/payload transport

PATH A is the TRAP #$A encoder path described in Section 7. It reads a source
byte from `(A2)+`, derives `payload = source_byte & 0x7f`, selects one of three
D0 classes, and emits:

```text
marker = (0x77 if source bit 7 is clear, 0x7a if source bit 7 is set) + D0
payload
```

The proven marker matrix remains:

| D0 | bit7=0 | bit7=1 |
|----|--------|--------|
| 0  | 0x77   | 0x7a   |
| 1  | 0x78   | 0x7b   |
| 2  | 0x79   | 0x7c   |

Current parser state: bytes `0x77`-`0x7c` are recognized as neutral protocol
markers, stored as one pending marker, and not rendered as ASCII. The
immediately following byte is consumed as that marker's payload and is also not
rendered as ASCII.

Known limitations: no marker class, payload, index, color, lamp, cursor,
selection, blink, or display-half meaning is proven. No inverse decoder or
panel-controller firmware is available in this repository.

#### PATH B: direct-text transport

PATH B is a separate direct transmit mechanism on DUART Channel B. It is not
produced by the TRAP #$A encoder.

The verified local routine `f89c94` loads `D2 = 0x66`, transmits `0x66`, then
transmits bytes from `A2` through the direct transmit helper until NUL. This
proves a local frame shape:

```text
0x66
NUL-terminated text payload
```

The current parser correction handles the verified prefix only when the
temporary upstream bridge has identified the trusted direct-text-frame context.
The following NUL-terminated payload is still rendered as text.

Evidence boundary: this proves that `0x66` is a local prefix before a
NUL-terminated direct-path text payload in the trusted `f89c94` behavior. It
does not prove that every byte `0x66` is a prefix, that `0x66` is a global
control command, or that nearby bytes `0x64`-`0x69` have related meanings.

#### PATH B lineage result: f89c94 item boundary

A bounded panel TX lineage capture established that the two observed dynamic
`f89c94` invocations were complete, uninterrupted direct-text items:

```text
f89c94 Path B      = ITEM_SERIALIZED
ring path          = RAW_BYTE_STREAM
combined THRB      = MULTI_PRODUCER_SERIAL
byte interleaving  = NOT_OBSERVED
```

For each captured `f89c94` invocation, the prefix was sent through `f89c48` and
each non-NUL `A2` payload byte was sent through `f89cb0`. No foreign THRB write
occurred while either invocation was active (`foreign_during_text = 0`).

The earlier raw `0x66...NUL` byte-stream parser experiment failed because the
combined THRB stream contains multiple producers. The later `0x66` that caused
the first raw-stream mismatch came from the independent ring-drain producer at
`f89aa4`, after the active `f89c94` item had already completed. This does not
make the combined THRB byte stream a self-describing PATH B stream.

### 8.3 Parser evolution

Historical visible display:

```text
Z
```

Why it appeared: marker byte `0x7a` from PATH A was treated as printable ASCII
`z`, and `ascii_to_14seg()` rendered it as `Z`. The following payload byte was
not understood as a payload. This was a parser artifact.

Before:

```text
7b 0b 7a 0b
```

After:

```text
marker 7b + payload 0b consumed neutrally
marker 7a + payload 0b consumed neutrally
```

Historical visible display:

```text
FNO INST OR BANK FILES
```

Why it appeared: after the PATH A marker artifact was fixed, PATH B byte `0x66`
from the verified direct-text frame was still treated as printable ASCII `f`.
The text payload then rendered normally, leaving a leading `F`.

Before:

```text
66 4e 4f ...
```

After:

```text
prefix 66 consumed in verified direct-text-frame context
4e 4f ... rendered as text payload
```

Current visible display:

```text
NO INST OR BANK FILES
```

Why it is now produced: PATH A markers/payloads are consumed neutrally, and the
verified PATH B `0x66` prefix is consumed by the temporary upstream bridge before
the byte-only panel parser renders the following payload. The decoded console
or VFD text "NO INST OR BANK FILES" should not be conflated with older
parser-artifact observations.

### 8.4 Evidence inventory

#### PROVEN

- The ASR-10 panel byte stream reaches DUART Channel B THRB in the current
  hand-written DUART shadow model.
- PATH A is a marker/payload transport produced by the TRAP #$A encoder.
- PATH A source payload is `source_byte & 0x7f`.
- PATH A marker bytes are `0x77`-`0x7c`, selected by source bit 7 and D0 class.
- `0x77`-`0x7c` are protocol markers, not printable text for the host-side
  parser.
- The visible `Z` was caused by treating marker `0x7a` as ASCII.
- PATH B is separate from the TRAP #$A encoder path.
- The local `f89c94` behavior transmits `0x66` before a NUL-terminated text
  payload from `A2`.
- One observed dynamic `f89c94` invocation corresponds to one complete,
  uninterrupted PATH B direct-text item.
- No foreign THRB writes were observed during the two captured `f89c94`
  invocations.
- The combined THRB stream is a multi-producer serial stream, not a
  self-describing PATH B byte stream.
- The TX ring stores queued bytes. `$03bc` counts queued bytes, and `$03bc == 0`
  means only that the ring is empty.
- The visible `F` in `FNO INST OR BANK FILES` was caused by treating the
  verified PATH B `0x66` prefix as ASCII.
- The current parser state produces visible `NO INST OR BANK FILES` for the
  established V161 baseline.
- `panel_receive_byte(u8 data)` is the parser-facing entry point for
  panel-bound Channel B bytes.
- The helper definitions `trace_region`, `trace_slot`, `ascii_to_14seg`,
  `address_region_guess`, `region_name`, `m68302_register_name`,
  `fdc_state_field_name`, and `is_fdc_state_field` have been extracted to
  `asr10_boot_defs.*`.

#### LIKELY

- `$c98` is a selection index used by the observed 8-entry selector loop.
- The external panel controller consumes the Channel B protocol outside the
  host ROM/OS artifacts currently available in this repository.
- The structural match between the 8-entry selector and three marker classes is
  relevant to physical panel behavior.
- `0x66` is a probable frame/control prefix in the verified PATH B direct-text
  behavior.
- A raw-byte panel path with a transport adapter is the currently supported
  future boundary for preserving firmware-side submission identity while bytes
  still pass through the DUART/serial model.

#### UNKNOWN

- The physical meaning of PATH A marker classes and payloads.
- Whether marker class, payload, or selector index corresponds to lamp color,
  lamp state, cursor, blink, display half, selected state, or unselected state.
- The global meaning, if any, of byte `0x66`.
- Whether nearby values `0x64`-`0x69` belong to the same direct-transmit
  structure.
- The original high-level producer or selection condition for every observed
  ring-drained `0x66` frame.
- Whether PATH A logical item boundaries can be recovered upstream from the
  encoder, marker/payload state, or producer invocation.
- The exact front-panel controller identity and firmware behavior.
- Authentic panel ACK/status vocabulary and timing.
- How to remove the current temporary Path B bridge without overfitting to
  firmware PCs. `f89c94` remains the verified firmware anchor for PATH B; the
  design is not fully PC-independent yet.

### 8.5 Architecture cleanup completed

Completed cleanup:

- Stateless helper definitions were extracted from `asr10_boot.cpp` to
  `asr10_boot_defs.h` and `asr10_boot_defs.cpp`.
- The visible `Z` parser artifact was removed by neutral marker/payload
  consumption for `0x77`-`0x7c`.
- The visible leading `F` artifact was removed for the verified PATH B
  direct-text prefix context.
- The panel parser entry point is now `panel_receive_byte(u8 data)`.
- The parser-facing API no longer accepts firmware PCs or ring provenance.

Remaining technical debt:

- The hand-written DUART shadow model is still authoritative.
- The Path B `0x66` handling still depends on a temporary upstream provenance
  bridge.
- PATH A framing remains unresolved; ring emptiness is not a logical item
  boundary and `$03bc == 0` proves only `RING_EMPTY_ONLY`.
- Panel RX/autorespond, IRQ6, IACK, timer/counter behavior, and status-register
  behavior are still part of the research harness rather than clean device
  architecture.
- Physical panel semantics remain unresolved.

### 8.6 Current project status

Finished:

- The established V161 baseline reaches the post-scan idle state with visible
  `NO INST OR BANK FILES`.
- The historical `Z` parser artifact is fixed.
- The historical leading `F` parser artifact is fixed for the verified PATH B
  direct-text context.
- The panel parser is behind a byte-only receive boundary.
- The stateless helper extraction is complete.

Remaining independent work tracks:

1. Remove the temporary Path B provenance bridge.
   Determine whether byte-stream framing alone can safely recognize the
   verified direct-text frame, or whether this should wait for real DUART TXB
   callback placement.

2. Migrate from the hand-written DUART shadow to a real MAME
   `mc68681_device`/`scn2681_device`.
   This must be done in small, separately validated steps because timer,
   IRQ6/IACK, RX/autorespond, SRB/RHRB, and TX timing can all affect boot.

3. Continue reverse engineering of PATH A and physical panel semantics.
   Semantic binding requires panel-controller firmware, service documentation,
   or direct hardware capture. For periodic phase-3 sequences, hardware LOAD
   indicator timing must be measured independently before any semantic binding.

### 8.7 Lessons learned

- Parser artifacts can look like firmware text. The visible `Z` and leading
  `F` were both parser artifacts, not proven user-facing firmware strings.
- Byte-pattern searches are not enough. The trusted `0x66` evidence came from
  verified callers and transmit paths, not from byte value alone.
- Execution provenance is not protocol semantics. PC and ring provenance can
  justify a temporary bridge, but should not become the parser's long-term
  protocol grammar.
- Evidence levels matter. Historical observations, superseded interpretations,
  strong inferences, and proven behavior need to stay separated.
- Transport and parser boundaries matter. PATH A marker/payload transport,
  PATH B direct text transport, and visible display rendering are separate
  concerns and should remain architecturally separate.
