# ASR-10 Floppy Subsystem Investigation Notes

## Purpose

This document summarizes the current evidence, hypotheses, and remaining questions regarding the Ensoniq ASR-10 floppy boot process, based on reverse engineering of the firmware, MAME instrumentation, and comparison with the existing Ensoniq SD-1/VFX MAME driver.

---

# Current Confidence

## Confirmed

- The boot firmware executes a substantial amount of floppy-related code.
- Callback scheduler and slot dispatcher are functioning.
- Trap handling is functioning.
- [Verified] Trap #6 dispatcher-save preserves the inline return PC.
- [Verified] Slot2 remains equalized after Trap #6 because its continuation field is empty.
- The firmware reaches multiple floppy callback routines.
- The firmware eventually disables a scheduler/list state by writing:

```
$0DF4 = 0000
$0DFA = FFFF
```

inside ROM routine:

```
FFF8E570
```

This is an unconditional write.

---

# Scheduler observations

Current execution:

```
007308
    jsr FFF8D01A
    jsr FFF8E570
    jsr FF8E56
    jsr FFF8DEB2

00733A
    move.w $0DFA,D0

00733E
    bne 007360
```

Since

```
$0DFA == FFFF
```

execution skips the entire re-arm block.

Skipped:

```
007340
tst.w $0DF4

007346
jsr $FFFF8F00

007352
jsr $8DD8

007358
move.w #FFFF,$0DFA
```

---

Later:

```
007360
bpl ...

trap #6
```

Trap #6 enters dispatcher logic instead of returning directly to inline code.

---

No execution reaches

```
007370
move.w $2400,$0DFA
```

Therefore:

- $2400 is never copied into $0DFA
- trap #8 is never executed
- trap #5 is never executed
- $0DFA is never cleared later

---

# What this probably means

FFF8E570 does **not** appear to be:

- an error handler
- an FDC timeout
- DMA completion failure

Instead it appears to be a scheduler/list state transition.

It disables one phase of the callback system.

The real question is therefore not

> "Why is FFF8E570 executed?"

because it is always executed.

The real question is

> "Why does nothing later re-enable the scheduler?"

---

# Evidence collected

Confirmed runtime order:

```
F87DD6
    $0DFA = 0003

FB90DC
    $0DFA = 0000

FB8AB6
    clears scheduler records

F8E576
    $0DFA = FFFF

F8E6B6
    writes FFFF again
```

---

# Scheduler records

Multiple record tables become cleared before FFF8E570 executes.

Examples:

```
00136E
001382
001394

0013B6
0013CA
0013DC

0013FE
001412
001424

001446
00145A
00146C
```

These appear to be scheduler/list records.

---

# Slot 2 observations

Slot structure:

```
002400
```

Important fields:

```
+02
+0C
+10
+12
```

Observed:

```
+02 = 8080
+0C = 0000
+10 = 0000
+12 = 0000
```

No continuation is pending.

Dispatcher therefore repeatedly takes the "equalized" path.

## Trap #6 dispatcher save state

[Verified] After callback `007308` reaches `007360`, `D0=FFFF` leaves the N flag set, so `007360: bpl $7366` is not taken and `007362: trap #6` executes.

[Verified] Trap #6 enters the ROM dispatcher/save path at `F880D6`.

[Verified] The trap frame contains the inline return PC:

```
SR = 0008
PC = 007364
```

[Verified] The dispatcher-save path writes the return frame back into slot2:

```
slot2 +06 = 007364
slot2 +0A = 0008
slot2 +0E = 1F14
```

[Verified] The return PC is therefore not lost.

[Verified] Slot2 still remains equalized:

```
slot2 +02 = 8080
slot2 +10 = 0000
slot2 +12 = 0000
```

[Verified] `F880E0` reads `002410 == 0000`, so the zero-continuation path runs:

```
F880E4  BEQ F880FC
F880FC  BSET D2,$0002(A2)
F88100  BSET D2,$0003(A2)
```

With `D2=7` and slot2 already `8080`, this preserves `byte2 == byte3`.

[Verified] No later dispatcher RTE to `007364` was observed in the baseline trace.

[Likely] Trap #6 save/yield mechanics are functioning correctly. The current blocker is not that Trap #6 loses the continuation PC; it is that slot2 is not made pending after the PC is saved.

[Likely] The missing behavior is upstream of the dispatcher-save path: some producer should populate slot2 continuation state or otherwise make slot2 pending.

---

# Major unanswered question

Nothing currently proves that the firmware ever believes that a floppy transfer has completed.

The firmware may simply be waiting forever for some hardware event.

---

# Comparison with SD-1 / VFX MAME driver

The SD-1/VFX driver provides several important architectural clues.

---

## WD1772 mapping

The floppy controller is memory mapped:

```
0x2C0000 - 0x2C0007
```

Firmware directly reads and writes the WD1772 registers.

---

## Floppy image formats

The driver supports:

```
ESQIMG
HFE
MFM containers
```

These are merely different host representations.

The firmware always communicates with the same WD1772 interface.

---

## Drive control is outside the FDC

The driver controls separately:

- drive select
- side select
- motor enable
- disk ready

through DUART outputs.

Therefore the floppy subsystem consists of more than the FDC registers.

---

## Disk Ready

DUART input reports

```
disk ready
```

to the firmware.

Thus firmware decisions may depend upon:

- drive selected
- motor spinning
- media present
- ready asserted

rather than FDC status alone.

---

## Disk Change interrupt

The SD-1 driver generates IRQs from disk change events.

Again:

the FDC is only one part of the floppy subsystem.

---

## DMA

Very important observation.

The SD-1/VFX configuration is largely CPU-driven.

However the EPS/EPS-16 driver contains:

```
HD63450 / MC68450 DMA
```

connected directly to

```
WD1772 data register
```

DMA channel 0 performs floppy transfers.

Completion generates IRQ2.

Therefore Ensoniq definitely used DMA-assisted floppy transfers on at least one hardware family.

---

# Implications for ASR-10

The ASR-10 likely consists of several layers.

```
Firmware Scheduler
        ↑
Completion Event
        ↑
IRQ / DMA completion
        ↑
FDC transfer
        ↑
Drive electronics
```

If any layer is missing,

the firmware may wait forever.

---

# Things that still require proof

## 1.

Was a floppy command actually issued?

Need evidence:

```
RESTORE
SEEK
READ SECTOR
READ ADDRESS
READ TRACK
```

---

## 2.

Which sector was requested?

Need:

- track
- side
- sector
- size

---

## 3.

Did the FDC begin transferring data?

Need evidence that

```
DRQ asserted
```

or equivalent data request occurred.

---

## 4.

Did firmware read transferred bytes?

Need:

```
FDC DATA register reads
```

or DMA activity.

---

## 5.

Was DMA involved?

Need proof whether:

- MC68302
- uPD72069
- other DMA mechanism

moves data automatically.

---

## 6.

Did completion interrupt occur?

Need evidence that

firmware receives the event that should wake the scheduler.

---

## 7.

Is the drive properly enabled?

Need confirmation that ASR-10 asserts equivalents of:

- drive select
- motor enable
- side select
- disk ready

as seen in the SD-1 driver.

---

# Suggested investigation order

[Verified] Further analysis of Trap #6 and dispatcher-save has reduced the likelihood of a dispatcher failure.

Recommended order:

1. Trace the last possible writers/producers of slot2 `+10/+12` before `F880E0`.
2. Identify whether the missing continuation producer is called from the `007308` callback path, a scheduler list path, or a hardware-completion path.
3. If the producer path branches on hardware status, then determine the exact hardware source being tested.
4. Only after that branch is proven, move downward into the relevant subsystem:
   - FDC command sequencing
   - Data transfer
   - DMA, if actually referenced
   - Interrupt/completion signalling
   - Drive ready/motor/select signalling

[Hypothesis] The missing producer may be caused by a lower-level floppy/DMA/completion event that never occurs, but the current verified control-flow evidence only proves a missing continuation/pending producer, not which hardware source should cause it.

---

# Current assessment

[Verified] The callback scheduler, slot dispatcher, Line-A behavior, Trap #6 entry, and Trap #6 dispatcher-save mechanics are working well enough to preserve return context.

[Verified] Trap #6 saves `007364` into slot2 `+06` and saves `0008` into slot2 `+0A`.

[Verified] Slot2 remains equalized at `8080` because slot2 `+10` is still `0000`.

[Verified] Since byte2 and byte3 are equal, the dispatcher does not resume slot2 at `007364`.

[Likely] The current blocker is not a dispatcher/RTE/trap-save failure. It is more likely a missing upstream producer of continuation state or pending queue state.

[Hypothesis] That producer may depend on floppy/DMA/completion hardware, but that remains unproven until a firmware branch is shown to wait on such status before deciding not to populate slot2 `+10` or pending bytes.

The highest-value next investigation is therefore to find the producer or skipped producer for slot2 continuation/pending state, and only then bind that path to FDC/DMA/interrupt hardware if the runtime branch evidence requires it.

# varning

OBS: Jämförelsen med SD-1/VFX visar arkitekturprinciper, inte ASR-10-bevis.
SD-1/VFX/EPS-spåret verkar WD1772-baserat enligt ES5570/GLU och MAME-driver.
uPD72069/uA765-spåret hör snarare till TS10/12 och möjligen ASR-10 enligt service/error-spår.
Kopiera därför inte WD1772-mappningen direkt till ASR-10.

## Trap #6 dispatcher save investigation (2026-06-29)

A dedicated instrumentation pass was performed around the Trap #6 dispatcher save path.

### Verified observations

The trap frame is preserved correctly.

The dispatcher saves:

- slot2 +06 = 007364 (return PC)
- slot2 +0A = 0008 (SR)

Therefore the return address is **not lost**.

The active slot after save is:

+02 = 8080
+06 = 007364
+0A = 0008
+0E = 1F14
+10 = 0000
+12 = 0000

### Important observation

Immediately before the save path:

```
007360: bpl $7366    ; not taken because D0=FFFF sets N
007362: trap #6
```

Trap #6 enters:

```
F880D6
```

At `F880E0`, the handler reads:

```
slot2 +10 = 0000
```

Therefore it takes:

```
F880E4 -> F880FC -> F88100
```

This sets bit 7 in both pending bytes, preserving:

```
slot2 +02 = 8080
```

### Current conclusion

[Verified] `007364` is saved into slot2.

[Verified] `007364` is not observed as a later dispatcher RTE target in the baseline trace.

[Verified] Slot2 does not become pending because `+10` is zero and `+02` remains equalized.

[Likely] The Trap #6 dispatcher-save mechanism is not the current blocker.

[Likely] The current blocker is upstream: a missing producer/re-arm path should either populate slot2 `+10` or directly make slot2 pending.

[Hypothesis] That missing producer may be triggered by floppy/DMA/completion hardware, but the Trap #6 investigation itself does not prove that.

### Next investigation

[Verified target] Trace all writes and candidate producers of slot2 `002410/002412` from `007308` entry through `F880E0`.

[Likely target] Identify the first branch or callback that could have produced nonzero continuation state but did not.

[Hypothesis target] If that branch depends on FDC/DMA/interrupt status, use that as the first justified hardware-specific experiment.
