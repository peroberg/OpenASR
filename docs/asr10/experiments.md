# ASR-10 experiments and stubs

This file records path-opening experiments, diagnostics, and disabled-by-default behavior tests.

Every experiment must state whether it is:

```text id="dvz77o"
- verified hardware behavior
- a diagnostic only
- a path-opener
- a negative result
- a hypothesis test
```

The current `asr10booth` target is an instrumented research harness, not a clean final production driver.

## Rule

```text id="ek52oc"
Log first.
Stub minimally.
Make every behavior experiment disabled by default.
Mark all behavior that depends on a stub.
Move only verified behavior into the clean driver.
Keep negative results; they prevent repeated dead ends.
```

Before committing harness changes:

```sh id="rv0bfi"
git diff --check
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

## Current experiment phase

The emulator now reaches:

```text id="h4y1cn"
ENSONIQ ASR-10
LOADING SYSTEM
```

Then the loaded runtime returns to dispatcher idle around:

```text id="7i2ny9"
f87f96 / f87f9a / f87fca
```

This is not treated as a crash. It is a firmware dispatcher queue scan / idle state.

Current focus:

```text id="0y1czp"
MC68302 / FC68xx service lifecycle
dispatcher queue/event payload
timer cadence / completion events
lowmem service state around $0d06/$0e82
```

Current important negative result:

```text id="htmdqt"
Clearing FC6816 0x2400 after the runtime service setter works mechanically, but does not advance boot.
```

So the current blocker is likely not just interrupt vectoring or FC6816 service clear.

---

# Stable path-opening experiments

## Experiment: panel/input bit 4 at `$FC4809`

### Type

```text id="uumljn"
Path-opener / status-gate diagnostic.
Not verified final hardware behavior.
```

### Purpose

Pass the ROM input/status gate at:

```asm id="iptwyb"
FB7C84 btst #4,$FFFC4809
```

### Experiment

Set bit 4 only at the semantic reader PC:

```text id="tp4j98"
PC == FB7C84
address == $FC4809
return value |= 0x10
```

Current/preferred flag name:

```cpp id="kz2ivn"
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

Older name sometimes seen in notes:

```cpp id="n2wbje"
ASR10_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

### Result

With bit 4 clear:

```text id="9n7vne"
ROM writes $049D=05 at FB7C9E
```

With bit 4 set:

```text id="x821kl"
ROM avoids FB7C9E $049D=05 write
$04EE becomes 01
execution reaches later FDC/media path
```

### Conclusion

```text id="adz49h"
$FC4809 bit 4 is an input/event/status gate candidate.
```

### Caution

This does not prove the real panel hardware always returns bit 4 set.

It only proves what ROM does if that status bit is set.

Panel input is probably not the immediate current blocker while the emulator is already at `LOADING SYSTEM` and dispatcher idle.

---

## Experiment: command `0x88` data-rate behavior

### Type

```text id="rh87k9"
Path-opener / FDC configuration helper.
Partly inferred behavior.
```

### Purpose

Allow the ROM to pass later FDC setup after the initial input/status gate.

### Current result

Later FDC sequence includes:

```text id="dc2fcy"
88, F3, 03, 07, 00, 08
```

Current interpretation:

```text id="smw4tq"
0x88 -> uPD72069 data-rate command
```

The harness uses a path-opening interpretation that allowed progress into later media/FDC behavior.

### Caution

Do not overgeneralize this into final FDC behavior without confirming exact uPD72069 semantics and ASR disk density/rate.

---

## Experiment: FC6860 busy bit clear after write

### Type

```text id="j1euzh"
Path-opener / control-plane helper.
Not yet final verified hardware behavior.
```

### Purpose

Allow boot/runtime control flow to proceed past a busy/status wait involving the FC68xx/MC68302 candidate area.

### Current/preferred flag

```cpp id="uvvndj"
ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE
```

### Caution

Keep this documented as a path-opener until exact MC68302/board-control semantics are known.

---

## Experiment: LRCLK clock/status bit to 68302

### Type

```text id="uyyw20"
Path-opener with strong supporting evidence.
```

### Purpose

Avoid earlier failure:

```text id="d6dr1m"
ERROR 009 No LRCLK input to 68302
```

### Current/preferred flag

```cpp id="x0iuh2"
ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3
```

### Result

With this enabled, boot progresses beyond the earlier LRCLK failure.

### Conclusion

The ASR-10 firmware expects the 68302/control-plane side to observe some LRCLK/audio-clock status.

### Caution

This does not mean PCM sample data flows through MC68302 registers.

More likely:

```text id="8jobl7"
analog/audio path -> codec/serial audio/OTIS/ESP/sample RAM
control/status/clock observation -> MC68302 or board glue
```

---

# FDC-related experiments

## Experiment: FDC result stubs for commands `1E` / `0E`

### Type

```text id="kubf1y"
Old path-opener.
Not valid hardware evidence when enabled.
```

### Purpose

Earlier path-opening experiments forced some FDC result bytes to expose later ROM paths.

### Caution

When enabled, these stubs make byte-level FDC result semantics invalid for those commands.

They do not invalidate ROM control-flow discoveries, but they must not be treated as real hardware evidence.

### Current policy

Keep FDC result stubs disabled by default.

Important:

```text id="b8lah6"
The later 0x68 result from F3/Recalibrate/Sense came from real upd72069_device FIFO, not from result stubs.
```

---

## Experiment policy for future `0x46 Read Data` success forcing

Avoid forcing command `0x46` success until raw image format and geometry are tested.

If ever added, use an explicit disabled-by-default flag:

```cpp id="j5ktpq"
static constexpr bool ASR10_EXPERIMENT_FORCE_CMD46_SUCCESS = false;
```

and log clearly:

```text id="brrr7d"
stubbed=1
```

Do not let forced FDC success leak into clean driver behavior.

Current caution:

```text id="xibggo"
FDC/media is not the immediate current blocker once the emulator reaches LOADING SYSTEM and returns to dispatcher idle.
Do not add FDC stubs unless logs show post-service FDC activity.
```

---

# MC68302 / IACK / FC68xx experiments

## Experiment: CPU-space IACK diagnostics

### Type

```text id="64o91d"
Diagnostics-only.
```

### Purpose

Determine whether M68K interrupt acknowledge cycles are using raw autovectors or device-supplied vectors.

### Background

Raw `m_maincpu->set_input_line(level, HOLD_LINE)` without a CPU-space IACK map uses M68000 autovectors.

M68000 autovectors:

```text id="ow2zdi"
IRQ1 -> vector 0x19
IRQ2 -> vector 0x1a
IRQ3 -> vector 0x1b
IRQ4 -> vector 0x1c
IRQ5 -> vector 0x1d
IRQ6 -> vector 0x1e
IRQ7 -> vector 0x1f
```

### Result

Runtime vector table shows:

```text id="okdp6m"
0x19..0x1f -> f882da -> ERROR 139 unused vector
```

### Conclusion

Raw autovector IRQs are wrong for the current ASR-10 runtime path.

The firmware expects a device-supplied vector or another interrupt-controller behavior.

### Caution

This diagnostic does not itself emulate a full MC68302 interrupt controller.

---

## Experiment: synthetic 68302 timer/source IRQ

### Type

```text id="yz6v6q"
Behavior experiment / path-opener.
Disabled by default.
```

### Purpose

Test whether a missing MC68302 timer/service interrupt is needed after `LOADING SYSTEM`.

### Current/preferred flags

```cpp id="17qed6"
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE
```

### Source

The synthetic source uses:

```text id="en139i"
0x2400
```

Observed pending/status register behavior:

```text id="6gjn1a"
FC6814 000b -> 240b
```

### Caution

This experiment is not proof that the real source is a simple timer, only that firmware responds meaningfully to a source/pending bit in this class.

---

## Experiment: IACK vector `0x40`

### Type

```text id="l7cg8f"
Negative result / placeholder.
```

### Result

Vector `0x40` maps to:

```text id="c085ay"
f882da -> ERROR 139 unused vector
```

### Conclusion

`0x40` is not a valid current vector for this service path.

It remains useful only as a placeholder/default value when the experiment is disabled.

---

## Experiment: IACK vector `0x21`

### Type

```text id="s4uw8j"
Vector matrix test.
Accepted-ish but not current best candidate.
```

### Result

Vector `0x21` maps to:

```text id="hvqwg2"
f87f76
```

Observed behavior:

```text id="u8tqv6"
avoids ERROR 139
enters dispatcher-related code
does not clear source properly
can lead to repeated IACK/service behavior
```

### Conclusion

`0x21` appears dispatcher-related but is not the current best vector for the `0x2400` source.

---

## Experiment: IACK vector `0x46`

### Type

```text id="4f08nx"
Negative result for this source/context.
```

### Result

Vector `0x46` maps to:

```text id="uxjki2"
f88f3e
```

Observed behavior:

```text id="85s1yh"
reaches real code
then causes ERROR 129 odd address error
```

### Conclusion

`0x46` is likely wrong for the current `0x2400` service source or requires a different state/context.

Do not use it as the current vector candidate.

---

## Experiment: IACK vector `0x4e`

### Type

```text id="2vjr12"
Accepted-looking vector candidate.
```

### Result

Vector `0x4e` maps to:

```text id="xw7rkz"
f88f06
```

Observed behavior:

```text id="2ko3ga"
handler writes FC6818=4000
FC6814 240b -> 000b clears naturally
runtime later sets FC6816 c080 -> e480 at 00bf1a
runtime sets $0d06 at 00bf22
final state still returns to LOADING SYSTEM / dispatcher idle
```

### Conclusion

`0x4e` is an accepted-looking service vector.

It is not sufficient by itself to advance beyond dispatcher idle.

---

## Experiment: IACK vector `0x4f`

### Type

```text id="dcph2n"
Accepted-looking vector candidate.
```

### Result

Vector `0x4f` maps to:

```text id="ztqh7f"
f88f22
```

Observed behavior:

```text id="f2bj6x"
handler writes FC6818=8000
then converges with 0x4e behavior
FC6814 clears naturally
FC6816 is set later at 00bf1a
final state still returns to LOADING SYSTEM / dispatcher idle
```

### Conclusion

`0x4f` is also an accepted-looking service vector.

The current blocker is probably not simply choosing between `0x4e` and `0x4f`.

---

## Experiment: one-shot / gated IACK lifecycle controls

### Type

```text id="4nrxh7"
Behavior control / negative-result test.
Disabled by default.
```

### Purpose

Determine whether interrupt flood was the sole blocker.

### Current/preferred flags

```cpp id="n64wje"
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP
```

### Result

One-shot/gated runs avoid flood, but still return to dispatcher idle.

Observed:

```text id="b63olq"
FC6814 clears naturally.
FC6816 later becomes e480.
Final panel remains LOADING SYSTEM.
Final hang remains around f87f96/f87f9a.
```

### Conclusion

Interrupt flood is not the sole blocker.

---

## Experiment: FC6816 service setter tracing

### Type

```text id="99uddi"
Diagnostics-only.
```

### Purpose

Find where FC6816 `0x2400` is set.

### Confirmed runtime code

```asm id="l66k5w"
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

### Result

At `00bf1a`:

```text id="vej8p6"
FC6816 c080 -> e480
set_bits=2400
```

At `00bf22`:

```text id="lav4j5"
$0d06 is set by ST $0d06
```

Observed context:

```text id="rpf34m"
D0=00000004
$0e82=0004
$0d06=0000 before 00bf22
FC6884=703b
FC6894=703b
recent dispatcher context: slot 2 was just handler-cleared at f87fb0
```

### Conclusion

The IACK handler does not directly set FC6816 `0x2400`.

Runtime code sets it later at `00bf1a`.

This looks like a real service/handshake routine.

---

## Experiment: gated FC6816 `0x2400` clear after setter

### Type

```text id="9lbmsv"
Narrow behavior hypothesis test.
Disabled by default.
Important negative result.
```

### Current/preferred flag

```cpp id="7w8aui"
ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER
```

### Purpose

Test whether FC6816 `0x2400` staying set is the sole blocker.

### Gating conditions

Only clear FC6816 `0x2400` after:

```text id="f4qsmn"
- runtime setter at/near 00bf1a has set FC6816 0x2400
- $0d06 has been set by 00bf22
- FC6814 0x2400 is already clear
- at least one later dispatcher RTE has occurred
- FC6816 still has 0x2400 set
```

### Result

For both `0x4e` and `0x4f` runs:

```text id="w0k2zy"
FC6816 e480 -> c080
clear at pc=f87fc0
rte_count=4
FC6814=000b
FC6818=0080
FC6884=703b
FC6894=703b
```

But:

```text id="9iaasj"
no panel advance beyond LOADING SYSTEM
no post-clear FDC activity
no new error
final hang remains f87f9a dispatcher idle
```

### Conclusion

FC6816 `0x2400` may still be part of an in-service/EOI/service-active lifecycle.

But FC6816 `0x2400` staying set is not the sole blocker.

### Current implication

The next blocker is likely one of:

```text id="fm287h"
- missing dispatcher queue re-arm
- missing event payload
- missing timer tick/timebase side effect
- missing FC6884/FC6894 completion behavior
- missing lowmem state transition involving $0d06/$0e82
```

---

# Current active question

The next experiment should not be another broad behavior stub.

The next diagnostics should answer:

```text id="dhxu82"
After 00bf1a and $0d06 set, which queue slot, lowmem flag, timer register, or completion signal should change to make the dispatcher leave f87f9a idle?
```

Focus areas:

```text id="55q37s"
dispatcher queue base $00c6
dispatcher queue end $00c8
active/current record $0b6a
slot byte2/byte3
slot 2 callback/context
last non-idle event before final f87f9a
FC6884/FC6894 behavior after service sequence
$0d06/$0e82 readers/writers
```

---

# Experiments not to repeat without new evidence

Do not spend more time on these unless new evidence appears:

```text id="qy68mb"
raw autovector IRQ as current path
vector 0x40
vector 0x46 for source 0x2400
interrupt flood as sole blocker
FC6816 0x2400 staying set as sole blocker
wrong choice between 0x4e and 0x4f as sole blocker
broad FDC success forcing before logs show post-service FDC activity
fake panel input before logs show runtime is waiting for user events
```

---

# Future cleanup direction

Eventually, current experiment flags should collapse into a small named control-plane model, for example:

```cpp id="ulx52z"
struct asr10_68302_state
{
    u16 pending;
    u16 in_service;
    u16 control;
    u16 timer_reload;
    u8 irq_vector;
    bool lrclk_present;

    void set_pending(u16 mask);
    void acknowledge(u8 level);
    void clear_pending(u16 mask);
    bool source_in_service(u16 mask) const;
    void end_of_interrupt(u16 mask);
};
```

Do not refactor too early.

Current priority is still empirical reconstruction of the next missing runtime event.
