# MC68302 Consolidation: Making The Silent Loud

## Scope

IDMA, sample-RAM sharing, and the interrupt path were all landed by
getting one configuration to work and hardcoding it. The risk: building
the keyboard on top means the next real bug surfaces in the keyboard
layer with its actual cause one layer down, in silently-assumed MC68302
behavior. This task adds no functionality. It makes every place the
model currently guesses, is missing, or stays silent say so instead —
entirely in Lua (observation), with zero new environment flags or
`printf` sites in the driver, per the standing rule this project has
followed since the 6917→3583-line cleanup.

## Del 1 — Exception Vector Inventory And Guard

### First attempt: wrong, and worth recording why

The obvious technique — tap PROGRAM SPACE reads over the whole
`$000000-$0000FF` vector table, since a plain MC68000 (no VBR) always
fetches a handler address via a longword read at `vector*4` — was already
validated once by `irq1-handler-chain-probe.md`, which used it
successfully to identify a genuine Address Error. Applied here over a
*full* clean boot + load (not a narrow window around one known crash),
it produced obvious nonsense:

```text
EVI_VECTOR n=49(0x31) count=197093
EVI_VECTOR n=50(0x32) count=1167945
EVI_VECTOR n=39(0x27) count=20760
```

Over one million "vector fetches" in ~22 seconds, and a uniform baseline
of exactly 6 reads across many unrelated, architecturally-unrelated
vector numbers (2,3,6,7,8,9,12,13,...). **This is not exception
activity** — vector 50 has no defined 68000 exception meaning at all, and
no real interrupt/trap source in this system fires anywhere near
50,000/second. It is proof firmware reuses part of that address range for
ordinary data (most likely a periodic low-RAM sweep), and that a blanket
address-range read tap cannot distinguish "the CPU took an exception"
from "ordinary code read this address for an unrelated reason."

Checked for a better API before abandoning the technique: MAME's
debugger has a real, dedicated exception-point mechanism
(`device_debug::exceptionpoint_set`, `src/emu/debug/debugcpu.h:120`,
backing the `epset` console command), called from
`debugger_exception_hook(vector)` at the exact moment of every genuine
CPU exception (`src/devices/cpu/m68000/m68kcpu.h:1164` and dozens of call
sites in `m68000-sdp.cpp`). **It has no Lua binding** — grepped
`luaengine_debug.cpp`: only `bpset`/`wpset` are exposed, not
`exceptionpoint_set`. TRAP/internal-CPU-exception inventory is therefore
**`[OPEN]`** — not guarded, because guarding on a heuristic already proven
to produce six-figure false positives would be worse than not guarding
at all.

### What is reliable: `cpu_space` IACK taps

This project's own completion architecture (`$51` for FDC/SCSI, IRQ6 for
the DUART) is already built entirely on `cpu_space` IACK taps, proven
correct throughout this whole project. Re-used here, after fixing one
more offset-convention bug: `cpu_space` reports the **even**,
word-aligned IACK address (`irq1_vector_probe.lua`'s own calibration
comment: level 1 as `$FFFFF2`), not the odd byte address
`cpu_space_map()` declares in C++ (`$FFFFF3`) — a first draft added a
spurious `+1` and got zero hits on every level as a direct result before
this was caught.

```text
EVI_LEVEL level=1 first_t=17.806435 count=33
EVI_LEVEL_VECTOR level=1 vector=0x51 count=33
EVI_LEVEL level=6 first_t=15.032440 count=7528
EVI_LEVEL_VECTOR level=6 vector=0x56 count=7528
EVI_LEVEL level=2/3/4/5/7 never_fired
```

- **[Verified]** Exactly two `(level, vector)` pairs ever fire during a
  clean boot + load: level 1 → `$51`, level 6 → `$56`. Levels 2, 3, 4, 5,
  7 never fire at all.
- **Guard** (`install_exception_guard`, `docs/asr10/lua/lib/
  asr10_guards.lua`): alarms (aggregated, first-occurrence-per-pair) on
  any `(level, vector)` outside this allowlist. Fault-injection tested:
  a scratch copy with a deliberately wrong level-6 allowlist entry
  produced exactly **one** aggregated alarm line despite the real vector
  firing 7,528 times, and the test correctly failed.

## Del 2 — SIB Coverage

`docs/asr10/lua/archive/sib-coverage-inventory.lua` inventories every
byte-offset in `$FC6000-$FC6FFF` firmware touches, classified against
`mc68302_device`'s own `classify_offset()`/`classify_full()`
(`mc68302.cpp:242-273`), ported into Lua by reading that source, not
re-derived.

### Tap lifetime: measured, not assumed

BAR (`$0000F2`, fixed, non-relocatable — outside the SIB window it
controls) was measured directly rather than assumed to settle at the
previously-cited "~5.4s":

```text
SCI_BAR_WRITE i=1 t=0.000004 data=00000FC6 mask=0000FFFF   -- full word, final value already
SCI_BAR_WRITE i=2 t=5.395043 data=00000F0F mask=0000FF00   -- high byte, redundant reconfirm
SCI_BAR_WRITE i=3 t=5.395061 data=0000C6C6 mask=000000FF   -- low byte, redundant reconfirm
```

`(0x0FC6 & 0xFFF) << 12 = $FC6000` — confirmed, not assumed. BAR reaches
its final value in one clean word write essentially immediately
(`t=0.000004`); the two later writes at `t≈5.395` reconfirm the *same*
value byte-by-byte and change nothing, but still trigger
`install_internal_window()`'s unconditional teardown+reinstall
(`mc68302.cpp:209-223`) — so the coverage tap must still be installed
only after the *last* one (§8.5), leaving `t=0-7s` genuinely uncovered.
This is a real, documented gap, not an oversight: the technique cannot
safely do better without risking exactly the reentrant-install class of
bug this project's own `tc_w()` episode already cost time on. Witnessed
against a sibling tap on the FDC FIFO (already known hot during the
load): 1,427,752 hits from install to end of run, proving the coverage
tap itself stayed alive for the whole post-install window, not just at
install time.

### Result

```text
class distribution (distinct offsets): 284 internal_ram, 88 known_unimplemented, 10 known, 0 unknown
```

- **[Verified]** Zero `unknown`-classified accesses — nothing firmware
  touches in this driver's boot+load path falls outside
  `classify_offset()`'s own documented ranges. No genuinely mysterious
  register access exists in the currently-exercised path.
- **[Verified]** 88 distinct `known_unimplemented` offsets touched: SCC1
  parameter RAM (`$0400-$043E`), SCC2 parameter RAM (`$0500-$053E`,
  clearly the same layout at `+$100`), scattered entries at `$0480-$04AE`
  / `$0580-$05AE` (per-channel, mirroring the `$400`/`$500` pairing),
  `$0814/$0816/$0818` (GIMR/IPR/IMR/ISR block), `$0820/$0822` (Port A),
  and `$0882-$08B4` (SCC1-3/SMC/SCP command/mode registers, including
  `$0884`/`$0894` both showing the already-independently-documented
  `$7033` value — current-status.md's "Disproved hypotheses" entry that
  `$7033` goes to SCM, not DSR, cross-confirmed here from a completely
  different measurement). All real, legitimate firmware behavior this
  device model doesn't implement — not bugs, and not silently returning
  a hardcoded value that hides absence, since the guard now names them.
- **[Verified]** The 10 `known` (modeled) offsets are exactly the 8 named
  IDMA registers plus `PBDAT` (`$0828`, 14,169 reads / 6,579 writes — the
  system's busiest I/O port, matching everything already established
  about Port B's role) and `FC6860` (the "busy register").
- **Guard** (`install_sib_coverage_guard`): alarms (aggregated,
  first-occurrence-per-offset) on any offset classified `unknown`, or
  classified `known_unimplemented` but **outside** this calibrated set of
  88 — i.e. a genuinely new access this run never saw, not a re-alarm on
  the already-inventoried, already-legitimate ones. "Outside the
  inventoried set," exactly as the task specified, not "outside the
  model."

## Del 3 — IDMA's Three Debts

`SCI_IDMA_VALUE` output from the same calibration run, over the same 21
real transfers:

```text
SAPR: FFFC5803 (21/21, no variation)
CMR:  00000002 x57 (RST=1,STR=0 -- a reset, no arm), 00000D51 x21 (STR=1 -- the one arm pattern)
BCR:  00000201 x3, 00000E01 x2, 00002801 x16  (matches 3+2+16=21 arms exactly)
DAPR: varies continuously (it is the destination pointer -- expected, not a debt)
```

### 1. SAPR — not payable now, made loud instead

Dereferencing SAPR requires decoding `$FC5803` to the FDC, which requires
a `mem_map` change, which is blocked until the E2 question is settled.
**Explicit dependency chain, as instructed:** SAPR decode → `mem_map`
change → E2 decision. Not payable this task. Made loud instead: the
guard alarms if SAPR is ever anything other than the one value observed
across all 21 transfers (`$FFFC5803`) — the assumption the current
FDC→memory hardcoding depends on now protests the moment it stops
holding, rather than continuing to silently assume a fixed source that
might have changed.

### 2. CMR — decoded against hardware documentation, cross-validated, not reimplemented

`docs/mc68302/idma-spec.md` (updated this task) now carries the CMR bit
table, sourced from the manual's own OCR full text via a web fetch — not
a direct read of the original scanned table, and labeled `[Likely]` not
`[Verified]` for that reason. Decoding the one previously-known value,
`$0D51`: `SAPI=0` (source does not increment), `DAPI=1` (destination
does), `INTN=INTE=0`. Both are **cross-validated**, not merely decoded:
`SAPI=0`/`DAPI=1` matches exactly what `idma_transfer_in()` already
hardcodes and dozens of transfers have already exercised; `INTN=INTE=0`
explains, independently of the already-measured `IMR` bit-11 mask, *why*
vector `$4B` never fires — the channel never requests it, not just gets
blocked from delivering it. This is real supporting evidence, not proof:
one value does not validate `REQG`/`SSIZE`/`DSIZE`/`BT`/`ECO`, which have
no independent behavioral check — said plainly in `idma-spec.md` itself.

**Not implemented in C++**, deliberately: this calibration run also
found a *second* real, legitimate CMR value, `$0002` (`RST=1`, no `STR`)
— 57 times, no corresponding arm. One additional data point doesn't
change the "one data point doesn't validate a field decode" conclusion;
it only means the guard's allowlist has two entries, not one. Building a
general field-driven transfer engine from two data points would be
guessing better, not protesting — exactly what this task's goal
statement rules out. **Guard**: alarms on any CMR value outside
`{$0002, $0D51}`.

### 3. BCR — rule kept, guarded exactly on the tested set

`BCR-1`'s byte count has been exercised for precisely three values, this
run reconfirming `file-loaded-verification-probe.md`'s own arm/byte-count
measurement exactly (`3+2+16=21`). **Guard**: alarms on any BCR value
outside `{$0201, $0E01, $2801}`.

### Bonus, not one of the three named debts

FCR (`$FC6810`, "function code register for IDMA bus cycles") was also
observed: always exactly `$0099`, 21/21, never decoded or used by the
model at all (just stored). Noted for the record, not guarded — outside
this task's named scope, and a fourth undirected guard risks diluting
the three the task actually asked for.

## Del 4 — The Regression Gate

`docs/asr10/lua/mc68302_guards.lua`, the 7th regression test. Wires all
three guards from `docs/asr10/lua/lib/asr10_guards.lua` around a normal
boot + `0A 23 02` load: exception guard installed immediately (no
BAR-window constraint), SIB coverage + IDMA guards installed after a 7s
margin past BAR's measured last write (`t≈5.395s`). Asserts `FILE LOADED`
**and** zero aggregated alarms across all three guards.

**Verified as an actual gate, not a trivial pass:** a fault-injection run
against a scratch copy with a deliberately wrong exception allowlist
entry produced exactly one aggregated alarm line (not 7,528 — dedup
confirmed working) and the test correctly failed. The real suite: 7/7.

## Verification

- `docs/asr10/regression-test.sh`: 7/7 (`boot`, `display`, `button`,
  `button_upper`, `nodisk`, `file_loaded`, new `mc68302_guards`), before
  and after every change in this task.
- No `mem_map` change. No new environment flags or `printf` sites added
  to `asr10_boot.cpp` or `mc68302.cpp`/`.h` — every guard lives in Lua.
- No ES5506 change; `src/devices/sound/es5506.h` not read, not modified,
  not staged this task.
- No keyboard, no new functionality.
- Bank 1 (voices 1-31 pointing at the unmapped ES5506 bank) left `[OPEN]`,
  untouched, per instruction.
- No `-log`.
- `git diff --check`: clean.

## Line Count

- `docs/mc68302/idma-spec.md`: +~35 lines (CMR bit table, hardware fact,
  cited).
- `docs/asr10/lua/lib/asr10_guards.lua`: new, ~200 lines — the reusable
  guard module all three Del-1/2/3 guards live in.
- `docs/asr10/lua/mc68302_guards.lua`: new, ~65 lines — the 7th
  regression test.
- `docs/asr10/lua/archive/exception-vector-inventory.lua`,
  `sib-coverage-inventory.lua`: written, run, archived — the calibration
  instruments every allowlist in the guard library was measured from.
- No C++ changed anywhere this task.
