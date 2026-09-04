# How much memory does firmware believe it has?

Date: 2026-08-23 (third follow-up the same day, after the interrupt controller
landing)
Machine: `asr10booth`, V3.50 boot run
Scope: locate firmware's own memory-size decision and determine whether it is
internally consistent with the current model's actual RAM backing. No code
change, no `mem_map` change, stereo/factor-two/clocks/bank 1/ES5510 untouched.

## Result

**The hypothesis holds.** Firmware's memory-size decision concludes the
machine has **`$00F80000` bytes (≈15.5 MB) of contiguous RAM from `$000000`**
— the maximum entry in ROM's own branch table, corresponding to a fully
expanded 16 MB machine. This is measured, not inferred: the decision is
stored live at longword `$0C62` and reconfirmed with a fresh witness this
session. `$7CE510` (≈8.18 MB) sits well inside that believed range, so the
stereo allocator's split at that address is not a wrong address — it is a
correct computation over a heap firmware genuinely believes is that large.

The root cause is not "unmapped space returns a plausible value." A direct
write/read-back test against genuinely unmapped space (`$500000`) fails
correctly in the current model — write `$BEEF`, read back `$0000`. The false
belief instead comes from a **different, earlier test that cannot fail in
this model by construction**: a four-address RAM-aliasing probe whose four
probe locations are modeled as fully independent registers, so they can
never show the address-line aliasing that would reveal a smaller real RAM
size. The model cannot represent anything other than "fully expanded"
through this path, regardless of what `mem_map` actually backs.

## 1. The real memory-size decision: ROM `$F8A166-$F8A244`

Already identified in `stereo-ram-decode-analysis.md`; reconfirmed live this
session with a fresh witness and exact PCs (`docs/asr10/lua/archive/
memory-size-probe.lua`):

```text
t=0.001787  PC=F8A180  WRITE $008000 <- $00000000
t=0.001789  PC=F8A18C  WRITE $808000 <- $00002222
t=0.001792  PC=F8A198  READ  $008000 -> $00000000   (D4)
t=0.001793  PC=F8A19E  READ  $808000 -> $00002222   (D5)
t=0.001801  PC=F8A218  WRITE $0C62.l <- $00F80000   (selected SIZE)
t=0.001803  PC=F8A22C  WRITE $0C4E.l <- $00000000   (selected BASE)
```

(`$408000<-$1111` and `$C08000<-$3333` are the same routine's other two
signature writes, already independently confirmed unchanged at end-of-run in
this session's own segment sweep — see Del 2 below.)

Per the already-established branch table (`stereo-ram-decode-analysis.md`),
`D4=$0000, D5=$2222` selects **base `$000000`, size `$00F80000`** — the
largest of six branches, corresponding to a fully expanded 16 MB machine
(`$1000000` total address space minus the `$080000` reserved for the ROM
alias window at `$F80000-$FBFFFF` = `$F80000` of usable RAM). That arithmetic
is not a model bug — it is the *correct* address budget for a real,
maximally-expanded ASR-10 whose ROM/peripheral space sits at the top of the
map. **The bug is that the model's alias probe can never select any other
branch**, regardless of which real configuration `mem_map` is meant to
represent (see Del 2).

`$0C62.l` is not a static "answer" cell — it behaves as a running free-size
counter as ROM immediately carves off working allocations. Read back after
`FILE 1` plus a 2s settle (well after several more reservations, including
the instrument load), it had already shrunk to `$00F47000`, consistent with
`stereo-ram-decode-analysis.md`'s independently-derived "initial allocator
size `$F67C00`" figure (`$F80000` minus the `$10000`/`$8000`/`$400`
reservations ROM makes immediately after the decision). This is expected
allocator behavior, not a discrepancy — the decision itself is captured at
`$F8A218`/`$F8A22C`, not by reading `$0C62` long after boot.

**Upper bound firmware believes: ≈15.5 MB, vastly more than the ≈8.18 MB
`$7CE510`.**

## 2. Is the `$100000-$1FFFFF` pattern sweep the same mechanism? No.

Disassembled live (ROM `$F87CE0-$F87E20`, file offset `$7CE0-$7E20` of the
byte-interleaved `asr10.bin`, hi-then-lo order, hash reconfirmed
`fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf`; Capstone
5.0.7 in an isolated venv, per this project's established RAM-code
disassembly method):

```text
F87CEE-F87D04  write $5555 to $100000, read back, compare -- mismatch -> error path
F87D52-F87D68  write $AAAA to $100000, read back, compare -- mismatch -> error path
F87DB8-F87DE8  unconditional tag-fill loop: a0 starts at $000200, writes a
               constant word (starting at 0, incrementing by 1 roughly every
               512 bytes) to every word from $000200 up to a HARDCODED
               `cmpa.l #$1fffff, a0` bound -- no per-block readback check
F87DEA-F87DF8  re-reads the very first tag word at $000200 as a final sanity
               check
```

This is **not** a capacity-detection routine. It is a two-value walking-bit
sanity check confirming *some* RAM exists at `$100000` (the first expansion
megabyte in the modeled base configuration), gated by a hardcoded upper
bound (`$1FFFFF`) that is completely independent of the `$00F80000` decision
made minutes earlier by the alias probe. It never asks "how much RAM is
there" — it assumes a fixed answer and paints tags across it. The previously
measured 524,290 writes to `$100000-$1FFFFF` are the upper half of this same
sweep; the lower half (`$000200-$0FFFFF`) lands inside the already-attributed
general low-RAM write traffic and was not separately counted before.

**Fresh live confirmation this session, segmented above `$1FFFFF`:**

```text
seg_200000 ($200000-$3FFFFF): 0 writes
seg_400000 ($400000-$7FFFFF): 4 writes, all at $408000-$408002 (the alias probe itself)
seg_800000 ($800000-$BFFFFF): 4 writes, all at $808000-$808002 (the alias probe itself)
seg_c00000 ($C00000-$EFFFFF): 4 writes, all at $C08000-$C08002 (the alias probe itself)
witness ($000000-$0FFFFF): 1,005,663 writes -- tap alive for the whole window (SS8.7)
```

No boot-time code ever attempts a general write anywhere in `$200000-$EFFFFF`
except the four narrow alias-probe addresses. The pattern sweep and the
capacity decision are two independent mechanisms; only the capacity decision
(Del 1) is responsible for the false belief.

## 3. What would a write/read-back test find against real unmapped space?

Direct characterization, this session, no firmware involved:

```text
addr=$500000  before=$0000  write($BEEF)  after=$0000
```

Unmapped space in the current model discards writes and reads back `$0000`
unconditionally. **A write-then-read-back test, like the one firmware
actually runs at `$100000`, would correctly detect "no RAM here" if firmware
ever ran it against `$500000`.** It never gets the chance to: the only
mechanism that ever concludes anything about capacity beyond the fixed
`$100000-$1FFFFF` sanity check is the alias probe (Del 1), which does not
use read-after-write-same-address verification at all — it compares two
*different* addresses against each other to detect real hardware's address-
line wraparound (aliasing), a different (and, for boot-time sizing across up
to 16 MB, more plausible) technique than a byte-by-byte sweep.

## 4. Does the hypothesis hold?

**Yes.** `$00F80000` (≈15.5 MB) is what firmware believes. `$7CE510`
(≈8.18 MB) sits well inside it. The stereo allocator's failure is not a
decode bug at one address — it is the direct, correct consequence of an
allocator operating over a heap size firmware was handed by a broken
capacity test.

## 5. What is the right model?

**Real ASR-10 configurations** (`ASR10_manual.pdf`, "About Memory"):

- Stock, out of the box: **2 MB** (1 Megaword), two standard 1Mx8 SIMMs.
- Expandable to five configurations via standard + expansion SIMM slots and
  a STD/EXP jumper: **2, 4, 8, 10, or 16 MB**, using 1Mx8 or 4Mx8 non-parity
  SIMMs. Addressable ceiling: 16 MB / 8 Megawords.
- No documented configuration is exactly 8.18 MB; `$7CE510` falls between
  the 8 MB and 10 MB configurations, which is what a firmware allocator
  computing a real remainder split inside a mis-sized 15.5 MB heap would be
  expected to produce — not evidence of a sixth hardware configuration.

**What this driver should represent:** the current `mem_map`
(`$000000-$0FFFFF` + `$100000-$1FFFFF` = 2 MB, matching the ROM's own
`$100000` sanity check exactly) already models the **stock, out-of-the-box
2 MB machine** — a deliberate, correct baseline choice, not an oversight.
The bug is entirely on the firmware-belief side: the alias probe should be
able to conclude "2 MB" for this configuration and currently cannot conclude
anything except "16 MB-class machine," because its four probe addresses
(`$008000/$408000/$808000/$C08000`, `asr10_boot_state::probe_or_alias_
region_*_r/w`) are modeled as isolated, always-independent shadow registers.
Real hardware with 2 MB of physical RAM and non-existent higher address
lines would show all four of these test points folding back onto the same
2 MB of physical RAM (address-line wraparound); ROM's own branch table
already has an entry for exactly that outcome
(`D4=$3333 -> base=$600000, size=$200000`, per `stereo-ram-decode-
analysis.md`'s table) — the model simply has no path to ever reach it,
because nothing decodes the four probe addresses as aliases of the same
backing store or of each other. This is the shape of the general fix a
future implementation task should take: make the probe/alias region behave
as a real decode consistent with whichever configuration `mem_map`
represents (2 MB here), not four independent registers — a decode model,
not a stereo patch, not four bytes of special-casing at `$7CE510`.

**Other symptoms a wrong memory size would predict.** No other documented
finding in this project currently matches this category (checked against
`current-status.md` and all `investigations/*.md` for other large-allocation
or "System Error"-class failures; none found). The only measured instance is
the stereo split's System Error 57. **Predicted, not measured:** any legitimate
allocation requiring more than the real ~2 MB backing — a large multitimbral
instrument, a long mono sample near the advertised ceiling, or sequencer data
past the real backed range — should fail the same way, for the same reason.
Not tested this task; a natural follow-up regression case for whichever
implementation task fixes the decode.

## Verification

- No `mem_map`, executable code, clock, bank, or ES5510 change.
- No `-log`; all observation via Lua `print()`.
- No fork with an open mandate.
- Witness requirement (SS8.7) satisfied: `$000000-$0FFFFF` witness tap fired
  1,005,663 times across the same measurement window as the zero-result
  segments above `$1FFFFF`.
- ROM merge (hi-then-lo byte interleave) reconfirmed against the handoff's
  own recorded hash before disassembly, so the disassembled bytes are known
  to be the exact ROM the live run executed.
- `docs/asr10/regression-test.sh`: 9 tests, 10 PASS lines, 11 total lines,
  unaffected (documentation- and measurement-only task).
- Probe retained as reproducible provenance:
  `docs/asr10/lua/archive/memory-size-probe.lua`.
