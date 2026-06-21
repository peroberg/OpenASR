# experiments.md

# ASR-10 experiments and stubs

This file records path-opening experiments. Every experiment must state whether it is proof of hardware behavior or only a way to expose the next ROM path.

## Rule

```text
Log first.
Stub minimally.
Mark all behavior that depends on a stub.
Move only verified behavior into the clean driver.
```

## Experiment: panel/input bit 4 at `$FC4809`

### Purpose

Pass the ROM input/status gate at:

```asm
FB7C84 btst #4,$FFFC4809
```

### Experiment

Set bit 4 only at the semantic reader PC:

```text
PC == FB7C84
address == $FC4809
return value |= 0x10
```

Current/old flag name:

```cpp
ASR10_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

Preferred future name:

```cpp
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

### Result

With bit 4 clear:

```text
ROM writes $049D=05 at FB7C9E
```

With bit 4 set:

```text
ROM avoids FB7C9E $049D=05 write
$04EE becomes 01
execution reaches later FDC/media path
```

### Conclusion

```text
$FC4809 bit 4 is an input/event/status gate candidate.
```

### Caution

This does not prove the real panel hardware always returns bit 4 set. It only proves what ROM does if that status bit is set.

## Experiment: FDC result stubs for commands 1E/0E

### Purpose

Earlier path-opening experiments forced some FDC result bytes.

### Caution

When enabled, these stubs make byte-level FDC result semantics invalid for those commands.

They do not invalidate ROM control-flow discoveries, but they must not be treated as real hardware evidence.

### Current policy

Keep FDC result stubs disabled by default.

Important:

```text
The later 0x68 result from F3/Recalibrate/Sense came from real upd72069_device FIFO, not from result stubs.
```

## Experiment policy for future 0x46 success forcing

Avoid forcing command 0x46 success until raw image format and geometry are tested.

If ever added, use an explicit flag:

```cpp
static constexpr bool ASR10_EXPERIMENT_FORCE_CMD46_SUCCESS = false;
```

and log clearly:

```text
stubbed=1
```

Do not let forced FDC success leak into clean driver behavior.


---
