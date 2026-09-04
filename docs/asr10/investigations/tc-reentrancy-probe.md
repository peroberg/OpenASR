# Terminal-Count Reentrancy Probe

## Scope

`disk-not-responding-probe.md` found a 4.994-second silence after READ
DATA's own transfer completed cleanly, ending in a generic firmware
timeout (`DISK NOT RESPONDING`), and hypothesized `idma_drq_w()`'s
synchronous `tc_w()` call as a reentrancy bug against
`upd765_family_device`'s own live per-bit engine. This discriminates
*whether* INTRQ was ever asserted, grounds the reentrancy claim in the
actual source, and tests the minimal fix the hypothesis predicts.

## Del 1 — Was INTRQ Ever Asserted?

### Measured, not assumed

`docs/asr10/lua/archive/intrq-assertion-probe.lua`, three independent
passive signals through the whole 4.994s window:

1. **`$FC4001` (MSR) polled directly from Lua**, not tapped — a genuinely
   passive read (`upd765_family_device::msr_r()` has no side effects: no
   `fifo_pop()`, no state mutation, confirmed by reading the function
   body). Result:

   ```text
   INTRQP_MSR t=18.312000 pc=F87FC2 msr=10
   INTRQP_MSR t=23.306000 pc=FBA636 msr=80    <- only after the reset
   ```

   `$10` = `MSR_CB` only = `PHASE_EXEC`. **No change for the entire 5
   seconds.** `PHASE_RESULT` (`$D0` = `MSR_RQM|MSR_DIO|MSR_CB`) is never
   observed. `main_phase` only becomes `PHASE_RESULT` inside the
   `COMMAND_DONE` state, which unconditionally calls `command_end()` in
   the same step (`upd765.cpp:2066-2078`) — and `command_end()`
   unconditionally sets `irq = true` and calls `check_irq()`
   (`upd765.cpp:1672,1677`). **If `command_end()` had run, MSR would have
   shown `PHASE_RESULT`. It never did.**

2. **SR interrupt-mask level, sampled every 2ms.** Mask reached **0, 576
   times**, in this exact window — the scheduler's idle-loop tail
   (`$F87FCC move #$2000,SR`) opens the mask roughly every 8-9ms during
   normal `FILE 1`/instrument-load operation (confirmed directly here, not
   assumed from the earlier boot-phase measurement in
   `irq1-vector-and-sr-probe.md`, which was a *different* phase with a
   much tighter, mostly-closed mask profile). 576 real opportunities
   existed for a pending level-1 interrupt to be taken.

3. **Level-1 IACK tap** (same calibrated `cpu_space` offset used
   throughout this investigation): zero events in the window, consistent
   with 1 and 2.

### Answer

**INTRQ was never asserted.** Not "asserted but blocked by the mask" (576
open windows, zero deliveries rules that out) and not "asserted but lost
in delivery" (MSR never showed the state that only exists after
`command_end()` already ran). The FDC's own command-level state machine
never completed. This is a **state-machine failure**, confirming the
reentrancy hypothesis over the masking/delivery alternative the task asked
to rule out.

### Grounded in source, not names

```cpp
// upd765.cpp:755-781, fifo_push() -- called from the live MFM-decode path
// (live_write_mfm()/live_write_fm()), itself called from inside
// live_run()'s own for(;;) loop while decoding disk data for READ DATA:
void upd765_family_device::fifo_push(uint8_t data, bool internal)
{
    ...
    if(!fifo_write && (!fifo_expected || fifo_pos >= thr || (fifocfg & FIF_DIS)))
        enable_transfer();   // asserts DRQ -> drq_cb -> idma_drq_w(), synchronously
    ...
}

// upd765.cpp:410-421
void upd765_family_device::tc_w(bool _tc)
{
    if(tc != _tc && _tc) {
        live_sync();          // <-- can re-enter live_run()
        tc_done = true;
        tc = _tc;
        if(cur_live.fi) general_continue(*cur_live.fi);
    } else tc = _tc;
}

// upd765.cpp:868-891
void upd765_family_device::live_sync()
{
    if(!cur_live.tm.is_never()) {
        if(cur_live.tm > machine().time()) {
            rollback();
            live_run(machine().time());   // <-- reenters live_run()
            ...
```

The original `idma_drq_w()` called `tc_w(true)` **from inside**
`enable_transfer()`'s own caller (`fifo_push()`, called from `live_run()`'s
`for(;;)` loop, processing the very byte that triggered this DRQ).
`tc_w()` → `live_sync()` → `rollback()` + a **second, nested `live_run()`
call**, while the **first** `live_run()` invocation — the one whose call
stack led here in the first place — is still active, mid-iteration, with
`cur_live` (shared, mutable device state) only partially updated for the
byte just delivered. `rollback()`/the reentrant `live_run()` operate on
that same `cur_live` struct. Nothing in this code is written to tolerate
reentrant invocation; there is no reentrancy guard. The measured effect
(state machine permanently stuck in `PHASE_EXEC`) is consistent with the
reentrant call leaving `cur_live` in a state the outer, still-executing
loop iteration does not correctly resume from once control returns to it.
Not proven instruction-by-instruction here, but every measured fact is
consistent with it and nothing found here contradicts it.

## Del 2 — Minimal Fix, One Variable

Only `tc_w()` moved off the DRQ callback's call stack, to a zero-delay
timer (`asr10_boot_state::m_idma_tc_timer`,
`TIMER_CALLBACK_MEMBER(idma_tc_deliver)`). `dma_r()` (`idma_transfer_in()`)
stays exactly where it was — synchronous, inside `idma_drq_w()`, unchanged.
One variable:

```cpp
void asr10_boot_state::idma_drq_w(int state)
{
    if (!state || !m_maincpu->idma_channel_active())
        return;

    const u8 data = m_fdc->dma_r();
    if (m_maincpu->idma_transfer_in(data))
        m_idma_tc_timer->adjust(attotime::zero);   // was: tc_w(true); tc_w(false); here
}

TIMER_CALLBACK_MEMBER(asr10_boot_state::idma_tc_deliver)
{
    m_fdc->tc_w(true);
    m_fdc->tc_w(false);
}
```

A zero-delay `emu_timer` fires from MAME's own scheduler on its own call
stack, not nested inside whatever device call happened to trigger it —
structurally the same separation a real DMA controller's independent TC
bus line would provide, not a same-stack-frame function call.

### Result

```text
IDMARESULT_FILE1     t=16.300000
IDMARESULT_DISPLAY    t=17.790000 text="L0ADING JM DIGI 5YN   "
IDMARESULT_DISPLAY    t=22.290000 text="FILE L0ADED           "
```

Reproduced twice, identical (`t=22.290000` both times).
`docs/asr10/lua/archive/disk-not-responding-probe.lua` re-run against the
fixed build shows **32 clean vector-`$51` IACKs** from `t=18.306` to
`t=22.227`, each with a full 7-byte result phase, spanning multiple
distinct sectors (`R=$0B`, `$0C`, `$0D`, ...) — the multi-sector case
`idma-implementation-plan.md`'s limitation 2 flagged as untested. **`DISK
NOT RESPONDING` is gone. `DISK ERROR - LOST DATA` is gone.** The
instrument-load sequence now completes: `FILE LOADED`.

**Limitation 2 update:** `BCR-1`'s single-sector-only status is
strengthened, not fully resolved — this run exercises many *separate*
single-sector `BCR` programmings (one CMR-arm cycle per sector, matching
the measured per-sector re-arm pattern from
`idma-register-map-probe.md`), not one `BCR` value spanning multiple
sectors in a single armed transfer. Still open for a genuine multi-sector
`BCR` value; no longer open for "does the sequence work across more than
one sector at all."

## Del 3 — The Real Model, Argued Not Landed

**Decision, not implemented this task, per instruction.**

The task's own framing is the right one: *a DMA controller is an
independent bus master.* Modeling it as a reentrant call inside the
peripheral's own state machine does not build a controller, it builds a
callback that happens to move bytes. This fix deliberately does the
minimum that resolves the measured symptom (only `tc_w()` deferred) to
isolate the variable, not because the minimum is architecturally correct.

**Argument for moving the whole per-DRQ transfer to timer context**, not
just `tc_w()`:

- `dma_r()` itself, though it did not reproduce this specific symptom, is
  *equally* a synchronous call into `fifo_pop()` from inside the same
  `enable_transfer()`/`live_run()` call chain. It happened not to trigger
  visible corruption in this measurement, but "happened not to" is not the
  same claim as "is safe" — `fifo_pop()` also touches `fifo_pos`/`fifo[]`
  state that the live engine's own loop iteration may be assuming is
  stable.
- A real IDMA channel is asynchronous by construction: it responds to a
  request line and drives its own bus cycles on its own schedule. Every
  byte transfer, not just the last one, should structurally happen off
  the requesting device's own call stack for the model to actually *be*
  an independent bus master rather than an elaborate callback.
- Deferring only `tc_w()` worked here because this specific request's
  failure mode only manifested at the terminal-count edge. That is a
  property of *this* measurement, not a guarantee that no other request
  shape (a longer transfer, a write, an aborted transfer) can hit the same
  underlying reentrancy through the per-byte `dma_r()` path instead.

**Argument against, i.e. for leaving it as-is for now:** the minimal fix
is measured to work, completely, for the one flow that exists to
exercise it. Deferring every byte to timer-context callbacks has a real
performance cost (one timer event per transferred byte instead of one per
transfer) and a real complexity cost (the transfer's internal count/
pointer state would need to survive across scheduler re-entry between
every byte, not just be read once at arm time), for a benefit
(robustness against a byte-level reentrancy bug not yet observed to
occur) that is currently hypothetical. Rule 4's spirit — finish or delete
a half-built abstraction rather than stack a fifth variant beside four
unfinished ones — cuts toward not rebuilding the channel until a second
measured failure actually implicates `dma_r()`'s own reentrancy, not
before.

**Recommendation:** leave `dma_r()` synchronous until a concrete case
breaks it. If one does, move the *entire* per-byte transfer to timer
context in one change, not `tc_w()` and `dma_r()` separately — the
underlying issue (synchronous re-entry into `live_run()`'s call stack) is
the same for both, and half-deferring invites exactly the "fifth variant
beside four unfinished ones" the project's rules warn about.

## New Method Rule

Added to `methods-static-analysis.md`: *a DMA controller is an
independent bus master — modeled as a reentrant call inside the
peripheral's own state machine, it is not a controller.* Same family as
*a PC-dependent stub is not a hardware model* — both name a shortcut that
looks like the real mechanism from the outside while being structurally
a different thing on the inside, and both cost a specific, measured
failure before the difference showed up.

## Verification

- `make SUBTARGET=mame -j4`: clean.
- `docs/asr10/regression-test.sh`: 5/5 before, 5/5 after.
- **Verified in this change:** the `DISK NOT RESPONDING` timeout is gone;
  the instrument-load sequence now reaches `FILE LOADED`; 32 vector-`$51`
  IACKs observed across multiple distinct sectors, each with a clean
  7-byte result phase.
- No `mem_map` change. No `-log`. No `getenv`.
- `git diff --check`: clean.
- One variable changed (`tc_w()` deferred); `dma_r()`/`idma_transfer_in()`
  untouched, per instruction.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: +~15 lines (timer allocation, timer
  callback, one call site changed from two direct `tc_w()` calls to one
  `timer->adjust()`).
- `docs/asr10/lua/archive/intrq-assertion-probe.lua`: written, run,
  archived — its question is answered.
