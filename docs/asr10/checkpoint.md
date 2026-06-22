# ASR-10 Current Checkpoint

## Current phase

ASR-10 MAME control-plane bring-up.

Current focus:

```text
68302 interrupt/service lifecycle
dispatcher/event payload after LOADING SYSTEM
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

The next blocker is probably one of:

```text
missing dispatcher queue re-arm
missing event payload
missing timer tick/timebase side effect
missing FC6884/FC6894 completion behavior
missing lowmem state transition involving $0d06/$0e82
```

## Next task

Analyze dispatcher queue/event payload after the accepted 0x4e/0x4f service sequence.

Do not edit files first.

Focus on:

```text
queue base $00c6
queue end $00c8
active/current record $0b6a
slot byte2/byte3
slot 2 callback/context
last non-idle event before final f87f9a
whether any slot should become pending again but does not
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
