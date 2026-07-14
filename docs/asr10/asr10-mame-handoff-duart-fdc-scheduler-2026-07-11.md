# ASR-10 MAME boot handoff — DUART/FDC/scheduler status

Date: 2026-07-11  
Scope: ASR-10 boot harness in `mame-upstream`, mainly `src/mame/ensoniq/asr10_boot.cpp`.  
Focus: DUART/panel activity, FDC activity, post-load scheduler/IRQ progress, and the current main blocker.

---

## 1. Executive summary

The boot now reaches a much better-understood post-load state than earlier. The FDC path is no longer a simple “disk does not load” failure: the system reads a substantial amount of data from the V161 floppy image, reaches `LOADING SYSTEM`, completes a sequence of initial dispatcher slots, and then parks in the dispatcher idle loop. The important remaining blocker is not a raw FDC failure, not a panel key wait during normal boot, and not a missing generic IRQ line by itself.

The current evidence points to a scheduler/event-model gap after a natural event node is posted to slot0. A node at `0x14f4` with payload `0x89a2` is enqueued into slot0 through the natural `f8f2f0 trap #9` path, but it is not consumed. Slot0 remains equalized unless force-woken; even when force-woken, it resumes a parked frame at `f87f66` and still does not read node `0x14f4`. Therefore, the next major task is to identify the queue consumer/promoter for slot0 `+0x10/+0x12` and why it is not reached.

DUART/panel diagnostics were essential because a raw IRQ experiment led to `ERROR 139 - REBOOT ?`. Service manual evidence confirms `139 = unused vector`, matching vector-table diagnostics: autovector `0x19` points to the unused-vector stub at `f882da`. That error is therefore not primarily an FDC error and not a panel problem. It is proof that raw level-1 autovector delivery is the wrong interrupt class.

Vectors `0x4e` and `0x4f` are valid handlers and avoid ERROR 139, but they are short latch/ack handlers only. They set `$0c3a/$0c36` or `$0c3b/$0c37` and write `FC6818=4000/8000`; they do not wake slot0 or consume the queued node.

---

## 2. Known memory map / hardware-relevant areas

Current harness mapping of the relevant regions:

```cpp
map(0x000000, 0x0fffff).rw(low_rom_or_lowmem_r, lowmem_w);
map(0x100000, 0x1fffff).ram();
map(0xf00000, 0xf7ffff).ram();
map(0xf80000, 0xfbffff).rw(high_alias_r, high_alias_w);
map(0xfc0000, 0xfc3fff).ram();
map(0xfc4000, 0xfc4003).rw(upd72069_fdc_r/w);
map(0xfc4004, 0xfc47ff).ram();
map(0xfc4800, 0xfc481f).rw(duart_panel_asr_candidate_r/w);
map(0xfc4820, 0xfc4fff).ram();
map(0xfc5000, 0xfc501f).rw(scsi_asr_candidate_r/w);
map(0xfc5020, 0xfc67ff).ram();
map(0xfc6800, 0xfc68ff).rw(m68302_internal_r/w);
map(0xfc6900, 0xffffff).ram();
```

Important regions in this investigation:

- `FC4000..FC4003`: NEC D72069 FDC.
- `FC4800..FC481F`: DUART/panel candidate registers.
- `FC6814/FC6816/FC6818`: MC68302/FC68 interrupt pending/in-service/control candidates.
- `FC6828/FC6829`: port/status area; bit3 LRCLK-style handshake observed.
- `FC6884/FC6894`: internal-register writes observed around service paths.
- `$00c0`: firmware error code storage; `ff8b` means error number `0x8b = 139`.
- `$00d8`: stable pointer to slot0/system-output slot, currently `0x23d4`.
- `$0c36/$0c37/$0c3a/$0c3b`: latch bytes written by vectors `0x4e/0x4f`.
- Slot0: `0x23d4`.
- Slot0 queue head/tail: slot0 `+0x10/+0x12` = `0x23e4/0x23e6`.
- Current event node: `0x14f4`, payload `0x89a2`.

---

## 3. FDC progress and current interpretation

### 3.1 What is proven about FDC loading

The V161 floppy load is not simply dead. Earlier diagnostics show:

- `LOADING SYSTEM` is reached.
- Approximately 20 READ DATA transactions are observed.
- Cumulative sectors read reached about 181.
- Total FIFO reads reached about 92,672 bytes.
- The historical suspicion “it stops at cylinder 4 / only reads 144 sectors” is no longer supported.
- The system gets through enough load activity to enter the post-load dispatcher/slot sequence.

Therefore:

```text
AVFÖRT:
- primary blocker = no FDC data read
- primary blocker = hard stop at cylinder 4
- primary blocker = only 144 sectors read
```

### 3.2 FDC status context observed near ERROR 139

When raw IRQ/autovector produced `ERROR 139`, the error context also contained FDC state such as:

```text
last_fdc_txn=20
last_aux=0e
last_cmd46_result=40,80,00,00,00,06,02
last_ST0_abnormal=1
last_ST1_end_of_cylinder=1
last_ST0_not_ready=0
last_ST1_data_error=0
last_ST1_no_data=0
```

This initially looked like it might be the direct source of the displayed error. That interpretation was later corrected.

The direct source of `ERROR 139` is the unused-vector stub, not FDC status conversion. However, the FDC status remains useful context because it describes the state the firmware was in when the wrong interrupt path was forced.

### 3.3 Current FDC classification

Current status:

```text
FDC-load path: substantially working / not primary proven blocker.
FDC final status details: still worth keeping in diagnostics.
Direct cause of ERROR 139: not FDC; it is unused vector.
```

FDC should not be the next primary area unless later scheduler progress shows a reproducible disk/file operation error independent of forced wrong vectors.

---

## 4. DUART / panel activity

### 4.1 Panel input loop disassembly

The panel input wait/read routine was identified around `f89cca`:

```asm
f89cca: move.l  #$1000,D1
f89cd0: move.w  #$1f40,D0
f89cd4: dbra    D0,f89cd4
f89cd8: btst    #0,$fffc4813
f89ce0: bne     f89ce8
f89ce2: subq.w  #1,D1
f89ce4: bcc     f89cd0
f89ce6: rts
f89ce8: moveq   #0,D1
f89cea: move.b  $fffc4817,D1
f89cf0: andi    #$fe,CCR
f89cf4: rts
```

Interpretation:

- `FC4813` bit0 is RX-ready / input-ready for this path.
- `FC4817` is channel B RX/TX buffer candidate.
- If `FC4813` bit0 is always forced ready and `FC4817` returns raw `00`, the firmware loops on a bogus input byte.

### 4.2 Mapping table for raw panel bytes

The firmware maps raw panel bytes through table `fff82484`. The diagnostic dump confirmed:

```text
raw=00 -> mapped=3a
raw=03 -> mapped=40
raw=0c -> mapped=30
raw=0d -> mapped=31
raw=0f -> mapped=17
raw=12 -> mapped=32
raw=13 -> mapped=33
raw=15 -> mapped=16
raw=18 -> mapped=34
raw=19 -> mapped=35
raw=1e -> mapped=36
raw=1f -> mapped=37
raw=21 -> mapped=23
raw=24 -> mapped=38
raw=25 -> mapped=39
raw=3b -> mapped=23
raw=3f -> mapped=3a
raw=8f -> mapped=31
raw=91 -> mapped=33
raw=93 -> mapped=35
raw=95 -> mapped=37
raw=97 -> mapped=39
```

`raw=00` maps to `0x3a`, which is not one of the expected special confirmations in the observed prompt path.

### 4.3 Command history and ERROR 139 prompt

Panel command history showed the system writing:

```text
ERROR 139 - REBOOT ?
```

This was later confirmed with a direct panel text marker:

```text
ASR10PANEL text="ERROR 139 - REBOOT ?"
```

This panel loop is not a normal boot user-input requirement. It is the error confirmation prompt after an unused vector.

### 4.4 Correct interpretation of panel involvement

```text
BEVISAT:
- DUART/panel output works well enough to display error text.
- The f89cd4/f89cd8 loop is panel input wait after an error prompt.
- In the raw autovector experiment, panel input is not the primary blocker.
- The prompt exists because the firmware has already entered ERROR 139.
```

Therefore:

```text
AVFÖRT:
- normal boot is blocked because it expects a physical key press
- f89cd4 is the primary hardware boot blocker
- panel auto-response is the main next fix
```

---

## 5. ERROR 139 / vector investigation

### 5.1 Service manual meaning

Service manual error table says:

```text
138 spurious interrupt
139 unused vector
```

This exactly matches the runtime vector diagnostics.

### 5.2 Direct ROM source of ERROR 139

Disassembly found:

```asm
f882da: moveq #-$75,D0   ; D0 = ffffff8b
f882dc: bra   f88280
f88284: move.w D0,$00c0.w
```

`0x8b = 139`, so this writes the unused-vector error code to `$00c0`.

Runtime context confirmed:

```text
lowmem_00c0=ff8b
error_number=8b
ASR10PANEL text="ERROR 139 - REBOOT ?"
```

### 5.3 Vector table findings

Runtime vector table dump after slot5 showed:

#### Vector `0x19`

```text
vector 0x19
vector table address 000064
handler f882da
opbytes 708b60a242380b7f
unused_vector_stub=1
error139_stub=1
```

This is level-1 autovector and is definitely wrong for the post-init source. It creates ERROR 139.

#### Vector `0x4e`

```text
vector 0x4e
vector table address 000138
handler f88f06
opbytes 4a380c3a660c11fc
unused/error = false
```

Disassembly:

```asm
f88f06: tst.b  $0c3a.w
f88f0a: bne    f88f18
f88f0c: move.b #$0c,$0c3a.w
f88f12: move.b #$01,$0c36.w
f88f18: move.w #$4000,$fc6818.l
f88f20: rte
```

Runtime override to `0x4e`:

- no ERROR 139
- writes `$0c3a=0c`, `$0c36=01`
- writes `FC6818=4000`
- ends back in idle; no slot0 progress

#### Vector `0x4f`

```text
vector 0x4f
vector table address 00013c
handler f88f22
opbytes 4a380c3b660c11fc
unused/error = false
```

Disassembly:

```asm
f88f22: tst.b  $0c3b.w
f88f26: bne    f88f34
f88f28: move.b #$0c,$0c3b.w
f88f2e: move.b #$01,$0c37.w
f88f34: move.w #$8000,$fc6818.l
f88f3c: rte
```

Runtime override to `0x4f`:

- no ERROR 139
- writes `$0c3b=0c`, `$0c37=01`
- writes `FC6818=8000`
- ends back in idle; no slot0 progress

### 5.4 Vector conclusions

```text
BEVISAT:
- 0x19/autovector is wrong and leads to unused-vector error 139.
- 0x4e and 0x4f are valid handlers.
- 0x4e/0x4f are short latch/ack handlers only.
- They do not consume slot0 queue and do not wake slot0 to continue boot.
```

Current interpretation:

```text
Raw IRQ experiment proved that an event/interrupt can wake the CPU out of dispatcher idle,
but raw level-1 autovector is semantically wrong.
0x4e/0x4f are real but insufficient by themselves.
```

---

## 6. Dispatcher and slot baseline

### 6.1 Dispatcher scan logic

Important dispatcher fragment:

```asm
f87f96 move.b $2(A2),D0
f87f9a move.b $3(A2),D1
f87f9e eor.b D1,D0
f87fa0 beq f87fc2
f87fa2 move.l $6(A2),-(A7)
f87fa6 move.w $a(A2),-(A7)
f87fb4 movea.w $c(A2),A5
f87fb8 clr.w $c(A2)
f87fbc move.w A2,$0b6a.w
f87fc0 rte
```

Pending rule:

```text
slot pending iff byte +2 != byte +3
```

Equalized examples:

```text
0202 = not pending
8080 = not pending
0101 = not pending
```

Pending example:

```text
0001 = pending
```

### 6.2 Initial slot sequence

Runtime pointers:

```text
$00c6=23d4
$00c8=2458
$00ce=1ccc
$00d0=1ce4
```

Six slots stride `0x16`:

```text
slot0 0023d4 table 1ccc raw 1e24 a3c2 => +0e=1e24, dispatch ffa3c2
slot1 0023ea table 1cd0 raw 1e88 c85a => +0e=1e88, dispatch ffc85a
slot2 002400 table 1cd4 raw 1f14 7308 => +0e=1f14, dispatch 00007308
slot3 002416 table 1cd8 raw 1fb4 90f4 => +0e=1fb4, dispatch ff90f4
slot4 00242c table 1cdc raw 1fdc 68a8 => +0e=1fdc, dispatch 000068a8
slot5 002442 table 1ce0 raw 2054 779c => +0e=2054, dispatch 0000779c
```

Observed dispatcher RTE history:

```text
rte_count=1 slot0 frame_pc=ffa3c2 after=ffa3c6/equalize f89ac2
rte_count=2 slot1 frame_pc=ffc85a after=ffc85e/equalize f88100
rte_count=3 slot2 frame_pc=007308 after=007308/equalize f8ce3a
rte_count=4 slot3 frame_pc=ff90f4 after=ff90f4/equalize f88100
rte_count=5 slot4 frame_pc=0068a8/equalize f88124
rte_count=6 slot5 frame_pc=00779c after=0077a0/equalize f88124
```

After slot5, normal final idle state:

```text
slot_pending_words=0202,8080,8080,8080,0101,0101
slot0 +10/+12 = 14f4/14f4
```

Dispatcher keeps scanning, but slot0 is equalized (`0202`), so its queue is not inspected.

---

## 7. Slot0 node `0x14f4` and trap #9 enqueue path

### 7.1 Node structure

At natural posting around `f8f2f0`:

```text
slot0 +02/+03 = 0202
slot0 +10/+12 = 0000/0000
node 14f4:
  +00 = 0000
  +02 = 89a2
  +04 = 0000
  +06 = 0000
```

At final idle:

```text
slot0 +02/+03 = 0202
slot0 +10/+12 = 14f4/14f4
node 14f4:
  +00 = 0000
  +02 = 89a2
  +04 = 0000
  +06 = 0000
```

Classification:

```text
0x14f4 is best classified as a queued continuation/event/message node.
+00 likely next pointer.
+02 is payload / dispatch key = 0x89a2.
+04/+06 unused in the observed path.
```

### 7.2 Natural lifecycle

Observed sequence:

```asm
f88088: move.w  (A5),$0b6c.w   ; pop free-list next
f8808c: clr.w   (A5)           ; clear node +00

f8f2e2: trap #2                ; allocates A5=0014f4
f8f2e6: move.w  #$89a2,2(A5)   ; node payload
f8f2ec: movea.w $00d8.w,A1     ; A1=0023d4 slot0
f8f2f0: trap #9
```

Trap #9 routine:

```asm
f88138  ori     #$0700,SR
f8813c  move.w  $0012(A1),D0
f88140  beq     f8814e
f88142  movea.w D0,A0
f88144  move.w  A5,(A0)
f88146  move.w  A5,$0012(A1)
f8814a  moveq   #$07,D0
f8814c  bra     f8816c
f8814e  moveq   #$07,D0
f88150  btst    D0,$0002(A1)
f88154  beq     f88164
f88156  cmpi.w  #$0000,$000c(A1)
f8815c  bne     f88164
f8815e  move.w  A5,$000c(A1)
f88162  bra     f8816c
f88164  move.w  A5,$0010(A1)
f88168  move.w  A5,$0012(A1)
f8816c  clr.w   (A5)
f8816e  bclr    D0,$0002(A1)
f88172  rte
```

### 7.3 Key mismatch

At `f8f2ec`:

```text
$00d8=23d4
A1=0023d4
resolved slot = slot0
slot0 +02/+03 = 0202
slot0 +10/+12 = 0000
A5=0014f4
node+02=89a2
```

Trap #9 then hardcodes `D0=7` and executes:

```asm
bclr #7,$0002(A1)
```

For slot0 this does nothing:

```text
slot0 +02/+03 = 0202 -> 0202
```

Slot0 would need bit1 clear:

```text
0202 -> 0002
```

Therefore:

```text
Node 14f4 is enqueued correctly into slot0 +10/+12,
but slot0 is not made pending.
```

### 7.4 `$00d8` target pointer

Diagnostics show `$00d8` is not a late accidental bad pointer:

- `$00d8` is set by `fb8ab6`, bytewise:
  - `0000 -> 2300`
  - `2300 -> 23d4`
- It resolves to slot0.
- It is stable in the observed run.
- Reads of `$00d8` occur at:
  - `f87e6c/f87e74`: post-loader init/check
  - `f89abe`: output/service arming path
  - `f8f2ec`: trap #9 target load

Interpretation:

```text
$00d8 is a stable firmware pointer to slot0/system-output slot.
The local mismatch is not that $00d8 randomly flips wrong.
The mismatch is that a bit7 trap #9 enqueue primitive is used with slot0/bit1.
```

---

## 8. f89abe/f89ac2 output arming path

Disassembly:

```asm
f89a9a  tst.b   $03bc.w
f89a9e  beq     f89ace
f89aa4  move.b  (A0)+,$fffc4817.l
f89ab8  subq.b  #1,$03bc.w
f89abc  bne     f89ac8
f89abe  movea.w $00d8.w,A0
f89ac2  bclr    #1,$0002(A0)
f89ac8  st      $03c5.w
```

This is a panel/output-buffer-empty arming path, not a general slot0 queue service.

It can arm slot0 correctly by clearing bit1:

```text
slot0 +02/+03: 0202 -> 0002
```

But in the observed path it runs before the `14f4/89a2` node is posted, when slot0 `+10/+12` are still `0000/0000`. After the trap #9 post, no later output-buffer drain reaches `f89ac2`.

Conclusion:

```text
f89ac2 is a real slot0 bit1 armer, but it is not the missing queue consumer/promoter after node 14f4 is posted.
```

---

## 9. Force experiment: wake slot0 after 89a2 post

A very narrow env-gated experiment was added:

```text
ASR10_EXPERIMENT_WAKE_SLOT0_AFTER_89A2_POST=1
```

It triggers only when:

- caller is `f8f2f0/trap #9`
- `A1 == $00d8 == slot0`
- `A5 == 14f4`
- `node+2 == 89a2`

It then clears bit1 in slot0 `+02` after trap #9:

```text
slot0 +02/+03: 0202 -> 0002
```

Result:

- The dispatcher does select slot0.
- Slot0 dispatches to parked frame-PC `f87f66`.
- At that dispatch:
  - `slot0 +0c = 0000`
  - `slot0 +10/+12 = 14f4/14f4`
- No read of node `14f4` occurs.
- Node `14f4` remains in the queue.
- Slot0 equalizes back to `0202`.

This proves:

```text
slot0 bit1 wake is necessary but not sufficient.
The active slot0 context/frame is not the queue consumer for +10/+12.
```

Current refined blocker:

```text
Need to find the consumer/promoter that reads slot0 +10/+12,
pops node 14f4,
uses payload 89a2,
and installs/promotes actual runnable slot0 work.
```

---

## 10. Current conclusions by area

### FDC

```text
Status: substantially working, not current primary blocker.
Evidence: significant sector/FIFO progress, reaches LOADING SYSTEM and post-load slots.
Caveat: retain FDC status diagnostics; later scheduler progress may reveal real disk/file errors.
```

### DUART / panel

```text
Status: panel output path works enough to show messages.
Panel input loop seen after ERROR 139 is an error-confirm prompt, not normal boot wait.
Do not pursue panel key/autoresponse as primary next fix.
```

### IRQ/vector

```text
0x19/autovector: wrong, unused-vector stub, causes ERROR 139.
0x4e/0x4f: valid short latch/ack handlers, insufficient for scheduler progress.
Raw IRQ experiment: useful proof-of-wake, wrong semantic delivery.
```

### Scheduler / slot0

```text
Main active blocker.
Node 14f4/89a2 is naturally posted to slot0 queue.
Trap #9 is bit7-hardcoded and does not make slot0 pending.
Force-clearing bit1 wakes slot0 but still does not consume node.
Need queue consumer/promoter for slot0 +10/+12.
```

---

## 11. Main unresolved questions

1. Which firmware routine reads/pops slot queue head/tail `+0x10/+0x12`?
2. What does payload `0x89a2` mean?
3. What role does parked PC `f87f66` play in slot0's state machine?
4. Is node `14f4/89a2` supposed to be promoted into slot `+0c`, or consumed by a separate service loop?
5. Which event/interrupt/service should run after `f8f2f0 trap #9` to process the slot0 side queue?
6. Is the missing path caused by incomplete DUART Tx-empty semantics, incomplete FC68/MC68302 interrupt semantics, or a scheduler state bug in the current harness?

---

## 12. Recommended next diagnostic

Add a diagnostic specifically for slot queue consumers/promoters:

```text
ASR10_DIAG_SLOT_QUEUE_CONSUMER=1
```

Targets:

- all reads from slot0 `+0x10/+0x12`
- all writes to slot0 `+0x10/+0x12`
- all reads from slot0 `+0x0c`
- all writes to slot0 `+0x0c`
- all reads/writes of node `0x14f4 +00/+02/+04/+06`
- execution at `f87f66`
- any static routines that use `move.w $0010(Ax)` / `move.w $0012(Ax)` patterns with slot bases

At `f87f66`, log:

```text
PC/previous PC/opcode
D0-D7/A0-A6/SR/SP
slot0 +02/+03/+06/+0a/+0c/+10/+12
node 14f4 +00/+02/+04/+06
FC6814/FC6816/FC6818
panel state
recent dispatcher history
```

Recommended Codex prompt:

```text
Analyze the consumer/promoter path for slot0 queue +10/+12.

Background:
- trap #9 posts node 14f4/payload 89a2 to slot0 +10/+12.
- slot0 remains equalized unless bit1 is forced.
- Force-clearing slot0 bit1 makes dispatcher select slot0, but it resumes parked frame-PC f87f66 and does not read node 14f4.
- Node 14f4 remains queued.
- f89ac2 is only output-buffer-empty arming and is not the queue consumer.

Tasks:
1. Find static references to slot offsets +0x10/+0x12/+0x0c.
2. Identify candidate pop/promote routines.
3. Add ASR10_DIAG_SLOT_QUEUE_CONSUMER=1.
4. Log all runtime reads/writes of slot0 +10/+12/+0c and node 14f4 +00/+02.
5. Instrument f87f66 with full slot/node/register context.
6. Explain why slot0 dispatch at f87f66 does not pop +10/+12.
7. Do not change default behavior.
8. Run git diff --check and make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1.
```

---

## 13. Short status line for next session

```text
FDC load is substantially past the old blocker.
DUART/panel shows ERROR 139 only because raw autovector 0x19 is unused vector.
0x4e/0x4f are valid latch/ack handlers but not scheduler progress.
The main blocker is now scheduler/event promotion:
node 14f4/payload 89a2 is posted to slot0 +10/+12,
but no observed routine consumes/promotes it.
Force-waking slot0 reaches parked PC f87f66 and still leaves node 14f4 queued.
Next step: find slot0 queue +10/+12 consumer/promoter.
```
