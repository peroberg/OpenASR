# Interrupt Topology: Firmware Support Without A Possible Source

Method, stated up front: this document does not fill in the interrupt
model register by register. It looks for handlers firmware has
installed for sources this project's model can never actually drive —
that is how missing hardware is found without guessing from a
datasheet. Factor two and E2 are untouched by this task.

## Del 1 — All 256 Vectors, Four Checkpoints, Classified

**Status: delvis.** The interrupt-relevant slots (CPU exception
vectors, autovectors, TRAPs, the 16 GIMR/SIB slots, and the two
external-vector hits) got the full four-checkpoint, plausibility-tested
treatment below. The rest of the 256-entry table — decimal 48-63 and
88-255, i.e. everything outside the ranges just named — was only
diffed for stability, not individually classified slot-by-slot against
the plausibility test. That remainder is demonstrably **not** vector
data (see below) and irrelevant to the gap hunt, but "irrelevant" is a
conclusion from the diff, not from having classified all 256 entries
one by one as the task literally asked. Recorded honestly rather than
rounded up to done.

Dumped the full vector table (`$000000-$0003FF`, 256 longword
entries) at four points: shortly after reset, at `FILE 1` (OS fully
loaded), at `FILE LOADED`, and after instrument selection. Full diff
run across all four checkpoints (not a spot-check) using
`/tmp/vtopo_out.txt`, the raw probe output.

**[Verified, corrected] The table is NOT stable overall.** An earlier
draft of this document claimed full stability from reasoning before
the four-way diff had actually been run; the diff itself disproves
that for large parts of the table:

| Checkpoint | Zero (`$000000`) | Generic stub (`$F882DA`) |
|---|---:|---:|
| reset | 184 | 34 |
| file1_os_loaded | 96 | 35 |
| file_loaded | 76 | 35 |
| selected | 72 | 35 |

Vectors 0-1 (reset SP/PC, read once by the CPU at power-on, not part
of the interrupt topology) and a large block from roughly decimal 48
through 239 change repeatedly between every checkpoint pair. Content
sampled from that block decodes as ASCII text ("...LOAD DIRECTORY...",
"...MENU NK...", instrument/patch name fragments) — this is a much
larger-scale instance of the *same* pattern that already disproved
vectors 4 and 11: the vector table's tail is reused as ordinary
RAM/string storage by firmware, not as vectors. **[Verified]** this
churn does not touch any vector this document treats as a real
handler: vectors 2-47 (exceptions, autovectors, TRAPs), 60-61, and
64-86 (all 16 GIMR/SIB slots plus the two confirmed external-vector
hits) are byte-for-byte identical across all four checkpoints —
confirmed by direct extraction and diff, not assumed. One single
exception inside the churn range is worth naming precisely so it
isn't mistaken for a topology finding: vector 59 (decimal, inside the
already-disproven 48-63 block) happens to read `$000000` at reset and
coincidentally equals the generic stub's address `$F882DA` from
file1_os_loaded onward — a data collision, not an installed handler,
consistent with the rest of that block being non-vector content.

The premise that the table fills in "progressively as the OS installs
handlers" turned out to be true, but for the *data* region, not the
*handler* region: every slot this document treats as a real interrupt
handler was already present at the reset checkpoint and never changed.

**Plausibility test applied** (the same one that closed vectors 4 and
11 previously): an address is not trusted as a real handler merely
because it looks like an opening opcode. Classification, applied to
the interrupt-relevant ranges named above:

- **verklig handlare**: non-zero, word-aligned, and either (a) inside
  ROM/high-RAM code territory (`$F80000-$FFFFFF`) with content that
  cross-references *other independently established facts* (register
  addresses, shared subroutine calls, EOI values already known from
  static analysis) — not just "looks like an opcode" — or (b) already
  directly confirmed via observed IACK activity.
- **oinstallerad**: `$000000`, or (for the churn region) plausible
  ASCII/data content — not individually confirmed slot-by-slot for
  every entry in that region, see status note above.
- **data (disproven)**: non-zero, word-aligned, but pointing into the
  SIB register window (`$FC6000-$FC6FFF`) with no plausible code
  content — the exact pattern that already disproved vectors 4 and 11
  in an earlier turn.

Result for the interrupt-relevant ranges only: **34 generic-stub (see
below), 2 disproven as data reuse (vectors 4/11), 8 distinct
CPU-exception stub entries (vectors 2-3, 5-10 — each a dedicated
4-byte error-code stub, not the shared fallback, but not a hardware
source either), 16 TRAP software primitives (vectors 32-47, excluded
from the topology as instructed, see below), and 8 real hardware-source
handlers proper: the 5 unmasked SIB sources (PB9/PB10/PB11/SCC1/SCC2),
IDMA, Timer2, and the two confirmed external vectors (IRQ1/IRQ6) —
these last 8 are what Del 3/Del 5 are built on.** The 184-uninstalled
figure from the original draft was a reset-only snapshot presented
without that qualifier; corrected above with all four checkpoints
shown.

### The generic stub

34 vectors (bus error family members not otherwise specialized, most
spurious/reserved slots, several GIMR-computed slots with no dedicated
handler) all point to the *identical* address, `$F882DA` — a generic
"unhandled — raise error code" stub (`moveq #$8B,D0 / ...`), part of
the already-catalogued `exception_stub_table` (`$F882AA-$F882DA`,
`subroutine-index.md`). Landing on this address is equivalent to
"oinstallerad" for this task's purposes: firmware has a fallback, not
a dedicated handler, for that source.

### Vectors 4 and 11, re-confirmed disproven

`$000010` (Illegal Instruction, vector 4) → `$FC6000`; `$00002C`
(Line-1111 Emulator, vector 11) → `$FC6014`. **[Verified, re-confirmed]**
Both point directly into the SIB register window — the exact pattern
this project already established as data reuse, not real handlers, in
an earlier turn's absolute-search sweep. Unchanged by this task.

### TRAP vectors, excluded from the interrupt topology as instructed

Vectors 32-47 (`TRAP #0`-`#15`) are software, not interrupts, and are
listed here only to keep them out of the gap analysis below, not as
candidates for it: `$88280` (exception_tail), `$87F76`, `$88066`,
`$88078` (TRAP #3, enqueue), `$880A2` (TRAP #4, dequeue), `$880B6`,
`$880D6` (TRAP #6, re-arm), `$88108` (TRAP #7), `$8812C` (TRAP #8),
`$88138` (TRAP #9, slot install), `$FF88E8` (TRAP #10), `$FF88E2`
(TRAP #11), `$88174` (TRAP #12, lands inside TRAP #9's own body —
plausibly a shared tail, not separately traced), `$881F6` (TRAP #13 =
"TRAP #D", the deferred-work primitive from
`keyboard-and-sample-bridge-11.md` — confirmed, same address), `$881E6`
(TRAP #14), `$88056` (TRAP #15). All already-known TRAPs match exactly
where previously found; the three new ones (#10/#11/#14/#15) are noted
but not traced further this task, per its own scope.

### Two informational, non-hardware oddities

Vector 60 → `$000FC6`, vector 61 → `$200000`. These fall in the plain
68000's own "unassigned/reserved" vector range (60-63) — no hardware
source, external or SIB, ever fetches them automatically. If firmware
uses these slots at all, it is as a private software jump table, not
an interrupt handler. **[OPEN]**, informational only, not pursued
further — irrelevant to the hardware-gap hunt since no real interrupt
source could ever land here.

## Del 2 — Predicted Vector Per MC68302 Source

**Status: färdigt.** All 16 GIMR/SIB slots predicted and checked
against the live vector table at all four checkpoints (stable
throughout, see Del 1).

From `docs/mc68302/interrupt-source-map.md` (hardware documentation,
read for chip semantics only): `vector = (GIMR bits 7:5 << 5) |
source_low_5`. **[Verified]** `GIMR = $8040`, written once at
`t≈0.002s`, never changed in any measurement this project has taken.
`$8040` bits 7-6-5 = `010` (2) → base = `2<<5 = 0x40`, matching the
manual's own "Vector at GIMR=0x8040" column exactly for every source:

| Source | Bit | Predicted vector | Live handler | Cross-check |
|---|---:|---:|---|---|
| PB11 | 15 | `$4F` | `$F88F22` | matches `mc68302-status.md`'s own `PB11 $F88F22 tst.b $0C3B.w` |
| PB10 | 14 | `$4E` | `$F88F06` | matches `mc68302-status.md`'s own `PB10 $F88F06 tst.b $0C3A.w` |
| SCC1 | 13 | `$4D` | `$FF8D56` | body reads control block `$12D8`, register base `$FC6880`, calls `$643C`, EOI `$2000` — all match the pre-existing static-analysis entry for `scc1_isr`, previously only located at its ROM-relative address `$008D56`; this is the same routine's *live, relocated* address (reconciled below) |
| SDMA_BusError | 12 | `$4C` | *(generic stub)* | not installed |
| IDMA | 11 | `$4B` | `$FF87E8` | real handler exists (`jmp $251A.w` opening); **source not unmasked** (bit 11 clear in IMR) |
| SCC2 | 10 | `$4A` | `$FF8D92` | body reads control block `$1320`, register base `$FC6890`, calls `$643C`, EOI `$0400` — matches the pre-existing `scc2_isr` entry (ROM-relative `$008D92`), same reconciliation |
| Timer1 | 9 | `$49` | *(generic stub)* | not installed |
| SCC3 | 8 | `$48` | *(generic stub)* | not installed |
| PB9 | 7 | `$47` | `$F8D072` | matches `instrument-to-otto-runtime.md`'s own `$F8D072` PB9/IRQV handler exactly |
| Timer2 | 6 | `$46` | `$F88F3E` | real, dedicated handler exists; **source not unmasked** (bit 6 clear in IMR) — this is the previously-`[OPEN]` "Timer 2's consumer" address, now located |
| SCP | 5 | `$45` | *(generic stub)* | not installed |
| Timer3 (watchdog) | 4 | `$44` | *(generic stub)* | not installed |
| SMC1 | 3 | `$43` | *(generic stub)* | not installed |
| SMC2 | 2 | `$42` | *(generic stub)* | not installed |
| PB8 | 1 | `$41` | *(generic stub)* | not installed |
| Level4Error | 0 | `$40` | `$F882DA` | the generic stub itself |

**IMR = `$E480`, re-derived from the source table's own bit column,
not trusted from a prior note**: `$E480` = bits 15,14,13,10,7 set.
Cross-referencing those bit numbers against the table's own "Bit"
column gives, independently: **PB11 (15), PB10 (14), SCC1 (13), SCC2
(10), PB9 (7)** — exactly five sources, exactly matching the prior
turn's carried-forward claim, now derived from the bit table itself
rather than trusted from a note.

### Reconciling the address discrepancy (SCC1/SCC2)

The pre-existing `subroutine-index.md` entries for `scc1_isr`/`scc2_isr`
cite `$008D56`/`$008D92` — this task's live vector-table dump shows
`$FF8D56`/`$FF8D92` installed instead. **[Verified, reconciled, not
contradictory]**: `$008D56`/`$008D92` are this routine's static
ROM-file locations (per `subroutine-index.md`'s own note: *"Identisk
adress i både V1.61 och V3.50"*, describing the ROM copy); `$FF8D56`/
`$FF8D92` is the same code's live, OS-relocated runtime address,
confirmed by matching content (control block pointers, register
bases, the shared `$643C` call, and the exact EOI bit values) — the
same routine, two addresses for two different lifecycle stages, not
two different findings.

## Del 3 — Cross-Reference Table

**Status: färdigt** for the 16 GIMR/SIB sources this table covers.
Physical PB9/PB10/PB11 wiring on real hardware remains **[OPEN]**, as
stated below.

| Vector | Source (GIMR formula) | Unmasked in IMR | Handler installed | Observed IACK (level 4) | Modeled in this driver |
|---|---|---|---|---|---|
| `$4F` | PB11 | yes | `$F88F22` | never (0 level-4 IACKs, whole run) | no |
| `$4E` | PB10 | yes | `$F88F06` | never | no |
| `$4D` | SCC1 | yes | `$FF8D56` | never | no |
| `$4C` | SDMA_BusError | no | — (stub) | never | no |
| `$4B` | IDMA | **no** | `$FF87E8` | never | **partially** — IDMA transfers themselves are implemented; the interrupt path is not, and firmware itself keeps it masked |
| `$4A` | SCC2 | yes | `$FF8D92` | never | no |
| `$49` | Timer1 | no | — (stub) | never | no |
| `$48` | SCC3 | no | — (stub) | never | no |
| `$47` | PB9 | yes | `$F8D072` | never | no |
| `$46` | Timer2 | **no** | `$F88F3E` | never | no — but firmware keeps this masked too, same as IDMA |
| `$45` | SCP | no | — (stub) | never | no |
| `$44` | Timer3/watchdog | no | — (stub) | never | no |
| `$43` | SMC1 | no | — (stub) | never | no |
| `$42` | SMC2 | no | — (stub) | never | no |
| `$41` | PB8 | no | — (stub) | never | no |
| `$40` | Level4Error | n/a | `$F882DA` (itself the stub) | never | n/a |

**The one row that matters, per the task's own framing**: **PB11,
PB10, SCC1, SCC2, and PB9** are unmasked (firmware genuinely wants
these), each has a real, content-verified, dedicated handler
(installed at reset, stable throughout), and **none can ever fire in
this project's model — level-4 IACKs never occurred once in this
task's entire measurement, across boot, load, and instrument
selection.** IDMA and Timer2 also have real dedicated handlers but are
currently masked *by firmware itself*, not by this model — a different
category, not a gap this model is responsible for.

**PB9/PB10/PB11 wiring on real ASR-10 hardware**: not established this
task. `mc68302-status.md`'s own prior work already flags PB9 as
"differs from PB10/PB11" (a real ISR clear at `$0080`) and both
PB10/PB11's handlers as merely testing local flag bytes (`$0C3A`/
`$0C3B`) without an identified decrementer or consumer — this task
adds the *vector-level* confirmation that all three are genuinely
wired to real, distinct, non-generic handlers, strengthening rather
than resolving that prior `[OPEN]` status.

## Del 4 — Autovector Levels

**Status: färdigt.**

**[Verified]** All seven autovector table slots (vectors 25-31,
`$000064-$00007C`) contain only the generic stub (`$F882DA`) at every
checkpoint. **[Verified]** IACK counts by level, across the full
run: level 1 = 33 (matches the already-modeled, already-wired IDMA/
storage-completion IRQ1 path, vector `$51`, supplied directly by the
MC68302's own external-request vectoring — not autovectored); level 6
= 8,442 (the already-modeled DUART IRQ, vector `$56`, same
mechanism); **levels 2, 3, 4, 5, and 7 = zero, throughout.** Level 4
being zero is the same fact Del 3's table already shows per-source
(no SIB-based INRQ ever fires). Level 7 (NMI): zero IACKs, generic
stub at its autovector slot, no dedicated handler anywhere in the
256-vector dump traceable to it — **no evidence of an NMI source in
this firmware's current configuration.**

Per the MC68302's own external-request model (`docs/mc68302/
interrupt-source-map.md`): levels 1, 6, and 7 are the *only* levels
that can carry an MC68302-generated vector at all in this chip;
levels 2, 3, and 5 have no such encoding and would need either an
external vector or true autovectoring to be used — the empty,
generic-stub-only slots at those levels are exactly what "never
wired, never used" looks like, not a gap.

## Del 5 — Ranked Gap List

**Status: färdigt** for the ranking itself (all candidate sources
covered by Del 2/3/4, none omitted). Each gap's *physical* explanation
(what SCC1/SCC2 carry, what drives PB9/10/11) is **[OPEN]** and stated
as such per item, not resolved by this document.

1. **SCC1 (`$FF8D56`, vector `$4D`) and SCC2 (`$FF8D92`, vector `$4A`)
   — best evidenced, and now confirmed in depth (`scc-hardware-gap.md`,
   follow-up task).** Firmware unmasks both, and both handlers are
   full, content-verified routines (not stubs): correct per-channel
   control-block pointer, correct per-channel register base, a shared
   `$643C` receive routine, correct per-channel EOI value written back
   to ISR. **Confirmed dynamically**: both channels have real,
   standards-shaped 8-entry buffer-descriptor rings in parameter RAM
   (`$FC6400`/`$FC6500`, wrap bit on the eighth descriptor, `MRBLR=
   $0320` matching every buffer's stride exactly) and real register
   configuration (`SCON`/`SCM` with `ENR=1`). **`$643C`
   (`scc_rx_common`) is disassembled**: it tests individual `SCCE`
   event bits, acknowledges each write-1-to-clear, and indexes into
   the exact same buffer-descriptor ring by `counter*8` — genuine
   character/frame-level serial reception, not a generic queue. This
   is firmware actively expecting two working serial communication
   channels this project's MC68302 model does not implement at all.
   **What modeling this would need**: an SCC implementation (channel
   registers, buffer-descriptor traversal honoring the ready/wrap bits
   exactly as measured, IMR-gated INRQ delivery) wired into the
   existing `mc68302_device` — **and, unresolved by any measurement so
   far, a real byte source on the ASR-10 board**, since MIDI and panel
   serial are already independently covered by the DUART. See
   `scc-hardware-gap.md` Del 5 for the full scope/risk breakdown.
   **Update, follow-up round:** both of `scc_rx_common`'s branches are
   now traced to their targets — a ring-full condition raises a real
   firmware error (`trap #0`, codes `$5`/`$6` → error `$2D`/`$2E`, 45/46
   decimal, undocumented elsewhere), and a second event condition
   (`SCCE` bit 2) silently disables **both** SCC channels via
   `$F8C0E6` (receiver off, both IMR/IPR bits cleared) with no error
   and no display message — a real, named, silent-abort path, not a
   guess. Neither branch reaches an actual "data arrived, here it is"
   consumer; that path is still `[OPEN]`. Buffers themselves
   (`$00F76600`+/`$00F74B00`+, a third RAM pool distinct from low RAM
   and DPRAM) stay zero-initialized and never fill at any checkpoint,
   confirming from the data side what the zero-IACK count already
   showed from the interrupt side: no real reception ever completes.
   **Update, board-source round (`scc-board-source-question.md`):**
   a fine-grained (20ms) display poll across boot — never done before
   at this resolution — shows this project's own model already
   displays "TUNING KBD - HANDS OFF" then "KEYBOARD TUNED" at
   `t≈15.06-15.2s`, and each of three display-phase transitions in that
   window (including the transition to `FILE 1` itself) is preceded,
   within single-digit-to-low-double-digit milliseconds, by an SCC1/
   SCC2 arm/disarm cycle (`SCM1`/`SCM2` toggling `$7033`↔`$703B`, `IMR`
   toggling to `$E480`) — zero level-4 IACKs throughout, confirming the
   sequence completes by timing out on each arm cycle, never by a real
   interrupt. This is a direct, repeated, dynamic timing correlation
   between the keyboard-calibration UI and real SCC activity, not
   circumstantial.
   **Observable effect if modeled**: level-4 IACKs would begin
   occurring (currently exactly zero); what SCC1/SCC2 actually carry
   remains open pending identification of that byte source, not pending
   further disassembly of the firmware side, which is now done.
2. **PB9 (`$F8D072`), PB10 (`$F88F06`), PB11 (`$F88F22`), vectors
   `$47`/`$4E`/`$4F` — well evidenced, smaller scope.** All three
   unmasked, all three have distinct, real (not generic-stub)
   handlers, none can fire. PB9's handler is already substantially
   understood (`instrument-to-otto-runtime.md`'s ES5506-like voice/IRQV
   service); PB10/PB11's handlers are thin (test a flag byte, per
   `mc68302-status.md`) with no identified setter — this task adds
   confirmation that the *vector-level* wiring is real and complete,
   not that the *physical* PB9/10/11 sources are identified. **What
   modeling would need**: identifying and driving whatever physical
   ASR-10 signal(s) toggle PB9/10/11 (not established here).
   **Observable effect**: level-4 IACKs for these specific sources,
   and — for PB9 specifically — the already-mapped voice/IRQV
   callback chain finally executing.
3. **IDMA (`$FF87E8`, vector `$4B`) and Timer2 (`$F88F3E`, vector
   `$46`) — real handlers exist, but not a gap this model is
   responsible for.** Both sources are masked by firmware's own IMR,
   not by anything this project fails to model; their handlers'
   existence-without-firing is expected, current behavior, not
   missing hardware. Timer2's handler address is new information —
   it directly answers the long-standing `[OPEN]` "Timer 2's
   consumer" question at the address level (not yet traced further).

## Evidence Levels, Stated Separately

- **[Verified]**: stability across four checkpoints for every vector
  this document treats as a real handler or generic stub (exceptions,
  autovectors, TRAPs, all 16 GIMR/SIB slots, both external-vector
  hits) — not for the table as a whole, which churns as non-vector
  data outside that range (see Del 1); the GIMR
  vector formula and its match to all installed handlers; IMR's five
  unmasked bits, independently re-derived from the source table; zero
  level-4 IACKs across the entire measured run; all seven autovector
  slots generic; vectors 4/11 re-confirmed as SIB-window data reuse;
  SCC1/SCC2/PB9/PB10/PB11 handler content cross-referencing prior
  independently-established facts (control blocks, register bases,
  EOI values, the shared receive routine).
- **[Likely]**: TRAP #10/#11/#14/#15's addresses are genuinely TRAP
  handlers (consistent placement, not disassembled further); vectors
  60/61 as a private software jump table rather than anything
  interrupt-related.
- **[OPEN]**: what SCC1/SCC2 physically carry on real hardware (the
  gap this ranked list exists to name, not resolve); which physical
  ASR-10 signals drive PB9/PB10/PB11; whether IDMA/Timer2 would ever
  be unmasked later in a longer run than this task measured; the ISR
  register's live value (`$0080`, PB9's bit set) despite zero observed
  level-4 IACKs — noted, not chased, plausibly an MC68302 model detail
  unrelated to this task's scope.

## Verification

- `docs/asr10/regression-test.sh`: run this pass, 9/9 (boot, display,
  button, button_upper, nodisk, file_loaded, mc68302_guards,
  note_audio, note_audio_wav) — unaffected, no code change this task,
  Lua probes and documentation only.
- No `mem_map` change, no clock change, no ES5510 activation. Factor
  two and E2 untouched, as instructed.
- No `-log`; every loud signal used Lua `print()`.
- No fork with an open mandate.
- `git diff --check`: clean.
- All four checkpoints re-diffed against the raw probe output
  (`/tmp/vtopo_out.txt`) before this pass's edits, correcting an
  overclaimed "table completely stable" statement in Del 1 that had
  been written from reasoning before the four-way diff was actually
  run — see Del 1's corrected section and status markers throughout
  this document.
- `src/devices/sound/es5506.h` and `.project`'s working-tree
  modifications are pre-existing, unrelated to this task, and are not
  part of this commit.
