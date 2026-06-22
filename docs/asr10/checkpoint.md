# ASR-10 Current Checkpoint

## Current phase

ASR-10 MAME control-plane bring-up.

Current focus:

```text
68302 interrupt/service lifecycle
dispatcher callback / scheduler re-arm after LOADING SYSTEM
```

The emulator reaches:

```text
ENSONIQ ASR-10
LOADING SYSTEM
```

Then it idles in the firmware dispatcher around:

```text
f87f96 / f87f9a / f87fca
```

This is not currently treated as a crash. It is a dispatcher idle/queue-scan state.

## Main file

```text
src/mame/ensoniq/asr10_boot.cpp
```

## Safe established boot aids

These are currently considered established:

```cpp
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 = true;
ASR10_EXPERIMENT_CMD88_RATE_500K = true;
ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE = true;
ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3 = true;
```

## Experimental flags should default false

```cpp
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ = false;
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR = false;
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT = false;
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR = false;
ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER = false;
```

Default placeholder vector:

```cpp
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE = 0x40;
```

`0x40` is known wrong/placeholder. It maps to unused vector / ERROR 139.

## Confirmed facts

Raw autovector IRQ is wrong:

```text
IRQ1 autovector -> vector 0x19 -> f882da -> ERROR 139
```

Useful runtime vector candidates:

```text
0x4e -> f88f06 -> writes FC6818=4000
0x4f -> f88f22 -> writes FC6818=8000
```

Both 0x4e and 0x4f are accepted-looking and converge.

Current 0x2400 service lifecycle:

```text
FC6814 pending set: 000b -> 240b
IACK vector 0x4e or 0x4f
FC6818 handler write: 4000 or 8000
FC6814 clears naturally: 240b -> 000b
Runtime code at 00bf1a sets FC6816: c080 -> e480
00bf22 sets $0d06
Optional gated clear can return FC6816: e480 -> c080
Final state still returns to dispatcher idle at f87f9a
```

## Confirmed service setter

```asm
00bef2  writes FC6894 = 703b
00bf00  writes FC6884 = 703b
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

Known context:

```text
D0=00000004
$0e82=0004
$0d06=0000 before 00bf22
FC6884=703b
FC6894=703b
```

Line-A/A000 in this path is now explained:

```asm
f882ca: move.w D0,(A7)
f882cc: addq.l #2,2(A7)
f882d0: rte
```

Documented effect:

```text
Line-A vector #10 points to ROM f882ca.
A000 writes D0.w to the stacked SR/CCR, skips the A000 opcode, and returns to the next instruction.
In the 00bf18 path, D0=0004 becomes the post-RTE SR/CCR.
A000 does not produce dispatcher queue/event payload.
```

## Dispatcher/RTE and slot 2 facts

The dispatcher builds a normal 68000 short RTE frame:

```asm
f87fa2  move.l $0006(A2),-(A7)   ; callback PC
f87fa6  move.w $000a(A2),-(A7)   ; stacked SR
f87fb4  move.w $000c(A2),D5
f87fb8  clr.w  $000c(A2)
f87fbc  move.w A2,$0b6a.w
f87fc0  rte
```

Slot field candidates:

```text
+0x02/+0x03  pending/equalized bytes
+0x06        callback / RTE PC
+0x0a        stacked SR
+0x0c        dispatch context / continuation, moved to D5 and cleared
+0x0e        extra context / USP-ish candidate
+0x10        secondary continuation/list pointer candidate
+0x12        companion state to +0x10
```

Baseline slot 2:

```text
slot base=002400
pending word=002402
frame SR=0000
frame PC=007308
dispatcher SR before RTE=2700
first-PC diagnostics showed actual_pc=007308
no immediate IACK was observed in baseline
007308 executes and starts with jsr $fff8d01a
```

## Slot 2 finalizer/completion facts

Post-service/finalizer code around `f8ce00..f8ce46` looks like a list-drain/completion/finalizer candidate, not a new-work producer:

```asm
f8ce00: move.w $0b6c.w,D1
f8ce04: move.w $000c(A2),D0
f8ce08: beq.s  f8ce1c
f8ce0a: move.w D0,$0b6c.w
f8ce0e: subq.b #1,$0b7f.w
f8ce12: move.w D0,D5
f8ce14: move.w D1,(A5)
f8ce16: move.w D0,D1
f8ce18: clr.w  $000c(A2)
f8ce1c: move.w $0010(A2),D0
f8ce20: beq.s  f8ce3a
f8ce22: move.w D0,D5
f8ce24: move.w D0,$0b6c.w
f8ce28: subq.b #1,$0b7f.w
f8ce2c: move.w (A5),D0
f8ce2e: beq.s  f8ce32
f8ce30: move.w D0,D5
f8ce32: bne.s  f8ce28
f8ce34: move.w D1,(A5)
f8ce36: clr.l  $0010(A2)
f8ce3a: move.w #$8080,$0002(A2)
f8ce40: move.w #$0000,D0
f8ce44: A000
f8ce46: rts
```

For slot 2:

```text
A2=002400
f8ce36 clears 002410/002412 if that path runs
f8ce3a writes 002402=8080
8080 is equalized/not pending because byte2 == byte3
```

Healthy pending production is separate:

```asm
f87f28: clr.b  $0002(A1)
f87f2c: move.b #$01,$0003(A1)
```

This creates `0001`, so byte2 != byte3 and the slot is pending.

## Latest negative result

Experiment:

```text
Clear FC6816 0x2400 only after:
- 00bf1a setter happened
- $0d06 was set
- FC6814 0x2400 is clear
- later dispatcher RTE happened
```

Result:

```text
FC6816 e480 -> c080 works mechanically.
No panel advance.
No post-clear FDC activity.
No new error.
Final hang remains f87f9a dispatcher idle.
```

Conclusion:

```text
FC6816 0x2400 staying set is not the sole blocker.
```

## Current best hypothesis

The next blocker is probably:

```text
callback-chain / scheduler re-arm / producer-path after slot 2 completion
```

Do not treat these as primary blockers without new evidence:

```text
A000 / Line-A
blind FC6816 clear
FDC/raw image
broad MC68302 refactor
```

## Next task

Analyze the actual callback chain and re-arm choice after slot 2 completion.

Do not edit files first.

Focus on:

```text
007308
f8d01a
f8d020 / f8d05a if reached
00bef2 / 00bf1a / 00bf22
f8ce00..f8ce46
first branch where callback-chain chooses producer/re-arm, wait-for-status, or finalizer
```

## Do not do next

Do not immediately:

```text
fake panel input
fake FDC activity
implement full 68302
chase audio/PCM/A-D path
add broad new behavior stubs
refactor the whole harness
```

## Validation before commits

```sh
git diff --check
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```
