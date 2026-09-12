# Modeling the memory size: giving the alias probe something to measure

> **Historical 2 MiB milestone.** The model described below was superseded on
> 2026-09-12 by the distinct 16 MiB production backing. Current canonical
> status is `docs/asr10/current-status.md`; this file remains provenance for
> the earlier stock-configuration alias implementation.

Date: 2026-08-23 (fourth follow-up the same day)
Machine: `asr10booth`, V3.50 boot run
Scope: implement the fix for the category confirmed in
`memory-size-belief-analysis.md` — ROM's memory-size alias probe cannot
detect real address-line aliasing because its four test addresses are
modeled as isolated, never-aliasing shadow registers. No stereo, factor
two, clock, bank 1, or ES5510 change.

## Result

**A new, sharper variant of the RAM/decode category, confirmed.** The
mechanism is not "unmapped space returns a plausible value" (a real
unmapped write/read-back fails cleanly, as already measured) — it is that
**the model cannot express the behavior firmware is testing for.** Real
DRAM with fewer address lines than the decoded window folds any address
back into the physically populated size; four addresses modeled as
independent registers can never do that, so ROM's alias probe always
concludes maximum expansion. **Absence of a behavior, not of a value.**

The fix: `asr10_boot_state`'s four probe addresses
(`$008000/$408000/$808000/$C08000`) and the whole `$200000-$EFFFFF` window
now wrap into the same backing store as `$000000-$1FFFFF`, modulo a
2 MB `SYSTEM_RAM_BYTES` constant. Confirmed, live, with a fresh witness:
ROM's own branch table now selects **base `$600000`, size `$200000`** (the
documented stock 2 MB machine) instead of the previous, permanent
`base $000000, size $F80000` (~15.5 MB). The machine still boots and loads
the instrument. Stereo recording no longer hits System Error 57 — it
**succeeds**, at a capacity correctly scaled down to what a real 2 MB
machine can actually hold, matching the owner's manual's own documented
"15.75 seconds of stereo sampling" at the stock configuration. Suite is
green throughout: 10 tests, 11 PASS lines, 12 total lines.

An expanded-configuration test (Del 4's last request) is **not** done this
round — see "What is deferred" below for why, and what it needs.

## Del 1 — Truth table, verified against ROM's own disassembly

Static disassembly of the probe itself, `$F8A166-$F8A244` (256 KB
byte-interleaved `asr10.bin`, hash reconfirmed
`fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf`,
Capstone 5.0.7 in an isolated venv) — not just the previously-known branch
table, the actual instruction sequence:

```text
D0=$0, D1=$1111, D2=$2222, D3=$3333
[$008000].l <- D0   [$408000].l <- D1   [$808000].l <- D2   [$C08000].l <- D3
D4 = [$008000].l     D5 = [$808000].l
cmp D3,D4  -- D4==$3333?
  yes: A0=$600000; size=$200000                          (2 MB)
  no:  cmp D2,D4  -- D4==$2222?
    yes: A0=$000000; size=$800000                         (8 MB)
    no:  cmp D1,D4  -- D4==$1111?
      yes: A0=$600000; cmp D3,D5 -- D5==$3333? size=$400000 (4 MB)
                        cmp D2,D5 -- D5==$2222? size=$A00000 (10 MB)
                        else: ERROR $3F
      no:  cmp D0,D4  -- D4==$0?
        yes: A0=$000000; cmp D3,D5 -- D5==$3333? size=$A00000 (10 MB)
                          cmp D2,D5 -- D5==$2222? size=$F80000 (max)
                          else: ERROR $3F
        no:  ERROR $3F
[$C4E].l = A0                 -- allocator base
[$C4E].l += $10000            -- reserve headroom
[$C62].l -= $10000; -= $8000  -- remaining size after reservation
```

The two-column table the earlier document carried forward is confirmed
exactly, but with one addition the earlier document did not have: **the
allocator base is stored as-is from A0**, and A0 is `$600000` for three of
the six branches, not `$000000`. This is the fact Del 1's truth table
needed and the earlier (informal) version missed.

**Predicted alias-probe reading per documented configuration** (uniform
address-space wraparound modulo the physical size, since `$600000 mod N ==
0` for every power-of-two `N` that divides it, e.g. 2 MB, 4 MB, 8 MB — the
union of writes/reads always collapses correctly under a *uniform* wrap,
not a per-address one):

| Config | Bytes | Naive single-region wrap prediction | Matches which branch | Note |
|---|---:|---|---|---|
| 2 MB (stock) | `$200000` | all four probes alias to one cell, last write ($C08000=$3333) wins → D4=$3333 | Row 1: `base=$600000, size=$200000` | **clean match, implemented** |
| 8 MB (2×4Mx8) | `$800000` | `$808000 mod N = $008000` (aliases with D4), `$C08000 mod N = $408000` (aliases with D1) → D4=$2222 | Row 2: `base=$000000, size=$800000` | clean match, not implemented this round |
| 4 MB (4×1Mx8) | `$400000` | `N` divides evenly into every 4 MB test spacing, same as 2 MB → D4=$3333 | **collides with Row 1**, not Row 3 | **not representable as a single-region wrap** — real hardware's 4 MB config is additively two independently-based banks (2 MB standard + 2 MB expansion), not one contiguous mirrored block; a uniform wrap cannot distinguish it from 2 MB with these four test points |
| 10 MB (mixed) | `$A00000` | not a power of two — a uniform address-space wrap is undefined for it | Row 4 or 5 | **not representable at all** by this technique; genuinely needs an independently-based second bank |
| 16 MB (4×4Mx8) | `$1000000` | no address in the tested range (up to `$C08003`) ever exceeds `N` → no wraparound at all | Row 6: `base=$000000, size=$F80000` | clean match — this is also the **existing, unfixed, default behavior**, coincidentally correct for exactly this one configuration |

Per the task's own instruction ("gör den inte det är sanningstabellen fel,
inte ROM"): the table is incomplete for 4 MB and 10 MB, not ROM. Those two
configurations are additive two-bank designs on real hardware and cannot
be produced by wrapping one contiguous region through a single modulus —
they would need a second, independently-based decoded region. This is a
named, honest gap, not a silent one.

## Del 2 — Design

**As asked, using `ram_device`:** `RAM(config, m_ram).set_default_size
("2M").set_extra_options("2M,8M,16M")` (only the three sizes with a clean,
unambiguous single-region-wrap mapping from Del 1's table) would give the
standard MAME UI/`-ramsize` mechanism for selecting a configuration, with
`m_ram->pointer()`/`m_ram->size()` as the backing store and size.

**Mirroring:** a `ram_device`'s size is resolved at `machine_start()`,
after `mem_map()` has already run — so the classic MAME idiom for a
runtime-variable mirrored region is *not* an address-map `.mirror()`
modifier (which needs a compile-time constant), but either (a) a loop of
`address_space::install_ram()` calls at `size`-aligned offsets across the
decoded window, each pointing at the same buffer, or (b) a custom `.rw()`
handler that computes `address % m_ram->size()` itself and indexes into
the buffer directly. **This task used (b)**, not (a): the existing
`low_rom_or_lowmem_r`/`lowmem_w` handlers already implement the boot-time
ROM/RAM overlay (`cs0_covers(0)`) as custom C++, and folding the wrap
logic into the same style of handler avoids mixing two different memory-
installation mechanisms in the same driver for what is conceptually one
piece of behavior.

**Address range:** the whole `$200000-$EFFFFF` window, not just the four
probe bytes — see "What went wrong the first time" below for why the
narrower version is insufficient. `$000000-$1FFFFF` needed no change:
every address in it is already less than any candidate wrap size, so
wrapping is a no-op there by construction.

**`low_rom_or_lowmem_r`/`lowmem_w` and bank 1:** unaffected in role. The
only change is deleting the `probe_or_alias_region_index()` special case
that diverted `$008000` into an isolated shadow register instead of
ordinary low RAM — the ROM overlay (`cs0_covers(0)`) and the
`m_lowmem_shadow` fallback are otherwise untouched, so ES5506 bank 1's
reuse of these same handlers (`es5506_wavetable_bank1_map`, `$000000-
$07FFFF` word range only) is unaffected: its own map range never reaches
past `$0FFFFE` bytes regardless of what the CPU's wider map now does.

**ES5506 bank 0's `.share(":asr10_sample_ram")`:** unaffected. `$100000-
$1FFFFF` keeps its existing `.ram().share()` declaration unchanged; the
new unified window only reuses the *same* shared pointer
(`required_shared_ptr<u16> m_sample_ram`) from C++ for addresses that wrap
into that range, it does not redeclare or move the share itself.

**8th/9th regression tests:** unaffected — neither exercises any address
this change touches (SCC/IDMA/interrupt-controller paths stay entirely in
`$FC6xxx`/low RAM/ES5506 register space).

## Del 3 — Implementation, in stages, per the replacement rule

### Stage attempt 1 (reverted): four narrow probe windows only

First attempt wrapped only the four probe addresses
(`$008000/$408000/$808000/$C08000`) against a hardcoded 2 MB modulus,
leaving everything else (including `$F00000-$F7FFFF`) unchanged. Built
clean. **Regression: `boot`/`display`/`button`/`button_upper`/
`file_loaded`/`mc68302_guards`/`note_audio`/`interrupt_controller` all
failed with `boot_timeout final_display="LOADING SYSTEM"`; `nodisk` alone
passed.** A follow-up direct run (outside the harness) crashed outright
(SIGSEGV, exit 139) on a plain `boot.lua` run — non-deterministic across
invocations, but real.

**Root cause, found by disassembling the probe itself (Del 1 above):**
`$C4E.l = A0`, and `A0=$600000` for the 2 MB branch — not `$000000`. The
allocator base for a genuine 2 MB machine is `$610000` (after the `+
$10000` reservation), not `$010000`. The narrow fix correctly made ROM
*believe* it had 2 MB, but backed nothing at `$610000` — firmware then
tried to use real memory there and found nothing, corrupting the boot/load
sequence. Confirmed safely, without touching the new C++ at all, via a
Lua-only stimulus against the *original*, known-safe driver: forcing
`$008000`'s readback to `$3333` and observing (not injecting into) the
resulting `$0C4E`/`$0C62` writes reproduced the same `base=$600000`
decision the crash implicated.

**Lesson, matches the project's own established pattern:** a fix scoped to
exactly the bytes a test *reads* is not the same as a fix scoped to
everything a test's *conclusion* subsequently touches. The same shape of
mistake as "a verified base is not a verified limit" (`methods-static-
analysis.md`), one level higher: a verified *decision* is not a verified
*consequence*.

### Stage attempt 2 (kept): unified `$200000-$EFFFFF` wraparound

Widened the wrap to the whole window a firmware decision could reach
(`$200000-$EFFFFF`, folding in the previously-separate `$F00000-$F7FFFF`
plain `.ram()`), still against the hardcoded 2 MB modulus. `$600000 mod
$200000 == 0`, so `$610000` now correctly folds back into the same,
already-backed low-RAM image instead of hitting nothing.

Built clean. Regression run **three consecutive times** (given the first
attempt's non-determinism) — green every time: 9 tests, 10 PASS lines, 11
total lines (the 10th test, `memory_size`, was added afterward — see
Del 5). Edge-set hash reconfirmed byte-identical before and after
(`472373eea0fac895f07d115c8168cc4551957a9a2b1c6f3a1e6909541c5715de`) — this
task never touches the ROM/OS images or `docs/asr10/static/`, so this is a
sanity check that nothing else was accidentally disturbed, not a
functional dependency; **no static-analysis regenerator script exists in
this checkout**, so "regenerated and compared" means exactly this
before/after hash comparison, reported honestly rather than assumed.

## Del 4 — The falsifiable prediction

**Does firmware now report ~2 MB, not 15.5 MB?** Yes, confirmed live with
a fresh witness, at the exact decision-store instructions
(`$F8A1A2`-`$F8A1B0` per the disassembly): `$0C4E.l = $00600000`,
`$0C62.l = $00200000` — base `$600000`, size `$200000`, exactly ROM's own
Row 1.

**Does the machine still boot and load the instrument?** Yes. Full
regression suite green (10/10 after Del 5's addition), including
`file_loaded`'s exact byte-count check (172,544 bytes, 21 arms — unchanged
from before this fix, since instrument loading uses fixed low addresses
independent of the alias-probe's reported capacity).

**Does stereo now fail with an honest memory error instead of Error 57?**
**No — it does better than that.** Driving the full RECORD/start sequence
in stereo mode (Sample-Source Select → cycle to L+R → Level Detect →
lower threshold ×24 → Enter-Yes) reaches **`WAITING...NNN SEC LEFT`**,
state `$0002` — the same state Mono LEFT already reaches — instead of
System Error 57. The heap walk (`$0C5E` to `$0C66`) is coherent, ends
cleanly at the reported heap boundary (`$7F1000`, matching `$C66`
exactly), and a real `UNNAMED WS` WaveSample object is created
(`$70E510`, identity bytes verified byte-for-byte against the known
`UNNAMED WS` string pattern). **Error 57 is gone because the capacity
planner correctly scales its requested split to the honestly-reported 2 MB
heap instead of the previous ~15.5 MB, and a scaled-down stereo recording
genuinely fits.** This matches the owner's manual precisely: a stock 2 MB
ASR-10 is documented to support 15.75 seconds of stereo sampling — not
zero. **`ERROR 57` was never a hardware limitation; it was the model
telling firmware it had eight times more memory than it backed, for
*any* stereo attempt regardless of requested duration.**

**Expanded configuration:** not tested this round. See "What is deferred."

## What is deferred, named explicitly

- **Runtime-selectable RAM size (`ram_device`, Del 2's `set_default_size`/
  `set_extra_options`).** This task hardcodes `SYSTEM_RAM_BYTES = 0x200000`
  in C++. Testing an 8 MB configuration (the next clean branch from Del 1's
  table) is not just a constant change: the current backing
  (`m_lowmem_shadow` + the `asr10_sample_ram` share, 1 MB each, 2 MB total)
  is sized for exactly the stock machine. Widening `SYSTEM_RAM_BYTES`
  without also widening the backing store would index out of bounds —
  exactly the class of mistake that caused the reverted first attempt.
  Given that attempt's demonstrated real cost (a non-deterministic crash),
  this was not attempted casually a second time in the same session.
  Building it properly needs the actual `ram_device` + a single unified
  backing buffer sized to `m_ram->size()`, replacing the fixed-size arrays
  — a distinct, self-contained follow-up task, not a quick edit.
- **4 MB and 10 MB configurations.** Not representable by a single-region
  wrap at all (Del 1). Would need a genuinely separate, independently-based
  second decoded region (an expansion bank), matching real hardware's own
  two-bank architecture — out of scope for this fix.
- **Physical decode of the two-bank architecture** (which address lines
  the real STD/EXP jumper and SIMM-size selection actually control) stays
  `[OPEN hardware]`, as before.

## Del 5 — Coverage and journal

**10th regression test**, `docs/asr10/lua/memory_size.lua`: captures the
first write to `$0C4E`/`$0C62` (reassembled from their two 16-bit tap
halves, so later allocator reservations don't overwrite the check) and
asserts `size=$00200000, base=$00600000`. Fault-injection verified: a
deliberately wrong expected size produced exactly one `FAIL` line, then
was reverted (confirmed byte-identical to the pre-fault file). Suite is
now **10 tests, 11 PASS lines, 12 total lines**.

**New category variant, journaled as asked:** the earlier framing
("frånvarande hårdvara ser giltig ut för firmware") described three
instances that all turned out to share one *deeper* mechanism once this
one was actually traced to its root: **absence of a behavior, not of a
value.** `$FC5020-$FFFFFF`'s catch-all and the unmapped-write
characterization in this task both show a *value* going missing (a write
that should persist doesn't, a read that should reflect real content
returns zero). The alias probe is different in kind: no single read or
write is wrong in isolation — `$008000`, `$408000`, `$808000`, and
`$C08000` each faithfully return exactly what was last written to *that
address*. What's missing is the *relationship* between them: real DRAM
with fewer address lines than the decoded window would make several of
these addresses collapse onto the same physical cell, and no amount of
correctness at any single address can substitute for that collapsing
behavior. A model can get every individual read and write right and still
fail this category, because the category was never about any one value —
it was about whether physically-impossible independence is being modeled
as though it were real hardware.

## Verification

- No `mem_map` change outside the replacement-rule stages described above;
  no stereo, factor-two, clock, bank-1, or ES5510 change.
- No `-log`; all observation via Lua `print()`.
- No fork with an open mandate.
- `es5506.h`/`.cpp`, `es5510.cpp`, `esqpump.cpp`: read, not modified.
- Suite green before and after every stage, checked explicitly: 9/9 before
  this task's changes, 9/9 after the corrected Stage A (checked three
  times), 10/10 after Del 5's new test.
- `docs/asr10/static/call-graph-edges.csv`: byte-identical hash before and
  after (`472373eea0fac895f07d115c8168cc4551957a9a2b1c6f3a1e6909541c5715de`)
  — reported honestly: no regenerator script exists in this checkout, so
  this is a before/after identity check, not a regeneration-and-diff.
- Probes retained as reproducible provenance under `docs/asr10/lua/
  archive/`: `memory-size-probe.lua`, `probe-address-reuse-diagnostic.lua`,
  `stage-a-hang-diagnostic.lua`, `base-relocation-stimulus-probe.lua`,
  `stereo-honest-failure-probe.lua`, `record-stereo-allocator-probe.lua`
  (pre-existing, rerun unmodified).
