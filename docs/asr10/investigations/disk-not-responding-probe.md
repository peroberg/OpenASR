# DISK NOT RESPONDING Probe

**Resolved:** `tc-reentrancy-probe.md` confirmed this document's reentrancy
hypothesis by direct measurement (INTRQ genuinely never asserted, not just
undelivered) and fixed it (`tc_w()` deferred to a zero-delay timer). `DISK
NOT RESPONDING` is gone; the instrument-load sequence now reaches `FILE
LOADED`. This document's diagnosis stands as written below.

## Scope

`idma-implementation-plan.md` resolved the `DISK ERROR - LOST DATA`
overrun. The instrument-load sequence now proceeds from `t≈18.3s` to
`t≈23.4s` before a different error, `DISK NOT RESPONDING`. This measures
what actually runs out, and tests the standing rate-source hypothesis
against it. Method only — the rate hypothesis is evaluated and refuted by
measurement and by source, no code change proposed as a result.

## Del 1 — What Happened In The Five New Seconds

`docs/asr10/lua/archive/disk-not-responding-probe.lua`: full FDC dialogue
(both directions), IDMA SIB register writes, vector-`$51` IACKs, and every
display change, from `FILE 1` through well past the error.

### The measured sequence

```text
t=18.306076  aux $88 (data rate select)
t=18.306249-18.306389  03 E1 08 / 07 00        SPECIFY(DMA) + RECALIBRATE
t=18.306434  IACK1 vector=$51                  RECALIBRATE completion
t=18.306499-18.306670  08 / 0F 00 01           SIS + SEEK track 1
t=18.308684  IACK1 vector=$51                  SEEK(1) completion
t=18.308749-18.309174  08 / 0F 00 00           SIS + SEEK track 0
t=18.311188  IACK1 vector=$51                  SEEK(0) completion
t=18.311253  08                                 SIS
t=18.311412-18.311605  46 00 00 00 08 02 08 1B FF   READ DATA
                                                 [IDMA transfer runs here --
                                                  no overrun, no LOST DATA]
-------- 4.994 seconds of TOTAL SILENCE: no FDC read, no FDC write, no
         IDMA register write, no vector-$51 IACK --------
t=23.305863  IDMA shared-prelude write (CMR=$0002, IPR, IMR)
t=23.305871  aux $36 (software reset)           PC=$0024AA, NOT the usual
                                                 aux-sender PC ($FB8D14)
t=23.305972  IACK1 vector=$51                   PC=$0069A2, also not the
                                                 usual context
t=23.306037  08 -> result $C0, $00              ST0=$C0 (ABRT, fid=0,
                                                 NR clear -- a *fresh*
                                                 ready-line-change artifact,
                                                 unrelated to READ DATA)
t=23.330000  display -> "DISK"
t=23.350000  display -> "DISK NOT RESPONDING"
```

### Answer: missing interrupt, not a timeout from slow transfer, not an unanswered command

The three candidates the task asked to distinguish:

- **A firmware timeout waiting on elapsed time regardless of cause** — the
  *symptom* (a ~5s wait then a give-up) looks like this, but that is what
  a timeout *always* looks like from outside; it doesn't distinguish *why*
  the timeout fired.
- **An unanswered command** — ruled out. READ DATA's 9 command bytes were
  fully accepted (`upd765_family_device`'s FIFO accepted all 9 without a
  `C_INVALID` re-entry into command phase, and the IDMA transfer
  demonstrably ran without triggering the previous overrun). The command
  was answered at the device level.
- **A missing interrupt** — this is what the evidence supports. READ
  DATA's own completion never produced a vector-`$51` IACK. Contrast with
  every other completion in this whole investigation (RECALIBRATE, both
  SEEKs, and even the earlier ready-line-artifact false completions): each
  produced an IACK within microseconds of the underlying event. This one
  produced total silence — no FDC register access of any kind, from any
  code path — for very nearly exactly 5.000 seconds
  (`23.305863 - 18.311605 = 4.994258s`), then a generic recovery action
  (software reset from an unfamiliar PC, i.e. not the normal aux-sender
  routine) that lands in the "DISK NOT RESPONDING" display path.

**[Verified dynamic]** No result-phase read and no vector-`$51` IACK ever
occurred for the `t=18.3116` READ DATA's own completion. **[Likely]** The
5-second gap is a firmware watchdog/timeout waiting specifically for that
missing interrupt, not a data-transfer duration — see Del 2 for why
duration cannot explain a gap this size regardless of rate.

### Why the interrupt is plausibly missing — a specific, testable hypothesis, not fixed here

`asr10_boot_state::idma_drq_w()` calls `m_maincpu->idma_transfer_in(data)`
and, on the last byte, `m_fdc->tc_w(true)`/`tc_w(false)` **synchronously,
inside the DRQ callback** — which is itself invoked synchronously from
*inside* `upd765_family_device`'s own live per-bit transfer engine
(`enable_transfer()` is called from deep within `live_run()`'s byte
delivery). `tc_w()` in turn calls `general_continue()` immediately
(`upd765.cpp:410-421`). This means the terminal-count-driven state
transition is invoked *reentrantly*, from inside the same call stack that
is still processing the live engine's delivery of that same byte — not
from a separate, later event as a real external DMA controller's TC output
line would deliver it (a real 68440/68442-class controller asserts TC as
an independent signal edge, not a same-stack-frame reentrant call).
`command_end()` (which sets `irq=true` and calls `check_irq()`) is only
reached via `general_continue()` walking the sector-boundary check through
to `COMMAND_DONE` — if the reentrant call disrupts that walk (e.g. because
`cur_live` state is mid-update from the byte that just triggered the DRQ
in the first place), the state machine could silently fail to reach
`COMMAND_DONE`, explaining exactly this symptom: the byte transfer itself
completes correctly (no overrun) but the command-level completion, and
therefore the interrupt, never fires.

**Not fixed in this task.** Per instruction, Del 1 is diagnosis. A correct
fix likely needs `tc_w()` deferred off the DRQ callback's own call stack
(e.g. a zero-delay timer, matching how a real DMA controller's TC line
would be an independent bus event) rather than invoked reentrantly. Left
as the concrete next step, not attempted here.

## Del 2 — The Rate Hypothesis, Tested Against Both Source And Measurement

**Refuted, on two independent grounds.**

### Ground 1: the rate is already forced to 500kbit/s, in already-active code

`upd72069_device::auxcmd_w()` (`upd765.cpp:3449-3463`) decodes aux `$88`
(`data & 0x70 == 0x00`) as `cur_rate = 250000`. But
`asr10_boot_state::upd72069_fdc_w()` (`asr10_boot.cpp:643-657`), the
driver's own existing code, immediately follows every aux-`$88` write with
`m_fdc->set_rate(500000)` when `ASR10_MISSING_FDC_RATE_SOURCE` is true —
and it is (`static constexpr bool ASR10_MISSING_FDC_RATE_SOURCE = true;`,
already active, already documented as a `KEEP`-category workaround in
`driver-instrumentation-audit.md`). `set_rate()` directly overwrites
`cur_rate` (`upd765.cpp:612-614`). **The effective rate for the exact aux
`$88` write observed at `t=18.306076` in this run was 500000, not
250000** — this was already fixed, active this entire investigation,
unrelated to `DISK NOT RESPONDING`.

### Ground 2: the magnitude does not fit regardless of rate

512 bytes at 250kbit/s ≈ 512×8/250000 ≈ 16.4ms. At 500kbit/s ≈ 8.2ms. The
difference between the two rates for one sector is on the order of **8
milliseconds**. The observed gap is **4,994 milliseconds** — roughly 600×
larger than the entire rate difference could account for, and larger even
than either rate's full transfer time by itself. A halved data rate
predicts a transfer that takes twice as long as expected; it does not
predict total silence with no FDC or IDMA activity of any kind for five
seconds. The measured pattern (total silence, then a generic timeout
recovery) is characteristic of a missing event, not a slow one.

**No test-and-measure change performed for this hypothesis**, per
instruction ("landa den bara om regressionen är grön och du kan namnge vad
som blev verifierat") — there is nothing to test: the rate is already
correct in the running configuration, confirmed by reading the exact code
path this run exercises, not by inference.

## Del 3 — Known Limitations, Journaled

Per instruction, recorded as limitations of the current minimal
implementation, not as claims about the model's correctness:

1. **The IDMA channel is direction-locked, FDC→memory only.** `SAPR` is
   stored (readable back correctly) but never dereferenced through the CPU
   address space — `idma_transfer_in()`'s caller (`asr10_boot_state::
   idma_drq_w()`) always supplies the byte directly from
   `m_fdc->dma_r()` and always writes to the DAPR-derived destination.
   ASR-10 writes to disk as well as reading; a WRITE DATA-style transfer
   (memory→FDC) is not implemented and will not work as-is. This will
   surface as a failure the first time firmware attempts to save.
2. **`BCR-1` is a choice made without known register semantics**, not a
   decoded convention (`idma-implementation-plan.md` item 5). It produces
   the correct 512-byte count for this one single-sector request. It has
   not been tested against a multi-sector transfer and should be
   re-examined the first time a different `N`/multi-sector `BCR` value is
   observed — the interpretation may not generalize.
3. **`$FC5803` (SAPR's measured value) is inside CS3
   (`$FC4000-$FC5FFF`, confirmed by `hardware-map.md`/`memory-map.md`),
   but the current `mem_map` decodes `$FC5020` and above as a generic
   `.ram()` catch-all**, not as a distinct peripheral register within that
   chip-select. This is a gap in the memory map, not in the IDMA
   implementation (which deliberately avoids depending on it, per
   limitation 1's flip side). `mem_map` was not changed, per instruction.
   To be addressed when the CS3 E2 question is otherwise revisited.
4. **DAPR is honored, not hardcoded** — confirmed by re-reading
   `mc68302_device::internal_w()`'s `OFFSET_IDMA_CMR` case: on channel
   arm, `m_idma_dest = m_idma_dapr;` seeds the working destination pointer
   from the actual programmed `DAPR` value, and `idma_transfer_in()`
   increments `m_idma_dest` (not a fixed address) per byte. No fourth
   "hardcoded destination" limitation applies.

## Verification

- No code changed this task — diagnosis and hypothesis only, per
  instruction (Del 1 explicitly scoped to diagnosis; Del 2's rate
  hypothesis was refuted before reaching the point of testing a change).
- `docs/asr10/regression-test.sh`: 5/5, unaffected (Lua-only probe, no
  driver/device change).
- No `mem_map` change. No `-log`. No `getenv`.
- `git diff --check`: clean.

## Line Count

- `docs/asr10/lua/archive/disk-not-responding-probe.lua`: written, run,
  archived — its question is answered.
