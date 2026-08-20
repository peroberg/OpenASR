# MC68302 IDMA Implementation Plan

## Known Limitations — Not The Model, The Current Slice

Follow-up (`disk-not-responding-probe.md`) found the `DISK ERROR - LOST
DATA` fix real but incomplete in ways worth stating plainly before anyone
mistakes this slice for a general IDMA implementation:

1. **Direction-locked, FDC→memory only.** `SAPR` is stored and reads back
   correctly, but is never dereferenced through the CPU address space —
   the board-side caller always supplies the transferred byte directly
   from `m_fdc->dma_r()` and always writes to the `DAPR`-derived
   destination. ASR-10 writes to disk as well as reading it; a WRITE
   DATA-shaped transfer (memory→FDC) is not implemented and will fail the
   first time firmware attempts to save, not just underperform.
2. **`BCR-1` has no known register semantics behind it** — a choice that
   produces the correct byte count for this one single-sector (`N=2`)
   request, not a decoded hardware convention. Must be re-examined the
   first time a multi-sector transfer or a different `N` is observed; it
   may not generalize.
3. **`$FC5803` (SAPR's measured value) is inside CS3
   (`$FC4000-$FC5FFF`, confirmed in `hardware-map.md`/`memory-map.md`),
   but `mem_map` decodes `$FC5020` and above as a generic `.ram()`
   catch-all**, not as a distinct register within that chip-select. A
   defect in the memory map, not in this implementation — which
   deliberately routes around it (limitation 1's flip side) rather than
   depending on it. `mem_map` intentionally not touched; revisit when the
   CS3 E2 question is otherwise addressed.
4. **`DAPR` *is* honored, not hardcoded** — confirmed by re-reading
   `internal_w()`'s `OFFSET_IDMA_CMR` case: `m_idma_dest = m_idma_dapr;`
   at arm time seeds the working pointer from the actual programmed value,
   which then increments per byte. No hardcoded-destination limitation
   applies.

## Status

Implemented (minimal slice) and measured. Builds on
`idma-register-map-probe.md`'s measured register map and
`ready-line-artifact-probe.md`'s working vector-`$51` completion chain.
Six open items were resolved before writing code (below); one of them
(item 1/2) corrects a bit-arithmetic error in an earlier draft of this
document that reached the opposite conclusion from the measurement.

## Retroactive Correction — Bit Arithmetic

An earlier draft of this document claimed *"bit 11 (`$0800`) is set [in
`IMR=$E480`], so IDMA is unmasked."* This is wrong:

```text
$E480 = 1110 0100 1000 0000
$0800 = 0000 1000 0000 0000  (bit 11)
$E480 AND $0800 = $0000      -- bit 11 is clear

$0400 = 0000 0100 0000 0000  (bit 10)
$E480 AND $0400 = $0400      -- bit 10 is set
```

`$E480 = $C080 | $2400` exactly: `$C080` is the ROM's PB9/10/11 unmask
(bits 15, 14, 7) and `$2400` is the OS's SCC1+SCC2 unmask (bits 13, 10) —
already-documented facts (`current-status.md`'s disproven-hypotheses
section, `mc68302-status.md`). Bit 10 (SCC2) is what's set in that nibble,
not bit 11 (IDMA). **IDMA's completion source is masked**, not unmasked, at
every point this was measured. This is corrected in place, not silently
fixed, per the standing rule that a wrong conclusion stays visible with
what replaced it.

## Six Items Resolved Before Writing Code

### 1–2. Is IDMA completion (vector `$4B`) masked?

**Yes, measured, at every one of the four observed vector-`$51` IACKs**
(`t=18.306451`, `18.308701`, `18.311205`, `18.436935`, all reading
`IMR=$E480` via the shared dispatcher-B prelude). No later write to
`$FC6816` (IMR) sets bit 11 anywhere in the observed window (`FILE 1`
through the READ DATA result and beyond). Coverage: `FILE 1` onward only
(same tap-installation-timing constraint as `irq1-imr-unmask-probe.md`) —
a pre-`FILE 1` unmask-then-remask cannot be ruled out, but is not needed to
answer the practical question below.

**Practical answer, also measured, not inferred from the mask alone:** all
four completions actually observed for this request — RECALIBRATE, two
SEEKs, and READ DATA's own result — arrived via the FDC's INTRQ through
the already-working external-IRQ1/vector-`$51` path
(`ready-line-artifact-probe.md`), not vector `$4B`. Vector `$4B` delivery
was not implemented. Nothing in the measured chain needed it.

### 3. Does firmware read CSR?

**Yes — read, not merely inferred from absence of writes.** The shared
dispatcher-B prelude explicitly executes `move.b $FFFC680E.l,D0` at every
one of the four IACKs (`PC=$FB7F12`, mask=`$FF00`), returning `$00` every
time. This is not a free-running poll loop, though: `$FC680E` is read
exactly 4 times in the whole captured run, each time as a side effect of
already being in the vector-`$51` handler (triggered by external IRQ1),
never on its own. Firmware does not poll CSR waiting for IDMA completion
independent of the `$51` path.

**This settles the design question the task named as decisive.** Since
(a) vector `$4B` is masked and (b) the CSR read is incidental bookkeeping
inside a handler already reached by a different mechanism, implementing
vector `$4B`/`IPR`/`ISR` delivery would build machinery nothing in this
flow consumes. The implementation below does not deliver vector `$4B`.
`CSR` bit 0 (DONE) is still set on completion, for manual-consistency and
in case a future flow does check it, but nothing currently depends on it.

### 4. What is `$FC5803`?

`hardware-map.md`/`memory-map.md`: CS3 spans `$FC4000-$FC5FFF` and already
hosts the FDC (`$FC4000`), DUART (`$FC4801`), and SCSI (`$FC5001`/`$FC5003`)
— `$FC5803` falls inside that *same* documented chip-select, not a second
one. The odd address matches this project's established peripheral
byte-lane signature (FDC `$FC4001`/`$FC4003`, DUART `$FC4801`). Supports
the hypothesis: a second FDC data-port decode within CS3 that would assert
DACK, distinct from the CPU's own `$FC4003` FIFO port. No direct read/write
to `$FC58xx` by firmware was found elsewhere (only the one SAPR programming
instance).

**`mem_map` was not changed**, per instruction. Current mapping:
`map(0xfc5020, 0xffffff).ram();` — a generic catch-all that treats
`$FC5020-$FC5FFF` (still inside CS3) the same as genuinely-undecoded bus
beyond `$FC6000`. **What would need to change, if this hypothesis is
pursued**: decode `$FC5803` as a distinct FDC-DMA-data alias within CS3,
separate from the plain-RAM catch-all — not attempted here. The current
implementation sidesteps this entirely (see "Implementation" below): it
does not dereference SAPR through the CPU address space at all.

### 5. Why 513 bytes (`BCR=$0201`)?

Not resolved to a single manual-cited convention — no bit-level `CMR`/`BCR`
table exists in the locally available `docs/mc68302/` material (checked;
`idma-spec.md` names `STR`/`INTN`/`INTE` in prose only, no bit positions).
Resolved instead by a correctness requirement found in `upd765.cpp` itself:
`command[4]==command[6]` (`R==EOT`, true immediately for this single-sector
`R=8`/`EOT=8` command) is guarded by `if(!tc_done)` — **terminal count is
required to complete this command cleanly regardless of BCR's exact
encoding**, and the FDC only ever raises exactly 512 DRQs for a 512-byte
(`N=2`) sector, independent of what the CPU-side counter says. Implemented
as "transfer `(BCR-1)` bytes" (512 from the measured 513) — the smallest,
least speculative correction that produces the byte count the command
itself requires, not a units/scaling reinterpretation of BCR's raw bytes.
Flagged in code as unverified beyond this one measurement.

### 6. CMR field decode

Only one bit identified with clean, repeatable measurement support: **bit
0 correlates exactly with "is this the transfer-starting write"** — clear
(`$0002`) in the shared prelude write that recurs identically at every
vector-`$51` IACK regardless of transfer state, set (`$0D51`, `...0001`)
only in the write immediately preceding READ DATA. Used as the arm/`STR`
trigger. Source/destination increment fields are **not** decoded from CMR
at all — hardwired instead in the implementation (source fixed, since it's
a peripheral register; destination incrementing, since it's a RAM buffer),
because guessing the wrong CMR bits for that would silently produce wrong
behavior (reading the same byte 513 times, or writing past the buffer)
while *not* decoding them and hardwiring the functionally-necessary
behavior cannot get it backwards.

## Implementation

`mc68302_device` (`mc68302.h`/`.cpp`): real storage for `CMR`/`SAPR`/`DAPR`/
`BCR`/`CSR`/`FCR` (`$FC6802-$FC6811`, previously `known_unimplemented`
shadow). `idma_channel_active()` and `idma_transfer_in(uint8_t)`: the
latter writes to the current `DAPR`-derived destination in program space,
increments it, decrements the working count, sets `CSR` bit 0 and returns
`true` on the last byte. Does **not** dereference `SAPR`'s address (item 4)
— the caller supplies the byte directly, sidestepping the unresolved
`$FC5803` decode entirely rather than reading real RAM through it.

`asr10_boot.cpp`: `idma_drq_w(int state)` wired to
`m_fdc->drq_wr_callback()`. On assertion: if `idma_channel_active()`, pull
one byte via `m_fdc->dma_r()` (not the CPU-visible FIFO port), pass to
`idma_transfer_in()`; on completion, `m_fdc->tc_w(true)` then `tc_w(false)`.

### A Real Bug, Caught By Regression, Not Guessed Around

First version omitted the `idma_channel_active()` guard, calling
`m_fdc->dma_r()` unconditionally on every DRQ assertion.
`upd765_family_device::enable_transfer()` has no `else` between its PIO
branch and its DMA branch:

```cpp
void upd765_family_device::enable_transfer()
{
    if (spec & SPEC_ND) { if(!internal_drq) { internal_drq = true; check_irq(); } }
    // DMA -- unconditional, no else above
    if (!drq) set_drq(true);
}
```

`drq_cb` therefore fires on **every** transfer, PIO included — the boot's
own unrelated polled READ DATA operations too. The unguarded handler
silently popped bytes out of the FDC's FIFO during those, desyncing the
CPU's own `fifo_r()` reads of the same data. Result: regression fell from
5/5 to 1/5, plain `boot` failing with `PLEASE INSERT DISK` even with media
mounted — the pre-IP0-fix failure signature, reappearing for an unrelated
reason. **Reverted immediately** (`git checkout` on the three changed
files, rebuilt, confirmed 5/5 restored) before re-diagnosing — not patched
on a red tree. Root-caused against `upd765.cpp`'s actual source before the
second attempt, which added the guard and passed 5/5 on the first retry.

## Measured Result

`docs/asr10/lua/archive/idma_result_check.lua`, full instrument-load
sequence against the implementation above:

```text
IDMARESULT_FILE1    t=16.300000
IDMARESULT_DISPLAY  t=17.790000 text="L0ADING JM DIGI 5YN   "
IDMARESULT_DISPLAY  t=23.390000 text="DI5K N0T RE5P0NDING   "
```

**`DISK ERROR - LOST DATA` is gone.** The targeted uPD765 overrun does not
recur — the transfer completes and terminal count is delivered correctly.
The instrument-load sequence proceeds substantially further (from
`t=18.3s`, where it previously stopped, to `t=23.4s`) before hitting a
**different** firmware error, `DISK NOT RESPONDING`. Reported exactly as
observed, not diagnosed: what specifically times out or fails between
`t=18.4s` and `t=23.4s` is not investigated here — new, later-stage work,
out of scope for this task.

## Relation To The Working `$51` Path

No interaction at the register/dispatch level, confirmed by measurement
rather than merely asserted: `$51` (RECALIBRATE, SEEK, READ DATA issuance
and its own completion) and the IDMA transfer (this implementation) are
independent — the four observed completions for this request all went
through `$51`, none through `$4B`. Vector `$4B`/`storage-completion-
dispatch.md`'s Dispatcher A (`$F01B1A`) remains statically identified,
never observed to run, and is not implemented here — item 2/3 established
that nothing in this flow needs it.

## Measured vs. Assumed — Final Separation

**Measured:**
- All six register offsets and write values/order for this one request.
- `IMR` bit 11 clear (IDMA masked) at all four observed IACKs; bit 10
  (SCC2) is what's actually set in that nibble.
- `CSR` is read (not just unwritten) at every IACK, as incidental
  prelude bookkeeping, never in a dedicated poll loop.
- `CMR` bit 0 correlates exactly with transfer-start across all five
  observed CMR writes (1 prelude-only + 1 start, ×4 IACKs minus overlap —
  see `idma-register-map-probe.md` for the raw sequence).
- Implemented behavior's measured result: overrun gone, chain reaches
  `t=23.4s` before a different, later failure.

**Assumed / still not measured:**
- CMR's remaining bits (direction, size, interrupt-enable fields) —
  hardwired functionally instead of decoded.
- BCR's `(raw-1)` interpretation generalizes beyond this one 512-byte,
  single-sector request.
- FCR's bit-field meaning (`$99`).
- Whether SAPR/DAPR auto-increment on real hardware, or whether this is
  purely a firmware/driver-side convention.
- The physical/logical path from `SAPR=$FC5803` to real FDC data on real
  hardware (this implementation avoids needing to know, item 4).
- What specifically produces `DISK NOT RESPONDING` after `t=18.4s`.

## Verification

- `make SUBTARGET=mame -j4`: clean, both the broken first attempt and the
  corrected second attempt.
- `docs/asr10/regression-test.sh`: 5/5 before, 1/5 (first attempt, reverted
  immediately per the standing rule against implementing further on a red
  tree), 5/5 after the corrected attempt.
- No `mem_map` change (item 4's fix is reported, not made). No `-log`. No
  `getenv`.
- `git diff --check`: clean.
