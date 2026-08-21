# E2: The Address-Model Consistency Question, Settled Where It Can Be

E2 has meant two different things over this project's history, and both
are addressed here, kept apart:

1. **The short-address mirror hypothesis** (`$FF8000-$FFFFFF` aliasing
   `$00xxxx` via sign-extended `abs.w` addressing, memory-map.md §4).
   Still `[OPEN]`. Untouched by this task, as instructed — the 1404
   `mapping_basis=mirror-hypothesis` call-graph edges stand exactly as
   they were.
2. **The address-model consistency question**, this task's actual scope:
   do firmware's 32-bit `$FFxxxxxx.l`-style addresses, MAME's 24-bit
   runtime decode, the static toolchain, and `mem_map` agree on where
   real hardware lives — and specifically, what real hardware accesses
   does today's catch-all `.ram()` silently swallow?

These two are related (both produce `$FFxxxx` addresses, both live partly
in the same `mem_map` catch-all line) but are **not the same question**,
and resolving one says nothing about the other. Factor two, SCC
implementation, the interrupt controller, and `mem_map` itself are
untouched by this task, per instruction.

## Del 1 — Layer Agreement, Five Samples

Three layers, checked independently, never assumed from each other:

| Address | `mem_map` (read from `asr10_boot.cpp:300-399`) | Runtime (measured this pass) | Static toolchain (`call-graph-edges.csv`) |
|---|---|---|---|
| `$FC4813` | `map(0xfc4800,0xfc481f)` → `duart_panel_asr_candidate_r/w` | **360 reads, 5 writes**, first at PC `$FFF884B6` | no rows (out of scope, see below) |
| `$FC4817` | same DUART range | **664 reads, 391 writes**, first at PC `$FFF89BE8` | no rows |
| `$FC4001` | `map(0xfc4000,0xfc4003)` → `upd72069_fdc_r/w` | **1,676,445 reads**, first at PC `$FFFB7DBA` — a tight polling loop, consistent with FDC status polling | no rows |
| `$FC4003` | same FDC range | **200,632 reads, 599 writes**, first at PC `$FFFB8DB8` | no rows |
| `$FC5803` | `map(0xfc5020,0xffffff).ram()` — the catch-all | **zero reads, zero writes**, the whole run | no rows |

**Verdict for the four hardware addresses: all three layers agree**, and
the runtime layer is no longer merely assumed — it is measured this
pass, over a run spanning boot through a played note (four checkpoints:
`file1`, `file_loaded`, `selected`, `note_played`; see witness below).
DUART and FDC both receive real, heavy traffic exactly where `mem_map`
puts them. This closes the original motivation for treating truncation
itself as open: it is not open, for these four.

**The static-toolchain row is "no rows," not "disagrees."** `call-graph-
edges.csv` is scoped to control-flow edges (`jsr`/`jmp` targets,
`edge_id` schema in `call-graph.md`) — MMIO data references like
`move.b $FFFC4813.l,D1` are never call targets, so the toolchain has no
opinion on any of these five addresses at all. Confirmed by direct
`grep` against the CSV: zero matches for any of the five, in any form.
Reporting this as a silent "static toolchain agrees" would have been
wrong — it simply does not cover this class of address. The static
*evidence* that does exist for these four is a **different** static
method: `memory-map.md` §1's BR/OR chip-select decode (ROM's own
CS3 programming, `$FC4000-$FC5FFF`, read+write, 3 wait states, no
function-code compare) — already canonical, reused here rather than
re-derived, and it agrees with both other layers.

**`$FC5803` is the one address with no agreement to report, because
nothing is there to agree on.** `mem_map` puts it in the catch-all.
Runtime shows zero accesses. The static toolchain has no edges for it
(out of scope, as above) and no BR/OR sub-decode exists either — CS3's
chip-select is one flat 8 KB window; nothing in ROM's own chip-select
programming distinguishes FDC-at-`$4000` from anything-at-`$5803`
within it. The only static fact about this specific address comes from
a third source, already established and reused here: `idma-register-
map-probe.md` measured that the MC68302's own SIB register **SAPR**
(`$FC6804-$FC6807`, a completely different address from `$FC5803`
itself) is *programmed with the value* `$FFFC5803`, in all 21 observed
transfers, never any other value.

## Del 2 — Full `$FFxxxx` Inventory, Static vs. Dynamic

**Static side** (reused, not re-derived, per instruction): the complete
set of `$FFxxxx`-form addresses firmware is already known to use splits
into two architecturally unrelated groups (memory-map.md §1.1):

- **32-bit long, `$FF`-filled, real MMIO**: ES5506 (`$FC2000-$FC207F`),
  ES5510 (`$FC3000-$FC31FF`), FDC (`$FC4000-$FC4003`), DUART
  (`$FC4800-$FC481F`), SCSI (`$FC5000-$FC501F`), the MC68302 SIB window
  (`$FC6800-$FC6FFF`, dynamically installed), and the one hardware
  target with no decode at all, `$FC5803` (IDMA SAPR's programmed
  value).
- **16-bit short, sign-extended, binding-table calls**: 334 distinct
  `jsr/jmp abs.w` targets in `$FF801E-$FF9FF6`, 1404 call-graph edges
  tagged `mapping_basis=mirror-hypothesis` — case B, untouched by this
  task.

**Dynamic side** (measured this pass): a read/write tap across the
entire `$FC0000-$FFFFFF` span, aggregated by distinct address (not
per-event — the same discipline `sib-coverage-inventory.lua` already
established, since a per-event log over a run this size would drown
in noise the way the original vector-table tap did). Witness, per
§8.7 — four checkpoints proving the tap was live for the *whole*
measurement window, not just its start:

```
E2_CHECKPOINT label=file1        t=16.30s   total_r=1,965,020 total_w=100,839
E2_CHECKPOINT label=file_loaded  t=21.79s   total_r=2,062,304 total_w=111,614
E2_CHECKPOINT label=selected     t=23.12s   total_r=2,093,984 total_w=118,334
E2_CHECKPOINT label=note_played  t=24.62s   total_r=2,125,489 total_w=124,134
```

Counts strictly increase at every checkpoint — the tap was alive and
accumulating through boot, load, instrument selection, and note
playback, not just an early burst followed by silence. This is the
live witness the negative result below depends on.

**Result:** 1,883 distinct read addresses, 16,484 distinct write
addresses in `$FC0000-$FFFFFF`. Classified against `mem_map`'s own
declared ranges:

| class | distinct read addresses |
|---|---:|
| `es5506` | 40 |
| `es5510_host` | 11 |
| `fdc` | 2 |
| `duart` | 8 |
| `scsi` | 1 |
| `sib_dynamic` (`$FC6000-$FC6FFF`) | 0 — **see caveat below, not a real zero** |
| `catchall` | 1,821 |

**The `sib_dynamic=0` line is a known instrument artifact, not a
finding.** This probe's tap was installed at `t=0`, covering the whole
`$FC0000-$FFFFFF` range in one call. `mc68302_device::install_internal_
window()` tears down and reinstalls the handler for exactly
`$FC6000-$FC6FFF` on every BAR write — the first at `t≈0.000004s`,
already documented in `asr10_guards.lua`'s own comments, with the
consequence spelled out there too: *"an earlier install would be
silently dropped."* That is what happened to this probe's coverage of
that one 4 KB slice; the other ~235 KB of the swept range has no such
dynamic reinstall anywhere in this driver (confirmed: `grep
install_readwrite_handler` across `asr10_boot.cpp` and `mc68302.cpp`
finds exactly one call site, this one, sized `0x0fff`) and is not
affected. The SIB window's *real* traffic is not unwitnessed — it is
already correctly measured, with correct install timing, by
`mc68302_guards.lua`/`asr10_guards.lua` (88 distinct
`known_unimplemented` offsets, zero `unknown`, run every regression
pass). Reused here rather than re-measured badly.

**Catch-all breakdown** — the part that matters:

```
E2_CATCHALL_BREAKDOWN fc5803=0 mirror_window_distinct=16384 unexplained_distinct=34
```

- **`$FC5803`: zero hits, confirmed dynamically.** Matches
  `idma-implementation-plan.md`'s own design note exactly: the current
  IDMA slice "does not dereference SAPR's address... sidesteps the
  unresolved `$FC5803` decode entirely." The catch-all is masking a
  real hardware target, but that target is not currently exercised by
  anything in this build — the risk is latent, not active. See Del 4.
- **`$FF8000-$FFFFFF`: 16,384 distinct write addresses, verified shape
  `min=$FF8000 max=$FFFFFE span=32767`** — every even, word-aligned
  address across the **entire** 32 KB window, not a partial sweep (an
  initial pass mis-stated this as half the window before the shape was
  actually checked; corrected here before writing it down as a finding,
  per this task's own rule about claims written before their
  measurement runs). This reads as one contiguous word-wide RAM-clear
  loop covering the whole mirror-hypothesis window, the same style
  already documented for `$100000-$1FFFFF`'s 524,290-write clear. This
  is new: previously the only known activity there was 566,229 *data
  reads* (2026-08-10 experiment) plus 334 static call targets. **This
  does not touch the mirror hypothesis itself** — it is a volume
  observation about the same address range, not a resolution of
  whether it aliases `$00xxxx`. Flagged `[OPEN]` for whoever next picks
  up E2's mirror question; not pursued further here, out of this task's
  scope.
- **34 unexplained distinct addresses, all in `$FF7F00-$FF7FF6`** — a
  new, previously-undocumented catch-all region, structurally
  interesting: `$FF7F00/02/04/06` show small, irregular counts (1-10
  writes, one read), then `$FF7F0E` through `$FF7FF6` step in an exact
  **stride-8 grid**, 4 writes each, 30 slots. This is the same style of
  finding as the already-documented stride-6 jump table in DPRAM
  (`memory-map.md` §2.1) — a regular, structured access pattern, not
  noise — but this task does not trace its purpose. `[OPEN]`: named
  precisely (addresses, stride, counts) so the next investigation does
  not have to rediscover it, per this task's own instruction to reuse
  rather than restart. It sits just below the mirror-hypothesis
  window's start (`$FF8000`), which under that same still-open
  hypothesis would correspond to just below the binding table's own
  start (`$00801E`) — worth keeping in mind if E2's mirror question is
  ever picked back up, but not asserted as fact here.

No other catch-all address, anywhere in `$FC0000-$FFFFFF`, was touched
in this run outside these three named cases. That is the negative
result Del 2 asked for, and its witness is the four strictly-increasing
checkpoint totals above.

## Del 3 — What Lifting The Barrier Would Require

**Decoding `$FC5803` to the FDC**: not urgent, because it is
provably inert (Del 2). If a future, bidirectional (memory→FDC as well
as FDC→memory) IDMA implementation is built and *does* dereference
SAPR, the concrete change would be a narrow handler for `$FC5803`
specifically (matching this driver's own established idiom for narrow
sub-windows inside a chip-select — see the ES5510 select-register lines
at `$FC3100`/`$FC3140`/`$FC3180`/`$FC31C0`, each one word, routed to a
dedicated function rather than widening an existing device's map).
`idma-register-map-probe.md` already ruled out the simplest
hypothesis — that `$FC5803` aliases the CPU's own FDC FIFO port
`$FC4003` via partial address decoding (both end in `...03`) — as
unconfirmed and not pursued; no new evidence from this pass changes
that. What real hardware wires there remains `[OPEN]`.

**Other map changes Del 2's inventory implies**: none are forced. The
`$FF7F00-$FF7FF6` region and the `$FF8000-$FFBFFF` write sweep are both
plain `.ram()` today and nothing observed there looks like a masked
register (no fixed-value poll/ack pattern, unlike the SAPR/CMR/BCR
signature already calibrated) — they look like RAM-resident data, not
misdecoded hardware. No map change is indicated for either; both are
named for future investigation, not acted on here.

**Which of the 1404 edges are at stake**: none, for the one concrete
change discussed above. The 1404 `mapping_basis=mirror-hypothesis`
edges all have `effective_address` in `$FF801E-$FF9FF6` (`call-
graph.md`'s own `to_space=HIGH-RAM-ALIAS-CANDIDATE` scope); `$FC5803`
is nowhere near that range. A narrow `$FC5803` decode and the mirror
hypothesis are independent questions that happen to share one `mem_map`
line today (`map(0xfc5020, 0xffffff).ram();`) — splitting that line
does not, by itself, touch any row whose `effective_address` starts
`$FF8xxx`/`$FF9xxx`. **A map change that touches the mirror window
itself** (`$FF8000-$FFFFFF`) is the one that would put the 1404 edges
at risk, and that is not what Del 3's specific question asks about.

**How to verify the 1404 edges are not silently invalidated**, for
whenever a map change *does* touch that window: there is no in-repo
regenerator for `call-graph-edges.csv` (confirmed — no script in this
tree produces it; it is a frozen, externally-produced artifact per
`DOCUMENTATION-MANIFEST.md`'s own `generated-static-data` category).
The check is therefore not "regenerate and diff," which the task's own
caution rightly distrusts ("ett omgenererat underlag som ser likadant
ut är inte ett verifierat underlag") — it is: rerun this pass's own
sweep plus `prompts-E1-E4.md`'s E2 direct-mirror test (`$DEAD`→`$008030`,
read `$FF8030`; `$BEEF`→`$FF9000`, read `$009000`) against the changed
`mem_map`, and confirm the 334 static targets in `$FF801E-$FF9FF6`
still resolve to the same bytes they do today. The CSV's row count and
SHA-256 staying the same proves nothing by itself (it is hand-frozen,
not regenerated); what proves something is the *runtime* mirror test
producing the same answer before and after the map change.

**Staged lifting**: yes, and only this way. One address at a time,
each stage: (1) a dynamic witness first, exactly as Del 2 did, proving
what real traffic the change would affect; (2) the narrowest possible
`mem_map` line, matching this driver's existing narrow-sub-window
idiom; (3) the full regression suite; (4) a guard added to
`asr10_guards.lua` for the new address, same pattern as the existing
SAPR/CMR/BCR allowlist, so a future regression catches drift
immediately instead of silently. `$FC5803` is not staged today because
stage (1) already shows nothing to protect yet.

## Del 4 — Recommendation

**The original E2 barrier's motivation — ambiguous truncation — is
dead.** Confirmed twice now: the 2026-08-10 experiment and this pass's
independent re-measurement both show firmware's 32-bit `$FF`-filled
MMIO addresses decode exactly where `mem_map` and the BR/OR chip-select
analysis say they do, for every address checked. No layer disagreement
exists for this class of address.

**That does not mean `mem_map` can be freely edited.** The 1404
mirror-hypothesis edges are real, unresolved, and shared address space
with the catch-all this task inventoried. Per the task's own framing:
agreement today is not proof the guard was unnecessary — it is proof
the *original justification* for the guard is gone. **Recommendation:
replace the rule, don't repeal it.**

- Old rule: *rör inte kartan* (don't touch the map) — justified by
  "truncation itself might be wrong." Retired; that premise is false
  for every MMIO address measured across two independent passes.
- New rule: *change the map in stages, and regenerate/verify the
  affected edge set after each stage* — justified by "the 1404 edges
  are real and this task did not verify what a map change near
  `$FF8000` would do to them." Enforced procedurally: witness first,
  narrowest line, full regression, new guard, per Del 3's staging
  recipe. A change confined to `$FC5803` specifically satisfies this
  trivially (zero edges nearby); a change touching `$FF8000-$FFFFFF`
  does not get to skip the mirror-test rerun.

**Remaining risks**: the mirror hypothesis itself is still open and
this task does not move it. The new `$FF7F00-$FF7FF6` stride-8 region
and the `$FF8000-$FFBFFF` write sweep are unexplained; either could,
in principle, turn out to matter to the mirror question, but neither
does today under any evidence gathered here. `$FC5803`'s real hardware
identity is still `[OPEN]` — inert today, but a generalized
bidirectional IDMA implementation would need an answer before it could
safely dereference SAPR.

## Del 5 — Journal Summary

**Layer comparison (Del 1).** Four MMIO addresses (`$FC4813`,
`$FC4817`, `$FC4001`, `$FC4003`) agree across `mem_map`, measured
runtime traffic, and the reused BR/OR static chip-select analysis.
`call-graph-edges.csv` has no opinion on any of the five samples —
out of scope by schema (control-flow edges only), not a disagreement.
`$FC5803` has no runtime traffic at all, measured this pass — the
catch-all masks a real hardware target that current firmware+model
never actually touches.

**Full `$FFxxxx` inventory (Del 2).** Static side reused from
`memory-map.md`/`idma-register-map-probe.md`, not re-derived. Dynamic
side newly measured: full `$FC0000-$FFFFFF` sweep, witnessed live
across four checkpoints (file1 → file_loaded → selected →
note_played, strictly increasing totals throughout). 1,821 distinct
catch-all read addresses; of those, exactly `$FC5803` (0 hits),
`$FF8000-$FFBFFF` (16,384 write addresses, a bulk clear, `[OPEN]` for
the mirror question), and 34 new addresses in `$FF7F00-$FF7FF6`
(stride-8 structure, `[OPEN]`, purpose untraced). No other catch-all
address was touched.

**Recommendation (Del 4).** The map is not edited today — nothing
forces it (`$FC5803` is inert). The barrier's original justification
is retired; its replacement is a staged, witnessed, regression-guarded
procedure, not a blanket "don't touch."

**The chain, `mem_map` → `$FC5803` → real SAPR in IDMA → transfer**,
stated precisely: `mem_map`'s catch-all decodes `$FC5803` as plain RAM.
The MC68302 SIB register SAPR (`$FC6804-$FC6807`, itself correctly
decoded, unrelated address) is programmed by firmware with the *value*
`$FFFC5803`, meaning real hardware would read floppy data from that
address during a transfer. This build's IDMA implementation does not
dereference SAPR at all — it takes the transferred byte directly from
`m_fdc->dma_r()` and writes it to the `DAPR`-derived RAM destination
(confirmed measured: source `$FFFC5803`, destination `$00000944`,
21/21 transfers, all one direction). The chain is FDC → IDMA →
destination RAM buffer, i.e. data arriving from the floppy is written
into memory — the direction this build actually implements and tests
(`file_loaded`, loading an instrument from floppy). No disk-write
(memory→FDC) path is implemented or measured; `idma-implementation-
plan.md` already flags that direction as unimplemented and untested.
`$FC5803` itself is never actually read or written as a bus address in
this build (Del 1/Del 2), by design — the catch-all's masking of it is
real but currently harmless.

**The ninth regression line.** The suite is 8 *tests*, named in
`regression-test.sh`'s own header comment: `boot`, `display`, `button`,
`button_upper`, `nodisk`, `file_loaded`, `mc68302_guards`, `note_audio`.
`note_audio` is a two-stage test: `note_audio.lua` checks structural
preconditions (MIDI received, ES5506 voice writes) and prints its own
`PASS note_audio`; `run_test_audio()` in `regression-test.sh` then
additionally runs `check_note_audio.py` against the `-wavwrite`
capture, checking for real, non-silent, correctly-pitched audio — not
just register writes, which `keyboard-and-sample-bridge-6.md`/`-7.md`
already found insufficient evidence of actual sound. That script prints
its own `PASS note_audio_wav` line. Counting *tests* gives 8; counting
printed *PASS lines* gives 9. Both numbers are correct, for what they
each count — the "9" is not a hidden new test and not drift, it is
`note_audio`'s own pre-existing second gate becoming visible the moment
someone counts lines instead of names. This document uses "8 tests, 9
PASS lines" going forward to avoid repeating the ambiguity.

**`[Hypothesis]`, not asserted further:** an unmasked SCC with a real,
content-verified handler (`interrupt-topology-gaps.md`'s SCC1/SCC2
finding) could be the keyboard calibration channel behind `TUNING
KEYBOARD - HANDS OFF`. Nothing in this task's measurements bears on
this either way; carried forward exactly as received.

**Vector table window, carried forward:** `interrupt-topology-gaps.md`
already established that vectors 48-239 are reused as text/data, not
code, and that the only vector-table window meaningful for future
interrupt work is 0-47, 60-61, and 64-86. Unaffected by this task;
restated here only because Del 5 asked for it explicitly.

## Verification

- No code change. No `mem_map` change. No clock change, no bank-1
  change, no ES5510 activation, no SCC implementation. Factor two
  untouched.
- No `-log`. All observation via Lua `print()`.
- `static/call-graph-edges.csv` and every other file in `static/` were
  not hand-edited. Run-time observations live in this document and in
  the scratch probe script, not merged into any CSV.
- No fork with an open mandate.
- `docs/asr10/regression-test.sh`: 8 tests, 9 PASS lines (see Del 5),
  run before and after this task's documentation edits — unaffected,
  Lua-only measurement.
- `git diff --check`: clean.
- `src/devices/sound/es5506.h` and `.project`'s working-tree
  modifications remain pre-existing and unrelated; not part of this
  commit.
