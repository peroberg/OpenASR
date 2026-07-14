# ASR-10 MAME Handoff — MC68302 IRQ6, DUART ISR/IMR, Channel B RX FIFO, and Current Channel A TxRDY Blocker

**Date:** 2026-07-12  
**Repository:** `mame-upstream`  
**Primary file:** `src/mame/ensoniq/asr10_boot.cpp`  
**Machine / harness:** `asr10booth`  
**Primary floppy image:** `floppies/asr10booth/V161.img`  
**Current focus:** Post-`LOADING SYSTEM` dispatcher idle, slot0 queued node `0x14f4/0x89a2`, MC68302 dedicated IRQ6, SCN2681-compatible DUART interrupt semantics, Channel B receive FIFO clear behavior, and the remaining Channel A transmitter-ready path.

---

## 1. Executive summary

The ASR-10 boot harness now reaches a well-defined post-load state:

- The floppy image is read successfully enough to reach `LOADING SYSTEM`.
- Slot3 naturally posts node `0x14f4` with payload `0x89a2` to slot0.
- Slot4 and slot5 execute and equalize normally.
- Slot4 and slot5 do **not** consume or promote the slot0 queue node.
- The dispatcher then parks in its idle loop with slot0 still holding `0x14f4` in `+0x10/+0x12`.

The earlier broad hypotheses have been narrowed substantially.

The current evidence shows:

1. No MC68302 internal level-4 source is `pending && enabled && !in_service` after slot5.
2. `GIMR=0x8040` places the MC68302 in dedicated interrupt mode and gives IRQ6 vector `0x56`.
3. Runtime vector `0x56` points to `0xF884BE`, a real DUART interrupt demultiplexer.
4. DUART register address `FC480B` has been separated correctly:
   - read side = ISR,
   - write side = IMR.
5. Firmware writes `IMR=0x2B`.
6. The diagnostic DUART model computes `ISR=0x21`, so `ISR & IMR = 0x21`.
7. An env-gated routing experiment connecting the computed DUART interrupt condition to MC68302 IRQ6 works:
   - level-6 IRQ is asserted,
   - CPU performs level-6 IACK,
   - dedicated vector `0x56` is returned,
   - handler `0xF884BE` runs,
   - ISR bit5 dispatches through `$00DE` to `0xFFB22A`.
8. An env-gated Channel B RX FIFO model proves that reading `FC4817` as RHRB can correctly pop one receive byte and clear Channel B receive status:
   - FIFO depth `1 -> 0`,
   - SRB `0x01 -> 0x00`,
   - RxRDYB `1 -> 0`,
   - ISR `0x21 -> 0x01`,
   - bit5 `1 -> 0`.
9. The next legitimate IRQ6 then selects ISR bit0 and dispatches through `$00E6` to `0xF88554`.
10. The current blocker is therefore no longer MC68302 vectoring, IRQ6 routing, or Channel B receive clear semantics. It is now the **Channel A transmitter-ready / TxRDY path through `0xF88554`**, including THRA, shift-register timing, TxRDY, TxEMT, and any indirect scheduler/output-drain effects.

No permanent default behavior should be changed yet. The routing and FIFO behavior remain env-gated experiments.

---

## 2. Scope and intent of this handoff

This handoff exists to allow a fresh Codex session to continue safely without re-deriving the current state.

The next session should:

- read this document first,
- inspect `src/mame/ensoniq/asr10_boot.cpp`,
- make no changes initially,
- restate the current evidence,
- then propose the smallest possible diagnostic patch for `0xF88554` and Channel A transmitter state.

The next session should **not**:

- force slot0,
- force scheduler state,
- manually inject vector `0x56`,
- directly clear ISR bit0,
- treat diagnostic computed ISR as unquestionably identical to real hardware,
- convert env-gated experimental behavior into default behavior without further proof.

---

## 3. Current boot and scheduler state

### 3.1 Natural boot progression

```text
disk activity
-> LOADING SYSTEM
-> dispatcher slots execute
-> slot3 posts node 0x14f4 / payload 0x89a2
-> slot4 executes and equalizes
-> slot5 executes and equalizes
-> dispatcher idle
```

### 3.2 Slot0 state at final idle

```text
slot0 +0x02/+0x03 = 0x0202
slot0 +0x10/+0x12 = 0x14f4 / 0x14f4
node 0x14f4 +0x02 = 0x89a2
```

The node is produced naturally and remains queued.

### 3.3 Slot4 result

Slot4 entry is around `0x0068A8`.

```asm
0068a8  move.b  #$7f,$1062.w
0068ae  move.w  #$ffff,$0dba.w
0068b4  move.b  $03bd.w,$0df1.w
0068ba  jsr     $8e80.w
0068be  jsr     $6844.w
0068c2  subq.b  #1,$0df0.w
0068c6  bne     $68fa
```

Observed:

- active slot = slot4 at `$242c`,
- slot0 still contains queued node `0x14f4`,
- no read of `$00d8`,
- no read of slot0 `+0x10/+0x12`,
- no reach of `0xF8F37A`,
- no slot0 promotion,
- exits through `0xF88124`,
- slot4 equalizes `0x0100 -> 0x0101`.

Conclusion:

> Slot4 is not the missing direct slot0 queue consumer.

### 3.4 Slot5 result

Slot5 entry is around `0x00779C`.

```asm
00779c  clr.w   $cdb4.w
0077a0  clr.w   $cdb2.w
0077a4  move.w  #$64,D0
0077a8  trap    #8
0077aa  clr.w   $cdb0.w
0077ae  jsr     $7cc4.l
...
0077dc  trap    #7
0077de  addq.w  #1,$cdb0.w
0077e2  cmpi.w  #$000b,$cdb0.w
0077e8  ble     $77ae
```

Observed:

- active slot = slot5 at `$2442`,
- slot0 still contains queued node `0x14f4`,
- no read of `$00d8`,
- no read of slot0 `+0x10/+0x12`,
- no reach of `0xF8F37A`,
- uses trap #8 and trap #7,
- exits through `0xF88124`,
- slot5 equalizes `0x0100 -> 0x0101`.

Conclusion:

> Slot5 is not the missing direct slot0 queue consumer.

---

## 4. `0xF8F37A` and payload `0x89A2`

Natural execution does not reach `0xF8F37A`.

A forced interpretation of the current state was suspicious:

```text
$8258 = 0x8140
payload = 0x89a2
signed lookup address:
  0xff8140 + signed(0x89a2) = 0xff0ae2
memory at 0xff0ae2 = 0x0000
```

Therefore:

- the immediate blocker occurs before `0xF8F37A`,
- the queued node is not promoted or consumed,
- payload `0x89A2` may not be intended for this exact dispatch path in the current state,
- or an earlier initialization / overlay / table patch may still be missing.

Do not force `0xF8F37A` as the next step.

---

## 5. MC68302 interrupt state

### 5.1 Register map

```text
FC6812 = GIMR
FC6814 = IPR
FC6816 = IMR
FC6818 = ISR
FC6850 = TMR1
FC6852 = TRR1
```

### 5.2 Post-slot5 snapshot

```text
GIMR = 0x8040
IPR  = 0x000b
IMR  = 0xe480
ISR  = 0x0080
```

### 5.3 No active internal level-4 source

Decoded result:

```text
active_sources = none
```

No source was simultaneously:

```text
pending && enabled && !in_service
```

This matches:

```text
iack_after_slot5 = 0
```

Correct conclusion:

> No natural MC68302 internal level-4 INRQ IACK should occur after slot5 in the observed state.

This does not exclude dedicated external IRQ1/IRQ6/IRQ7.

### 5.4 GIMR decode

`GIMR=0x8040`:

```text
MOD = 1
dedicated interrupt mode
V7-V5 = 2
vector base = 0x40
IV7 = 0
IV6 = 0
IV1 = 0
```

Dedicated vectors:

```text
IRQ1 -> 0x51
IRQ6 -> 0x56
IRQ7 -> 0x57
```

### 5.5 Runtime dedicated vector handlers

```text
vector 0x51 -> 0xFF87CE
  starts with: jmp $0000A48C.l
  real handler/trampoline

vector 0x56 -> 0xF884BE
  starts with: movem.l D0-D3/A0-A2/A5,-(A7)
  real handler

vector 0x57 -> 0xF882DA
  unused-vector stub
```

IRQ6 is the important path.

---

## 6. DUART / IRQ6 classification

### 6.1 IRQ6 handler

```asm
f884be  movem.l D0-D3/A0-A2/A5,-(A7)
f884c2  move.b  $fffc480b.l,D0
f884c8  move.b  D0,D1
f884ca  btst    #5,D0
f884ce  beq     f884d6
f884d0  movea.l $00de.w,A0
f884d4  jmp     (A0)

f884d6  and.b   #$06,D1
f884da  beq     f884e2
f884dc  movea.l $00e2.w,A0
f884e0  jmp     (A0)

f884e2  btst    #0,D0
f884e6  beq     f884ee
f884e8  movea.l $00e6.w,A0
f884ec  jmp     (A0)

f884ee  btst    #3,D0
f884f2  beq     f884f8
f884f4  jmp     $8638.w
```

Firmware-level demultiplexing:

```text
ISR bit5      -> long pointer at $00de
ISR bits1/2   -> long pointer at $00e2
ISR bit0      -> long pointer at $00e6
ISR bit3      -> $8638
no known bit  -> error/trap path
```

This strongly classifies IRQ6 as the DUART interrupt path.

### 6.2 FC480B read/write semantics

```text
read  FC480B = DUART ISR
write FC480B = DUART IMR
```

Firmware write:

```text
pc = 0xF88448
FC480B <- 0x2B
```

Interpretation:

```text
DUART IMR = 0x2B
```

### 6.3 IMR bits

```text
0x2B = 0010 1011b
```

Enabled:

```text
bit5
bit3
bit1
bit0
```

This matches the classes tested by `0xF884BE`.

---

## 7. Separated DUART ISR/IMR result

After slot3, slot4, slot5, and final idle:

```text
computed ISR = 0x21
IMR          = 0x2B
ISR & IMR    = 0x21
DUART_INTRN  = active
actual IRQ6 line = 0
no IRQ6 IACK
```

Bit reasons:

```text
ISR bit5 <- SRB=0x01 / Channel B panel RX/status shadow
ISR bit0 <- SRA=0xEE / Channel A TX-ready shadow
```

Evidence boundary:

- ISR and IMR are now separated in the harness.
- The computed ISR is no longer copied from IMR.
- The exact fidelity of `computed ISR=0x21` to real hardware remains an experimental model assumption.

---

## 8. Env-gated IRQ6 routing experiment

### 8.1 Flag

```text
ASR10_EXPERIMENT_ROUTE_DUART_INTRN_TO_IRQ6=1
```

Behavior:

```text
logical_duart_irq_condition = (computed_isr & duart_imr) != 0
MC68302 IRQ6 asserted iff logical_duart_irq_condition is true
```

Properties:

- default-off,
- level-driven,
- no pulse injection,
- no manual vector forcing,
- no slot0 modification,
- no scheduler forcing.

### 8.2 Result

```text
DUART ISR = 0x21
DUART IMR = 0x2B
ISR & IMR = 0x21
IRQ6 asserted
CPU performs level-6 IACK
GIMR dedicated vector = 0x56
vector 0x56 -> 0xF884BE
FC480B read returns 0x21
bit5 selected
$00DE = 0xFFB22A
```

Proven experiment chain:

```text
computed DUART interrupt condition
-> IRQ6
-> level-6 IACK
-> dedicated vector 0x56
-> f884be
-> DUART ISR read
-> bit5 demux
-> ffb22a
```

---

## 9. Channel B receive path

### 9.1 Runtime callback pointers

```text
$00DE = 0xFFB22A
$00E2 = 0xF88968
$00E6 = 0xF88554
```

Observed character:

```text
$00DE / 0xFFB22A
  reads FC4813
  masks 0x50
  later reads FC4817
  jumps through $03C0

$00E2 / 0xF88968
  service path with trap #3, trap #9, $00DA, and RTE epilogue

$00E6 / 0xF88554
  resume/service path via $86F8/$86FC
  trap #4 / node-dispatch-like behavior
```

### 9.2 Candidate lane/register mapping

```text
FC4813 = Channel B status read candidate / SRB
FC4817 = Channel B RX/TX data candidate / RHRB/THRB
```

This mapping is strongly supported by firmware behavior but remains a harness-level mapping rather than complete board-level proof.

### 9.3 Initial routing-only failure

Before FIFO semantics:

```text
f884be -> bit5 -> ffb22a
ffb22a reads FC4813 / FC4817
computed ISR remains 0x21
bit5 remains active
IRQ6 immediately retriggers
```

Storm guard stopped after:

```text
65 identical IRQ6 IACKs
reason = identical_irq6_iack_without_progress
```

No artificial clear or mask was performed.

---

## 10. Env-gated Channel B RX FIFO experiment

### 10.1 Flag

```text
ASR10_EXPERIMENT_DUART_CHB_RX_FIFO=1
```

Behavior:

- diagnostic Channel B receive FIFO/valid state,
- RHRB read pops one byte,
- receive status recomputed from FIFO depth,
- ISR bit5 recomputed from receive state,
- no direct special-case ISR clear.

Default remains off.

### 10.2 Successful run command

```bash
SDL_VIDEODRIVER=dummy ASR10_DIAG_IRQ6_DUART_PANEL=1 ASR10_EXPERIMENT_ROUTE_DUART_INTRN_TO_IRQ6=1 ASR10_EXPERIMENT_DUART_CHB_RX_FIFO=1 ./mame asr10booth   -flop floppies/asr10booth/V161.img   -bench 45   -skip_gameinfo   -log
```

### 10.3 Key proof

```text
post-init enqueue seeds Channel B RX FIFO with byte 0x71

IRQ6 IACK
-> vector 0x56
-> f884be
-> read ISR 0x21
-> bit5
-> $00DE = ffb22a

ffb22a reads FC4813
-> SRB = 0x01
-> FIFO depth = 1

ffb242 reads FC4817
-> returns 0x71
-> pops FIFO
```

State transition:

```text
FIFO depth 1 -> 0
SRB        01 -> 00
RxRDYB      1 -> 0
ISR        21 -> 01
bit5        1 -> 0
```

Interpretation:

```text
Channel B byte pending
-> ISR bit5
-> IRQ6
-> f884be
-> ffb22a
-> SRB/RHRB read
-> FIFO pop
-> RxRDYB clear
-> ISR bit5 clear
```

The previous bit5 storm is diagnostically resolved.

---

## 11. Current blocker: Channel A TxRDY / ISR bit0 / `0xF88554`

After Channel B clears:

```text
ISR 0x21 -> 0x01
```

Since:

```text
IMR = 0x2B
ISR & IMR = 0x01
```

IRQ6 remains legitimately active.

Next IRQ6:

```text
f884be reads ISR=0x01
bit5 clear
bits1/2 clear
bit0 set
jump via $00E6
$00E6 = 0xF88554
```

Final state still:

```text
slot0 +0x10/+0x12 = 0x14f4 / 0x14f4
no slot wake or promotion
```

Current blocker:

> Channel A transmitter-ready state and the bit0 callback at `0xF88554`.

---

## 12. TxRDY modeling requirements

Do not model this as simply “write THRA, clear bit0 forever.”

Likely lifecycle:

```text
THRA empty
-> TxRDY set
-> CPU writes THRA
-> THRA occupied
-> TxRDY clears temporarily
-> byte moves to shift register
-> THRA becomes empty
-> TxRDY sets again
-> next byte may be requested
```

Distinguish:

```text
TxRDY = transmitter can accept another byte
TxEMT = holding register and shift register both empty
```

Repeated bit0 IRQs can be valid if byte or buffer progress occurs.

A true storm means repeated identical IRQ6/IACK without change in:

- THRA state,
- shift-register state,
- current byte,
- software buffer position,
- ISR cause,
- callback progress.

---

## 13. Next task for a fresh Codex session

Start with:

```text
Read docs/asr10/asr10-mame-irq6-duart-handoff-2026-07-12.md
and src/mame/ensoniq/asr10_boot.cpp.

Do not change anything yet.

Explain:
- the proven IRQ6/DUART chain,
- which behavior is env-gated,
- why Channel B bit5 is no longer the blocker,
- why Channel A bit0 / TxRDY / f88554 is the sole narrow target,
- the smallest diagnostic patch you would make next.
```

Then use this narrow task:

```text
Analyze IRQ6 ISR bit0 / Channel A TxRDY path through $00E6=0xF88554.

Background:
- Env-gated experiments:
  ASR10_EXPERIMENT_ROUTE_DUART_INTRN_TO_IRQ6=1
  ASR10_EXPERIMENT_DUART_CHB_RX_FIFO=1
- IRQ6/vector 0x56/f884be works.
- Initial ISR=0x21.
- bit5 dispatches through $00DE=0xFFB22A.
- FC4817/RHRB pop clears Channel B RX:
  FIFO 1 -> 0
  SRB 0x01 -> 0x00
  RxRDYB 1 -> 0
  ISR 0x21 -> 0x01
- The next valid IRQ6 selects:
  bit0 -> $00E6=0xF88554.
- Slot0 still contains:
  +0x02/+0x03 = 0x0202
  +0x10/+0x12 = 0x14f4/0x14f4.

Task:
1. Disassemble 0xF88554 completely and follow all control flow to the common IRQ epilogue/RTE.
2. Log each f884be entry with ISR=0x01:
   - FC480B read value
   - selected bit0 branch
   - $00E6 target.
3. At f88554 entry log:
   - D0-D7/A0-A6/SR/SP
   - ISR/IMR/ISR&IMR
   - SRA/SRB
   - Channel A TxRDY and TxEMT
   - THRA state
   - transmit shift-register state
   - $03BC/$03C5
   - $00D8
   - slot0 +0x02/+0x03/+0x0C/+0x10/+0x12.
4. Identify every FC48xx access in the path.
5. If the path writes Channel A THRA:
   - log byte value
   - log source buffer and position
   - mark THRA occupied
   - recompute TxRDY and TxEMT from state
   - recompute ISR bit0
   - do not directly clear ISR bit0.
6. Add env-gated diagnostic transmitter state if needed:
   - THRA valid/occupied
   - shift-register valid/occupied
   - current byte
   - pending completion time
   - TxRDY
   - TxEMT.
7. Model transmitter progress only behind a new default-off env flag if behavior remains synthetic.
8. After THRA write, verify whether:
   - TxRDY temporarily clears
   - ISR bit0 goes 1 -> 0
   - IRQ6 deasserts if no other masked cause exists.
9. When emulated transmitter time advances:
   - move THRA data to shift register as appropriate
   - allow TxRDY to return naturally
   - log whether firmware supplies the next byte.
10. Classify repeated bit0 IRQs:
    - progress if byte/buffer/transmitter state changes
    - storm if identical IACKs recur without state change.
11. Follow whether f88554 or its callees reach or modify:
    - $03BC
    - $03C5
    - 0xF89A80..0xF89AC8
    - $00D8
    - slot0 pending bit1
    - slot0 +0x0C/+0x10/+0x12
    - trap #4/#6/#9
    - 0xF880E0
    - 0xF97F10
    - 0xF8F37A.
12. Keep all new behavior default-off and env-gated.
13. Do not force slot0, scheduler, vector, IRQ completion, or direct ISR clear.
14. Run:
    git diff --check
    make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

---

## 14. Current env flags

### `ASR10_DIAG_IRQ6_DUART_PANEL`

```text
Type: diagnostic
Default: off
Behavior change: none intended
Purpose:
- FC48xx DUART/panel snapshots
- ISR/IMR/status logging
- IRQ line state
- handler markers
- callback pointers
- Channel B RX diagnostics
```

### `ASR10_EXPERIMENT_ROUTE_DUART_INTRN_TO_IRQ6`

```text
Type: experiment
Default: off
Behavior change: yes
Purpose:
- route computed DUART interrupt condition to MC68302 IRQ6
- level-driven
- no manual vector forcing
- normal dedicated-mode IACK selects vector 0x56
```

### `ASR10_EXPERIMENT_DUART_CHB_RX_FIFO`

```text
Type: experiment
Default: off
Behavior change: yes
Purpose:
- diagnostic Channel B RX FIFO/valid state
- FC4817/RHRB read pops one byte
- recompute SRB/RxRDYB/FFULLB/ISR bit5
- no direct ISR clear
```

### `ASR10_DIAG_MC68302_EXTERNAL_IRQ`

```text
Type: diagnostic
Default: off
Behavior change: none intended
Purpose:
- decode GIMR dedicated mode
- log IRQ1/IRQ6/IRQ7 vectors
- log IACK levels 1/6/7
- log PB8-PB11 candidate transitions
- separate IPR bit0 ERR reporting
```

Inspect the source for additional related flags before adding new ones.

---

## 15. Build and validation commands

Passed repeatedly:

```bash
git diff --check
```

```bash
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

Successful runtime:

```bash
SDL_VIDEODRIVER=dummy ASR10_DIAG_IRQ6_DUART_PANEL=1 ASR10_EXPERIMENT_ROUTE_DUART_INTRN_TO_IRQ6=1 ASR10_EXPERIMENT_DUART_CHB_RX_FIFO=1 ./mame asr10booth   -flop floppies/asr10booth/V161.img   -bench 45   -skip_gameinfo   -log
```

The run completed the full 45-second bench interval after the FIFO fix.

---

## 16. Evidence classification

### Proven by natural execution

- FDC reads enough data to reach `LOADING SYSTEM`.
- Slot3 posts node `0x14F4/0x89A2`.
- Slot4 and slot5 run but do not consume slot0.
- Dispatcher idles with slot0 `+0x10/+0x12=0x14F4`.
- No active MC68302 internal `pending && enabled && !in_service` source exists after slot5.
- No natural IRQ6 IACK occurs without the routing experiment.
- Runtime vector `0x56` points to `0xF884BE`.
- Firmware writes `0x2B` to FC480B write-side IMR.
- `0xF884BE` reads FC480B read-side ISR and demultiplexes bits.

### Proven inside env-gated experiments

- Routing computed DUART interrupt condition to IRQ6 causes:
  - IRQ6 assertion,
  - level-6 IACK,
  - vector `0x56`,
  - `0xF884BE` execution.
- ISR `0x21` causes bit5 dispatch through `$00DE=0xFFB22A`.
- Diagnostic Channel B RX FIFO byte `0x71` is consumed through FC4817.
- FIFO pop causes:
  - depth `1 -> 0`,
  - SRB `0x01 -> 0x00`,
  - RxRDYB `1 -> 0`,
  - ISR `0x21 -> 0x01`.
- Next IRQ6 selects bit0 through `$00E6=0xF88554`.

### Strong model assumptions

- Computed ISR bit5 accurately represents Channel B receive-ready state.
- Computed ISR bit0 accurately represents Channel A TxRDY state.
- FC4813 is SRB in the current lane mapping.
- FC4817 is RHRB/THRB in the current lane mapping.
- The diagnostic RX FIFO semantics are representative of real hardware behavior.

### Not yet proven

- Real hardware has ISR exactly `0x21` at this point.
- The Channel A bit0 path directly causes slot0 promotion.
- `0xF88554` writes THRA.
- Correct TxRDY timing changes `$03BC/$03C5`.
- Output-drain reaches `0xF89A80..0xF89AC8`.
- Bit0 service reaches `0xF97F10`, `0xF8F37A`, or consumes node `0x14F4`.
- Experimental FIFO and routing behavior should become permanent default emulation.
- Exact physical routing of SCN2681 INTRN to MC68302 IRQ6.

---

## 17. Hardware context

Relevant board parts:

```text
U20 = SCN2681AC1N40 DUART
U28 = MC68302FC16C MPU
U34 = NEC D72069GF FDC
U41 = Ensoniq ES5701 / SuperGLU
U43 = Ensoniq ES5510/ESP
```

Current board interpretation:

- SuperGLU is primarily an OTTO/ESP/audio bus and clock interface.
- No strong evidence shows FDC or DUART routed through SuperGLU.
- DUART and FDC appear in separate peripheral/glue regions.
- IRQ6 handler behavior strongly associates IRQ6 with DUART/panel.
- Physical confirmation would require continuity tracing or underside PCB inspection.

Do not block software work on hardware disassembly.

---

## 18. FDC and IRQ1 status

FDC is no longer the primary blocker because:

- substantial disk reads complete,
- `LOADING SYSTEM` is reached,
- post-load slots execute,
- node `0x14F4/0x89A2` is posted.

FDC is not completely excluded from later completion/status behavior, but it is lower priority.

IRQ1:

```text
vector 0x51 -> 0xFF87CE -> jmp 0x0000A48C
```

The target appears more loader/peripheral-service oriented. No natural IRQ1 transition or IACK was observed in the relevant run.

IRQ1 remains secondary.

---

## 19. Known pitfalls

1. Do not conflate FC480B ISR and IMR.
2. Do not treat IMR `0x2B` as active status.
3. Do not directly clear ISR bits.
4. Do not model TxRDY as a permanent one-shot clear.
5. Do not confuse TxRDY with TxEMT.
6. Do not force vector `0x56`.
7. Do not force slot0, `$00D8`, or scheduler state.
8. Do not call repeated IRQ6 a storm unless no transmitter/buffer progress occurs.
9. Code in `0xF884BE..0xF88560` may also execute during init; confirm explicit level-6 IACK and handler entry.
10. Do not promote env-gated behavior to default prematurely.
11. Do not return to Timer 1 as primary without new evidence.
12. PB candidate transitions are not automatically proven physical pin transitions.

---

## 20. Repository preservation

Before ending the current session:

```bash
git diff --check
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

Save diff externally:

```bash
git diff > /tmp/asr10-irq6-duart-diagnostics.patch
shasum -a 256 /tmp/asr10-irq6-duart-diagnostics.patch
```

Record state:

```bash
git rev-parse HEAD
git diff --stat
git diff --numstat
git status --short
```

Stage only intended files explicitly.

Suggested commit title:

```text
asr10: add gated DUART IRQ6 and RX FIFO diagnostics
```

Suggested body:

```text
- separate DUART ISR and IMR semantics
- add env-gated DUART-to-IRQ6 routing experiment
- add env-gated Channel B RX FIFO/RHRB pop experiment
- verify IRQ6 vector 0x56 and f884be demux path
- verify RHRB pop clears ISR bit5 from 0x21 to 0x01
- leave default behavior unchanged
```

---

## 21. Suggested evidence extraction

```bash
mkdir -p docs/asr10/evidence/irq6-duart-2026-07-12

grep -E 'channel_b_rx_enqueue|channel_b_rx_rhrb_pop|event=iack|pc=f884be|pc=ffb22a|pc=ffb242|pc=f88554|selected_branch_from_fc480b|ISR=21|ISR=01|slot0_10=14f4' error.log > docs/asr10/evidence/irq6-duart-2026-07-12/key-events.log
```

Commit the compact evidence log rather than the full `error.log`.

---

## 22. Compact current status

```text
Natural boot:
- reaches LOADING SYSTEM
- slot3 posts node 14f4/89a2
- slot4/slot5 do not consume it
- dispatcher idles
- slot0 +10/+12 remains 14f4/14f4

MC68302:
- GIMR=8040 dedicated mode
- IRQ6 vector = 0x56
- vector 0x56 -> f884be
- no natural internal pending+enabled source after slot5

DUART:
- FC480B write = IMR
- firmware writes IMR=0x2b
- FC480B read = ISR
- diagnostic computed ISR=0x21

Routing experiment:
- DUART condition routed to IRQ6
- level-6 IACK works
- vector 0x56 works
- f884be works

Channel B:
- bit5 -> $00de=ffb22a
- FC4817/RHRB pop works in env-gated FIFO model
- FIFO 1->0
- SRB 01->00
- ISR 21->01
- bit5 clears

Current blocker:
- ISR bit0 remains
- next IRQ6 goes via $00e6=0xF88554
- Channel A TxRDY/THRA/shift-register/TxEMT behavior is not yet modeled or understood
- slot0 still not consumed
```

---

## 23. Final instruction to the next session

Do not broaden the investigation.

The next session has one target:

> Determine exactly what `0xF88554` does for Channel A transmitter-ready service, and model only the minimum env-gated THRA/shift-register/TxRDY/TxEMT state needed to observe correct IRQ6 progress.

A successful next milestone is one of:

```text
A. f88554 writes THRA and ISR bit0 temporarily goes 1 -> 0.
B. transmitter state advances and TxRDY reasserts with real byte progress.
C. output buffer position changes.
D. $03BC/$03C5 changes.
E. slot0 pending/service state changes.
F. the path reaches f89a80..f89ac8, f880e0, f97f10, or f8f37a.
```

Always preserve the distinction between:

- natural execution,
- env-gated diagnostic behavior,
- model assumptions,
- permanent emulator behavior.

---

## 24. One-line handoff

**The MC68302 IRQ6/vector `0x56` path and Channel B RHRB-pop semantics now work in env-gated experiments; the only narrow current blocker is Channel A ISR bit0 / TxRDY service through `0xF88554`, including THRA, shift-register timing, TxRDY/TxEMT, and any resulting scheduler/output-drain re-entry.**
