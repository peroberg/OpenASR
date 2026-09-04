# IRQ1 Storage Completion — First Wiring Attempt

**Retroactive correction:** this document describes `ERROR 129 - REBOOT`
below as "the firmware's own error path" catching and reporting something.
That is wrong. `docs/asr10/investigations/irq1-handler-chain-probe.md`
later measured directly: `129` is the ASR-10 service manual's *odd address
error* code — a 68000 **Address Error exception** (vector 3), a CPU-level
fault from `jmp (A0)` through an uninitialized pointer, not an
application-level branch. And `docs/asr10/investigations/
ready-line-artifact-probe.md` further found the interrupt itself was never
a storage completion at all — a ready-line-change artifact of MAME's FDC
model. This document's regression result and before/after test (the actual
falsifying case) both stand; only the *interpretation* of what the failure
means is corrected here, not silently rewritten below.

## Scope

`docs/asr10/reference/architecture-handoff.md` and
`docs/asr10/reference/storage-completion-dispatch.md` establish that
MC68302 external IRQ1, at this board's fixed `GIMR=0x8040`, supplies
firmware vector `$51` — the shared FDC/SCSI storage-completion entry — and
that the observed `LOADING JM DIGI SYN` stall sits exactly upstream of that
delivery: RECALIBRATE completes on the device side (uPD72069 INTRQ), but no
modeled path carries that completion to the CPU.

This note documents the first concrete implementation attempt: the smallest
possible vertical slice, `FDC INTRQ -> MC68302 external IRQ1 -> IACK ->
vector $51`, mirroring the existing IRQ6/DUART wiring exactly. It was built,
regression-tested, and the board-policy half was disproven and reverted.
The chip-level half was kept.

## What Was Implemented

[Verified static] `mc68302_device::irq1_ack_vector()` added next to the
existing `irq6_ack_vector()` in `src/devices/machine/mc68302.h`:

```cpp
uint8_t irq1_ack_vector() const { return 0x40 | 0x11; }
```

Same formula as `irq6_ack_vector()` (`0x40 | 0x16`), same justification:
`docs/mc68302/vector-origin-map.md`'s External Vectors table gives IRQ1 low
bits `0x11` at this board's fixed `GIMR=0x8040`. External EXRQ levels 1/6/7
bypass the internal INRQ pending/mask/priority machinery entirely — they are
CPU IPL lines asserted directly, not sources that need the (still
`known_unimplemented`) internal interrupt controller. This is verified chip
fact, independent of board wiring, and stays in the tree.

[Verified static] `asr10_boot_state::maincpu_iack_r()` gained a level-1
branch calling `irq1_ack_vector()`, parallel to the existing level-6 branch.
`cpu_space_map()` already had the level-1 IACK map entry from before this
change (`0xfffff3 -> maincpu_iack_r(1)`); it simply fell through to
`autovector(1)` previously. This is inert plumbing until something drives
CPU input line 1.

[Tested and reverted] The board-policy line
`m_fdc->intrq_wr_callback().set_inputline(m_maincpu, 1);`, added next to the
existing `m_fdc->idx_wr_callback().set(m_duart, FUNC(scn2681_device::ip0_w))`
in `asr10_boot_state::asr10_boot()`. This is the same idiom as the DUART's
`irq_cb().set_inputline(m_maincpu, 6)` a few lines above it.

## Result

[Verified dynamic] With the board-policy line in place:

```text
$ ./docs/asr10/regression-test.sh
FAIL boot timeout final_display="ERR0R 129 - REB00T    "
FAIL display boot_timeout final_display="ERR0R 129 - REB00T    "
FAIL button boot_timeout final_display="ERR0R 129 - REB00T    "
FAIL button_upper boot_timeout final_display="ERR0R 129 - REB00T    "
PASS nodisk display="  PLEA5E IN5ERT DI5K  "
FAIL regression failures=4
```

This is not a MAME emulator-side crash or hang — it's a rendered VFD
message, so at first glance it looks like ordinary firmware error handling.
**Corrected, see the notice at the top of this document:** it is actually
a genuine 68000 Address Error exception (vector 3) that firmware's generic
crash handler renders as `ERROR 129`, not an application-level status
check catching a mismatch. The failure happens on plain `boot-to-FILE1`, with no button
pressed and no instrument-load sequence run at all. `nodisk` — the only
other test that never reaches FDC RECALIBRATE — still passes, which
localizes the break to the RECALIBRATE/FDC-completion path rather than to
something broader like reset or the IACK dispatch table itself.

[Verified dynamic] Removing the line restored the previous baseline exactly:

```text
PASS boot display="FILE 1  TUT0RIAL BNK  "
PASS display display="FILE 1  TUT0RIAL BNK  "
PASS button display="FILE 2  JM DIGI 5YN   "
PASS button_upper button=23 rhrb_delta=2
PASS nodisk display="  PLEA5E IN5ERT DI5K  "
PASS regression
```

This before/after pair is the falsifying case rule 6 requires: a concrete,
reproducible run that breaks, and reverts to green on removal of exactly one
line.

## Why: Boot Itself Issues RECALIBRATE

[Verified dynamic] `docs/asr10/lua/archive/irq1_chain_probe.lua` taps FDC
FIFO writes at `$FC4003` for the RECALIBRATE command bytes `07 00` across a
plain boot-to-FILE1 run, with IRQ1 *not* wired (the reverted build), sequenced
with a subsequent instrument-load button press:

```text
IRQ1PROBE_RECALIBRATE t=3.699445  pc=FB8CF2 phase=boot
IRQ1PROBE_RECALIBRATE t=12.106337 pc=FB8CF2 phase=boot
IRQ1PROBE_FILE1        t=16.300000                    boot_recalibrate_events=2
IRQ1PROBE_RECALIBRATE t=18.306365 pc=FB8CF2 phase=instrument_load
IRQ1PROBE_SUMMARY boot_recalibrate=2 instrument_load_recalibrate=1
```

RECALIBRATE runs twice during the OS's own disk-load sequence, at t=3.7s and
t=12.1s — four and twelve seconds before `FILE 1` is even displayed (t=16.3s)
— in addition to the already-known instrument-load RECALIBRATE at t=18.3s.
`docs/asr10/investigations/instrument-load-v350.md` established that the
instrument-load FDC path uses SPECIFY `03 E1 08` (DMA/interrupt-mode, ND=0)
where the boot path uses `03 E1 09` (polled, ND=1) — but MAME's
`upd765_family_device::command_end()` asserts `intrq_wr_callback()` on
RECALIBRATE completion unconditionally, regardless of the ND bit. ND governs
the *data-transfer* phase, not RECALIBRATE/SEEK command completion
signaling.

[DISPROVEN by later measurement] This paragraph originally speculated that
the OS binding table slot `$87CE.w` might not be correctly populated yet
when the interrupt fires during boot, and that this — an uninitialized
*binding table entry* — explained the crash. `irq1-handler-chain-probe.md`
measured this directly and found the opposite: `$87CE.w`/`$FFFF87CE` **is**
correctly populated by this point (the OS is already resident;
`irq1-vector-and-sr-probe.md` observed execution already running in loaded
high-RAM OS code, not ROM, at the moment of interrupt), and the handler
runs correctly all the way through `SENSE INTERRUPT STATUS`. The actual
crash is one step further down: the dispatcher's own async continuation
pointer `$0402` — a *different* variable, populated per-caller immediately
before each command is issued, not once at OS-load time — is never
installed by the boot's own polled FDC path, only by the instrument-load
path's own RECALIBRATE issuer. And `ready-line-artifact-probe.md` found
that the specific interrupt observed here was not a RECALIBRATE completion
at all, but a ready-line-change artifact from an unrelated, ordinary
motor-off command. Kept here for the record, not deleted, per the standing
rule that disproven hypotheses stay visible.

## Disposition

[DISPROVEN] "FDC INTRQ physically drives MC68302 external IRQ1
unconditionally, at all times" is not the board's completion-delivery
contract. Some gating condition — board-level enable, a different physical
source, or a mode/state qualifier not modeled here — must distinguish the
boot-time RECALIBRATEs (where IRQ1 must not fire, or must be ignorable) from
the instrument-load RECALIBRATE (where firmware is waiting for it). This
was not guessed around with FDC-specific or state-dependent logic in the
driver, per the standing rule against encoding firmware-specific knowledge
into board policy.

[Kept] The generic chip-level vector-supply fact
(`mc68302_device::irq1_ack_vector()`) and the level-1 IACK dispatch branch.
Both are inert, verified, and reusable once a correct board policy is
identified.

[Reverted] The direct `intrq_wr_callback().set_inputline(m_maincpu, 1)`
board-policy wiring.

## What This Narrows

- The blocker is not "no vector-$51 path exists at all" — the vector
  formula and IACK plumbing are now in place and verified to compile and
  run without regressing anything when unused.
- The blocker is specifically: what distinguishes an IRQ1 the firmware wants
  (post-OS-load, instrument-load RECALIBRATE) from an IRQ1 the firmware does
  not expect yet (ROM's own boot-time RECALIBRATEs) — same physical
  command, same device-level completion signal, different firmware
  readiness.
- Candidate directions, none implemented or verified here: firmware may
  mask CPU interrupt level 1 via SR during the polled boot path and only
  drop the mask once the OS's async storage state machine is armed; or the
  physical IRQ1 source may not be a bare uPD72069 INTRQ pin at all, but
  something gated by board logic that is only enabled post-boot; or ND
  itself may gate a latch between INTRQ and IRQ1 on real hardware even
  though it does not gate the MAME device's `intrq_wr_callback()` today.
  Distinguishing these is the next falsifiable step, not a continuation of
  this one.

## Verification

- `make SUBTARGET=mame -j4`: clean build, both with and without the
  board-policy line.
- `docs/asr10/regression-test.sh`: 5/5 before, 1/5 (`nodisk` only) with the
  board-policy line, 5/5 after reverting it.
- No `mem_map` changes. No `-log`. No `getenv`. No commits made.

## Line Count

- `src/devices/machine/mc68302.h`: +9 lines (`irq1_ack_vector()` plus
  comment).
- `src/mame/ensoniq/asr10_boot.cpp`: +2 lines net (`maincpu_iack_r()` level-1
  branch; the board-policy line was added then reverted, replaced by a
  7-line comment recording the disproven attempt and pointing here).
- `docs/asr10/lua/irq1_chain_probe.lua`: written, run, moved to
  `docs/asr10/lua/archive/` — its question is answered.
