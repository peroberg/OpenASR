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