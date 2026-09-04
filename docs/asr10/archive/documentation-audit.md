# ASR-10 Documentation and Source Audit

**Audit date:** 2026-07-21
**Repository state at audit time:** branch `asr10-es5510-host`, HEAD
`da1b4c385256404fe9cd597837be45bf19cbf742`. `src/mame/ensoniq/asr10_boot.cpp`
is byte-identical to HEAD (10,343 lines; the temporary panel-diagnostic
addition has been fully removed and archived outside the repository). Only
`docs/asr10/current-status.md` (new), `docs/asr10/documentation-audit.md`
(this file, new) and `docs/asr10/README.md` (rewritten) were changed by this
audit round; `docs/asr10/panel-protocol.md` carries an uncommitted addition
from the prior round.

This is a recommendation document. It does not move, merge, or delete
anything itself.

---

## 1. Canonical documents now

| File | Why |
|---|---|
| `current-status.md` | New in this round. The single dated entry point for build/run/status/device-architecture claims. |
| `panel-protocol.md` | Host-side panel-TX protocol findings (TRAP #$A, encoder, marker matrix, record library) — actively maintained through the most recent investigation round, evidence-graded. |

A document is canonical only if it is (a) current, (b) internally
evidence-graded, and (c) the one place a new session should trust by
default. No other file in this directory meets both criteria today.

## 2. Candidate canonical documents (require revision before promotion)

| File | Why candidate, not canonical | What's needed |
|---|---|---|
| `architecture.md` | Broad, structurally sound "consolidated" register/architecture reference (900 lines, referenced twice from source), but its SCN2681 section reads as a hardware register map without stating that the *emulation* is a hand-written shadow model, not `scn2681_device`. | Add one explicit "current emulation status" line per subsystem, matching `current-status.md` §4. |
| `hardware-map.md` | Reasonable chip/register candidate inventory; already appropriately hedges the 80C52 panel-controller claim as "possible", not proven. | Cross-reference `panel-protocol.md` §7.4 for the panel-controller evidence status; otherwise sound. |
| `roadmap.md` | Right kind of document (project phases), but dated 2026-06-22, predating the entire boot-progress arc documented in `current-blocker.md`/`filesystem-browser-map.md`. | Re-baseline phase list against the current boot state before trusting it for planning. |
| `vfx-reuse.md` | Predates the ES5506/ES5510 *real-device* integration that is now done; still useful for what remains not yet reused (pump, sample RAM). | Mark which reuse items are now DONE vs still open. |

## 3. Specialist technical references (narrow scope, evidence-bearing, not meant to be an entry point)

`es5506-chain-verification.md`, `es5701-wiring.md`, `fdc.md`,
`memory-map.md`, `movep-library.md`, `panel-input-display.md`,
`subsystems.md`, `troubleshoot.md`, `vfx-es5510-comparison.md`.

Notes:
- `panel-input-display.md` substantially overlaps `panel-protocol.md` for the
  panel-specific content (secondary tag: merge candidate, §5 below).
- `troubleshoot.md` is service-manual-derived (real hardware, not emulator
  state) and is expected to stay valid regardless of project phase.

## 4. Evidence/claim registers

| File | Notes |
|---|---|
| `evidence-tree.md` | Explicitly a claims register ("classifies every claim... into five strict categories"), companion to `architecture.md`/`panel-protocol.md`/`current-blocker.md`. Predates the TRAP #$A findings — structurally sound, content-stale at the newest layer. |
| `experiments.md` | Catalog of every intentional experiment/stub and what it proved. Contains a stale validation command (`make SOURCES=... -j1`, missing `SUBTARGET=mess`) — see contradiction register. |
| `open-questions.md` | Genuinely intended as a living open-questions register, but has not been reconciled with the questions `panel-protocol.md` §6 has since resolved or newly opened. |

## 5. Chronological investigation journals

| File | Notes |
|---|---|
| `filesystem-browser-map.md` | 4,596 lines / 266 KB, by far the largest document, referenced **87 times** from source-code comments — this is the primary evidence log the driver itself points readers to. Most recently updated 2026-07-21. Recommend physical relocation to `docs/asr10/evidence/filesystem-browser-map.md` with a compact index (Phase 6 detail below) — **not** a rewrite. |
| `current-blocker.md` | 1,370 lines, append-only dated updates through 2026-07-21. Its *latest* entry (ES5510 0xE0 fix, effect-download blocker resolved) is accurate and matches `current-status.md`. Its bulk (Channel B/slot0 continuation chronology) predates and overlaps `panel-protocol.md`'s TRAP #$A layer. |

## 6. Historical handoffs

`asr10-boot-slot0-2026-07-13.md`, `asr10-handoff-2026-06-22.md`,
`asr10-mame-handoff-addendum-2026-07-09.md`,
`asr10-mame-handoff-duart-fdc-scheduler-2026-07-11.md`,
`asr10-mame-irq6-duart-handoff-2026-07-12.md`,
`asr10-panel-slot0-handoff-2026-07-13.md`,
`floppy-investigation-2026-06-29.md`,
`mc68302-sib-investigation-2026-06-29.md`,
`scheduler-slot0-continuation-findings-2026-06-29.md`,
`superglu-investigation-2026-06-29.md`.

All are dated point-in-time handoffs, superseded as *entry points* by
`current-status.md`, but each may contain unique evidence not yet folded
into a canonical document (see §14 below before deleting anything).
Proposed future destination: `docs/asr10/evidence/handoffs/` (recommendation
only — not moved this round).

`asr10-panel-slot0-handoff-2026-07-13.md` specifically overlaps
`panel-protocol.md` §2 (the ring routines) at an earlier, less complete
stage — merge/supersede candidate.

## 7. Obsolete status/checkpoint documents

| File | Why obsolete |
|---|---|
| `checkpoint.md` | Describes "dispatcher idle after LOADING SYSTEM" as the current phase/blocker. This was resolved and superseded multiple investigation arcs ago; the project is now well past filesystem scan and into panel-protocol work. |
| `status.md` | Describes the "slot 2 finalizer/continuation fields are zero" blocker as current. Same era as `checkpoint.md`, equally superseded. |
| `running.md` | Build/run instructions use `./mame` (proven stale binary in this repo's history) and lack `SUBTARGET=mess` entirely; routine `-log` usage contradicts current practice. Fully superseded by `current-status.md` §2. |
| `boot-flow.md` | Boot control-flow notes frozen at 2026-06-22, before a month of major progress (LOADING SYSTEM → NO INST OR BANK FILES). Early ROM prompt-decision content may still be individually accurate but the document as a whole no longer reflects current boot state. |

None of these are recommended for deletion yet — see §14.

## 8. Duplicate/merge candidates

- `panel-input-display.md` → `panel-protocol.md` (panel-specific content).
- `asr10-panel-slot0-handoff-2026-07-13.md` → `panel-protocol.md` (ring-routine
  content, earlier/less complete version of §2).
- `checkpoint.md` / `status.md` → both superseded by `current-status.md`;
  candidates to merge any still-unique facts into `current-status.md` or
  `current-blocker.md`, then archive.

## 9. Deletion candidates (after evidence extraction — do not delete yet)

No file is recommended for outright deletion this round. `checkpoint.md`,
`status.md`, and `running.md` are the closest candidates once (a) any unique
facts are confirmed extracted and (b) they've been moved to an archive
location rather than deleted outright, per the standing instruction not to
delete historical documents until unique information has been checked.

## 10. Contradiction register

| # | Document | Line | Issue | Type | Authoritative replacement | Recommended action |
|---|---|---|---|---|---|---|
| 1 | `running.md` | 17, 34, 47 | Build/run commands use `./mame` and lack `SUBTARGET=mess` | stale/dangerous command | `current-status.md` §2 | annotate as historical; do not follow |
| 2 | `README.md` (pre-audit) | 77 | `make SOURCES=... -j1` — missing `SUBTARGET=mess` | stale/dangerous command | `current-status.md` §2 | **corrected this round** (README.md rewritten) |
| 3 | `experiments.md` | 32 | Same missing-`SUBTARGET=mess` validation command | stale command | `current-status.md` §2 | annotate as historical; retain unchanged as historical evidence otherwise |
| 4 | `asr10-mame-irq6-duart-handoff-2026-07-12.md` | 519, 830 | `./mame ... -log` example commands | stale command, but clearly dated/historical | `current-status.md` §2 | retain unchanged as historical evidence (dated handoff, not presented as current) |
| 5 | `checkpoint.md` | whole file | Describes a project phase resolved long ago as current | superseded conclusion | `current-status.md` | annotate as historical/obsolete |
| 6 | `status.md` | whole file | Same class of issue as #5, different superseded blocker | superseded conclusion | `current-status.md` | annotate as historical/obsolete |
| 7 | `architecture.md` | §2 (SCN2681) | Describes the real SCN2681 register layout without stating the current emulation is a hand-written shadow, not `scn2681_device` | misleading omission (not a false statement) | `current-status.md` §4 | annotate/add one status line (not done this round — recorded here per Phase 15 scope) |

No occurrences were found of: `SUBTARGET=mame`, `SUBTARGET=asr10boot`,
`nproc`, `Line-A` misapplied to TRAP #$A (all existing "Line-A"/"vector #10"
references correctly describe the separate, pre-existing `f882ca` mechanism),
`scn2681_device`, `"fully modelled"`, `0x0000cbb8`/`0xffffcbb8` literal
strings, or any reference to the now-removed
`ASR10_EXPERIMENT_PANEL_ENCODER_TRACE` flag or `panel_encoder_trace.txt`
outside this session's own history. "80C52" occurrences are all
appropriately hedged ("possible", "may be", "or") — not asserted as proven
anywhere in the tree.

## 11. Proposed target directory structure (recommendation only)

```
docs/asr10/
  current-status.md            (canonical)
  panel-protocol.md            (canonical)
  documentation-audit.md       (this file)
  README.md                    (entry point)
  architecture.md, hardware-map.md, memory-map.md, subsystems.md, ...
                                (specialist references, stay at top level)
  evidence/
    filesystem-browser-map.md  (moved, with a new compact index)
    evidence-tree.md           (moved)
    current-blocker.md         (moved)
    handoffs/
      asr10-handoff-2026-06-22.md
      asr10-mame-handoff-*.md
      asr10-panel-slot0-handoff-2026-07-13.md
      asr10-boot-slot0-2026-07-13.md
    investigations/
      floppy-investigation-2026-06-29.md
      mc68302-sib-investigation-2026-06-29.md
      scheduler-slot0-continuation-findings-2026-06-29.md
      superglu-investigation-2026-06-29.md
  archive/
    checkpoint.md
    status.md
    running.md
```

## 12. Phased migration plan (not executed this round)

1. Add the one-line emulation-status clarifications identified in §10 item 7
   and the cross-reference in §2's `hardware-map.md` row.
2. Move `checkpoint.md`, `status.md`, `running.md` to `docs/asr10/archive/`
   with a one-line banner pointing to `current-status.md`.
3. Move dated handoffs/investigations into `evidence/handoffs/` and
   `evidence/investigations/` respectively.
4. Move `filesystem-browser-map.md`, `evidence-tree.md`, `current-blocker.md`
   into `evidence/`; write the compact `filesystem-browser-map.md` index
   described in §13.
5. Fold `panel-input-display.md` and the ring-routine portion of
   `asr10-panel-slot0-handoff-2026-07-13.md` into `panel-protocol.md`, then
   archive the originals.

Each step should be its own small, reviewable commit with link verification
(§13) run before and after.

## 13. Link-update requirements

Before any move: `grep -rn "filesystem-browser-map\.md\|architecture\.md\|panel-protocol\.md\|current-blocker\.md\|evidence-tree\.md\|hardware-map\.md\|status\.md\|checkpoint\.md\|subsystems\.md" src/mame/ensoniq/asr10_boot.cpp docs/asr10/*.md` and update every hit. Source-code comment cross-references found this round: `filesystem-browser-map.md` (87), `architecture.md` (2), `es5506-chain-verification.md` (3), `evidence-tree.md` (2), `subsystems.md` (2). Source comments referencing a moved file must be updated in the same commit as the move, since they are effectively part of the "link".

## 14. Documents that must not be deleted — unique evidence check

- `checkpoint.md` / `status.md`: contain detailed, specific slot-2/dispatcher
  register-level traces (`f8ce00-f8ce46`, slot field layout candidates) not
  duplicated verbatim elsewhere — archive, do not delete.
- `running.md`: no unique technical evidence, but historical practice
  reference — archive rather than delete.
- All historical handoffs (§6): each was written at a genuine investigation
  milestone and may contain a specific disassembly snippet or register trace
  not repeated in `filesystem-browser-map.md`. None should be deleted before
  an explicit line-by-line unique-content diff against
  `filesystem-browser-map.md`/`current-blocker.md` — not performed this
  round.

---

## 15. Source structural inventory — `src/mame/ensoniq/asr10_boot.cpp`

Verified by script-assisted counting plus manual spot-checks; this file was
**not modified** to produce these numbers.

| Metric | Count |
|---|---|
| Total lines | 10,343 |
| `#include` directives | 13 |
| Anonymous `namespace { }` blocks | 3 |
| Classes | 1 (`asr10_boot_state : public driver_device`) |
| Member fields (heuristic: typed declarations inside the class body) | ~454 |
| Member function definitions (`asr10_boot_state::`) | 213 |
| `emu_timer *` fields | 5 |
| `TIMER_CALLBACK_MEMBER` implementations | 10 |
| `required_device<>`/`optional_device<>` fields | 5 (`m_maincpu` = `m68000_device`, `m_fdc` = `upd72069_device`, `m_floppy_connector`, `m_es5506_host` optional, `m_es5510_host` optional) |
| Address-map (`map(0x...)`) entries | 44 (across `mem_map`/`cpu_space_map`) |
| Functions named `*_r(...)` (read-handler convention) | 21 |
| Functions named `*_w(...)` (write-handler convention) | 20 |
| `logerror(...)` call sites | 315 |
| `std::getenv(...)` call sites (environment-flag reads) | 25 |
| `machine_start()` / `machine_reset()` | line 969 / line 1670 |
| `mem_map()` / `cpu_space_map()` | line 2137 / line 2252 |
| `asr10_boot(machine_config&)` | line 10260 |
| `ROM_START(asr10booth)` | line 10334 |

## 16. Responsibility-to-line-range map (representative anchors, not exhaustive)

| Group | Representative range(s) | Notes |
|---|---|---|
| A. Machine-driver composition | 10260-10343 | `asr10_boot(machine_config&)`, `ROM_START`, `CONS(...)` |
| B. Board address decode/glue | 2137-2340 | `mem_map`, `cpu_space_map`, `high_alias_r/w`, `low_rom_or_lowmem_r`/`lowmem_w` |
| C. Provisional MC68302 behavior | 4265-4387 | `m68302_internal_r/w` core; state fields scattered earlier in class body |
| D. Provisional SCN2681/DUART behavior | 4767-5150 | `duart_panel_asr_candidate_r/w`, `panel_text_byte`, `flush_panel_text`; counter/timer helpers at 3062-3155 |
| E. FDC board glue/experiments | 4388-4766, 8871-9051 | `upd72069_fdc_r/w` plus FDC logging helpers (`log_fdc_*`) |
| F. ES5506/OTIS host integration | 2872-2899, 9414-9425 | `es5506_host_read_par_diag`, VFX-candidate stubs |
| G. ES5510/ESP host integration | 2899-2943 | select-GPR/INSTR/GPR+INSTR handlers (includes the committed 0xE0 fix) |
| H. SCSI stub/shadow | 9337-9377 | `scsi_asr_candidate_r/w` |
| I. Front-panel protocol/parser/autoresponder | 2542-3502, 5031-5150 | `panel_b_*`/`panel_c_*`/`panel_e_*`/`panel_l_*` experiment families, `panel_text_byte` |
| J. Scheduler/timer/interrupt experiments | 3062-3155, IACK handlers | DUART counter-timer arm/start/stop; `maincpu_iack_r` |
| K. Diagnostics/instrumentation | scattered throughout (315 `logerror` sites) | No longer includes the removed `ASR10_EXPERIMENT_PANEL_ENCODER_TRACE` block |
| L. ROM/driver declarations | 10334-10343 | `ROM_START`/`CONS` |
| M. Unknown/mixed | VFX/TS "candidate" stub families (9378-9432) | Small probe stubs not yet attributed to a single subsystem with confidence |

## 17. Provisional device-model inventory

Matches `current-status.md` §4 exactly — see that table for the canonical
version. Summary: FDC and ES5506 host use real MAME devices with provisional
glue; ES5510 uses a real host-interface device with DSP execution disabled;
DUART, MC68302, and SCSI are hand-written shadow/stub models, not real MAME
devices.

## 18. Board-glue inventory

The `mem_map`/`cpu_space_map` address decode (group B) and the `high_alias_r`/
`low_rom_or_lowmem_r`/`lowmem_w` trio are the board-level glue that every
other group depends on for address translation, including the established
0xF8xxxx-mirrors-low-address convention this whole investigation relies on.
This glue currently also carries diagnostic side logic (e.g. FDC-state
logging inside the lowmem read path) that is architecturally board-glue
territory doing device-level logging — a coupling worth resolving during
extraction (see §21).

## 19. Diagnostic/experiment inventory still present in accepted HEAD

The 506-line `ASR10_EXPERIMENT_PANEL_ENCODER_TRACE` mechanism was fully
removed in the prior round and is **not** present in HEAD or the working
tree. What remains in HEAD is the pre-existing, long-standing diagnostic
infrastructure: 315 `logerror` call sites and 25 environment-flag reads
spanning FDC, panel, ES5506/ES5510 host, and DUART-counter experiments —
these predate this investigation arc and are outside this round's scope to
re-audit individually.

---

## 20. Dependency graph

```
CPU (m68000) / address map (mem_map, cpu_space_map)
        |
        +-- high_alias_r/w  (0xF8-0xFB mirrors low RAM; foundational, board-glue)
        |         |
        |         +-- low_rom_or_lowmem_r / lowmem_w  (0x000000-0x0FFFFF)
        |                   |
        |                   +-- FDC-state field logging (group E leaking into group B)
        |                   +-- lowmem overlay flag (m_lowmem_overlay_enabled)
        |
        +-- m68302_internal_r/w  (group C) ---- IACK/vector routing (group J)
        |                                              |
        +-- duart_panel_asr_candidate_r/w  (group D) --+-- panel_text_byte / flush_panel_text (group I)
        |         |                                          |
        |         +-- DUART counter/timer (group J)          +-- autorespond timer (group I/J shared)
        |
        +-- upd72069_fdc_r/w  (group E, real device + glue)
        |
        +-- es5506_host_* (group F, real device + glue) ---- es5506_host_read_par_diag (diagnostic, group K)
        |
        +-- es5510_host_* (group G, real device + glue)  ---- committed 0xE0 fix
        |
        +-- scsi_asr_candidate_r/w  (group H, stub)
        |
        +-- VFX/TS "candidate" probe stubs (group M) -- overlap multiple address windows, unclear owner
```

**Cycles / hidden coupling:** the DUART model (group D) and the scheduler/IRQ
routing (group J) share mutable state (`asr10_boot_state` fields such as ring
pointers, `m_duart_counter_*`) directly rather than through a narrow
interface — this is the central coupling blocking a clean DUART extraction.
The board-glue read/write handlers (group B) also directly call into
FDC-specific logging helpers (group E), meaning board-level address decode
currently owns device-level diagnostic knowledge it shouldn't need.

**Misplaced ownership:**
- Board glue (group B) owns FDC-state-field knowledge that belongs in group E.
- The DUART shadow model (group D) owns scheduler/ring semantics
  (`f89a72`-style enqueue/dequeue is *firmware* behavor being *modeled* by
  watching DUART register accesses) that conceptually belongs to the panel
  protocol layer (group I), not the DUART register model itself — though in
  a real system these are genuinely coupled (the firmware ring only exists
  because of how it uses the real DUART), so this coupling is partly
  inherent, not purely accidental.

## 21. Target architecture proposal (not implemented)

| Component | Kind | Responsibility | Notes |
|---|---|---|---|
| `src/mame/ensoniq/asr10.cpp`/`.h` | driver-local | Machine-driver composition, address maps, ROM/machine config only | Eventual rename target for `asr10_boot.cpp` once it's thin; **not** proposed as a first step |
| `src/devices/machine/mc68302.*` | reusable generic device | MC68302 core (timers, SIB, interrupt controller) | Only worth it if a second Ensoniq/other driver would reuse it — evaluate before committing effort; MC68302 appears in several Ensoniq products, so plausibly reusable |
| `src/devices/machine/asr10_glu.*` | ASR-10-specific board device | High-alias/low-mem decode, board-level glue | Board-specific; not a generic reusable device, but isolating it clarifies the one piece every other subsystem depends on |
| `src/devices/machine/asr10_panel.*` | ASR-10-specific board device (host-side half only) | Encapsulate the ring/encoder/TRAP-#$A host-side model from `panel-protocol.md` | Physical panel semantics are unresolved, so this can only ever model the *host* side faithfully; document that limitation in the device itself |
| Existing `scn2681_device` | reusable generic device (already exists in MAME) | Replace the hand-written DUART shadow, *after* board-glue extraction, as its own separate milestone | Do not combine with any "move code" step — this is a model *replacement*, not a relocation |
| Diagnostic helper (driver-local, optional) | diagnostic-only | Any future bounded, reusable trace helper | Only introduce if a concrete future need re-emerges; nothing currently qualifies (see the panel-cleanup round's Phase 5 criteria) |

For each proposed extraction, migration risk is dominated by the
board-glue/DUART/scheduler coupling identified in §20 — any device boundary
drawn without first resolving that shared-state coupling will need a wide,
hard-to-review interface just to preserve current behavior.

## 22. Safe extraction order (proposed, not started)

1. Move pure constants/structures (register offset enums, marker tables,
   ROM region descriptors) into a small header — no semantic change, purely
   mechanical, easiest possible first step.
2. Extract the panel presentation/autoresponder helper (`panel_text_byte`,
   `flush_panel_text`, `panel_c_*`/`panel_e_*`/`panel_l_*` families) into a
   driver-local helper class, still called the same way from the same
   handlers.
3. Extract bounded diagnostics (the FDC/ES5506 logging helpers currently
   living inside board-glue read/write handlers) so board glue stops owning
   device-level log formatting.
4. Isolate the SCSI stub behind a narrow interface (smallest, lowest-risk
   real subsystem boundary — it's already just a stub).
5. Isolate ES5510 host glue behind a narrow interface (already a thin wrapper
   around a real device; low risk).
6. Isolate ES5506 host glue the same way.
7. Isolate FDC glue the same way (real device already present; more
   surrounding state than ES5506/ES5510, hence ordered after them).
8. Introduce a real `scn2681_device` to *replace* the hand-written DUART
   shadow — separate milestone from any code-moving step, since this changes
   device semantics, not just code location.
9. Introduce an `mc68302_device` — same caveat; almost certainly the
   highest-risk step given how much scheduler/interrupt behavior currently
   flows through the hand-written shadow.

Each step must remain separately buildable/testable and must not combine
"move code" with "replace model" (steps 8 and 9 are model replacements;
1-7 are pure relocations).

## 23. First recommended refactor for the next round

**Step 1 from §22: move pure constants/structures into a header.**

- Files to create: `src/mame/ensoniq/asr10_boot_defs.h` (or similar).
- Files to change: `src/mame/ensoniq/asr10_boot.cpp` (replace inline
  constant/struct definitions with an `#include`).
- Expected lines moved: on the order of 100-300 lines (register-offset
  enums, small lookup tables, ROM-region descriptor structs) — exact count
  requires a follow-up grep pass, not estimated further this round.
- State ownership change: none — these are compile-time constants/types, not
  runtime state.
- Acceptance: `make -j12 SUBTARGET=mess SOURCES=src/mame/ensoniq/asr10_boot.cpp`
  builds with zero warnings introduced, and the established baseline run
  (`current-status.md` §2) reaches `NO INST OR BANK FILES` exactly as before,
  with the pre/post binary producing byte-identical `logerror` output for a
  short bounded regression run (proving zero behavior change).
- Known risks: near-zero — this step cannot change runtime behavior if done
  correctly, since it moves only compile-time declarations.
- Stop condition: if the constants/structs turn out to be entangled with
  macros or `constexpr` functions that reference `asr10_boot_state` members,
  stop and re-scope — that would no longer be a "pure" move.

This is deliberately the least risky, most mechanical possible first step,
chosen specifically because it reduces file size without touching any
device semantics, matching the explicit "prefer initial steps that reduce
file size without changing hardware semantics" guidance.

---

## 24. Confirmation

No C++ source file was modified to produce this audit. No document was
deleted. No historical document was rewritten. All proposed moves, merges,
and deletions in this file are recommendations only.
