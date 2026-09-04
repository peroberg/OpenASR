# IRQ1 Handler Chain Probe

## Scope

Follow-up to `irq1-vector-and-sr-probe.md`. That investigation confirmed
vector `$51` is correctly fetched and the CPU reaches the documented
dispatcher entry — but did not follow what the handler does next, or
whether `ERROR 129` is actually a firmware-detected FDC status mismatch as
previously assumed. `docs/asr10/archive/troubleshoot.md` documents `129` as
the ASR-10 service manual's **"odd address error"** — a 68000 **Address
Error exception** (vector 3), a CPU-level fault, not necessarily an
application-level error path. This investigation tests that directly
instead of continuing to assume either explanation, and follows the
six-step chain as far as it actually goes.

Method only. The naive wiring was reintroduced for this one measurement and
removed again immediately after. Nothing was built.

## Del 1 — The Chain After SIS

`docs/asr10/lua/archive/irq1_handler_chain_probe.lua` logs, from reset
through the crash: FDC FIFO bytes in both directions (not just writes, as
in earlier probes — needed to see SIS/READ-DATA *result* bytes), reads of
the low-RAM async continuation pointer `$0402`
(`storage-completion-dispatch.md`'s `movea.l $0402.w,A0 / jmp (A0)`
mechanism), reads of the low 68000 internal-exception vector table
(`$000008-$00001F`), writes to the IDMA registers, and the IACK event.

### The measured chain, in order

```text
CHAIN_IACK1  t=15.032527 vector=51 target=FFFF87CE pc=FFA2A2
CHAIN_FDC_W  t=15.032592 pc=FB8CF2           value=08         <- SIS issued
CHAIN_FDC_R  t=15.032619 pc=FB8DB8 FC4003    value=C8         <- ST0
CHAIN_FDC_R  t=15.032643 pc=FB8DB8 FC4003    value=00         <- PCN
CHAIN_0402_R t=15.032677 pc=00BAE6 offset=000402 data=00000000  <- movea.l $0402.w,A0
CHAIN_EXC_VECTOR_R t=15.032688 pc=00000C vector=3 offset=00000C target=0000FFF8
```

[Verified dynamic] The handler at `$FF87CE`/`$00BAB6` (high/low-view of the
same code, per the established alias convention) runs, and 65
microseconds after the IACK issues `SENSE INTERRUPT STATUS` (`$08`) exactly
as `storage-completion-dispatch.md`'s FDC branch (`$FB7C5A`) documents.

[Verified dynamic] `ST0=$C8`, `PCN=$00`. `$C8 = 1100_1000`: `IC` (bits 7-6)
`= 11` — abnormal termination, and bit 3 (`NR`, Not Ready) is set. This is
the uPD765-family status for a **spontaneous drive-ready-line-change
interrupt**, not a RECALIBRATE-completion status (`$20`, `IC=00`) and not
this run's last READ DATA's own result-phase status (`$40`/`$80`, `IC=01`
+ End-of-Cylinder — read completely by the boot's own polled code at
`t≈15.009s`, which per standard uPD765 protocol already cleared *that*
command's own interrupt). See Del 2.

[Verified dynamic] 34 microseconds after reading `ST0`/`PCN`, at PC
`$00BAE6`, the handler reads `$0402` and gets **`$00000000`**. `$00BAE6` is
the reported (post-instruction) PC for `movea.l $0402.w,A0` — the
`storage-completion-dispatch.md` `$F114E2` continuation-dispatch
instruction in its low-view alias (`$00BAB6`+`0x2C` = `$00BAE2` start,
+4-byte instruction length = `$00BAE6` reported, matching the project's
established "tap reports PC after the instruction" convention exactly).

[Verified dynamic] 11 microseconds later, the CPU takes a **genuine vector
3 (Address Error) exception**: `PC=$00000C` (fetching the Address Error
vector-table entry itself), target `$0000FFF8`.

**This is the crash.** `jmp (A0)` with `A0=$00000000` jumps to address
`$000000` — itself an even address, so the jump alone doesn't fault — but
`$000000` holds the reset vector's initial-SSP data, not code. Interpreting
those bytes as instructions produces, within roughly a dozen instructions
(11µs), an access at an odd address, which is exactly the documented
`archive/troubleshoot.md` meaning of error `129`. This matches the
evidence precisely: not a firmware application-level "wrong FDC status"
error path, a genuine CPU-level fault from jumping through an
uninitialized pointer.

### Why `$0402` is zero

[Likely] `storage-completion-dispatch.md`'s "FDC RECALIBRATE To SEEK
Example" shows `$0402` is populated (`$0402 <- $BA5E`) immediately before
the *instrument-load* path's own FDC RECALIBRATE issuer sends its command
(`$F11444`/`$F1144A`). The boot's own polled FDC code
(`$FB8CF2`/`$FB8AA8`/etc., the PCs behind every FDC byte in this entire
run) is a structurally different, direct/polled code path that has no
reason to ever touch `$0402` — it doesn't use the interrupt-driven
completion mechanism at all. The generic vector-`$51` dispatcher
(`$F114B6`/`$F114E2`) is shared and unconditional: it always finishes with
`movea.l $0402.w,A0 / jmp (A0)` regardless of *which* state machine's
completion triggered it. When the interrupt fires during boot's unrelated
polled FDC activity — a context that never installed a `$0402` value —
the dispatcher's own precondition (a live caller-installed continuation)
is violated, and it crashes. Not measured directly here (would need to
confirm `$0402` is *always* zero at this point in boot, not just this one
run) but consistent with every other measured fact and requiring no
additional unverified assumption.

### Del 1 Answer

**2 of 6 steps reached**, chain breaks between step 2 and step 3:

```text
1. vector $51 delivered                    REACHED
2. $F114B6/$FF87CE handler runs, SIS       REACHED (SIS issued, ST0=$C8 read)
3. SEEK 0F 00 01                           NOT REACHED
4. READ DATA $46 (as part of this chain)   NOT REACHED
5. IDMA registers programmed for transfer  NOT REACHED
6. vector $4B (IDMA completion)            NOT REACHED
```

Two writes to `$FC6803` were observed near the IACK (`t=15.022171`,
`t=15.032542`, both `PC=$FB7F3C`) — these are **not** step 5. `$FB7F0A`,
documented in `storage-completion-dispatch.md` as dispatcher B's *shared
prelude* (`F114C0 jsr $FFFB7F0A`, called unconditionally before the
FDC-vs-SCSI branch decision, reading/acknowledging IDMA CSR bits for
unrelated bookkeeping), is a few instructions before `$FB7F3C` — this is
that same shared-prelude touch, running before the FDC branch is even
selected, not IDMA transfer setup for a READ that never got issued.

**The user's IDMA hypothesis and the earlier SR-mask-gate framing are both
superseded by direct measurement, not merely disproven:** the chain never
gets far enough to need IDMA (steps 3-6 are never reached), and the crash
has nothing to do with *when* the interrupt was delivered — the interrupt
was delivered correctly and the handler responded correctly through step
2. The failure is structural: an unconditional dispatcher dereferencing a
pointer only one specific caller ever initializes.

## Del 2 — Which Completion Was Actually Pending

Not measured via INTRQ line edges — no Lua-visible state exists for the
CPU's IPL input pins (checked: `m68000_device`'s `state_add()` calls
register only `D0-D7`/`A0-A7`/`SR`/`PC`/`IR`/`USP`/`SP`, no input-line
state; `manager.machine.debugger` needed for `bpset`/`wpset` requires
`-debug`, which is out of scope here — see "What This Doesn't Answer"
below). Answered instead by the `ST0` byte itself, which is a more direct
answer than a bare timestamp would have been: firmware's own diagnosis of
*what* was pending, read via the exact mechanism firmware uses to identify
it.

**Del 2 answer:** `ST0=$C8` (`IC=11`, `NR` bit set) is the uPD765-family
signature for a **spontaneous drive-ready-line-change interrupt** — not
tied to any specific command's completion. This **updates**
`irq1-vector-and-sr-probe.md`'s `[Likely]` guess ("more plausibly traces to
a late SEEK"): that guess was inference from timing gaps alone;
`ST0=$C8` is direct evidence and takes priority. Neither the original
`t≈3.7s`/`t≈12.1s` RECALIBRATEs nor the last READ DATA (whose own 7-byte
result phase — `$40 80 00 00 00 06 02`, `ST0 IC=01`+End-of-Cylinder — was
read completely by the boot's own polled code at `t≈15.009s`, clearing
that command's own interrupt per standard uPD765 protocol) is the
source. `[OPEN]` in the prior investigation is now `[Verified dynamic,
narrower scope]`: not resolved to a specific *command*, but resolved to a
specific *event category* (ready-line change), directly evidenced rather
than inferred.

[OPEN] Whether this ready-line-change event reflects genuine emulated
floppy hardware behavior (motor/index-adjacent) or is itself an artifact
of running the boot's own FDC command sequence under an interrupt line
that was never meant to be connected. Not resolved here — the `ST0` value
is a fact about what the device reported when asked, not a claim about the
device's own correctness.

## What This Doesn't Answer

- The exact instruction-level path from `$00000C`'s fetch to whichever
  specific odd-address access completes the Address Error — not traced
  past the vector-table fetch itself. Not needed for Del 1's question, but
  a gap for anyone who wants the literal faulting instruction.
- True INTRQ assert/deassert edge timestamps — inferred from `ST0` and
  FIFO sequencing, not read directly off the line. `bpset`/`wpset` via
  `manager.machine.debugger` could get this directly, but requires
  `-debug`, out of scope for this headless investigation per project rules.

## Verification

- `make SUBTARGET=mame -j4`: clean, both with the temporary wiring and
  after reverting it.
- `docs/asr10/regression-test.sh`: 5/5 before, 5/5 after. Temporary wiring
  was in place only during this one measurement run.
- No `mem_map` changes. No `-log`. No `getenv`. No commits made. No IDMA
  implementation, no interrupt controller — reported and stopped, per
  instruction.
- `git diff --check`: clean.

## Line Count

- C++: net zero (board-policy line reintroduced and removed again within
  this session).
- `docs/asr10/lua/archive/irq1_handler_chain_probe.lua`: written, run,
  archived — its question is answered.
