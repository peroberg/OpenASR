# ASR-10 Filesystem/Browser Address Map (PASS 1 complete + PASS 2 partial)

This document is **PASS 1 (complete) + PASS 2 (partial)**, not a finished
PASS 2. Section 4 adds a genuine live-access table gathered under a new,
off-by-default instrumentation flag (`ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE`)
and reports what PASS-1 static hypotheses live data confirmed, refuted, or
narrowed. Section 4.7 lists what remains open — do not read this document
as a closed investigation.

Detailed address inventory supporting `docs/asr10/current-blocker.md`. All
addresses are ROM-side (`0xf80000-0xfbffff`) unless marked `lowmem` (CPU
`0x000000-0x000fff` range, backed by `m_lowmem_shadow` in
`asr10_boot.cpp`). Evidence classes:

- **CONFIRMED** — read directly from ROM disassembly and/or cross-validated
  against existing live MAME-side instrumentation.
- **INFERRED** — a semantic label assigned from code shape/constants that is
  very likely correct but has not been exercised live.
- **CANDIDATE** — a plausible but unverified interpretation; flagged
  explicitly as uncertain.
- **LIVE** — observed firing in an actual `./mess asr10booth` run.

**Absolute-short sign-extension rule (load-bearing for section 4.5):** on
this real 68000, a `jsr`/`jmp $xxxx.w` (absolute-short addressing) operand
with bit 15 set sign-extends to `0xFFxxxx`, not `0x00xxxx`. This was already
documented (`current-blocker.md` section 10) for data addressing, and a
first pass of section 4 initially missed that it applies equally to code
addresses reached via absolute-short `jsr`/`jmp` — six of slot 0's own
jump-vector targets, and five of `f894a4`'s internal vector calls, were
originally dumped at the wrong (low) address as a result. Corrected in
4.5; kept prominent here since it's easy to re-introduce.

Static disassembly source: full ROM disassembly built earlier this session
at `/Users/paroberg/.claude/jobs/b32a764f/tmp/asr10_full_rom.dasm`
(`unidasm -arch m68000 -basepc 0xf80000` over the correctly hi/lo-interleaved
256 KB ROM image; word reconstruction verified against known-good
disassembly at `f87f92`).

## 1. Compact diagram

```
sector I/O (uPD765-class FDC, CONFIRMED)
  fc4001 (aux/cmd port)        fc4003 (fifo/data port)
        |                              |
        v                              v
  fb8cfc "send N via fc4001"     fb8cda "send N via fc4003"
  fb8d1e/fb8d40 status+timeout->fb81ae (error -> lowmem $49d/$4ae)
  fb8d78 "read N result via fc4003"
        |
        v
  fb7c5a  SENSE INTERRUPT STATUS (cmd 0x08)
  fb7bc8  cmd 0x1e (1-byte, purpose not yet named)
  fb7c3c  cmd 0x04,0x00 (2-byte)
  fb7ca6  SPECIFY-family param block (0x88/0xf3/... into $4e8-$4ec)
  fb8dd8  init: aux reset (0x36) -> delay -> fb7c5a
        |
        v
  fb8c6e  SEEK wrapper: target=$49e.w(lowmem), cache=$4ac.w(lowmem)
          cmd 0x0f (SEEK, 3 bytes) if target != cached track;
          on completion, $4ac.w <- $49e.w (track-cache update)
        |
        v
  fb846a  generic "load one unit via FDC into ($40e.w)" (retry ct $4a9.w)
  fb834e  loop: 5x fb846a, advance $40e.w by 0x800 (2048B) each time,
          outer index written to $49e.w  -> CANDIDATE multi-cluster/
          multi-group table loader (2048B "cluster"?)
        |
        v
  fb82a4  boot/format sector load+validate:
          $49e.w=0x20 (track 32) -> fb8c6e (seek) -> $40e.w=$544 ->
          fb846a(D6=5) -> verify $544..$743 (512B, one sector) uniform,
          else error; AND check fb81e0[$49b.w] & buf[0] == fb81e0[$49b.w],
          else $49d.w=0x0e (format/media-id mismatch)
          -- LIVE: never entered in the captured run (section 4.4); the
          confirmed live loader is fb8ab6/fb8a54 instead (section 4.4).
        |
        v
  fb83e4  classify $49b.w (raw device/media byte) -> $40a.w = (raw-1)/3
          (device CLASS 0/1/2), $49b.w overwritten with remainder
          (index within class) -- selects among 3 vector-table sets
          (fb81ea/fb81f6 vs fb8202/fb8208)
        |
        v
  fb895a  GENERIC RANGE READ: A2 = $4fe (lowmem, FIXED global descriptor)
          D2=$416.w (lowmem, start), D3=$41a.w (lowmem, end); bounds-check
          against (A2+0xe) [-> lowmem $50c, "size/limit" field of the
          current descriptor]; count = D3-D2+1; if $4b0.w(lowmem) mode
          flag set: bulk path via fbadb2 (unexamined), else: fb7e04
          (unexamined) -> fb8c6e (seek) -> fb89f6 (unexamined, likely
          READ DATA cmd 0x46 issuance) -- CANDIDATE "load current
          entry's data" path, reused for boot catalog, directory, and
          (INFERRED) file data
        |
        v
  fb8938/fb8940/fb8948  mode dispatch: $4b3.w(lowmem) <- 2/1/0, then
          fb894e copies $40a.w(lowmem, "current position cursor") into
          $416.w/$41a.w and falls into fb895a -- CANDIDATE "read
          current/next/previous entry" (mode codes not yet confirmed
          against observed UI behavior)
        |
        v (UI side, distinct code region, near PANEL_ENQUEUE f89a7a)
  f894a4  "get entry N" accessor over table at lowmem $544, stride 0x1a
          (26) bytes, bounds N<=0x27 (39); reads entry+0 as validity
          word; on success dispatches via vectors $88c0.w/$88c4.w
          (sign-extend to ff88c0/ff88c4 -- see 4.5); on failure (out of
          range or empty) sets error return (A2=$16ee, carry set)
          -- LIVE: canonical entry (f89494) and table-read (f894b4) both
          never observed to fire (section 4.1).
  fb7ba6  scans same $544 table in reverse (idx 38..1, stride 0x1a),
          sets lowmem $4bf.w if ANY entry nonzero (CANDIDATE: "table has
          a valid entry" flag)
  fb7b90  reads $544.w as scalar, compares to 0x20, sets/clears lowmem
          $4be.w (CANDIDATE: "entry 0 type == category marker" flag)
        |
        v
  PANEL_ENQUEUE (f89a7a/f89a8a, CONFIRMED, pre-existing hooks) -> ring
  buffer $378-$3b7 -> Channel B DUART -> panel text -> "FILE n <name>"
  display (harness-side reconstruction in m_panel_text)
```

The bottom two stages (entry decode -> "FILE n <name>" format string
construction -> enqueue call site) are the biggest PASS-1 gap: `f894a4`'s
canonical entry (`f8948e`) has no static caller found via direct `bsr`/
`jsr` either. Section 4.1 enumerates candidate entry mechanisms in detail.

## 2. Routine / state-field table

| Address/Field | Role | Class | Live? |
|---|---|---|---|
| `fc4001` | FDC aux/command register (CPU-mapped, `upd72069_fdc_w`) | CONFIRMED | yes (existing `ASR10_FDC_CMD*`/`ASR10_FDC_OS` logs) |
| `fc4003` | FDC fifo/data register | CONFIRMED | yes |
| aux cmd `0x36` | software reset | CONFIRMED (C++ `detail=... "upd72069_software_reset"`) | yes |
| fifo cmd opcode `&0x1f` length table (`0x03/0x0f`=3B, `0x04/0x07/0x0a`=2B, `0x05/0x06/0x09/0x0c/0x11/0x19/0x1d`=9B, `0x0d`=6B) | NEC765-class command framing, already implemented in `upd72069_fdc_w` (asr10_boot.cpp:3742-3768) | CONFIRMED | yes |
| `fb8cda`/`fb8cfc` | send N bytes via data/aux port | CONFIRMED | — |
| `fb8d1e`/`fb8d40` | status-check + timeout -> error | CONFIRMED | — |
| `fb8d78` | read N result bytes via fc4003 | CONFIRMED, cross-validated vs. existing live hook at `pc==0x00fb8db2` in `asr10_boot.cpp` | yes |
| `fb81ae` | shared error setter (D2=0x0d fixed, D3=sub-code -> lowmem `$4ae.w`) | CONFIRMED | — |
| `fb7c5a` | SENSE INTERRUPT STATUS (cmd 0x08) | CONFIRMED (matches uPD765 semantics exactly: no params, 2 result bytes) | — |
| `fb8c6e` | **SEEK wrapper** | CONFIRMED | yes — 20 entries, all pre-milestone-A (4.4) |
| lowmem `$49e.w` | seek target track/cylinder (also reused as a generic small scratch by some callers) | CONFIRMED (written by every `fb8c6e` caller before the call) | — |
| lowmem `$4ac.w` | cached "current track" (seek is skipped if target==cache; `0xff`="unknown/invalid") | CONFIRMED | yes — writer `fb8cc2` confirmed exactly (4.4) |
| `fb846a` | generic "load one FDC unit into `($40e.w)`" | CONFIRMED (3 call sites) | yes — **never entered**, whole run (4.4) |
| lowmem `$40e.w` | **current destination buffer pointer** for FDC loads (reused/reassigned per call site) | CONFIRMED | yes — real writer `fb8a54`, not the call sites this row names (4.4) |
| lowmem `$406.w` | secondary saved buffer-pointer/byte-count slot (dual use observed at different call sites) | CONFIRMED presence, INFERRED dual role | yes — 2 writes, `fb90dc` (4.4) |
| `fb82a4`-`fb8302` | boot/format sector load+validate into lowmem `$544` | CONFIRMED (static) | yes — **never entered**, whole run (4.4) |
| lowmem `$544` (span `$544`-`$8ff`) | **shared sector/table buffer** — used once as a raw 512B sector buffer (uniformity+format-ID check) and separately as a 0x1a-byte-stride entry table (up to 40 entries) by UI-side code | CONFIRMED span+dual use, CANDIDATE "this is the DIR buffer" | yes — fully populated (all 40 entries) before milestone A by `fb8ab6`, stable after (4.4) |
| lowmem `$49b.w` | raw device/media-type byte pre-classification; **overwritten** post-`fb83e4` to hold class-remainder index | CONFIRMED | yes — writer `fb90dc`, not `fb83e4` (4.4) |
| lowmem `$40a.w` | device CLASS selector (0/1/2) after `fb83e4`; **also** reused later as "current position cursor" by `fb894e`/`fb895a` (distinct use, same address, different phase) | CONFIRMED presence, two distinct roles | yes — writer `fb8a44` (4.4) |
| `fb834e` | nested loop: 5x `fb846a`, `$40e.w += 0x800` each, outer index -> `$49e.w` | CONFIRMED (static) | not observed (implied by `fb846a`'s zero count) |
| `fb895a` | **generic bounds-checked range reader**, A2=lowmem `$4fe` (fixed descriptor), bounds field at `(A2+0xe)` = lowmem `$50c` | CONFIRMED (static), 12+ call sites across fb88xx/fb96xx/fb97xx/fb9axx/fbaxxx/fbbxxx | yes — 10 entries, all pre-milestone-A (4.4) |
| lowmem `$4fe` | **current/global descriptor struct base** (fixed address, not per-entry — heavily reused, 20+ references) | CONFIRMED presence, CANDIDATE "current file/entry descriptor" | yes — writer `fb8ab6` (4.4) |
| lowmem `$50c` (`$4fe+0xe`) | descriptor size/limit field, used as an upper bound for range reads | CONFIRMED (relative access `(0xe,A2)`) | yes — writer `fb8ab6` (4.4) |
| lowmem `$416.w`/`$41a.w` | range start/end passed into `fb895a` | CONFIRMED | not instrumented |
| lowmem `$4b0.w` | mode flag selecting bulk-DMA-like path (`fbadb2`, unexamined) vs. seek+read-command path (`fb7e04`+`fb8c6e`+`fb89f6`, both largely unexamined) | CONFIRMED presence, CANDIDATE meaning | not instrumented |
| `fb8938`/`fb8940`/`fb8948` | set lowmem `$4b3.w` to 2/1/0 then fall into `fb894e` | CONFIRMED (static) | yes — `fb8948` confirmed as a writer (4.4) |
| lowmem `$4b3.w` | mode selector, 60 references ROM-wide — CANDIDATE "read current/next/previous entry" op code | CANDIDATE | yes — 187 writes, high traffic (4.4) |
| lowmem `$40a.w` (2nd role, see above) | "current position cursor" copied into range-read bounds | CANDIDATE | see above |
| `f894a4`-`f894f4` | **"get entry N" accessor** over `$544` table, stride 0x1a, bound 0x27; canonical entry `f8948e`, dispatches via vectors `$88c0.w`/`$88c4.w` (sign-extend, see 4.5) on success | CONFIRMED (static); canonical entry has no static caller found — see 4.1 for the full enumeration | yes — canonical entry (`f89494`) and table-read (`f894b4`) both zero, whole run (4.1/4.4) |
| `fb7ba6` | scan `$544` table (idx 38..1) for any nonzero entry -> lowmem `$4bf.w` | CONFIRMED (static) | yes — writer confirmed (4.4) |
| `fb7b90` | `$544.w` scalar == 0x20 -> lowmem `$4be.w` | CONFIRMED (static), CANDIDATE meaning | yes — writer `fb7b9a` confirmed exactly (4.4) |
| lowmem `$49d.w`/`$4ae.w` | shared FDC/filesystem error code / sub-code | CONFIRMED (established in earlier sessions, still holds) | yes (existing `ASR10_TASK1_DISK_SIG` trace) |
| lowmem `$4b2.w` | validity gate flag read at `f89494`; set by `fb9332` (`fb9300`-`fb9342`, error-checked init chain) — see 4.2 | CONFIRMED (static and live) | yes — `0xff` (true), stable at every milestone (4.2/4.4) |

Existing pre-existing, already-committed live instrumentation directly
relevant to this map:
- `ASR10_FDC_OS` (asr10_boot.cpp:3779) — logs opcode/track/sector for every
  9-byte-class NEC765 command, i.e. every READ/WRITE DATA-family issuance.
- `ASR10_FDC_CMD46` (asr10_boot.cpp:3801) — start-of-transaction detail for
  the specific `0x46` READ DATA command.
- `ASR10_POST_LOADING_FDC` (asr10_boot.cpp:3833) — fires on **every** FDC
  register write once the "LOADING SYSTEM" prompt has been seen, including
  the live `m_panel_text` content.
- `ASR10_TASK1_DISK_SIG` (asr10_boot.cpp:3028-3043, gated by
  `ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE`) — tracks lowmem `$49c` (error
  flag), `$4ae` (sub-code), `$944` ("sector1_buffer_start"), `$954`
  ("signature_compare_word0"), `$4b8` ("disk_valid_flag"). `$944` is a
  **different** buffer address than the `$544` table — `fb88f2`'s caller
  sets `$40e.w=$944` before calling `fb895a`, a separate use of the same
  generic range-reader, not the same buffer as `$544`.

## 3. Filesystem shape (PASS 1 item 5)

**Uncertain — not established.** The available evidence is more consistent
with a category-filtered, largely flat structure with FAT-like clustered
reads than with a hierarchical/nested-directory filesystem, but this is a
leading hypothesis from partial static tracing, not a proven conclusion.
Do not treat "flat, category-filtered" as confirmed:
- No code found that pushes/pops a "path" or walks parent/child
  directory links.
- The `$544` entry table (stride 0x1a, up to 40 entries) is a flat array,
  not a tree.
- `fb834e`'s 0x800-byte (2048B = 4x512B) per-unit advance is consistent
  with a cluster-based allocation table (FAT-like), but no explicit
  FAT/cluster-chain-follow code (i.e. "read next cluster number from
  cluster N's slot") was identified in the routines examined.
- The previously-documented ROM string table at `~f8ba80`
  ("FACTORY SNDS"/"MY SOUNDS"/"FACTORY BNKS"/"MY BANKS"/"FACTORY SEQS"/
  "MY SEQUENCES") is a fixed 6-entry UI category menu, consistent with
  "category" being a UI-level filter applied on top of the flat entry
  table, not a filesystem-level directory. This remains CANDIDATE, not
  CONFIRMED, since the code that cross-references category selection
  against `$544`-table entries was not located.

Nothing in section 4's live pass bears on this question; it remains open.

### Note on the error-string approach (PASS 1 item 2)

`"DIR SEEK ERROR"`/`"FILE SEEK ERROR"`/`"DIR buffer"`/`"FAT buffer"`/
`"BACKUP FILE"` (found on `V161.img` in an earlier session, at disk byte
offsets ~0x16d69/~0x17489) are **not ROM-resident** — not found in the
interleaved ROM image by any encoding tried. Static ROM disassembly
therefore cannot show the code that references them. The filesystem layer
above was instead reached by tracing forward from the mapped low-level FDC
command engine and backward from the UI's `$544`-table accessor — a
different route than PASS 1 item 2 specified, reaching the same layer.
Locating the actual error-string referencing code would require a live
string-address scan of loaded RAM, or disassembling the loaded overlay
image directly (not attempted).

## 4. Live verification (PASS 2)

New diagnostic flag: `ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE` (env var,
off by default — verified via Gate 1, see 4.6). All state lives in one
`fsb_state` struct (`m_fsb` member); code-range dumps are table-driven
(`FSB_DUMP_TARGETS`) rather than one bool per target. Captured from
`./mess asr10booth -flop1 floppies/asr10booth/V161.img -seconds_to_run 55
-log` with `ASR10_DIAG_PANEL_AUTORESPOND=1
ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE=1 ASR10_EXPERIMENT_TUNING_STALL_TRACE=1
ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE=1`. Milestone log anchors (also
usable to locate these moments in a fresh capture): A = panel text first
recognizable as `TUNING KBD`; B = first `K` of `KEYBOARD TUNED` enqueued;
C = `ASR10_QUEUE_PRODUCER_CODE_DUMP trigger_pc=f880fc`; D = each of the
five `ASR10_DISPATCHER_RTE_FIRST_PC` events in the post-tuning burst
(slots 1, 3, 0, 4, 5); E = `ASR10HANG reason=max_poll_count` at the run's
55-second cutoff. Raw log: `/Users/paroberg/.claude/jobs/b32a764f/tmp/fsb_live3_error.log`
(supersedes two earlier captures in the same directory, `fsb_live_error.log`
and `fsb_live2_error.log`, kept only for reproducibility).

### 4.1 `f894a4`: canonical entry vs. table-read, disassembled

The routine's canonical entry is `f8948e` (no direct `bsr`/`jsr` caller
found for `f8948e` itself, `f894a4`, or `f894b4` — see the enumeration
below). Full control flow, disassembled:

```
f8948e  jsr   fb813c            ; precondition/setup call
f89494  tst.b $4b2.w            ; canonical entry proxy: reads the validity flag
f89498  bne   f894a4            ; PATH A: flag set -> skip ahead, bypass the vector call
f8949a  jsr   $88c0.w           ; -> sign-extends to ffff88c0 (high-view)
f8949e  bcc   f894a4            ; PATH B: carry clear -> proceed
f894a0  jmp   $88c4.w           ; PATH C: carry set -> early exit, f894a4 not reached
f894a4  cmp.w #$27,D0           ; (reached via A or B)
f894a8  bhi   f894ba            ; PATH D: D0>0x27 -> early exit (error return), table not read
f894aa  lea   $544.w,A0
f894b0  mulu.w #$1a,D0
f894b4  move.w (0,A0),D1        ; the table read
```

Path C has a real memory reference (the gate check at `f89494`) but its
own exit (`jmp $88c4.w`) is a pure control transfer with nothing to tap.
Path D is **entirely register/control-flow** (`cmp.w`, `bhi`, `movea.w
#$16ee,A2`, `ori #1,CCR`, `rts`) — no memory reference exists anywhere on
it, so it cannot be tapped with the read/write-tap technique used
throughout this file. This is a genuine limitation (no working opcode-fetch
breakpoint exists for this core; an RTE-based hook doesn't apply since this
path never traps), reported as such rather than worked around.

**Entry enumeration (item 2).** Checked for `f8948e`/`f894a4`/`f894b4`/
`f894ba`, ROM-wide:
- **Direct BSR/JSR/JMP**: none found.
- **Branch targets**: none found from outside the routine itself (its own
  internal `bne`/`bcc`/`bhi`/`bra` all stay local).
- **Jump-table or callback data**: no occurrence of these addresses as a
  raw 16- or 32-bit constant anywhere in the static ROM image.
- **Computed/address-register jumps**: ~69 `jsr (An)`/`jmp (An)` sites
  exist ROM-wide (generic callback/node dispatch — e.g. the mechanism at
  `f88352`, which loads a callback pointer from a node structure's `+0x14`
  field into A1 before `jsr (A1)`; this is the same *shape* of mechanism
  already documented for the scheduler's slot/node records elsewhere in
  `current-blocker.md`). None was checked against live pointer contents;
  a runtime-loaded callback value in any of these could in principle
  target this routine, and this has **not been exhaustively ruled out**.
- **Loaded high-view pointers**: not checked; would require live
  inspection of specific RAM locations known to hold callback addresses,
  which were not identified this session.

Given this, the entry hook (`FSB_ENTRY_F894A4_ROUTINE`, tapping `f89494`)
proves execution through the *canonical* `f8948e`/`f89494` path
specifically — it says nothing about a hypothetical non-canonical entry
that jumps straight into `f894a4` or later, skipping the gate check. No
such entry was found to exist, but its absence is not proven either.

**Result**: both `f894a4_routine_entry` (canonical entry, `f89494`) and
`f894a4_table_reached` (`f894b4`) recorded **zero** hits, at every one of
the nine milestones, first through last. Precise statement: **no execution
through the canonical `f8948e`/`f89494` entry path was observed, and the
`$0544` table-read path at `f894b4` was never reached.**

### 4.2 The validity predicate: found, shown, evaluated at A-E

The gate flag `f894a4` itself reads (`$4b2.w`) is set by a small family of
routines shaped like this one at `fb9300`-`fb9342`:

```
fb9300  tst.b $49d.w                 ; error flag
fb9304  bne   fb9342                 ; bail (rts) on error
fb9306  movea.w #$4fe,A2             ; descriptor base
fb930a  bsr   fb7e5a
fb930e  bsr   fbb55a
fb9312  tst.b $49d.w
fb9316  bne   fb9342                 ; bail on error
fb9318  bsr   fbb4ec
fb931c  tst.b $49d.w
fb9320  bne   fb9342                 ; bail on error
fb9322  move.l #3,$41e.w
fb932a  move.l #4,$422.w
fb9332  st    $4b2.w                 ; validity flag set true, only if nothing above errored
fb9336  bsr   fb8018
fb933a  clr.w $b50.w
fb933e  bsr   fb7b90                 ; the $544-scalar-vs-0x20 flag routine (section 1)
fb9342  rts
```

A sibling routine at `fb9244`-`fb928a` has the identical shape with
different callees (`fbb494`/`fbb462` instead of `fbb55a`/`fbb4ec`).

**Caller (item 5).** `fb9332`'s containing block is reached only by
falling through from `fb92ce` (`bsr fb9344; tst.b $49d.w; ...`), which has
two direct callers:
- `fb917a`, inside a device-enumeration loop (`fb9154`-`fb91ac`) that
  counts down `$4b0.w` from 8 across up to 3 iterations, calling `fb92ce`
  once per device/media candidate and branching to `fb93f4` or `fb94ec` on
  the result. `fb92ce`-`fb9600` is already named **"task1"** in this
  codebase's own pre-existing diagnostic comments (`dump_loaded_code_range`
  call tags from an earlier session), tying this directly to the existing
  `ASR10_TASK1_DISK_SIG`/`ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE`
  instrumentation.
- `fba828`, reached via `fba816`, itself entered through `movea.l
  $3de.w,A0; jmp (A0)` — a genuine indirect dispatch through a pointer
  stored at lowmem `$3de.w` (one instance of the "computed jump" category
  from 4.1's enumeration). After `st $4b2.w` here, this caller continues
  by setting `$4b3.w=1` (the mode field) and calling `fba856`/`fba87c`
  (not traced further).

Neither caller's own caller (who decides to run "task1" and when) was
traced further.

**Live**, tracked as a field split from the unrelated `$04b3` mode field
it shares a word with (an earlier combined-word tracking conflated the
two — `$04b3` is written 187 times per run by `fb8938`/`fb8940`/`fb8948`,
which would have masked `$04b2`'s own 4-write history): last writer
`fb9332` (exactly the instruction above), 4 writes, 3 reads, value `0xff`
(true) — **identical at every one of the nine milestones, first through
last**. Set true before the earliest snapshot, never cleared again.

**Precise conclusion**: the mapped structures (`$0544` table, `$04fe`/
`$050c` descriptor) are populated, and the firmware validity gate `$4b2`
is true and stable at milestones A through E. This is the full extent of
what the predicate shows; it is not generalized into a broader "browser
init" claim (see 4.3).

### 4.3 Boundary classification

> **Directory/cache structures are populated before tuning, and firmware
> validity gate `$4b2` is true and stable through every milestone — but no
> execution through the canonical `f8948e`/`f89494` entry path was
> observed, and the `$0544` table-read path at `f894b4` was never
> reached.**

This is the fully evidenced statement. "Browser init" is not treated as a
single located routine, so no claim is made about it as a named entity —
what is proven is narrower: the specific, mapped consumption path
(`f894a4`/`f894b4`) does not run through its canonical entry, and the flag
that would let it proceed if it did is true.

### 4.4 Live access table

All counts below are identical at every one of the nine captured
snapshots, meaning every field/routine finished its activity before the
earliest snapshot and none of it was touched again through the whole
tuning-print, six-slot-burst, and idle window.

| Field/routine | Write/entry count | Last writer PC | Last reader PC |
|---|---|---|---|
| `$0544` table | 3478 writes / 536 reads, through entry index 39 (all 40 touched) | `fb8ab6` | `fba4f2` |
| `$04fe` descriptor | 6 writes / 1 read | `fb8ab6` | `fb90dc` |
| `$050c` limit field | 6 writes / 23 reads | `fb8ab6` | `fb896c` |
| `$040e` buffer pointer | 382 writes / 394 reads | `fb8a54` | `fb8a54` |
| `$0406` buffer pointer | 2 writes / 1 read | `fb90dc` | `fb90dc` |
| `$049b` device byte | 2 writes / 1 read | `fb90dc` | `fb90dc` |
| `$040a` position cursor | 33 writes / 86 reads | `fb8a44` | `fb89ea` |
| `$04b2` validity flag | 4 writes / 3 reads, value `ff` (true) | `fb9332` | `fb9332` |
| `$04b3` mode field | 187 writes / 214 reads | `fb8948` | `fb8a48` |
| `$04ac` track cache | 11 writes / 23 reads | `fb8cc2` | `fb8c7c` |
| `$04be`/`$04bf` flags | 3 writes / 3 reads, final `04be=ff,04bf=00` | `fb7b9a` | `fb9404` |
| `fb82a4` entry | 0 (never entered) | — | — |
| `fb846a` entry | 0 (never entered) | — | — |
| `fb895a` entry | 10 (all pre-milestone-A) | — | — |
| `fb8c6e` entry | 20 (all pre-milestone-A) | — | — |
| `f894a4` canonical entry (`f89494`) | 0 (never observed) | — | — |
| `f894a4` table-read (`f894b4`) | 0 (never observed) | — | — |
| FDC command issue | 7 (all pre-milestone-A) | — | — |

**Confirmed exactly as PASS-1 mapped**: `$04ac` writer `fb8cc2`, `$04be`/
`$04bf` writer `fb7b9a`, `$04b3` writer `fb8948`.

**Refuted by live data**: section 1 hypothesized `fb82a4`/`fb846a` (via
`fb82c0`, and `fb834e`'s loop) as the loader that populates the `$0544`
table and `$04fe`/`$050c` descriptor. Live data shows neither is ever
entered. The actual, confirmed live writer of the table, descriptor, and
`$040e` buffer pointer is `fb8ab6`/`fb8a54` — inside the already-documented
(`evidence-tree.md`) CMD46/READ DATA transaction loop (`fb8aa2`-`fb8db2`),
a *different* routine than section 1 proposed. `$049b`'s and `$040a`'s
actual writers (`fb90dc`, `fb8a44`) likewise don't match section 1's
`fb83e4`/`fb894e` guesses. Not resolved: why `fb82a4`/`fb846a` never fire
in this run (unexercised code path vs. mis-attribution during static
tracing).

### 4.5 Slot downstream tracing, corrected

**Slots 1 and 3.** Their resume PCs (`ffc85a`/`ff9106`) come directly from
`m_dispatcher_rte_frame_pc`, already a real 24-bit CPU address — not
sign-extend-eligible, so unaffected by the bug this section corrects.
`read_highview_word()`/`dump_highview_code_range()` confirm
`0xfc6900-0xffffff` is genuine, non-empty, loaded RAM (not ROM, not an
alias, not placeholder — ordinary loader-populated overlay RAM, per this
driver's own `map(0xfc6900, 0xffffff).ram();`). Traced further this pass:
- **Slot 1** (`ffc85a`): touches lowmem device-state flags (`$0e8b`,
  `$0e9a`, `$0e9d`), calls ROM `f9783a`/`f973f0` (not traced), then at
  `ffc8bc` loads `A1 = $23d4` — **the scheduler's own slot-0 base address**
  (matches `current-blocker.md` section 8's documented `slot0 base =
  0023D4`) — and executes **`trap #9`**, the same node-promotion trap
  already documented there. No `$0544`/`$04fe` reference.
- **Slot 3** (`ff9106`): this address is itself one entry in a jump table
  (consecutive `jsr $fff8xxxx.l`/`jmp` entries spanning `ff90ee`-`ff9164`);
  the instruction at `ff9106` is `jmp $fff8f38c.l` (masked `0xf8f38c`,
  ROM). That target writes `$8140` to `$8258.w` (sign-extends `ff8258`) —
  this is the **same `FF8258 = 8140`** value `current-blocker.md` section
  9 already documents and marks unresolved ("the consumer and meaning of
  node type 89A2 remain unknown"). No `$0544`/`$04fe` reference; this
  converges on that pre-existing open question rather than a new one.

**Slot 0's six jump-vector targets — corrected.** All six are `jsr
$xxxx.w` with bit 15 set, so all six actually execute at `0xFFxxxx`, not
the `0x00xxxx` address a first pass of this section dumped:

| Vector operand | Real target | What it actually does |
|---|---|---|
| `$87f2` | `ff87f2` | one entry in a jump table; tail-jumps to lowmem `$2b14`, which itself does a genuine **`jsr (A2)`** (computed jump, contents not traced live) and calls the `$8bb6`/`$8bbc` "return true"/"return false" stub pair below |
| `$bc8e` | `ffbc8e` | genuine code: `cmp.w #7,D2`-gated loop calling `jsr $ffb8e2`/`jsr $ff8cc2` (a shared "look up pointer in table at `$14ac`/`$14aa` indexed by D1" helper) or `jmp $ff8bb6` depending on state |
| `$a26e` | `ffa26e` | **`jmp $711e.w`** — a plain tail-jump to lowmem `$711e`, which loads `A1 = $23ea` (**slot 1's** scheduler base) and executes **`trap #9`** — the same node-promotion trap slot 1 itself uses, now confirmed reached from slot 0's side too |
| `$8864` | `ff8864` | reads/writes lowmem fields `$304`/`$306`/`$332`/`$33b`, then `jmp $f8938e`, which does bit-tests on an A2-relative structure and issues `trap #a` with a service code (`0x74`/`0x75`/`0x76`) in D2 — not traced further |
| `$8bb6` | `ff8bb6` | trivial 4-byte stub: `ori.b #1,CCR` / `rts` ("return true"); adjacent `ff8bbc` does `andi.b #$fe,CCR` / `rts` ("return false") |
| `$9650` | `ff9650` | one entry in a second jump table; tail-jumps to ROM `f90890`, which tests `$33b.w`/`$e70.w` then itself `jsr`s back into the *same* `ff9650` jump table (`jsr $ffff9650.l`) — a re-entrant/sibling-slot call within the table |

**None of the six reference `$0544` or `$04fe`.** A first pass of this
section reported that `$a26e` "writes the literal `$544` into `$46a.w`,
writes `$416.w`, reads `$4bf.w`, then calls `fba0d6`/`fba5a2`/`fb8034`/
`fba5d0`" — **retracted**. That read the wrong (low, sign-extension-
ignoring) address; the real `$a26e` target is the single 4-byte
`jmp $711e.w` above, and that call chain does not exist.

What is now established instead: slot 0's downstream calls repeatedly
reach the scheduler's own `trap #9` node-promotion mechanism (against both
slot 0's and slot 1's base records) and a jump table that converges on the
already-open "node type 89A2" question from earlier in this document's
history. Neither is proven to relate to filesystem/browser work
specifically; both are concrete, real interactions with the same
dispatcher machinery central to the original stall question. `jsr (A2)`
(from the `$2b14` target) is a genuine computed jump whose register
contents were not captured live.

Slot 4's five targets are unaffected by the sign-extension bug (`jsr
$fffxxxxx.l`, absolute-long, no ambiguity) and remain as found: generic
status-bit-test code and further `jsr`s into the `0xfc` device-register
space, no `$0544`/`$04fe` reference.

`f894a4`'s own five internal vector calls (`$88ac`/`$88bc`/`$88c0`/`$88c4`/
`$8984`, sign-extend to `ff88ac`-`ff88c4`/`ff8984`) were never dumped
because their trigger (`f89494`) never fired — consistent with 4.1.

### 4.6 Gate 1 and build

`GATE1_EXIT=0`, `grep -c "ASR10_FDC_TC" error.log` → `0` — unchanged from
the historical baseline (a flagless 6-second `-seconds_to_run` boot),
reverified after every round of corrections in this section. Recipe
recovered from this session's own prior transcript
(`/Users/paroberg/.claude/projects/-Users-paroberg-develop-mame-upstream/b32a764f-a23a-4017-9810-3ee355300fbc.jsonl`),
substituting `./mess` for the original `./mame` per the already-documented
stale-binary note in `current-blocker.md`.

### 4.7 Still unresolved

- Who calls `fb8a44`-`fb8ab6` (the real, confirmed early loader of the
  `$0544` table/`$04fe` descriptor) — not traced backward to its caller.
- Who calls `fb917a` and `fba816`/`$3de.w`'s pointer setup (task1's own
  callers) — only task1's two direct callers of `fb92ce` were found, not
  what schedules task1 itself.
- Where slot 0's `$2b14` target's `jsr (A2)` actually goes at runtime
  (register contents not captured); where `f90890`'s jump-table re-entry,
  `f8938e`'s `trap #a` (service codes `0x74`-`0x76`), and `ff8f36` (slot
  3's other jump-table neighbor) lead.
- Whether the `~69` indirect `jsr (An)`/`jmp (An)` sites ROM-wide (4.1)
  include one whose live pointer targets `f8948e` or later in `f894a4`'s
  routine — not checked against runtime contents.
- Why `fb82a4`/`fb846a` never fire in this run.
- The `trap #9`/node-89A2 convergence in 4.5 is real but unresolved — it
  predates this session's investigation and remains open in
  `current-blocker.md` section 9.

### 4.8 Live test of the smallest edge, plus stack-backing verification (new flag, not yet reviewed)

New diagnostic flag: `ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE` (off by
default). First pass instrumented the 18 ROM-wide genuine `jsr (An)` sites
from 4.1, the live-discovered lowmem `jsr (A2)` at `002b38`, both known
`trap #9` sites (`ffc8c0`, `00713a`), and a tap on `0000ae20` (slot 0's
early-return target) — all fired zero times. Before interpreting that,
the mechanism itself needed proof: every one of these hooks detects its
target only via a *stack* memory access (JSR/TRAP push a return
address/frame, RTS reads one) reaching `lowmem_w`/`low_rom_or_lowmem_r`
(the `0x000000-0x0fffff` handlers) — if the active stack were actually
elsewhere, all of these would silently and permanently read as zero
regardless of what really executes.

**Stack-backing evidence, logged at f880fc, every post-tuning RTE resume
(all five slots, including slot 0 at `ae18`), and one proven-executing
JSR:**

| Milestone | PC | `M68K_SP` | `M68K_USP` | Supervisor (SR bit 13) |
|---|---|---|---|---|
| `f880fc` | `f880fc` | `0002fa` | `001e20` | **1** (supervisor) |
| RTE resume, slot 1 | `ffc85a` | `000300` | `001e88` | **0** (user) |
| RTE resume, slot 3 | `ff9106` | `000300` | `001fb0` | **0** |
| RTE resume, slot 0 | `00ae18` | `000300` | `001e20` | **0** |
| RTE resume, slot 4 | `0068ae` | `000300` | `001fdc` | **0** |
| RTE resume, slot 5 | `0077a0` | `000300` | `002054` | **0** |

Two things established here: **task code runs in user mode** (SR
supervisor bit clear at every resume, set only during the dispatcher's own
`f880fc`-area execution), and **`M68K_SP` does not track the active mode**
— it reads a constant `000300`/`0002fa` regardless of whether the CPU is
in user or supervisor mode, while `M68K_USP` (the actual register JSR/RTS
use while in user mode, per the 68000 ISA) visibly grows/differs per
slot/task (`1e20`-`2054`). Both values are `<0x100000` — genuine lowmem in
either case — so region alone was never the problem; trusting `M68K_SP`
as "the stack" while in user-mode task code would have been.

**Positive control**: slot 5's resume code unconditionally executes `clr.w
$cdb0.w` (`0077aa`, a plain, always-reached lowmem write) immediately
followed, with no intervening branch, by `jsr $00007cc4.l` (`0077ae`, a
proven-executing direct call). The expected pushed return address is
`0077ae + 6 = 0077b4`. Captured live: two `lowmem_w` writes at `0077ae`,
to `002050` (value `0000`, the high word) and `002052` (value `77b4`, the
low word) — combined, exactly `0x000077b4`, and `002050`/`002052` sit
exactly 4 bytes below that resume's own `M68K_USP` (`002054`). **The
control succeeds**: the mechanism correctly identifies the push, at the
correct address (USP-4, not the misleading `M68K_SP` value), with the
exactly correct value.

**Conclusion**: the stack backing for JSR/RTS in task code is `M68K_USP`,
not `M68K_SP`; it is genuine lowmem, and does pass through the
`lowmem_w`/`low_rom_or_lowmem_r` handlers this diagnostic already taps.
No new tap over a different memory range was needed — the technique's
*address range* assumption (`lowmem_w` covers it) was correct all along;
only `M68K_SP` as a stand-in for "which address" would have been wrong,
and this diagnostic never relied on it for the pc-gated detection itself
(detection keys on PC, not on SP). **The first PTI run's zero results
therefore stand as genuine null results, not an inconclusive/broken
measurement**: `trap #9` and the `jsr (A2)` at `002b38` are statically
reachable but were not observed to execute in either captured run; 4.5's
"repeatedly reach `trap #9`" language overstated confirmed live behavior
with static reachability and remains superseded, exactly as reported
before this verification — this verification confirms that correction
rather than reopening it.

One anomaly noted but not explained: `M68K_SP` reading a constant value
regardless of the supervisor bit contradicts the m68kcommon.h comment that
it "fetches the current SP, be it USP, ISP, or MSP" — `M68K_ISP` reads
`000000` throughout (plausibly just unused on this plain 68000, which has
no hardware ISP/MSP split), and `M68K_SP` appears to always report
whatever the supervisor-mode stack pointer is, not the currently-active
one. Not investigated further; a MAME m68k-core detail, not an ASR-10
finding.

**Item 6 classification (unchanged by this verification)**: none of the
five stated categories fit cleanly. No browser-callback-related activity
— installation, promotion, or invocation — was observed anywhere in the
instrumented paths, now on a verified-sound mechanism rather than an
unproven one. This still does not distinguish "no browser callback exists
in this boot path" from "one exists via a mechanism not covered here" (the
six direct `jsr $xxxx.w` vector calls themselves were not tapped, being a
different addressing mode than this round's `jsr (An)` scope, and `jmp
(An)` sites remain untappable by this technique — real coverage gaps, not
resolved by the stack-backing proof). The honest result is a
verified-genuine narrowed negative, not a resolved classification.

**Not yet done, now covered in 4.9**: direct taps on the six `jsr
$xxxx.w` absolute-short vector calls themselves. Checking whether slot
1's own `bcc`-gated approach to `ffc8c0` behaves the same way across
multiple runs/boots remains not done.

### 4.9 Slot 0's six direct vectors: authoritative disassembly, live result, and full timeline (new, not yet reviewed)

**Correction first.** Re-disassembling `00adf0-00ae91` with `unidasm`
(rather than the hand-decoded hex read used earlier) shows the "six
vectors" framing in 4.5/4.8 conflated **two separate subroutines**. The
routine slot 0 actually resumes into (`ae0c-ae20`) only ever calls `jsr
$87f2.w`:

```
ae0c  jsr   $8856.w
ae10  moveq #0,D0             ; loop top
ae12  trap  #5                ; poll; result -> D0
ae14  and.b #$80,D0
ae18  beq   $ae20             ; <- RESUME PC (mid-loop, testing trap #5's result)
ae1a  jsr   $87f2.w           ; VECTOR 1 of 1 for *this* routine
ae1e  bra   $ae10             ; loop
ae20  rts                     ; early-exit path if D0&0x80==0
```

`bc8e`, `a26e` (x2), `8864`, and `9650` all belong to a **separate**
routine starting at `ae22`, reached only if something else calls it —
not via any control-flow edge from `ae18`'s loop or its `rts`:

```
ae22  cmp.w #8,D2
ae26  bcs   $ae2a
ae28  moveq #0,D2
ae2a  jsr   $bc8e.w                    ; unconditional once this routine is entered
ae2e  cmp.w $cdc.w,D2                  ; (bc8e's return point)
ae32  sne   D0
ae34  move.w D2,$cdc.w
ae38  bmi   $ae72                      ; D2<0 -> "site 2" path
ae3a  clr.b $332.w
ae3e  bset  D2,$332.w
ae42  move.w D2,$cda.w
ae46  addq.w #1,$cda.w
ae4a  bsr   $ae8e                      ; calls the shared $9650 mini-routine
ae4c  move.w A1,$330.w                 ; (9650's return point via this path)
ae50  move.w A1,-(A7)
ae52  tst.b D0
ae54  beq   $ae62                      ; D0==0 -> skip a26e entirely
ae56  cmpi.b #3,$183.w
ae5c  bne   $ae62                      ; $183.w!=3 -> skip a26e
ae5e  jsr   $a26e.w                    ; VECTOR: a26e, site 1
ae62  tst.w  $31c.w                    ; (a26e site 1's return point)
ae66  beq   $ae6c                      ; $31c.w==0 -> skip 8864
ae68  jsr   $8864.w                    ; VECTOR: 8864
ae6c  movea.w (A7)+,A1                 ; (8864's return point)
ae6e  jmp   $8bbc.w                    ; tail-jump, "return false" stub -- exit
ae72  cmpi.b #3,$183.w                 ; (D2<0 path)
ae78  bne   $ae84                      ; $183.w!=3 -> skip a26e site 2
ae7a  tst.b $306.w
ae7e  beq   $ae84                      ; $306.w==0 -> skip a26e site 2
ae80  jsr   $a26e.w                    ; VECTOR: a26e, site 2
ae84  clr.w $cda.w                     ; (a26e site 2's return point)
ae88  bsr   $ae8e                      ; calls the shared $9650 mini-routine again
ae8a  jmp   $8bb6.w                    ; tail-jump, "return true" stub -- exit
ae8e  jsr   $9650.w                    ; VECTOR: 9650 (shared, called from ae4a or ae88)
```

Note also: `$8bb6` and `$8bbc` are reached only via **`jmp`** (`ae8a`,
`ae6e`), never `jsr` — they push no return address and are not "vector
call sites" by this technique's definition, contrary to how they were
described in earlier turns.

**Instrumentation**: all six named jsr sites tapped via their genuine
return-address push (`ae1a`, `ae2a`, `ae5e`, `ae68`, `ae80`, `ae8e`), one
count per 16-bit word of the pushed 32-bit address (so 2 per actual call,
reported as `word_count`). Five gate taps capture the branch inputs via
their own genuine memory references: `ae34` (write, D2's sign, gates the
`ae38 bmi` split), `ae56`/`ae72` (reads of `$183.w`), `ae62` (read of
`$31c.w`), `ae7a` (read of `$306.w`). Return-taps where a genuine memory
reference exists at the return point: `ae2e` (bc8e), `ae62` (a26e site
1 — shared with its own gate tap), `ae6c` (8864), `ae4c` (9650 via the
`ae4a` path only — `ae8a`'s `jmp` has no memory reference, so 9650's
return via the `ae88` path is not directly observable).

**Live result**: `jsr $87f2.w` fires exactly once. Both of its pushed
words were captured and are exactly correct: `001e1c:0000` (high word),
`001e1e:ae1e` (low word) — combined, `0x0000ae1e`, exactly `pc+4`
(`ae1a+4`). `D0=0x00000080` at the call, confirming the `and.b #$80,D0`
test was genuinely nonzero (branch correctly not taken). **Nothing else
fired**: no `bc8e`, no `a26e` (either site), no `8864`, no `9650`, none of
the five gate taps, and — consistent with the branch having gone the
"call `$87f2`" way — no `ae20` early-return either. Slot 0 is dispatched
exactly once post-tuning (one `rte_resume_slot0` at `ae18`); it does not
resume again for the rest of the run.

`$87f2` (confirmed reachable) sign-extends to `ff87f2`, a jump-table entry
tail-jumping to lowmem `$2b14` (4.5), which itself contains a genuine `jsr
(A2)` at `002b38` — already independently instrumented (4.8). **That tap
did not fire either**, and no `trap #9` site fired. So even though `87f2`
executes, its own downstream reach (`2b14`→`2b38`) is gated out before the
`jsr (A2)`, by one of `2b14`'s own internal branches (not individually
tapped this round).

**Slot 0's complete post-tuning timeline, this run:**

```
rte_resume_slot0   ae18   D0=0x80 (from trap #5's poll result)
ae18 beq $ae20      -> NOT taken (D0&0x80 != 0)
ae1a jsr $87f2.w     -> TAKEN, confirmed (pushed 0000:ae1e = pc+4, exact)
  -> ff87f2 (sign-extended) -> tail-jump to 002b14 (per 4.5, not re-tapped this round)
  -> 002b14's internal branches -> gated out before 002b38's jsr (A2) (not reached)
ae1e bra $ae10        -> loops back to poll again (not observed to complete before run end)
[no further slot 0 resume observed]
```

**Comparison against the requested targets (item 7)**: no observed edge
in this timeline reaches `f8948e`/`f89494` (0 hits, 4.1), `f894b4` (0
hits, 4.1), `PANEL_ENQUEUE` (last enqueue at `seq=757`, well before
tuning's end, per section 4.4's milestone table history), the
filesystem/browser state (`$0544`/`$04fe` untouched post-tuning, 4.4), or
scheduler/node state (`trap #9` never fires, 4.8's negative result
stands). The one genuinely new fact from this round is that `jsr
$87f2.w` — and by extension its `002b14` jump-table target — **is**
reached, but dead-ends before reaching anything on the requested-target
list.

**Revised split (item 8):**
- **Stack-hook validation**: proven sound (4.8) — `USP` is the real stack
  for user-mode task code, is genuine lowmem, and the positive control's
  pushed value matched exactly.
- **Indirect-call (`jsr (An)`) negative result**: unchanged, still stands
  as genuine (4.8) — none of the 18 ROM-wide sites, the `002b38` lowmem
  site, or either `trap #9` site fired.
- **Direct-vector (`jsr $xxxx.w`) calls**: now resolved for slot 0's own
  `ae18` routine — exactly one of six named targets (`87f2`) actually
  executes; the other five belong to a different subroutine (`ae22`) not
  shown to be entered from this path at all. `87f2`'s own downstream reach
  is confirmed to dead-end before `002b38`.

**Smallest remaining edge (superseded by 4.10)**: which of `002b14`'s own
internal branches gates out before `002b38`'s `jsr (A2)`; and, separately,
whether the `ae22` routine (`bc8e`/`a26e`/`8864`/`9650`) is ever called
from any context in this boot path, or is dead code for this ROM
revision/disk image — the second question is not established either way,
but the first is now resolved, see 4.10.

### 4.10 002b14 resolved: this is a live-confirmed consumer of node 14F4's `+02` field, previously marked unknown

**Correction to the task framing first**: the branch at `002b2c`/`002b32`/
`002b3e` compares **A2** (an address register) via `cmpa.w`, not "D2.w" —
confirmed by re-disassembling `002af0-002b50` with `unidasm` rather than
hand-reading hex (the same discipline as 4.9). `D2` is read separately (by
`f89170`, see below) but is not what gates `002b38`.

**`f89170`, disassembled and traced (item 2):**

```
f89170  moveq #0,D0
f89172  moveq #0,D2
f89174  moveq #0,D3
f89176  movea.w A5,A4        ; save A5
f89178  addq.w #2,A5         ; A5 += 2 (skip the node's first field)
f8917a  movea.w (A5)+,A2     ; A2 = word at node+2  (MOVEA -> sign-extends)
f8917c  movea.l (A5),A6      ; A6 = long at node+4
f8917e  movea.w (A5),A3      ; A3 = word at node+4  (same bytes, sign-extended)
f89180  move.w (A5)+,D2      ; D2 = word at node+4  (same bytes again, as data)
f89182  move.w (A5),D3       ; D3 = word at node+6
f89184  movea.w A4,A5        ; restore A5
f89186  rts
```

This is a **generic node-field deserializer**: `A5` is the node pointer
(caller-supplied), and it reads the node's `+2` field as a pointer
(`A2`), the `+4` field both as a pointer (`A3`) and as a plain word
(`D2`) — the same bytes, two interpretations — and the `+6` field as a
word (`D3`). No static caller of `f89170` was found (same
indirect/vectored-dispatch limitation as `f894a4`'s own callers); it is
reached here via `002b14`'s `jsr $fff89170.l`, with `A5` already set by
whatever called into this routine (not traced further back than slot 0's
own `ae1a jsr $87f2.w`, which supplies the execution context but not
necessarily `A5` itself — `A5` is not modified by the `jmp $2b14.w` tail
call from `ff87f2`, so its value is whatever slot 0's task held before
calling `$87f2`).

**Live result — decisive**: at `002b1a`/`002b26`/`002b2a` (trap #4),
`A5 = 0x0014f4` and `A2 = 0xFF89A2`. **`A5=0x14f4` is the exact node
address, and `0x89a2` sign-extended via `A2`'s `movea.w` load is the exact
value, already documented in `current-blocker.md` section 9 as "node
14F4 has `+02` = `89A2`"** — this is the same node the rest of this
document's (pre-this-session) history has been asking "who consumes
this?" about. `D2=0`, and both `$035e.w` and `$031c.w` read as `0` at
this observation; `USP=0x001e1c`; `active_slot=0`.

**Item 3, resolved by the CMPA.W semantics, not by search**: `cmpa.w`
always sign-extends its source to 32 bits before comparing, regardless of
addressing mode. `0x6b1c` and `0x6cf2` have bit 15 clear (sign-extend to
themselves); `0xd10a` has bit 15 **set** — it sign-extends to
`0xFFFFd10a`. So the three values actually being compared against `A2`
are `0x00006b1c`, `0x00006cf2`, and `0xFFFFd10a` — genuinely
address-shaped 32-bit values, consistent with `A2` being validated as a
callback pointer, not an arbitrary tag. A ROM-wide search for these three
16-bit constants as data (immediate operands, table entries) found only
false-positive matches against unrelated ROM *address labels*
(`f86b1c`, `f96cf2`, `f8d10a`, etc.) — no genuine data/table occurrence of
any of the three was found in static ROM. Per the task's caution, `0x6b1c`/
`0x6cf2`/`0xffd10a` were **not** disassembled as handlers — no evidence
was found that they are, in fact, used as code pointers anywhere else,
and `A2` (the only live value observed) matches none of them anyway, so
there was nothing to confirm by looking.

**The live branch, determined by elimination (item 4)**: `A2=0xFF89A2`
matches none of `0x00006b1c` / `0x00006cf2` / `0xFFFFd10a`. Therefore,
with full confidence from the observed value alone (the three compares
are register-only and have no memory reference to tap directly): `002b30`
(`beq`, `A2==$6b1c`) **not taken**; `002b36` (`bne`, `A2!=$6cf2`) **taken**
(branches to `002b3e`); `002b42` (`beq`, `A2==$d10a`) **not taken**; falls
through to `002b44 jmp $8bbc.w`. This is confirmed by a genuine downstream
effect, not just arithmetic: `002b38`'s `jsr (A2)` has never fired, in
any run, across every round of this investigation (4.8, 4.9, and this
round) — fully consistent with, and now explained by, `A2` never matching.

**trap #4, verified (not assumed)**: vector 36 (32+4), handler at
`f880a2`:

```
f880a2  ori   #$700,SR          ; mask interrupts (IPL 7)
f880a6  movea.w $b6c.w,A0       ; A0 = current list head
f880aa  move.w A5,$b6c.w        ; list head := A5 (this node)
f880ae  move.w A0,(A5)          ; node's own +0 field := old list head
f880b0  subq.b #1,$b7f.w        ; decrement a counter
f880b4  rte
```

`trap #4` is a **linked-list push**: it inserts the node `A5` points to
(here, `0x14f4`) at the head of a list rooted at lowmem `$b6c.w`, and
decrements a counter at `$b7f.w`. This is not a dispatch or message-post
mechanism — it is exactly the kind of primitive a "return this node to a
free/pending list" operation would use. `$b6a.w` was already established
elsewhere in this document's history as an *active slot* pointer; `$b6c.w`
is the immediately adjacent cell and, per this trace, a **different**
list — not previously identified.

**Cross-reference against the requested targets (item 5)**: this whole
sequence neither reaches nor is reached by `f8948e`/`f89494` (4.1),
`f894b4` (4.1), or `PANEL_ENQUEUE` (last activity long before tuning
ends). It *is* a scheduler/node-callback-table consumer — specifically,
of node `14F4`'s own `+02` field, the node this document's own history
(section 8/9) already flagged as queued and unconsumed. The producer of
`14F4`'s `+02=89A2` value itself was not traced this round (would require
finding who wrote it, likely at node-creation time via whatever posted it
through `trap #9`, per section 9's existing "posted to slot0 +10/+12
through trap #9" note) — genuinely still open.

**Report (item 6):**
- `D2=0`, `D2w=0`, `D5=0` (both reads returned 0 at this observation),
  `A2=0xFF89A2`, `A5=0x0014f4` at the gate.
- `f89170`'s source is a generic node record pointed to by `A5`: `+2`
  word → `A2` (pointer), `+4` word → `A3`/`D2` (dual-purpose), `+6` word
  → `D3`. No offset-0 field is read by this routine.
- `trap #4` verified: vector 36, handler `f880a2`, a linked-list push of
  the node onto `$b6c.w`'s list (not a dispatch mechanism).
- The three constants are genuinely address-shaped (via `cmpa.w`
  sign-extension: `0x6b1c`, `0x6cf2`, `0xFFFFd10a`), consistent with being
  callback-pointer whitelist entries — but not confirmed as code, since
  nothing here or elsewhere in static ROM independently corroborates their
  content, and the one live value seen (`A2`) never matches any of them.
- Every producer of each accepted value: **none observed** — `A2` never
  equalled any of the three in this run, so no producer-side evidence for
  any of them was generated; this remains unknown.
- **Revised smallest unresolved producer/consumer edge**: who writes
  `0x89a2` into node `14f4`'s `+2` field in the first place (the true
  producer side of this consumer), and whether any *other* node, ever
  posted through this same `002b14` path, actually carries `A2` equal to
  one of the three recognized values (which would be the first live
  confirmation that `002b38` is reachable at all, for any node). Neither
  was established this round.

This is a live-confirmed **consumer** of node `14F4`'s `+02` field,
answering part of `current-blocker.md` section 9's open question — but it
is a *different* mechanism than the one section 9 already considered and
retracted (`FF8258=8140` / `FF8140+signed(89A2)=FF0AE2`, which added the
type value as a signed offset to a base; this consumer instead
sign-extends it directly into `A2` via `movea.w` and validates it against
three fixed constants, with no reference to `FF8258`/`8140`/`FF0AE2` at
all). Both can be true at once — this does not reopen the earlier
retraction, it identifies an unrelated, separate reader of the same
field.

### 4.11 `006f7c`/`007000`'s trap #2/#12/#14: full handler disassembly, live negative result, and node+2=0x1a search (new, not yet reviewed)

Continuing from the prior (static-only) round, which established that
`006cf2` tail-jumps to `006f7c`, and that `006f7c`/`007000` executes
`trap #2` before writing `A5+2=0x001a` and `A5+4=A6`, then routes through
`trap #12` or `trap #14`. That round explicitly warned not to assume `A5`
still points to the incoming node after `trap #2`. This round resolves
that warning and answers tasks 1–5.

**Vector table, read live at the `f880fc` gate (one-shot, always fires):**

```
vector34 (trap #2)  addr=000088  handler=f88066
vector44 (trap #12) addr=0000b0  handler=f88174
vector46 (trap #14) addr=0000b8  handler=f881e6
```

**TASK 1 — `trap #2` (vector 34, handler `f88066`), full disassembly:**

```
f88066  cmpi.b  #$bb,$b7f.w      ; allocated-count >= 187 (0xbb) ?
f8806c  bcs     f88074           ; no: continue to real pop
f8806e  ori.w   #$1,(A7)         ; yes: SET CARRY in saved SR (graceful fail)
f88072  rte
f88074  andi.w  #$fffe,(A7)      ; clear carry in saved SR (success path)
f88078  ori     #$700,SR         ; mask interrupts
f8807c  movea.w $b6c.w,A5        ; A5 := list head (SAME list trap #4 pushes onto)
f88080  move.w  A5,D0
f88082  bne     f88088           ; head != 0: continue
f88084  moveq   #-$70,D0
f88086  trap    #$0              ; head == 0 (list empty): HARD TRAP, not a graceful return
f88088  move.w  (A5),$b6c.w      ; POP: list head := popped node's own +0 field
f8808c  clr.w   (A5)             ; popped node's +0 field cleared
f8808e  addq.b  #1,$b7f.w        ; allocated-count++
f88092  move.b  $b7f.w,D0
f88096  cmp.b   $b80.w,D0
f8809a  bls     f880a0
f8809c  move.b  D0,$b80.w        ; update high-water mark
f880a0  rte                      ; return, carry clear, A5 = the newly allocated node
```

This is a **free-list allocator**, the exact inverse of the already-documented
`trap #4` push (`f880a2`, section 4.10): both operate on the same
`$b6c.w`-rooted singly-linked list, using each node's own `+0` field as the
"next" pointer.

Answering the task's specific questions:
- **Does it change A5?** Yes — definitively. `A5` is **overwritten** at
  `f8807c` to be the node just popped off `$b6c.w`'s list. This is a
  *different* node from whatever `A5` held on entry. The prior round's
  caution ("do not assume `A5` still points to the incoming node") is
  confirmed correct: it does not.
- **Does it allocate/pop a node?** Yes — pops from the `$b6c.w` free list.
- **Which list/pool?** The same list `trap #4` (`f880a2`) pushes onto —
  a shared alloc/free pair over one pool.
- **Carry-flag / success-failure semantics:** two distinct failure paths,
  not one:
  - allocated-count `>= 0xbb` (187): **graceful failure**, carry set,
    `rte` immediately, list never touched.
  - allocated-count `< 0xbb` but list head is zero (pool exhausted):
    **`trap #0`** — a hard trap, not a carry-set return. `trap #0`'s own
    handler was not examined this round.
  - otherwise: pop succeeds, carry clear, `rte`.
- **Node contents before return:** only the popped node's own `+0` field
  is touched by `trap #2` itself (cleared). Every other field (`+2`,
  `+4`, `+6`, …) is whatever was left over from the node's *previous* use
  — until the caller (`007000`) explicitly overwrites `+2` and `+4` right
  after the trap returns.

**Consequence for the earlier framing:** `007000`'s `A5+2=0x001a` /
`A5+4=A6` writes are **not** modifying the incoming node (`14F4` or
otherwise) — they initialize a **freshly allocated, unrelated node**
drawn from the `$b6c.w` pool. Whatever node arrived in `A2`/`A5` before
`007000` ran is not the node being tagged `0x1a`.

**TASK 2 — `trap #12` (vector 44, `f88174`), `trap #13` (vector 45,
`f881f6`), and `trap #14` (vector 46, `f881e6`):**

> **RETRACTION (superseded by 4.12):** the original version of this
> section claimed `f881f6` was `trap #14`'s fall-through enqueue body.
> That is wrong: `f881e6`'s handler `rte`s at `f881f4`, so nothing after
> it is reachable by fall-through. `f881f6` is **`trap #13`'s own
> handler** (vector 45), confirmed by a live vector-table read. See 4.12
> for the corrected control-flow account, the exact `jsr` encoding at
> `f881f0`, and the xref search. The trap #12 disassembly and field
> analysis below (`f88174`-`f881b4`) are unaffected and stand as
> originally reported, with the same incomplete-tail caveat (`f881d6`,
> `f881ba` not captured).

```
f88174  ori    #$700,SR
f88178  clr.w  (A5)
f8817a  tst.w  ($10,A1)          ; gate/enable flag
f8817e  beq    f881d6            ; disabled: different path (not captured this round)
f88180  addq.w #1,($c,A1)        ; count++
f88184  move.w ($8,A1),D1        ; D1 := tail pointer
f88188  bne    f88190
f8818a  move.w A5,($6,A1)        ; queue was empty: head := A5
f8818e  bra    f88194
f88190  movea.w D1,A0
f88192  move.w A5,(A0)           ; old tail's +0 (next) := A5
f88194  move.w A5,($8,A1)        ; tail := A5
f88198  move.w ($c,A1),D1
f8819c  cmp.w  ($a,A1),D1        ; count vs. limit
f881a0  bls    f881d4            ; within limit: done (not captured this round)
f881a2  move.w ($12,A1),D0       ; over limit:
f881a6  move.w ($e,A1),D1
f881aa  bne    f881ba            ; (not captured this round)
f881ac  movea.w $b6a.w,A0        ; A0 := active-slot pointer
f881b0  clr.w  ($4,A0)
f881b4  move.w A0,($0,A1)
```

`trap #14`'s own disassembly and the corrected `trap #13` disassembly are
given in full in 4.12, along with the resolved `A1=0x14c0` field meaning
(unchanged: head `+6`/tail `+8`/limit `+a`/count `+c`/gate `+10`), which
still applies to all three handlers (`trap #12`/`#13`, and `trap #14`
indirectly via its nested call into `trap #13`).

**TASK 3 — live positive proof for `007000`:**

Instrumented exactly as specified: `007000` entry, the `trap #2`
entry/return pair (with `A5` before/after and the carry bit), both node
field writes (`+2=0x1a`, `+4=A6`), and both `trap #12`/`trap #14` call
sites. Full boot run, gate open confirmed (`f880fc` fired). Result,
checked directly against the raw log:

```
$ grep -c "ASR10_PTI_TRAP2_ENTRY\|ASR10_PTI_TRAP2_RETURN\|ASR10_PTI_NODE_FIELD_WRITE\|ASR10_PTI_TRAP12\|ASR10_PTI_TRAP14" trap2_live_error.log
0
$ grep "ASR10_PTI_VECTORS_2_12_14" trap2_live_error.log
ASR10_PTI_VECTORS_2_12_14 vector34_addr=000088 handler=f88066 vector44_addr=0000b0 handler=f88174 vector46_addr=0000b8 handler=f881e6
```

**`007000` does not execute during the normal boot.** This is a valid
negative, per the task's own fallback: reported as such rather than
forced. All of TASK 1/2's conclusions rest on static handler analysis
only; nothing above is live-confirmed to actually run in this boot path.

**TASK 4 — searching for `0x001a` written to/compared against node+2:**

Searched the full static ROM disassembly for genuine (non-address-label)
occurrences of `#$1a`/`#$001a` in `cmpi`/`cmp`/`move` immediate contexts.
Four candidates, all checked in context:

- `f84df6 cmpi.b #$1a,(A0)+` — false positive: this is inside a run of
  sequential `ILLEGAL`/opcode-table filler around `f84dd0`-`f84e30`
  (`dc.w` garbage interleaved with decodable-looking `cmpi.b` forms at
  regular displacement-mode steps), not real code. Discarded.
- `f91bb4 cmp.w #$1a,D0`, inside a loop at `f91ba6`-`f91bb8`
  (`clr.w (A4,D0.w)` / `addq.w #2,D0` / `cmp.w #$1a,D0` / `ble`) that
  zeroes a table from offset `$a` to `$1a` in steps of 2. This is a loop
  **bound**, not a type tag, and the surrounding code (`$8434.w` bit
  ops, `$827c`/`$839e`/`$8414` lowmem cells) belongs to an unrelated
  peripheral-service routine. Coincidental numeric match — false
  positive relative to node `+2`.
- `fbafce move.b #$1a,(A3)`, one of a long run of `move.b <src>,(A3)`
  writes (constants and lowmem bytes `$49e`-`$4a1`, `$414`-`$415`,
  `$4b8`, `$409`, each group separated by a `move.b #$0,(A3)` and calls
  to `fbb0d0`/`fbb0b2`) in high-view code around `fbafa0`-`fbaff8`. Shape
  is consistent with serializing fields into a buffer with `0x1a` as one
  tag among several (`0x12` appears the same way a few lines down), but
  this is a **different subsystem** (high-view, likely display/text or a
  SysEx-style builder) with no established link to the `007000`/node
  path. Not the same field.
- `f8991a cmp.w #$1a,D0`, inside a genuine multi-way dispatcher at
  `f898b8`-`f89934` that loads `D0` from **lowmem `$354.w`** (not from any
  node offset) and compares it against a run of small integers — `3, 4,
  5, 6, 7, 9, 0x17, 0x18, 0x19, 0x1a, 0x1b(implicit), 0x1c, 0x1d, 0x1e,
  0x21` — grouping several of them onto two shared targets. This is the
  best-fitting candidate: `0x1a` behaves exactly like one member of a
  small enumerated type/opcode set. **But** the dispatch input here is
  `$354.w`, and no code was found this round proving `$354.w` is ever
  loaded from the specific node's `+2` field that `007000` writes. The
  link from "node `+2`" to "`$354.w`" is not established.

**Conclusion for TASK 4:** `0x1a` plausibly functions as a small
type/opcode-ID constant somewhere in this ROM (the `f898b8` dispatcher is
good circumstantial evidence for that general pattern), but **no direct,
proven consumer of the exact node field `007000` writes was found**.
This is a CANDIDATE-level finding only, not confirmed — reported as such
rather than overclaimed.

**TASK 5 — reassessing the smallest remaining edge:**

Two candidates now compete for "smallest remaining edge," and the
`007000` path itself is no longer it, since it is now fully explained
(alloc → tag → enqueue) and live-confirmed **not to run** in this boot:

1. The pre-existing `CA24`-`CC50` chunk-mapping question (section from
   the prior Swedish-language round): still unresolved, still blocks
   disassembling that specific jump-table-target region from the static
   disk image.
2. **New candidate, arguably smaller and more directly load-bearing**:
   `trap #14`'s unconditional `jsr ($f87f3e,PC)` lands inside the
   already-established, already-instrumented dispatcher/scheduler code
   range (`f87f40`-`f87fd0`). Since that range is central to this whole
   investigation's control-plane model, and `f87f3e` itself sits just
   *before* the documented start of that range, disassembling
   `f87f3e`-`f87f40` and confirming exactly which dispatcher entry point
   it targets is a small, well-scoped, high-value static task — smaller
   in scope than the chunk-mapping problem, and more directly connected
   to already-proven live behavior (the dispatcher range is confirmed to
   execute; `007000`/`trap #14` is confirmed not to, this round, so this
   thread only matters if some *other* caller reaches `trap #14`, which
   was not searched for this round).

No further action was taken on either candidate this round — this is a
reassessment only, per the task's own instruction to defer action until
after tasks 1-4.

**Build/Gate 1/diff-check for this round:** build succeeded; Gate 1
passed (`GATE1_EXIT=0`, `grep -c "ASR10_FDC_TC"` on the flagless run =
0); `git diff --check` clean. Not committed, per instruction.

### 4.12 Control-flow correction: `f881f6` is trap #13's own handler, not trap #14's body (new, not yet reviewed)

4.11's original framing of `trap #14` was wrong: `f881e6`'s handler
`rte`s at `f881f4`. Code after an `rte` is not reachable by fall-through,
so `f881f6` cannot be part of the same handler. This section corrects
that error and answers the six items requested. (Numbered "item" here to
avoid collision with 4.11's own "TASK 1"-"TASK 5" labels, which cover a
different set of questions.)

**Item 1 — live vector-table read, extended to vectors 45 and 47:**

```
ASR10_PTI_VECTORS_2_12_13_14_15 vector34_addr=000088 handler=f88066
  vector44_addr=0000b0 handler=f88174 vector45_addr=0000b4 handler=f881f6
  vector46_addr=0000b8 handler=f881e6 vector47_addr=0000bc handler=f88056
ASR10_PTI_F881F6_XREF vector45(trap13) handler equals f881f6
```

**`f881f6` is `trap #13`'s handler (vector 45), read directly from the
live vector table.** It is not part of `trap #14` (vector 46, `f881e6`,
unchanged) or `trap #15` (vector 47, `f88056`, a third and different
handler, disassembled below for completeness though not requested).

**Item 2 — full disassembly of each handler, extended through every branch target and exit:**

`trap #14` (vector 46, `f881e6`) — complete, self-contained, four
instructions, ends in its own `rte`:

```
f881e6  ori    #$700,SR
f881ea  move.w D0,-(A7)
f881ec  trap   #$d               ; genuine nested trap: calls trap #13 (f881f6)
f881ee  move.w (A7)+,D0
f881f0  jsr    ($f87f3e,PC)      ; see item 3
f881f4  rte
```

Confirmed by direct disassembly that `f881e6`-`f881f4` is the entire
handler. It reaches `f881f6` exclusively via the CPU's own trap
mechanism (the `trap #$d` instruction), never by fall-through or direct
call — a real, but indirect, control-flow edge, not the same thing as
"`f881f6` is part of this handler's body."

`trap #13` (vector 45, `f881f6`), extended through every branch target:

```
f881f6  ori    #$700,SR
f881fa  clr.w  (A5)
f881fc  tst.w  ($10,A1)          ; gate/enable flag
f88200  beq    f8821c            ; disabled/cold-start path
f88202  addq.w #1,($c,A1)        ; enabled path: count++
f88206  move.w ($8,A1),D0        ; D0 := tail pointer
f8820a  bne    f88212
f8820c  move.w A5,($6,A1)        ; queue was empty: head := A5
f88210  bra    f88216
f88212  movea.w D0,A0
f88214  move.w A5,(A0)           ; old tail's +0 (next) := A5
f88216  move.w A5,($8,A1)        ; tail := A5
f8821a  bra    f8822a            ; -> rte directly. NOTE: unlike trap #12,
                                 ; this path never reaches a count/limit
                                 ; check at all -- trap #13 has no overflow
                                 ; handling of its own.
f8821c  move.w A5,($4,A1)        ; disabled path: A5 -> A1+4 (a side slot,
                                 ; distinct from head/tail)
f88220  move.w #$1,($10,A1)      ; sets the gate/enable flag
f88226  movea.l (A1),A0          ; A0 := long read from A1+0 (a function
                                 ; pointer slot, distinct from +4)
f88228  jsr    (A0)              ; call through it -- the genuine
                                 ; "activate on first item" kick
f8822a  rte
```

Extending one instruction further than requested surfaces something
worth flagging: `f8822c` (`move SR,-(A7)`) begins immediately after
`f8822a`'s `rte` and looks like a separate, ordinary (non-trap) callable
subroutine — its body (`f88232 move.w ($6,A1),D0` / `bne f88244` /
`f88238 clr.w ($10,A1)` / `f88244 movea.w D0,A5` / `move.w (A5),($0,A1)`)
reads the *head* pointer and, if non-zero, appears to unlink it — the
shape of a **dequeue-from-head**, the likely counterpart to the enqueue
above. Not chased further this round (out of scope for this correction);
noted as a lead.

`trap #15` (vector 47, `f88056`) — disassembled since it was captured
during the same vector read, though not one of the requested three:

```
f88056  movea.w $b6a.w,A2        ; A2 := active-slot pointer (same one
                                 ; trap #12 touches on overflow)
f8805a  bset   D0,($2,A2)        ; set bit D0 in active-slot+2
f8805e  bset   D0,($3,A2)        ; set bit D0 in active-slot+3
f88062  bra    $f87f80           ; tail-jump into the dispatcher range
```

**Item 3 — exact `jsr` encoding at `f881f0`:**

Raw words: opcode `4eba`, extension `fd4c`.

- `4eba` = `JSR` with effective-address mode/register `111 010` — Program
  Counter Indirect with Displacement Mode, `(d16,PC)`.
- Extension word `fd4c` = `0xFD4C`, a signed 16-bit displacement =
  `0xFD4C - 0x10000 = -692 = -$2b4`.
- 68000 `(d16,PC)` addressing computes the effective address from the PC
  value **at the extension word**, i.e. `f881f2` (the opcode is at
  `f881f0`, extension word immediately follows at `f881f2`):
  `f881f2 + (-$2b4) = f87f3e`.
- **Resolved target: exactly `$f87f3e`** — confirmed by direct hex
  arithmetic on the actual captured opcode bytes, not assumed.

Checked directly against the ROM disassembly (`asr10_full_rom.dasm`)
whether `f87f3e` is a real instruction boundary or a coincidental
mid-instruction byte:

```
f87f3a: 65de       bcs     $f87f1a
f87f3c: 6054       bra     $f87f92
f87f3e: 4e68       move    USP, A0
f87f40: 48e0 3f3e  movem.l D2-D7/A2-A6, -(A0)
f87f44: 211f       move.l  (A7)+, -(A0)
f87f46: 3117       move.w  (A7), -(A0)
f87f48: 212f 0002  move.l  ($2,A7), -(A0)
f87f4c: 4e60       move    A0, USP
f87f4e: 40d7       move    SR, (A7)
f87f50: 2f7c fff8 7f66 0002  move.l #$fff87f66, ($2,A7)
f87f58: 3478 0b6a  movea.w $b6a.w, A2
f87f5c: 01ea 0002  bset    D0, ($2,A2)
f87f60: 01ea 0003  bset    D0, ($3,A2)
f87f64: 601a       bra     $f87f80
```

`f87f3e` is a genuine, valid instruction start (`move USP,A0`) — the
first instruction of a full context-save sequence (saves `D2`-`D7`/`A2`-
`A6` and the exception frame onto the *user* stack via `A0`, then builds
a fixed return frame with `#$fff87f66`, then does the identical
`$b6a.w`/`bset +2`/`bset +3` sequence `trap #15` does directly), which
converges at `f87f80` — the same target `trap #15`'s tail-jump reaches.
So: **the target really is `f87f3e`, not `f87f40`**, and it is not
"something else" — it is one instruction earlier than this document
previously described as the start of the dispatcher range, and it is a
meaningful entry point (a generic "save full context, tag the active
slot, and fall into the shared f87f80 continuation" routine), not a
coincidence. The previously-documented `f87f40`-`f87fd0` range should be
understood as starting at `f87f3e`.

**Item 4 — every direct or table-based reference to `f881f6`:**

```
$ grep -n "881f6" asr10_full_rom.dasm | grep -v "^[0-9]*:f881f6:"
(no output)
```

**No static `jsr`/`jmp`/`bsr`/`bra` or table entry anywhere in the ROM
references `f881f6` directly.** The only established control-flow edge
to it is the CPU's own trap-vector dispatch (vector 45), consistent with
the vector-table population being dynamic rather than a visible
immediate store (same pattern already established for vectors 34/44/46
in 4.11). Searching independently for other callers of `trap #$d`
(vector 45's own trap number) found three more genuine call sites
elsewhere in the ROM, unrelated to `trap #14`:

```
$ grep -n "4e4d" asr10_full_rom.dasm
f881ec: 4e4d  trap  #$d      <- inside trap #14's own handler (item 2)
f883ac: 4e4d  trap  #$d
f88df8: 4e4d  trap  #$d
f89b54: 4e4d  trap  #$d
```

This confirms `trap #13` is a genuine, independently-used, general-
purpose primitive (called from at least three other ROM locations besides
`trap #14`), not something that exists solely to be `trap #14`'s
fall-through body. **Conclusion: `f881f6` must not be called "`trap
#14`'s body" — it is `trap #13`'s own handler**, reachable from `trap
#14` only via one genuine nested trap edge, and independently from at
least three other, unexamined sites.

**Item 5 — 4.11 corrected:**

- `trap #2`/free-list conclusion: unchanged, stands as originally
  reported.
- `trap #12` queue analysis: unchanged, stands with its original
  incomplete-tail caveat (`f881d6`, `f881ba` not captured).
- **Retracted:** the claim that `trap #14` itself has the `f881f6`
  enqueue body. It does not; see item 2 above.
- `trap #14` is now described only as: mask interrupts, nested `trap
  #13` call, PC-relative `jsr $f87f3e` (dispatcher context-save entry,
  item 3), `rte` — nothing more, until `trap #13` and `f87f3e`'s full
  routine (through its convergence at `f87f80` and beyond) are decoded
  in their own right (partially done here for `trap #13` and for
  `f87f3e`-`f87f64`, but `f87f80`'s own continuation was not chased this
  round).
- 4.11's "TASK 5" reassessment (`CA24`-`CC50` vs. the `f87f3e` lead) is
  superseded in one respect: `f87f3e` is now fully decoded rather than a
  bare address, and turns out to be the dispatcher's generic context-save
  entry point (shared with `trap #15`), which raises its importance
  somewhat — but `007000`/`trap #14` is still confirmed not to execute in
  the normal boot (4.11 TASK 3), so this thread remains relevant only if
  some other, unsearched-for caller reaches `trap #14` or `trap #13`
  directly (the three `f883ac`/`f88df8`/`f89b54` sites above are
  candidates for that search, not yet pursued).

**Item 6 — the `7004` hook renamed:**

`log_pti_trap2_return()` renamed to `log_pti_trap2_success_observation()`
(and its log tag from `ASR10_PTI_TRAP2_RETURN` to
`ASR10_PTI_TRAP2_SUCCESS_OBSERVATION`), with a comment clarifying it taps
the first write reached *after* `trap #2`'s `rte`, on the carry-clear
(success) path only — a proxy for "trap #2 succeeded," not a direct hook
on the trap's own return (that instruction has no memory reference to
tap). No behavioral change; name/log-tag/comment only.

**Build/Gate 1/diff-check for this round:** build succeeded (clean
recompile of `asr10_boot.cpp`, link succeeded); Gate 1 passed
(`GATE1_EXIT=0`, flagless run, `grep -c "ASR10_FDC_TC"` = `0`); live run
with `ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE=1` produced the vector
read above; `git diff --check` clean. Not committed, per instruction.
Execution was not forced anywhere in this round — all conclusions above
come from either a live one-shot vector-table read (guaranteed to fire)
or static ROM disassembly.

### 4.13 The three static `trap #13` callers: disassembly, live instrumentation, and a broader negative result (observation only, not yet reviewed)

Per the working hypothesis that the missing browser/UI transition may be a
producer/routing issue, this section disassembles the three known static
callers of `trap #13` outside its own nested call from `trap #14`
(`f883ac`, `f88df8`, `f89b54`), instruments all three live, and cross-
references the results. All static disassembly used `unidasm` against the
already-extracted flat ROM image; no manual hex decoding.

**TASK 1 — authoritative disassembly of all three callers:**

*Site 1 — `f883ac`.* Containing routine: `f88300`-`f883fa` (bounded by an
`rts` immediately before `f88300` and a shared `bra $f88f3e` exit used by
two internal branches). Structurally a periodic tick-service routine: two
bounded loops over fixed-stride record arrays (`$c6.w`-`$c8.w`, stride
`$16`, decrementing a per-record countdown and clearing a bit at `+2` on
expiry; `$ca.w`-`$cc.w`, stride `$1a`, decrementing a countdown at `+0x14`
and, on expiry, calling through a function pointer at `+0x16`), followed
by fixed non-looped countdown logic on lowmem cells `$b6e`/`$b70`/`$b72`/
`$b74`/`$b76`/`$b78`/`$b82`/`$b83`. The exact path to `trap #13`:

```
f88394  movea.w $d6.w,A1     ; unrelated trap #9 site, different A1 source
f88398  trap    #$9
f8839a  subq.b  #1,$b83.w    ; counter
f8839e  bpl     f883b4       ; if still non-negative, SKIP trap #13 entirely
f883a0  trap    #$3
f883a2  move.w  #$e,($2,A5)  ; node+2 := 0x000e (static)
f883a8  movea.w $dc.w,A1     ; A1 := lowmem $dc.w
f883ac  trap    #$d
f883ae  move.b  #$3,$b83.w   ; reset counter
f883b4  bra     f88f3e       ; shared exit
```

- **A1 source:** lowmem `$dc.w` (a pointer *variable*, not a fixed
  constant — see below, shared with all three sites).
- **A5 source:** **not established this round.** `A5` is used
  (`($2,A5)`) but never (re)loaded anywhere in the `f88300`-`f883fa`
  range; it must be inherited from whatever called this routine. No
  `movem`/entry convention was found bounding this specific routine (unlike
  site 2, below), so its true caller and A5's provenance are unresolved —
  reported as a gap, not assumed.
- **Was the node allocated via `trap #2` first?** No `trap #2` occurs
  anywhere in `f88300`-`f883fa`. Whatever node `A5` points to is reused
  from ambient context, not freshly allocated.
- **Node writes before enqueue:** only `+2 := 0x000e`, at `f883a2`. No
  `+0`/`+4`/`+6` writes found on this path.
- **A6/D0 at the call:** not written on this path; whatever the caller
  left them as.
- **Gating condition:** `bpl f883b4` on `$b83.w` after `subq.b #1` — the
  call only happens when this counter underflows negative (i.e., every
  256th... no, every time the byte count goes negative, which given the
  reset to `#$3` at `f883ae` means every 4th pass through this specific
  branch of the tick routine).
- **Immediately after return:** `move.b #$3,$b83.w` (counter reset), then
  falls into the shared `bra f88f3e` exit (a separate helper,
  `f88f3e`+, not fully traced this round).

*Site 2 — `f88df8`.* Containing routine: `f884be`-`f88e02`, bounded
**exactly** by a matching register save/restore: `f884be movem.l
D0-D3/A0-A2/A5,-(A7)` and `f88dfe movem.l (A7)+,D0-D3/A0-A2/A5` /
`f88e02 rte`. This is a genuine, provable routine boundary. Entry reads a
hardware status byte and dispatches by bit:

```
f884be  movem.l D0-D3/A0-A2/A5,-(A7)
f884c2  move.b  $fffc480b.l,D0     ; hardware status register read
f884c8  move.b  D0,D1
f884ca  btst    #5,D0 / beq ...    ; dispatch on individual/grouped bits
f884d6  and.b   #6,D1 / beq ...    ; of D0, each branch jsr/jmp-ing through
f884e2  btst    #0,D0 / beq ...    ; a lowmem function-pointer cell
f884ee  btst    #3,D0 / beq ...    ; ($de.w/$e2.w/$e6.w) or a fixed target
f884f8  moveq   #-$6f,D0 / trap #0 ; no bit matched: hard trap
```

The path actually reaching `trap #13` (deep inside this dispatch, after
much intervening scan/encoder-table logic using `trap #3`/`trap #9` pairs
on other lowmem-sourced `A1` values `$d6.w`/`$d8.w`/`$da.w`, tagging nodes
with `+2` values `0x000e`/`0x000a`/`0x0010` at various points):

```
f88dbe  cmpi.b  #$1,$17e.w
f88dc4  bne     f88dfa
f88dc6  move.w  $b76.w,D2 / bne f88dd2 / bsr f88e04 ...   ; drain loop
f88dd2  bsr     f88e04
f88dd4  move.w  #$3,$b76.w
f88dda  move.w  $b72.w,D0 / lsr.w #2,D0 / move.w D0,$b78.w / move.w D0,$b74.w
f88de8  clr.w   $b72.w
f88dec  trap    #$3
f88dee  move.w  #$e,($2,A5)   ; node+2 := 0x000e (static, same value as site 1)
f88df4  movea.w $dc.w,A1      ; A1 := lowmem $dc.w -- SAME cell as site 1
f88df8  trap    #$d
f88dfa  clr.w   $b72.w
f88dfe  movem.l (A7)+,D0-D3/A0-A2/A5
f88e02  rte
```

- **A1 source:** lowmem `$dc.w` — confirmed identical to site 1.
- **A5 source:** the routine's own entry `movem.l ...,-(A7)` **saves**
  (not sets) `A5`; it is whatever the interrupted context held at
  `f884be`, restored unchanged at `f88dfe`. Provably not assigned within
  this routine.
- **`trap #2`?** None found anywhere in `f884be`-`f88e02`. One `trap #4`
  occurs elsewhere in the routine (`f8855e`, unrelated to this path).
- **Node writes before enqueue:** `+2 := 0x000e` at `f88dee`, same as
  site 1. No `+0`/`+4`/`+6` writes on this path.
- **Gating:** `cmpi.b #$1,$17e.w`/`bne` (a flag check), then a countdown
  drain loop on `$b76.w` calling a helper (`f88e04`) repeatedly before
  reaching the trap — structurally a "flush N pending sub-events, then
  post one summary node" pattern.
- **Immediately after return:** `clr.w $b72.w`, then the routine's own
  `movem.l`/`rte` epilogue.

*Site 3 — `f89b54`.* Reached from within a large scan-code classification
cascade (`f89aec`+) that itself sits inside a region also containing a
UART/serial-style transmit-ring handler (`f89a5a`-`f89a98`, which reads a
ring buffer bounded by `$3b8.w`/`$3ba.w`/`$3bc.w` and writes bytes to a
hardware register `$fffc4817.l`, and separately calls `jsr $fff87f3e.l` —
another independent static caller of the same dispatcher context-save
entry point used by `trap #14`). This handler ends in `rte` at `f89a58`/
`f89a70`, i.e. it is a **different** routine from the one containing
`f89b54`, which instead ends in a bare `rts` at `f89b56`:

```
f89b44  and.b   $cde.w,D0
f89b48  move.b  D0,$3c7.w
f89b4c  move.b  $3c7.w,(A1)   ; A1 here is NOT yet the queue header -- it
                              ; still holds whatever value it had from
                              ; earlier scan-table addressing in this
                              ; cascade (a different, unresolved structure)
f89b50  movea.w $dc.w,A1      ; A1 reloaded HERE, immediately before the trap
f89b54  trap    #$d
f89b56  rts
```

- **A1 source:** lowmem `$dc.w` — confirmed identical to sites 1 and 2 (a
  third independent confirmation that this is a shared, well-known cell).
- **A5 source, node allocator, node writes:** **not established.** No
  `movea`-to-`A5` and no `($2,A5)`-style tag write were found in the
  immediate vicinity of this call (unlike sites 1/2). The node passed to
  this call site was not identified this round.
- **Gating:** part of a longer `sub.b #$3a,D2`/`bne`/`tst.b D3`/`sne`
  classification cascade earlier in the same block (`f89aec`-`f89b48`);
  the specific condition immediately gating the trap itself was not
  isolated from the surrounding cascade this round.
- **Immediately after return:** `rts` — no memory write, hence the
  read-side (stack-pop) tap described in TASK 3.

**TASK 2 — destination queue classification:**

All three sites source `A1` from the same lowmem cell `$dc.w`. Because
`A1` is a *variable*, not a constant, the queue-header structure itself
was not statically locatable by address; its field meanings are known
from the already-decoded `trap #12`/`#13` handler bodies (4.11/4.12) and
apply here as the same structure shape: `+0` activation/function
pointer, `+4` side slot, `+6` head, `+8` tail, `+0xa` limit, `+0xc` count,
`+0xe`/`+0x12` auxiliary (overflow path only), `+0x10` gate/enable. No
live value for `$dc.w` itself was captured this round (see TASK 3 — it
was never read or written post-gate), so the actual header address it
resolves to remains unknown; field values could not be dumped this round.

- **Static references to `$dc.w`:** read (never written) at **20
  separate addresses** ROM-wide, including one especially significant
  site, `f8a068`-`f8a0a4`: a routine that walks a table of slots spanning
  `0x106e`-`0x12d8` (stride `0x48`), and for every node found in each
  slot's own linked list, sets that node's `+2 := 0x0002` and enqueues it
  onto the `$dc.w` queue via **`trap #12`** (not `#13`) — i.e. a
  **separate, fourth producer** of this same queue exists, structurally a
  "drain every scheduler slot's pending list into the shared queue"
  operation, and it unconditionally overwrites whatever `+2` tag the node
  previously had with `0x0002`. This means `+2` as observed *at* this
  queue is not always the value its ultimate originator gave it — this
  drain path stamps its own tag over any prior one.
- **Initializer:** no static writer of `$dc.w` was found anywhere in the
  ROM (same immediate-store search technique already used for the trap-
  vector table and for `$dc.w`'s siblings). Consistent with the already-
  established pattern that certain lowmem pointer cells are populated by
  a bulk copy at boot rather than individual instructions, not
  independently re-verified this round.
- **Consumer/dequeue routine:** not identified this round (out of scope
  for the three producer sites requested; a plausible candidate is the
  unlabeled subroutine immediately following `trap #13`'s own `rte` at
  `f8822c`, noted in 4.12 as looking like a head-based dequeue, not
  chased further).
- **Subsystem classification:** kept structural. Confirmed structural
  facts: `f884be`'s routine reads a hardware status byte (`$fffc480b.l`)
  and, on one dispatch path, writes/reads other `$fffc48xx.l`-mapped
  bytes; `f89a5a`'s adjacent (but distinct) routine drains a byte ring
  into hardware register `$fffc4817.l`. These are genuine hardware I/O
  accesses consistent with a serial/DUART-style peripheral service, but
  **no consumer was traced this round proving `$dc.w`'s queue itself is
  MIDI, panel, or keyboard-specific** — the hypothesis is not asserted
  beyond the literal, confirmed hardware-register facts above.

**TASK 3 — live instrumentation after `KEYBOARD TUNED`:**

Reused the existing `ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE` flag
and `m_pti` state (no new overlapping flag). Added: per-site entry taps
(on each trap's own exception-frame push, same technique as the already-
validated `trap #4`/`#9` taps) logging full registers, the node at `A5`
(`+0`/`+2`/`+4`/`+6`/`+8`/`+0xa`), `+2` in raw/sign-extended/24-bit-bus
form classified against all five known constants, and the queue header
at `A1` — all captured *before* the trap runs; per-site return taps
(write-side for sites 1/2, a stack-pop read-side tap for site 3, mirroring
the already-established `ae20` early-return proxy) comparing queue-header
before/after and testing node reachability from the head/tail chain; one
shared, caller-agnostic tap on the dispatcher's `f87f82` (a proxy for
"reached the `f87f3e`/`f87f80` context-save continuation," since `f87f80`
itself is register-only); and a broad, PC-independent net on every read
and write of `$dc.w` itself.

Raw hit counts, full boot, gate confirmed open (`ASR10_PTI_GATE_OPEN`
fired, vector table dumped as in 4.12):

```
ASR10_PTI_TRAP13_CALL:        0   (all three sites combined)
ASR10_PTI_TRAP13_RETURN:      0
ASR10_PTI_DC_POINTER_WRITE:   0   (broad net, any PC)
ASR10_PTI_DC_POINTER_READ:    0   (broad net, any PC -- includes all 20
                                   static read sites, not just the three
                                   trap #13 callers)
ASR10_PTI_DISPATCHER_F87F82:  5   (active_slot = 1,2,3,4,5, one hit each)
```

Also reconfirmed in the same run: `ASR10_PTI_TRAP2_ENTRY` = 0,
`ASR10_PTI_TRAP12` = 0, `ASR10_PTI_TRAP14` = 0, `ASR10_PTI_TRAP4` = 0,
`ASR10_PTI_TRAP9` = 0.

**`$dc.w` is not touched — read or written — at all after the tuning
gate opens, in this captured boot.** This is a valid negative, broader
than just the three requested call sites: it covers every one of the 20
static read sites ROM-wide, not only `f883ac`/`f88df8`/`f89b54`. All of
the enqueue mechanisms examined across this and the prior round (`trap
#2`, `#4`, `#9`, `#12`, `#13` at all three sites, `#14`) are silent post-
tuning in this run.

The one live signal that *did* fire — `f87f82`, five times, once per
`active_slot` 1 through 5 — is not explained by any producer instrumented
this round (all of them show zero), and was not itself traced to a
specific caller (candidates include `trap #15`, whose own vector-46-style
tail-jump also reaches this code, and the independent `f89a68 jsr
$fff87f3e.l` site found in TASK 1's site-3 disassembly; neither was
instrumented this round). Reported honestly as an unresolved, separate
observation — not claimed to be caused by anything above.

No positive-control node-reachability result exists this round, since no
`trap #13` call fired to test it against; the reachability check in
`log_pti_trap13_return()` is implemented and was exercised at build/Gate 1
time only in the sense that it compiles and does not corrupt state — it
has not yet observed a real call.

**TASK 4 — producer/consumer cross-reference:**

Because every relevant tap (all three sites, the broad `$dc.w` net, and
the other five known enqueue mechanisms) returned zero hits, there is no
*live* node `+2` value to cross-reference this round. Falling back to the
static writers already found:

- `0x000e`, written at `f883a2` and `f88dee` (sites 1 and 2) — both
  writers execute *inside* hardware-interrupt-style dispatch code
  (`f88300`-`f883fa` and `f884be`-`f88e02` respectively); neither was
  observed live to run at all post-gate (their containing routines were
  not separately instrumented, only the exact trap sites — see the TASK 1
  caveat that whether these *routines* run, versus whether they take the
  *specific branch* leading to `trap #13`, are different unanswered
  questions).
- `0x0002`, written at `f8a088` by the separate slot-drain routine
  (TASK 2), which enqueues via `trap #12` — also zero live hits this
  round (`ASR10_PTI_TRAP12` = 0).
- Site 3's node `+2` value: unknown, producer not found.

**None of `f8948e`/`f89494`, `f894b4`, `PANEL_ENQUEUE`, `$0544`, `$04fe`,
`$04b2`, or callback identities `6b1c`/`6cf2`/`d10a`/`89a2`/`001a` were
observed on any live path this round**, because no path through any of
the instrumented producers executed at all. No browser relationship is
claimed from numeric proximity — there is simply no live path to compare
against this round.

**TASK 5 — comparison table:**

| | Site 1 (`f883ac`) | Site 2 (`f88df8`) | Site 3 (`f89b54`) |
|---|---|---|---|
| Containing routine | `f88300`-`f883fa`, tick-service, entry/A5 origin unresolved | `f884be`-`f88e02`, hardware-status-byte IRQ dispatch, provably bounded | scan-code cascade near `f89aec`, entry/A5 origin unresolved |
| `A1` queue | lowmem `$dc.w` (shared, all 3) | lowmem `$dc.w` (shared) | lowmem `$dc.w` (shared) |
| Node allocator | none (`trap #2` absent); `A5` ambient | none (`trap #2` absent); `A5` ambient (saved/restored, not assigned) | not established |
| Node `+2` value | static `0x000e` | static `0x000e` | not established |
| Node `+4`/`+6` args | not written on this path | not written on this path | not established |
| Pre/post-tuning hits | 0 post-tuning (live); pre-tuning not measured | 0 post-tuning (live) | 0 post-tuning (live) |
| Queue activation observed | no | no | no |
| Downstream consumer | not identified | not identified | not identified |
| Browser/UI relevance | none observed | none observed | none observed |

**Classification: (A) — no `trap #13` producer runs after tuning**, in
this captured boot. Extended by this round's broader net: no producer of
*any* kind (`trap #2`/`#4`/`#9`/`#12`/`#13`×3/`#14`) touches this whole
node/queue subsystem after the tuning gate opens. This does not rule out
B-E for the separately-observed `f87f82` hits, whose cause is a genuinely
open, different thread (candidates: `trap #15`, or the independent
`f89a68` caller — neither instrumented this round).

**TASK 6 — documentation discipline, self-check:**

- The 4.12 `trap #13`/`#14` correction is preserved and not overwritten
  above; this section builds on it (`trap #13` = vector 45 = `f881f6`,
  reached from `trap #14` only via one genuine nested-trap edge).
- Static reachability (`$dc.w` read at 20 sites; `f883ac`/`f88df8`/
  `f89b54` statically call `trap #13`) is kept distinct from live
  execution (all zero this round) throughout.
- Full register values (`a1_full`/`a5_full`, 32-bit as read from
  `state_int`) are logged distinctly from the 24-bit bus-masked forms
  (`a1_24`/`a5_24`) in `ASR10_PTI_TRAP13_CALL`.
- No queue was named MIDI/panel/browser in this section without a proven
  consumer; only literal hardware-register facts are asserted (TASK 2).
- Zero-hit results are reported as valid negatives throughout, not
  omitted.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`, confirmed twice across two rebuilds this
round); `git diff --check` clean. Raw hit counts and the comparison table
are shown above. **Single smallest remaining producer/consumer edge:**
the five unexplained `f87f82` hits (one per `active_slot` 1-5) — the only
live signal in the entire node/queue/dispatcher family observed this
round, with an as-yet-unidentified caller. No node, branch, or queue
state was synthesized or forced; nothing was committed.

### 4.14 The five `f87f82` hits resolved: `trap #6` (direct) and `trap #7`, not `trap #15` or any `f87f3e` caller (narrow, not yet reviewed)

**Important scope note, per this round's own instruction:** the 4.13
result — every instrumented `$dc.w`-queue path (`trap #2`/`#4`/`#9`/`#12`/
`#13`×3/`#14`) silent post-tuning — is a negative **for that specific
queue family only**, not for every possible node/scheduler mechanism in
the ROM. This section's findings (a different trap family entirely,
`trap #6`/`#7`, unrelated to `$dc.w`) demonstrate that directly: they are
live and active in the very same window where the `$dc.w` family is
silent.

**TASK 1 — control-flow map of `f87f1a`-`f87fd0`:**

Full `unidasm` disassembly of this range (already captured in 4.12,
extended here) resolves it as **the scheduler's own slot table and core
dispatch loop**. `f87f1a`-`f87f3a` initializes a table of 22-byte (`0x16`)
slot records spanning lowmem `$c6.w`-`$d0.w` (bound `$d0.w`, distinct from
the live dispatch loop's bound `$c8.w` — the init pass covers a larger
range than the steady-state scan). `f87f92`-`f87fd0` is the **idle/dispatch
scan**: walk slot records from `$c6.w` to `$c8.w` (stride `0x16`), test
`(+2) XOR (+3)`; if zero, advance to the next slot; if non-zero ("ready"),
pop that slot's saved context (`+6` long = saved PC, `+0xa` word = saved
SR, `+0xe` word = saved USP, `+0xc` word = a node pointer restored into
`A5`) and `rte` directly into it, first setting `$b6a.w := A2` (confirming,
again, that `$b6a.w` is a pointer *into this same slot table*, not a
separate structure). If the scan reaches `$c8.w` without finding a ready
slot, it re-enables interrupts to level 0 (`move #$2000,SR`) and loops
back to `f87f92` — this is the idle spin.

- **`f87f3e`** (`move USP,A0`): a **full context-save + synthetic-return-
  frame builder**. Saves `D2`-`D7`/`A2`-`A6` and three exception-frame
  words onto the *old* USP-based frame via `A0`, builds a *new* frame
  whose return PC is hardcoded to `#$fff87f66` (not the real caller — a
  classic cooperative-switch trick: this task will not resume by normal
  `rte` at its own call site, but via the scheduler's own `f87fc0` `rte`,
  whenever its slot is next found ready, landing at `f87f66`), tags bits
  in the *current* `$b6a.w` slot's `+2`/`+3` via `bset D0,...`, then falls
  into `f87f80`.
- **`f87f66`** (`move USP,A0`): a **separate context-*restore*** routine
  (pops `D2`-`D7`/`A2`-`A6`, restores `A1`, `jmp (A1)`). Entered *only* by
  the synthetic frame `f87f3e` built — never by direct call or branch (see
  TASK 2: exhaustive search found exactly one occurrence of `f87f66`
  anywhere in the ROM, the immediate-construction instruction itself).
  Classified: **RTE-built synthetic return frame**, the only such edge
  found this round.
- **`f87f80`** (`move USP,A0`): the **shared suspend continuation**,
  saving the current USP into the active slot's `+0xe` (at `f87f82`,
  genuinely tapped), then the frame's SR/PC into `+0xa`/`+6` (`f87f86`/
  `f87f8a`), before unconditionally masking interrupts and falling into
  the idle scan at `f87f92`.
- **`f87f82`**: not an entry point — the second instruction of `f87f80`'s
  own body, kept as the tap proxy since `f87f80` itself is register-only.

**TASK 2 — every static producer, found and classified:**

```
$ grep -n "87f3e" asr10_full_rom.dasm | grep -v "^[0-9]*:f87f3e:"
f881d0: bsr     $f87f3e        <- inside trap #12's extended body (new
                                  finding this round; 4.11/4.12 had not
                                  disassembled this far into trap #12)
f881f0: jsr     ($f87f3e,PC)   <- trap #14 (already known, 4.12)
f89a28: jsr     $fff87f3e.l    <- new, inside an unlabeled UART/serial-
f89a4e: jsr     $fff87f3e.l    <-   ring-adjacent cascade (f89a0e-f89a72,
f89a68: jsr     $fff87f3e.l    <-   3 near-identical repeated blocks)

$ grep -n "87f66" asr10_full_rom.dasm | grep -v "^[0-9]*:f87f66:"
f87f50: move.l  #$fff87f66,($2,A7)   <- ONLY the constructor. Zero callers.

$ grep -n "87f80" asr10_full_rom.dasm | grep -v "^[0-9]*:f87f80:"
f87f64:  bra $f87f80   <- f87f3e's own tail
f88062:  bra $f87f80   <- trap #15 (f88056)
f88104:  bra $f87f80   <- new: trap #5/#6 fall-through chain (below)
f88128:  bra $f87f80   <- new: trap #7 (f88108)

$ grep -n "87f76" asr10_full_rom.dasm | grep -v "^[0-9]*:f87f76:"
(no output — zero call/branch references anywhere)
```

`f87f76` having **zero** static call/branch references (not even a
constructed-address pattern like `f87f66`'s) meant its entry mechanism was
unresolved by this search alone — resolved by a live, one-shot scan of
every autovector and trap vector (extending the existing vector-dump
gated at `f880fc`, TASK 4/5 below), which found it is **`trap #1`'s own
vector target directly** — reached purely through the CPU's trap
dispatch, never a call or branch. The same scan resolved the two other
previously-unidentified `bra $f87f80` sites:

```
ASR10_PTI_TRAPVECTOR trap=0  handler=f88280   (spurious/error dispatch)
ASR10_PTI_TRAPVECTOR trap=1  handler=f87f76   <- direct, zero xrefs, confirmed
ASR10_PTI_TRAPVECTOR trap=2  handler=f88066   (alloc, 4.11)
ASR10_PTI_TRAPVECTOR trap=3  handler=f88078   (alloc without the count-limit
                                                check -- f88066's own +0xc
                                                fall-through target; resolves
                                                4.13's open "what is trap #3"
                                                question)
ASR10_PTI_TRAPVECTOR trap=4  handler=f880a2   (free, 4.10)
ASR10_PTI_TRAPVECTOR trap=5  handler=f880b6   <- new
ASR10_PTI_TRAPVECTOR trap=6  handler=f880d6   <- new
ASR10_PTI_TRAPVECTOR trap=7  handler=f88108   <- new
ASR10_PTI_TRAPVECTOR trap=8  handler=f8812c
ASR10_PTI_TRAPVECTOR trap=9  handler=f88138
ASR10_PTI_TRAPVECTOR trap=10 handler=ff88e8   (high-view)
ASR10_PTI_TRAPVECTOR trap=11 handler=ff88e2   (high-view)
ASR10_PTI_TRAPVECTOR trap=12 handler=f88174   (enqueue, 4.11)
ASR10_PTI_TRAPVECTOR trap=13 handler=f881f6   (enqueue, 4.12)
ASR10_PTI_TRAPVECTOR trap=14 handler=f881e6   (nested call + jsr f87f3e, 4.12)
ASR10_PTI_TRAPVECTOR trap=15 handler=f88056   (4.12)
ASR10_PTI_AUTOVECTOR level=1..7  handler=f882da (all seven, same handler --
                                                   the generic spurious/
                                                   panic dispatch; none of
                                                   the hardware autovector
                                                   levels are individually
                                                   serviced)
```

Note in passing: `f880fc` — used throughout this whole investigation
since the very first round as "the gate, a point already guaranteed to
fire" — is now understood to be a specific instruction (`bset D2,($2,A2)`)
*inside* `trap #6`'s own handler body (the empty-sub-queue branch). This
explains why it reliably fires, but re-litigating the gate's own semantics
is out of scope for this round.

`trap #5` (`f880b6`) falls straight through (no `rte` in between) into
`trap #6`'s body (`f880d6`), which is *also* an independently-addressable
vector — i.e. `f87f80` can be reached via `trap #5`'s vector (through
`f880d6`) or via `trap #6`'s vector directly. `trap #7` (`f88108`) is
separate and self-contained. This gives the complete, exhaustive static
edge inventory into `f87f80`/`f87f3e`/`f87f66`: five direct `f87f3e`
callers, plus four trap-vector paths (`#1` direct, `#5`/`#6` chain, `#7`,
`#15`) that reach `f87f80` without ever touching `f87f3e`.

**TASK 3 — live source classification (per-entry, not just the shared proxy):**

Instrumented all nine distinct entries individually under the existing
`m_pti` state (no new flag): each stashes a full snapshot (PC, active
slot, `D0`-`D3`, `A0`-`A6`, `USP`, `SR`) into a single-slot "last entry"
record on hit; `f87f3e`'s own body entry (tapped at `f87f40`, its first
genuine memory access) and `f87f82` both *consume* that record if — and
only if — it is still fresh and unconsumed, so a correlation is only
reported when a proven entry event genuinely precedes the hit, never by
nearest-log-line timing (satisfying the explicit instruction not to
correlate by approximate timing).

**TASK 4 — `trap #15` specifically:** instrumented at its first genuine
access (`f8805a`, the first `bset`), logging `D0` (the bit number),
`$b6a.w` (active slot pointer), and slot `+2`/`+3` immediately before
either `bset` runs. **Result: zero hits.** `trap #15` did not fire at all
in this captured run — ruled out as a cause of any of the five `f87f82`
hits, not merely un-correlated.

**TASK 5 — every confirmed direct `f87f3e` caller specifically:**
instrumented all five (`f881d0`, `f881f0`, `f89a28`, `f89a4e`, `f89a68`)
at their own call instruction (a genuine return-address push in every
case), plus `f87f3e`'s own body-entry tap as a cross-check. **Result: all
five show zero hits, and the `f87f3e` body-entry counter itself is zero.**
`f87f3e` did not execute at all this run — ruling out every one of its
five known callers, including `f89a68`, as a source of the five `f87f82`
hits. This is the opposite of an assumption: it is a live, per-caller
negative.

**TASK 6 — the five-hit timeline, live:**

```
ASR10_PTI_F87F82_SEQ seq=1 pc=f87f82 active_slot=1 source_known=0 source_name=UNKNOWN_STALE
ASR10_PTI_F87F82_SEQ seq=2 pc=f87f82 active_slot=2 source_known=1 source_name=trap6_direct_f880d6 source_pc=f880e0
ASR10_PTI_F87F82_SEQ seq=3 pc=f87f82 active_slot=3 source_known=1 source_name=trap6_direct_f880d6 source_pc=f880e0
ASR10_PTI_F87F82_SEQ seq=4 pc=f87f82 active_slot=4 source_known=1 source_name=trap7_f88108     source_pc=f8810c
ASR10_PTI_F87F82_SEQ seq=5 pc=f87f82 active_slot=5 source_known=1 source_name=trap7_f88108     source_pc=f8810c
```

| seq | source entry | source PC | mechanism | active slot | D0 (at entry) | D2 (at entry) | slot `+2`/`+3` change (static, not live-confirmed this round) |
|---|---|---|---|---|---|---|---|
| 1 | unresolved — see below | — | trap-vector (almost certainly `trap #6`, direct) | 1 | — | — | — |
| 2 | `trap6_direct_f880d6` | `f880e0` | trap-vector, direct (bypassed `trap #5`) | 2 | `0000ffff` | `00000007` | pop sub-queue at slot`+0x10`; `bclr`/`bset` `D2`(bit 7) on `+2`; `bset` `D2` on `+3` |
| 3 | `trap6_direct_f880d6` | `f880e0` | trap-vector, direct | 3 | `00000007` | `00000007` | same |
| 4 | `trap7_f88108` | `f8810c` | trap-vector, direct | 4 | `00000000` | `00000007`(stale, not trap #7's own) | `bclr`/`bset` `D1`(bit 0) on `+2`; `bset` `D1` on `+3` |
| 5 | `trap7_f88108` | `f8810c` | trap-vector, direct | 5 | `00000063` | `00000007`(stale) | same |

`via_trap5_chain` was confirmed `0` for both `trap #6` hits — **`trap #5`
itself never fired** (its own entry counter is zero); both arrivals at
`trap #6`'s body came from `trap #6`'s *own* vector directly, not from
`trap #5`'s fall-through. Both `trap #6` hits and both `trap #7` hits show
`D2=7` at entry — consistent for `trap #6` (it is `trap #6`'s own fixed
`moveq #$7,D2`) but for `trap #7` this is a *stale* value left over from
elsewhere (`trap #7`'s own dispatch register is `D1`, zeroed by its body
just after this tap fires, not `D2`).

**`seq=1`'s "unknown" source is explained, not just reported:** `f880fc`
— the pre-existing gate-open landmark — is itself *inside* `trap #6`'s
body (the empty-sub-queue branch). The very invocation of `trap #6` whose
`f880fc` instruction flips the gate open necessarily executed its *own*
`f880e0` entry-tap instruction slightly earlier in that same pass, while
the gate was still closed — so that one entry event was never stashed,
even though its later `f87f82` consequence, occurring after the gate
opened, was captured. This is a boundary artifact of the gate mechanism
itself, not a fourth, unidentified mechanism — `seq=1` is almost certainly
also `trap #6`, direct.

**Classification: (E) — a mixture of multiple mechanisms**, though a
narrow one: only two distinct trap vectors are involved (`trap #6`
direct ×2-3, `trap #7` ×2), both software `trap #n` instructions (not
hardware autovectors — all seven autovector levels point to the single
generic spurious/panic handler `f882da`, confirmed live), neither `trap
#15` nor any `f87f3e` caller. Not (A) in the sense of "already known" —
this mechanism was not previously characterized in this document; it *is*
plausibly a one-time-per-slot initialization/priming pattern (one hit per
`active_slot` 1 through 5, no repeats), consistent with, but not proven
to be, (A)'s spirit.

**TASK 7 — browser relevance, slot 0 traced one edge back:**

**`active_slot` was 1, 2, 3, 4, or 5 at every one of the five hits — never
0.** Neither `trap #6` nor `trap #7` (nor any of the seven other
instrumented mechanisms) was observed live to touch slot 0's own
`+2`/`+3` ready flags in this run. Tracing one edge back: both `trap #6`
and `trap #7` operate on `$b6a.w`'s *current* value (whatever slot is
presently active) — they are self-referential "check my own pending
sub-queue, update my own ready flags" primitives, not "post to an
arbitrary target slot" operations. For either to affect slot 0, `$b6a.w`
would have to equal slot 0's own record address at the moment one fires —
which never happened in this run. Slot 0's own `ae18` resume (established
in 4.9, from earlier rounds) therefore is **not** driven by `trap #6`/`#7`
in this capture; the mechanism that actually marks slot 0 ready is a
different, not-yet-live-reinstrumented one — most plausibly `trap #9`
(the "post to an explicit target slot via `A1`" primitive already
partially characterized in earlier rounds, which does not depend on
`$b6a.w`'s current value the way `trap #6`/`#7`/`#15` do). This is one
edge back, as requested, not a full re-investigation.

`ae18` being slot 0's saved resume PC is unchanged from 4.9: it is simply
whatever was stored in slot 0's own `+6` field the last time it was
suspended, before any of this round's newly-examined mechanisms fired.

**Whether an expected later request for slot 0 is absent:** yes, within
this round's scope — no live event among any of the nine instrumented
entries ever touched slot 0's ready flags, in the entire post-gate
window. This is a valid negative for *these nine mechanisms specifically*
(matching TASK 6's explicit framing above) — it says nothing about
`trap #9` or any other mechanism not reinstrumented this round, and per
this round's own instruction, `$dc.w` producer analysis was not revisited
since no live source pointed there.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); live run confirmed all nine entries'
per-caller hit counts, the `f87f3e` body-entry counter (`0`), and the
five-hit `f87f82` sequence above; `git diff --check` clean. No state was
forced; nothing was committed.

### 4.15 The blocker resolved, live: node `+2=0x89a2`, posted to slot 0 by trap #9 the instant "KEYBOARD TUNED" completes, never matches the browser-callback whitelist (not yet reviewed)

**Documentation corrections (per this round's explicit instruction):**
- **`f880fc` is inside `trap #6`'s own body** (the `bset D2,($2,A2)` empty-sub-queue
  branch, established in 4.14). It is **too late, and the wrong tool, for a
  universal "post-tuning" causality gate** — as this section proves directly:
  every capture in 4.11-4.14 that gated on `m_pti.seen_f880fc` was, in fact,
  observing a boot that had **not yet reached, or barely reached, tuning
  completion at all** (see below — those captures never enabled
  `ASR10_DIAG_PANEL_AUTORESPOND`, so the boot stalled waiting for a panel
  prompt well before tuning). Every "zero hits post-`f880fc`" result in
  4.11-4.14 is **still a true fact about those specific captures**, but must
  **not** be read as "does not happen after tuning" — it was never proven
  that tuning had happened yet in those runs.
- The `trap #6`/`#7` `f87f82` burst (4.14) is now understood as generic
  **task/scheduler initialization** (it recurs for slots 1 through 5 on
  every capture regardless of tuning progress) — not given any tuning- or
  browser-specific role here or previously.
- Correction acknowledged: this document does **not** claim hardware
  servicing is unable to use vectored interrupts. 4.14 observed that all
  seven *autovector* levels share one generic handler; it says nothing
  about the MC68302's own internal vectored-interrupt scheme (trap
  numbers 0-15 are plausibly *are* that scheme, per 4.14's own read —
  software `trap #n` instructions, not CPU autovectors, are what the ROM
  actually uses for per-device servicing).

**Epoch correction:** all new instrumentation below is gated purely on
`m_pti.enabled`, active from machine reset — never on `seen_f880fc`.

**TASK 1 — the real display timeline, from reset, with
`ASR10_DIAG_PANEL_AUTORESPOND=1` also enabled (without it, the boot never
gets past "PLEASE INSERT DISK"-style prompting and tuning never starts —
itself a finding, see below):**

```
time=10.411s  "qf   ENSONIQ  ASR-10    f    LOADING SYSTEM    "  (boot logo)
time=10.422s  "q"
time=10.437s  "qq~"                                    first_char_pc=f89c48
time=10.437s  "t" (x16, one flush per repeated poll)     char_pc=f89aa4
time=10.519s  "fTUNING KBD - HANDS OFF"                 char_pc=f89aa4
time=10.562s  "q"
  [never flushed again -- but see below: "f    KEYBOARD TUNED" IS fully
   composed in the character buffer, byte by byte, confirmed via 97
   separate character-arrival snapshots ending in the complete string]
```

**Exact firmware wording, confirmed live, character by character:**
`"TUNING KBD - HANDS OFF"` (verbatim, matching the manual) and
`"KEYBOARD TUNED"` (verbatim) — **both** are emitted, as two separate,
complete messages, with a leading `f`/4-space prefix (a control/segment
byte, not text). Static ROM search had found `"TUNING"`, `"HANDS OFF"`,
`"TUNED"` and `"CALIBRAT"` as *separate* string-table fragments at
different offsets (`0x1196`, `0x1676`, `0x12cd`/`0x12d3`) with no
contiguous `"KEYBOARD TUNED"` literal anywhere in the ROM — the live
capture resolves this: the firmware assembles the full message
character-by-character at runtime (not from one contiguous ROM string),
and the assembled result is exactly `"KEYBOARD TUNED"`, confirmed by 97
sequential single-character snapshots (`panel="f"`, `"f "`, ...,
`"f    KEYBOARD TUNED"`) with no truncation or abbreviation.

**The higher-level producer:** both messages are written through the same
low-level per-character path (`panel_text_byte`, gated on the
`DUART_PANEL_ASR_CANDIDATE` hardware write region); `"TUNING KBD - HANDS
OFF"`'s characters all arrive at PC `f89aa4`, and `"KEYBOARD TUNED"`'s
first character also arrives at `f89aa4` per the buffer-reset log
(`first_char_pc=f89aa4` is implied by the sequential single-char captures
immediately following the `f89aa4`-sourced `"t"` flushes) — i.e. **the
same panel-output routine (`f89aa4`) drives both messages**; the
higher-level routine that *selects which message to send* (as opposed to
the generic per-character output primitive) was not separately isolated
this round — a remaining static task, not chased further given TASK 2's
result below made it moot for the blocker itself.

**Critical, decisive finding: the completed `"KEYBOARD TUNED"` string is
never flushed as a discrete display event again for the rest of the
55-second capture, because no further byte ever arrives at all** — not
because of a missing terminator specifically, but because **panel output
stops completely** the instant the message finishes. This is confirmed
across the *entire* 55-second, 393,000-line capture: zero further
`ASR10PANEL`/`ASR10_PTI_PANEL_TIMELINE` events after the stray `"q"` at
10.562s (which itself precedes the character-by-character assembly of
`"KEYBOARD TUNED"` — the buffer resets to empty and then simply never
gets flushed once full).

**TASK 2 — calibration completion, traced to its exact producer:**

```
f8f2d2  jsr    $9106.w
f8f2d6  move.w A7,$8400.w
f8f2da  btst   #$4,$8435.w        ; the completion condition tested
f8f2e0  beq    f8f2f8              ; not yet complete: skip straight to yield
f8f2e2  trap   #$2                 ; ALLOCATE a fresh node (A5 := new node)
f8f2e4  bcs    f8f2f8              ; allocation failed: skip
f8f2e6  move.w #$89a2,($2,A5)      ; *** THE PRODUCER OF "89A2" ***
f8f2ec  movea.w $d8.w,A1           ; A1 := lowmem $d8.w (resolved live to
                                   ; 0023d4 -- slot 0's own record address)
f8f2f0  trap   #$9                 ; POST the tagged node to slot 0
f8f2f2  bclr   #$4,$8435.w         ; clear the completion flag (one-shot)
f8f2f8  trap   #$6                 ; yield (the same trap #6 from 4.14)
f8f2fa  jsr    $ffff9650.l
```

This fully resolves `current-blocker.md` section 8/9's long-standing
"producer and meaning of node type `89A2` unknown": **`0x89a2` is a
literal, hardcoded immediate constant**, written into a *freshly
allocated* node's `+2` field (via `trap #2`, matching 4.11's allocator),
the instant bit 4 of status byte `$8435.w` is found set. It carries no
computed/measured value — it is a fixed message-type tag, nothing more.

**Calibration counters/fields, as observed on this exact path:** the
decision input is `btst #$4,$8435.w` — a single status bit, not a
counter or retry/failure flag. `$0183`/`$031c`/`$031f`/`$ce00`/`$ce30`
were **not** found on this specific path (this routine's gating condition
is `$8435.w` alone); `$031c` and `$031f` do appear slightly further down
the same routine (`f8f346`-`f8f35a`, gating a *different*, subsequent
branch not chased this round, since the blocker was already resolved by
this point — see TASK 5). **Destination PC after "successful"
completion:** the routine does not jump anywhere special — it falls
through to its own `trap #6` yield at `f8f2f8`, same as the "not yet
complete" path (`f8f2e0`'s `beq`), just having additionally posted the
node. There is no separate "final success state" distinct from this: the
firmware reaches a *message-posting* event, not a distinct terminal
"calibration complete" mode.

**Does the firmware reach a truly final calibration state, or only an
intermediate one?** Based on this path alone: it reaches the point of
*posting one tagged node* and immediately yields — whether `$8435.w` bit
4 ever gets set *again* later (e.g. for a hypothetical "fully verified"
second stage) was not observed in this capture (the bit-4 branch fires
once, per the single live `ASR10_PTI_TRAP9_EPOCH` hit in the whole run).

**TASK 3 — delay/event arming, from reset (not just post-`f880fc`):**

```
ASR10_PTI_TRAP8 count=1 pc=f88134 caller=0071a6 d0_delay=0x3e8(1000) target_slot=23d4(slot0) time=10.5623s
ASR10_PTI_TRAP8 count=2 pc=f88134 caller=006876 d0_delay=0x0004        target_slot=242c(slot4) time=10.5714s
ASR10_PTI_TRAP8 count=3 pc=f88134 caller=0077a0 d0_delay=0x0064(100)   target_slot=2442(slot5) time=10.5714s
ASR10_PTI_TRAP9_EPOCH count=1 pc=f8813c caller=f8f2e6 a1=0023d4(slot0) a5=0014f4 targets_slot0=1 time=10.5713s
ASR10_PTI_CA_LIST_DECREMENT: 0 hits (the whole "$00ca-list" tick routine
                                       never runs in this capture)
ASR10_PTI_CA_LIST_CALLBACK:  0 hits
ASR10_PTI_SLOT0_READY_BIT_WRITE: 97 hits total (full sequence below)
```

Only **one** `trap #9` fires in the entire 55-second run, and it is the
exact one from TASK 2 (`caller_pc=f8f2e6`), posting node `14f4` — **not**
a freshly-allocated node's own address, but the *already-known,
already-tagged* `0x14f4` node this whole investigation has centered on
since section 8/9 (i.e. `f8f2e2`'s `trap #2` allocation *is* what
produces node `14f4` here — the two are the same node, live-confirmed by
address, not inferred). `trap #8` (delay-arming) fires three times in
tight succession right around tuning completion, targeting slots 0, 4,
and 5 — consistent with several tasks re-arming short timeouts as part of
general scheduler housekeeping at this moment, not obviously part of the
tuning-completion decision itself (their delay values, `1000`/`4`/`100`
ticks, don't match the manual's "~3 seconds" figure at any obvious tick
rate, and this section does not attempt to resolve the tick rate).

**TASK 4 — the causal timeline, in full, live:**

```
10.571,268,875  trap #9  pc=f8813c caller=f8f2e6  a1=0023d4(slot0) a5=0014f4  panel="...TUNED" (complete)
10.571,275,875  slot0 ready-bit write  pc=f8816e (trap #9's own bclr)  value=0181  -> XOR=0x80: READY
10.571,299,000  slot0 ready-bit write  pc=f87fb0 (scheduler's OWN dispatch code)  value=0000  active_slot(stale)=3
       ASR10_DISPATCHER_RTE_PRE  slot=0  frame_pc=00ae14  slot_record: +00=03e8 +0a=0004 +0e=1e20 ...
       ASR10_DISPATCHER_RTE_FIRST_PC  actual_pc=00ae18
       ASR10_PTI_STACK_PROBE  milestone=rte_resume_slot0  pc=00ae18  active_slot=0
       ASR10_PTI_VECTOR_CALL  name=87f2  pc=00ae1a  (slot 0's OWN established ae18 loop -- 4.9)
       ASR10_PTI_NODE_GATE  name=2b1a_35e_vs_a5  a2=ff89a2  a5=0014f4   (002b14's node-classify -- 4.10)
       ASR10_PTI_NODE_GATE  name=2b26_31c_to_d5  a2=ff89a2  a5=0014f4
       ASR10_PTI_TRAP4  pc=002b2a  count=1-3  a2_before=ff89a2  a5=0014f4  (frees the node -- 4.10)
10.571,336,625  slot0 ready-bit write  pc=f880ce (trap #6, entered via slot 0 itself)
10.571,337,625  slot0 ready-bit write  pc=f880d2
10.571,342,250  slot0 ready-bit write  pc=f880fc
10.571,343,250  slot0 ready-bit write  pc=f88100  value=8181 -> XOR=0: not ready again (slot 0 yields)
       ASR10_PTI_F87F82_SEQ seq=4  source=trap5_6_chain (slot 0's own yield)
       [scheduler resumes scanning; later dispatches slot 4 (frame_pc=0068a8),
        then slot 5 (frame_pc=00779c); no further slot-0 or panel activity
        for the rest of the 55-second capture]
```

| time | panel state | executing PC/routine | calibration state | timer/event action | target slot | saved/resume PC | result |
|---|---|---|---|---|---|---|---|
| 10.519s | "TUNING KBD - HANDS OFF" flushed | `f89aa4` | in progress | — | — | — | displayed |
| 10.562-10.563s | "KEYBOARD TUNED" assembling char-by-char | `f89aa4` | in progress | `trap #8`×1 arms slot 0 (delay 1000) | slot0 | `23d4` | buffered |
| 10.5713s | "KEYBOARD TUNED" complete (buffered, never flushed) | `f8f2e6` | **bit 4 of `$8435.w` found set** | `trap #2` allocs node 14f4; tag `+2=89a2`; `trap #9` posts to slot 0 | slot0 | `ae14`/`ae18` | node posted, slot 0 marked ready |
| 10.5713s | same | `ae18`→`87f2`→`002b14` | (n/a — this is the consumer, not the producer) | node classified: `A2=ff89a2`, matches none of `6b1c`/`6cf2`/`d10a` | slot0 | — | `trap #4` frees the node |
| 10.5713s | same | `f880b6`-`f88100` (`trap #6`) | — | slot 0 yields | slot0 | — | ready-flags settle to "not ready" |
| 10.5714s+ | same, frozen | scheduler scan / idle | — | `trap #8`×2 more (slots 4, 5); scheduler dispatches slots 4, 5 | slots 4,5 | — | unrelated housekeeping |
| 10.57s-55s | **frozen, forever** | `f87f92`-`f87fc0` idle scan | — | none | — | — | **no further transition** |

**TASK 5 — classification: (E) — the task wakes but the LOAD/browser path
rejects (dead-ends) the node it was given.** Fully proven, not inferred:
slot 0 *does* wake (dispatched, `ae18` resumed, confirmed by the
pre-existing `ASR10_DISPATCHER_RTE_FIRST_PC`/`ASR10_PTI_STACK_PROBE`
instrumentation already in this driver); it *does* run its established
`ae18`→`87f2`→`002b14` node-classify path (4.9/4.10); the node it
receives is *by construction* tagged `0x89a2` (TASK 2), which — as
`current-blocker.md` and 4.10 already established — matches none of the
three whitelisted browser-callback values, so the classify step frees it
via `trap #4` and slot 0 yields with nothing further to do. This is not
(A) (completion *is* accepted — the `btst #$4,$8435.w` gate *is* taken),
not (B) (an event *is* armed and *does* fire — `trap #9`, immediately),
not (C)/(D) (there is no timer expiry in this path at all; the "wake" is
a direct, synchronous post-and-ready, not a delayed timer), and not (F)
(nothing internal to LOAD/browser ever runs — see TASK 6).

**TASK 6 — browser proof:** in this entire 55-second capture, **zero**
reads of `f8948e`/`f89494`/`f894b4`, **zero** `ASR10_FSB_ENTRY` events, and
no LOAD-mode state writes or filename `PANEL_ENQUEUE`s were observed
(confirmed by direct grep of the full log). The browser/LOAD code is
never reached at all — fully consistent with (E): the one node posted at
tuning completion is rejected before ever reaching that code, and no
other producer posts a different, better-matched node in this capture.
Per this round's instruction, `trap #9` *was* the mechanism live-proven to
wake slot 0, and has now been examined to the depth the evidence
warranted — no further reinvestigation of `$dc.w` was needed or performed,
since no live path pointed there.

**TASK 7 (carried over from 4.14, now fully answered):** the exact
status condition that requests slot 0 is `btst #$4,$8435.w` inside the
routine at `f8f2c8`-`f8f2fa`, tested once "KEYBOARD TUNED" finishes being
written to the panel. Slot 0's saved resume PC (`ae14`/`ae18`) is simply
what was already stored in its own slot record from its *previous*
suspension (unchanged from 4.9). Whether a later, *different* request
for slot 0 is absent: **yes, confirmed** — after this single `trap #9`
post-and-classify-and-free cycle, slot 0's ready flags never change again
for the rest of the capture, and no second node is ever posted to it.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. Both the
autorespond-enabled and (for comparison) autorespond-disabled captures
are preserved as raw logs. Nothing was forced — the `btst #$4,$8435.w`
branch was taken because the emulated hardware/firmware state reached it
naturally; no state was altered by this instrumentation. Not committed.

### 4.16 Node `+2=0x16`'s producer/consumer chain, and the missing edge into it (not yet reviewed)

**Baseline for every run this round:** `ASR10_DIAG_PANEL_AUTORESPOND=1`
alongside the existing PTI flag, per this round's instruction.

**TASK 1 — the routine containing `f8f2c8`-`f8f360`, disassembled and
corrected:**

`f8f2c8`-`f8f2d0` is the *tail* of a small, unrelated bit-packing helper
(`rts` at `f8f2d0`) — not part of the target routine. The actual routine
starts at `f8f2d2` (no static `jsr`/`bsr` reaches it anywhere in the ROM):

```
f8f2d2  jsr    $9106.w
f8f2d6  move.w A7,$8400.w
f8f2da  btst   #$4,$8435.w
f8f2e0  beq    f8f2f8
f8f2e2  trap   #$2                    ; alloc (see 4.15)
f8f2e4  bcs    f8f2f8
f8f2e6  move.w #$89a2,($2,A5)
f8f2ec  movea.w $d8.w,A1
f8f2f0  trap   #$9
f8f2f2  bclr   #$4,$8435.w
f8f2f8  trap   #$6                    ; <- resume point after this is f8f2fa
f8f2fa  jsr    $ffff9650.l
f8f300  beq    f8f30e
f8f302  move.w #$9,D0
f8f306  add.b  $33b.w,D0
f8f30a  move.w D0,$cda.w
f8f30e  tst.b  $33a.w
f8f312  beq    f8f346
f8f314  cmpi.w #$16,($2,A5)           ; *** the node+2=0x16 consumer ***
f8f31a  bne    f8f346
f8f31c  move.w #$880a,D3
f8f320  cmpi.b #$1,($6,A5)
f8f326  bne    f8f32c
f8f328  move.w #$8810,D3
f8f32c  moveq  #$0,D2
f8f32e  move.b ($4,A5),D2
f8f332  add.b  #$40,D2
f8f336  move.w D3,($2,A5)             ; relabels the node in place
f8f33a  move.w D2,($4,A5)
f8f33e  movea.w $d8.w,A1
f8f342  trap   #$9                    ; re-posts the relabeled node
f8f344  bra    f8f2f8                 ; loops back to the yield
f8f346  cmpi.w #$1,$31c.w
f8f34c  bne    f8f376
f8f34e  cmpi.b #$0,$31f.w
f8f354  beq    f8f376
f8f356  tst.b  $320.w
f8f35a  bne    f8f376
f8f35c  move.w $89c0.w,D0
f8f360  cmp.w  #$89ca,D0
f8f364  bne    f8f370
```

**Task-loop entry/back-edge:** there is no explicit branch back to
`f8f2d2` anywhere — the "loop" is the scheduler's own cooperative
round-trip. `trap #6` unconditionally yields (4.14: masks interrupts,
tags the active slot's ready bits, falls to `f87f80`, which captures the
trap's own auto-pushed return address — `f8f2fa` — into the slot's `+6`
field). The **explicit** `bra f8f2f8` at `f8f344` is the one coded loop
edge, and it re-executes the same yield, so the net effect across
multiple scheduler rounds is a loop through `f8f2fa`-`f8f344`.

**What `trap #6` does on this exact path:** exactly what 4.14 already
established — a generic ready-flag/yield primitive. **Correction
confirmed, not contradicted:** it does **not** dequeue anything itself.

**Where `A5` comes from after `trap #6`:** **not preserved across the
yield.** `f87f80`'s save path (4.14) captures only `USP`/`SR`/`PC` into
the slot record — `D0`-`D3`/`A0`-`A6` are not saved or restored by this
mechanism at all. `A5` is instead **reloaded fresh on every dispatch**
from the *active slot's own* `+0xc` field (`f87fb4: movea.w ($c,A2),A5`,
4.14), which is then cleared (`f87fb8`). This is the same "node mailbox"
convention already established for slot 0's own `A5=0014f4` — i.e. `A5`
at `f8f314` is whatever node was most recently posted **to whichever slot
this routine is running as**, not anything carried over from before the
yield.

**Slot-record/subqueue fields read or changed:** the active slot's `+0xc`
(node mailbox, read+cleared by the scheduler itself, not by this code);
node `+2` (read then overwritten), `+4` (read, `+0x40` added, written
back), `+6` (read only, selects `880a` vs `8810`). No direct read/write of
any slot-record field *by this routine itself* beyond what the generic
dispatch mechanism already does.

**Is a node actually dequeued?** Yes — via the generic mailbox mechanism
(`+0xc` cleared on dispatch, 4.14), not by anything specific to this
routine.

**Recognized node `+2` values on this path:** exactly one, `0x16` (via
`cmpi.w #$16,($2,A5)` at `f8f314`); no other `+2` comparison exists
anywhere in this routine.

**How `D3=0x880a`/`0x8810` is used:** written back into the *same* node's
own `+2` field (`f8f336`) — a relabel-in-place, then the node is
re-posted via `trap #9` to whatever `$d8.w` currently resolves to (not
confirmed live to be the same target as the `89a2` post — `$d8.w` is a
variable, and this round did not observe this code path executing at all,
so its live target here is unproven).

**Does downstream code reach FDC/filesystem/directory-cache/completion-event code?** Not observed — this routine never executed in
either capture this round (see TASK 6). Statically, `f8f30e`-`f8f376`
continues into further `$31c.w`/`$31f.w`/`$320.w`/`$89c0.w` checks not
fully traced this round (out of scope once TASK 6 showed no live
execution).

**Evidence for/against "slot 3 is the disk service task":** **Against
treating it as proven:** in this round's own capture, the *only* observed
dispatch of active-slot-3 resumed at `ff90f4`/`ff9106` (a high-view
routine, confirmed via the driver's pre-existing
`ASR10_DISPATCHER_RTE_PRE`/`FIRST_PC` instrumentation), **not** at
`f8f2fa`. This means `f8f2d2`'s routine is most plausibly a **shared
subroutine**, reached via a `jsr`/`bsr` from *whichever* task happens to
call it (slot 3, in the one invocation captured, per the established fact
this round opened with) — not slot 3's own top-level scheduler entry
point. **For:** `$d6.w` (the confirmed destination of the `0x16`
producers, see TASK 3) resolves live, at boot init, to `0x2416` — **slot
3's own address**, confirmed by cross-referencing the `RTE_PRE` dump
above (`a2=002416 slot=3`). This is suggestive but not conclusive:
`$d6.w` pointing at slot 3 supports slot 3 being *a* consumer of `0x16`-
tagged nodes, but does not by itself prove slot 3's specific role (disk
service or otherwise) — that would require observing the `880a`/`8810`-
tagged node actually being processed, which did not happen live this
round.

**TASK 2 — the `89a2` wake event's semantic role, slot 0 traced fully:**

Continuing directly from 4.15's causal chain (`002b14`→ no match on any of
the three whitelisted values → falls through the `beq`/`bne`/`beq` chain
→ `4ef8 8bbc jmp $8bbc.w`, the **false-exit** target, confirmed by
4.15's own disassembly dump above showing `2b0e`-`2b14`: `cmpa.w
#$d10a,A2` / `beq 2b08` / `jmp $8bbc.w` — `8bbc` is reached specifically
because `A2` (`ff89a2`) matched none of `6b1c`/`6cf2`/`d10a`). `8bbc` was
not itself disassembled this round (out of scope of the live capture,
which showed `trap #4` firing directly next — 4.15 — meaning `8bbc`
itself must lead quickly to the free/yield sequence already captured).

- **D0/condition codes returned by `87f2`:** not distinguished live this
  round; the existing `ASR10_PTI_VECTOR_CALL` taps (4.9) log the call but
  not `87f2`'s own return value distinctly from the rest of slot 0's
  state. Not newly instrumented this round (out of the round's explicit
  scope, which centered on the producer search).
- **State changes caused by processing `89a2`:** exactly what 4.15 already
  showed, live: `002b1a`/`002b26` node-gate reads, `trap #4` (frees the
  node back to the `$b6c.w` pool, ×3 in the log — matching the three
  logged `ASR10_PTI_TRAP4` hits at `count=1,2,3`, i.e. the free actually
  happens three times in immediate succession, not once — a detail 4.15
  did not flag explicitly), then `trap #6`'s ready-flag cascade settling
  to "not ready."
- **Does `ae10` immediately poll for another node?** Not proven this
  round — no second node ever arrives at slot 0 in either capture, so
  this was not observed to be exercised.
- **Is the only effect of `89a2` making slot 0 runnable?** Consistent with
  everything observed: yes — no other lasting state change (beyond the
  three `trap #4` frees and the transient ready-flag flips already fully
  accounted for in 4.15's timeline) was found.
- **Exact condition that would keep slot 0 running or wake it again:**
  another `trap #9`/`#12`/`#13` post targeting slot 0's own address
  (`0x23d4`, or wherever `$c6.w` resolves to). None occurs in either
  capture.

**Classification: (A) — pure wake ping.** `0x89a2` carries no payload
that changes any observed state beyond triggering the classify-and-free
sequence; it does not route to a specific consumer (it explicitly fails
the routing check) and does not itself encode a state change beyond "a
node arrived, of an unrecognized type." Not (C) — it was never observed
to reach anything past `8bbc`'s false-exit path. Not (D) in the sense of
"unresolved" — the evidence is sufficient to call it (A), with (C)
explicitly excluded by the live-confirmed classify-and-reject path.

**TASK 3 — producers of node `+2=0x0016`:**

Three confirmed static producers, all via `trap #3` (alloc without the
count-limit check, 4.14), all `move.w #$16,($2,A5)` immediates (exhaustive
ROM-wide search for this exact encoding — no register-mediated or
table-driven construction found):

| PC | routine | alloc | `+4` | `+6` | destination | gate |
|---|---|---|---|---|---|---|
| `f88e2a` | `f88e04`-`f88e42` | `trap #3` | `D3` (1/2/3, caller-selected) | `0` (cleared) | `$d6.w` via `trap #9` | `cmpi.b #$1,$17e.w` |
| `f9068a` | `f90638`-`f906a6` | `trap #3` | `3` (fixed) | `1` (fixed) | not confirmed (falls to `jmp $9454.w` before any visible `trap #9`) | reached after `bset #$4,$8434.w`/`bset #$2,$8435.w` in the same routine |
| `f942f2` | `f942d2`-`f9430a` | `trap #3` | `3` (fixed) | `0` (fixed) | `$d6.w` via `trap #9` | reached immediately after **this same routine sets `bset #$4,$8435.w`** — i.e. this routine is a strong candidate for the actual calibration-completion setter itself, not just a consumer of it |

**`$d6.w` resolves live, once, at boot (`fb8ab6`, ~5.4s emulated), to
`0x2416` — slot 3's own address — and never changes again for the rest
of either capture.** None of the three producers executed in this round's
45-second autorespond-enabled capture (TASK 6). `+4`/`+6` fields (`3`/`0`
or `3`/`1`) match exactly the `f8f320` `cmpi.b #$1,($6,A5)` check in
TASK 1's consumer routine — direct static confirmation that these
producers and that consumer are the same family, with `+4` becoming the
`D2`/`+0x40`-adjusted field and `+6` selecting `880a` vs `8810`.

**Reachable during the tuning-to-LOAD transition?** Not proven live this
round — none fired in the one 45-second post-tuning capture taken. The
nearest dominating branch was not traced for `f9068a`/`f942f2` (their own
callers were not identified this round); `f88e2a`'s static gate,
`cmpi.b #$1,$17e.w`, was not separately live-tapped.

**TASK 4 — producers of `0x6b1c`/`0x6cf2`/`0xd10a`:**

**`0x6b1c` and `0x6cf2`: zero producers found**, via both a ROM-wide
`unidasm`-text search for the literal immediate and a raw byte-level
search of the full disk image (`floppies/asr10booth/V161.img`) for the
exact `move.w #$imm,($2,A5)` encoding (`3b7c <imm> 0002`). This is an
exhaustive negative for this specific encoding; a differently-encoded or
table-driven construction cannot be ruled out, but none was found.

**`0xd10a`: exactly one producer found**, via the disk-image byte search,
at RAM address `0x012f88` (verified against the already-established
`+0x2600` low-view delta — the surrounding code decodes cleanly with no
`ILLEGAL` opcodes, and correctly-computed relative branches, confirming
the delta and alignment):

```
0x12f6a  move.w #$a,$14d4.w
0x12f70  move.l #$cb7e,$14d6.w
0x12f78  jsr    $879e.w
0x12f7c  bra    $12f3a
0x12f7e  trap   #$3
0x12f80  move.l #$ffffd110,($4,A5)   ; a LONG value in +4, not a small int
0x12f88  move.w #$d10a,($2,A5)
0x12f8e  movea.w #$23d4,A1            ; HARDCODED immediate, not $d6.w/$d8.w
0x12f92  trap   #$9
```

- **Used as code pointer, identity, or table entry?** `+4` holds
  `0xffffd110` — a full long value structurally shaped like a pointer
  (one word above the node's own `+2=d10a` tag) — consistent with, but
  not proven to be, a callback address embedded alongside the identity
  tag. Not resolved further this round.
- **Destination:** hardcoded immediate `#$23d4` — **slot 0's own address**
  (matching the live-observed `$c6.w` value throughout this whole
  session) — a fixed constant, not a variable indirection like
  `$d6.w`/`$d8.w`. This is a different construction style than every
  other producer found this round.
- **Gate/state variables, relationship to `$0183`/`$031c`/`$031f`/
  `$ce00`/`$ce30`:** not established — this producer's own gating
  condition (what decides whether `0x12f7e`'s `trap #3` path is taken at
  all, versus the `bra $12f3a` skip) was not traced back far enough this
  round.
- **Depends on completion of a disk request?** Not established.
- **Reaches `f8948e`/`f89494`, `f894b4`, `$0544`/`$04fe`, or
  `PANEL_ENQUEUE`?** Not observed — this producer did not execute in
  either capture this round (TASK 6).

**TASK 5 — transition initiators, cross-referenced:**

The `$00ca` callback list (4.13/4.15): confirmed silent again this round
(`ASR10_PTI_CA_LIST_DECREMENT`/`CALLBACK` both `0`). `trap #8`
sleep/countdowns: three hits, all clustered at tuning completion (4.15),
targeting slots 0/4/5 — not observed to arm anything specific to the
`0x16`/`d10a` producers found this round. `a26e`/`6cf2`/`6d02` family:
unchanged from the pre-session Swedish-round static findings — `6cf2`
tail-jumps to `6f7c`, confirmed not reached live (4.11's `007000`
instrumentation, still zero this round via the same code path).
`$0183`/`$031c`/`$031f`/`$ce00`/`$ce30`: `$031c`/`$031f` appear in TASK
1's own routine (`f8f346`-`f8f354`), gating a *further* branch beyond the
`0x16` check — not reached this round since `f8f314`'s own `bne f8f346`
requires a `0x16` node that never arrived. No cross-reference to
`$ce00`/`$ce30`/`$0183` was found in either TASK 1's routine or the three
TASK 3 producers.

**TASK 6 — live producer confirmation (45+s, autorespond-enabled):**

```
ASR10_PTI_NODE16_PRODUCER (all 3 sites combined): 0
ASR10_PTI_D10A_PRODUCER:                          0
ASR10_PTI_D6_POINTER_WRITE: 2  (both at boot init, fb8ab6, values 2400 then 2416)
ASR10_PTI_D6_POINTER_READ:  2  (both at f87e6c/f87e74 -- a generic linear
                                 memory-verify/copy loop over 0x0000-0x160,
                                 not a semantic consumer of $d6.w's content
                                 -- a false-positive relative to this search)
```

**None of the four TASK 3/4 producers executed** in this capture. The
nearest dominating branch for each was **not** traced back to a specific
live value this round (their own callers were not identified) — reported
honestly as a gap rather than guessed.

**TASK 7 — final causal table:**

| producer PC | node `+2` | purpose candidate | target | gate | gate live value | executed? | consumer | browser relevance |
|---|---|---|---|---|---|---|---|---|
| `f8f2e6` | `0x89a2` | wake ping | slot 0 (`$d8.w`, live=`23d4`) | `btst #4,$8435.w` | **set** (live) | **yes** | `002b14`→`8bbc` (reject) | none — false exit |
| `f88e2a` | `0x0016` | disk/task-3 event (candidate) | `$d6.w`=slot 3 | `$17e.w==1` | not sampled | no | `f8f314` (TASK 1) | unproven |
| `f9068a` | `0x0016` | same family | not confirmed | `$8434`/`$8435` bits | not sampled | no | `f8f314` | unproven |
| `f942f2` | `0x0016` | same family; **also sets `$8435` bit 4 itself** | `$d6.w`=slot 3 | (see above) | not sampled | no | `f8f314` | unproven |
| `0x012f88` | `0xd10a` | UI-callback-identity candidate | slot 0 (hardcoded `23d4`) | untraced | untraced | no | `002b14` (would match, if reached) | unproven |

**First missing edge:** between **"calibration completion"** and
**"initial disk request."** Calibration completion (`$8435` bit 4) is
proven live to fire exactly once and to produce the `89a2` wake ping,
which is proven to be a dead end. **No live evidence this round shows
anything triggering the `0x16` producers, the `0xd10a` producer, or any
other transition-initiating code** — the chain breaks immediately after
the one proven event, before a disk request, disk-service task, or UI
callback of any kind is ever produced. This does not prove such code is
unreachable in general (see TASK 3/4/5's static-only gaps above) — only
that nothing in this specific 45-second, autorespond-enabled capture ever
triggers it.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. Nothing forced;
not committed. Earlier retractions (4.12's trap #13/#14 correction, the
4.15 `f880fc` gate correction) are unmodified by this section.

### 4.17 Correction applied: `012f66` and `012f7e` are separate branches; `$00CA` proven to link `$14C0` directly; the family never runs (not yet reviewed)

**Correction accepted and proven, not just applied:** the `012f00`
dispatcher family was fully disassembled (RAM `0x012c00`-`0x013200`, disk
offset `+0x2600`, zero `ILLEGAL` opcodes across the whole window,
confirming the delta and alignment). `012f7c`'s `bra $12f3a` is
**unconditional** and lands in a shared buffer-append continuation used
by *two other* cases (`D1=$f0`/`$f7`) — it never falls through to
`012f7e`. `012f7e` (the `trap #3`/`d10a` producer) has **zero** static
callers anywhere in the disassembled window or the rest of the ROM. The
two are proven-separate branches of the same family, exactly as this
round's correction stated.

**TASK 1 — complete control-flow graph, `012efc`-`012f94`:**

```
012efc  tst.b  $101a.w                          <- outer entry A
012f00  bne    12f12
012f02  st     $101a.w                           ; one-shot latch
012f06  movea.w #$14d5,A2 / move.w A2,$8cf6.w
012f0e  jmp    $8848.w                            ; tail-jump out (case 1)
012f12  rts
012f14  movea.w #$11c6,A2 / move.w A2,$8cf6.w    <- outer entry B (reached
012f1c  clr.b  $101a.w                             independently -- no
012f20  jmp    $8856.w                             static caller found for
                                                    this address either;
                                                    toggles the SAME latch
                                                    the other way)
012f24  trap   #3                                 <- family entry, buffer/
012f26  move.l #$ffffd12e,($4,A5)                    d10a case A
012f2e  bsr    $12f88                              ; SAME shared subroutine
012f32  move.b #$f0,D1                              as 012f7e's fall-through
012f34  bra    $12f3a
012f36  move.b #$f7,D1                             <- alternate byte case
012f3a  movea.l $100e.w,A0                          <- SHARED continuation
012f3e  move.b D1,(A0) / addq.l #2,A0 / ...           (buffer-append loop)
012f4a  cmpa.l $1012.w,A0
012f4e  bcs    12f6a                                ; room left: arm-timer tail
012f50  move.w #$23,$ce02.w / jsr 6d66 / jsr 87aa
012f5e  move.w #0,$14d4.w                          ; buffer full: disarm+return
012f64  movem.l (A7)+,D0-D3/A0-A2/A5 / rte
012f6a  move.w #$a,$14d4.w                         <- TASK 3 arm-timer tail
012f70  move.l #$cb7e,$14d6.w
012f78  jsr    $879e.w
012f7c  bra    $12f3a                               ; ALWAYS returns to the
                                                      SAME buffer loop -- NOT
                                                      to 12f7e, ever.
012f7e  trap   #3                                  <- family entry, d10a case B
012f80  move.l #$ffffd110,($4,A5)                    (independent; own
012f88  move.w #$d10a,($2,A5)   <- SHARED subroutine   allocation, own +4)
012f8e  movea.w #$23d4,A1
012f92  trap   #9
012f94  rts
```

**Selector controlling the branches:** `D1` (`$f0` vs `$f7`, set by
`012f24`'s two sibling entries, not shown as a single condition — each
is its own call site) picks the byte appended to the growing buffer at
`$100e.w`; the `012f4e bcs 12f6a` branch (buffer-not-full test against
`$1012.w`) is what selects the **arm-timer tail** vs the **buffer-full
disarm-and-return** tail. Neither of these is the same test that gates
entry into `012f7e` — no static edge connects them.

**Can `012f66` and `012f7e` occur in separate invocations of the same
state machine?** Yes, and *only* separately — no control-flow edge joins
them in either direction. `012f66`'s own tail always returns to `012f3a`
(the buffer loop), never to `012f7e`; `012f7e` has no static entry edge
from anywhere in this family (or the rest of the ROM) at all.

**Callers/computed dispatch tables/return paths:** `012f24`/`012f7e` both
begin with `trap #3` and both end (via the shared `012f88` tail, or the
`012f24`-`012f68` `rte` tail) in trap-style epilogues, but **no trap
vector points to either** (checked against the live-read full 16-trap +
7-autovector table, 4.14/4.16 — no match). `012efc`/`012f14` are also
unreferenced by any static `jsr`/`bsr` found. **All four entries into
this family are reached by some mechanism not identified this round** —
most plausibly an indirect call through a register or table (the `$8cf6.w`
function-pointer cell set by `012f06`/`012f14` is itself suggestive of an
indirect-dispatch convention used *by* this code, and may hint that this
code is likewise *reached* the same way from one level up — not
confirmed).

**TASK 2 — the `$00CA` list root, resolved: (A).**

```
ASR10_PTI_00CA_WORD value=14c0    (one-shot, gate-open moment)
ASR10_PTI_CA_ROOT_WRITE pc=fb8ab6 count=1 value=1400  time=5.394s
ASR10_PTI_CA_ROOT_WRITE pc=fb8ab6 count=2 value=14c0  time=5.394s
```

**`$00CA` is written, at boot (`fb8ab6`, a high-view init routine, ~5.4s
emulated), directly to `0x14c0`.** This is **(A)**, proven, not (B)/(C)/
(D) — `$14C0` is the list's first (and, per the static bound check
`$ca.w`-`$cc.w`, possibly only, given the stride) member, linked
directly, not indirectly. The earlier caution ("membership in the live
timer list is not yet proven") is now resolved: it is a member, confirmed
live.

A separate, generic boot-time sweep (`f87e60`-`f87eaa`, already partly
seen in 4.16) walks every record from `$ca.w` to `$cc.w` (stride `0x1a`)
clearing `+6`/`+0xc` (long) and `+0x10`/`+0x14` (word) fields — this is
what produces the one `$14d4=0000` write from `f87e9e` at `10.41s` (right
at tuning completion, but this is coincidental timing from a linear sweep
over the whole table, not something specific to `$14c0`). **`+0x14`
matches exactly the already-established `$14c0+0x14=$14d4` countdown
field** — direct, independent confirmation of the base+offset
relationship stated in this round's own established facts.

**TASK 3 — writes to `$14d4`/`$14d6`-`$14d9`, full trace:**

```
14d4  pc=f87dd6  value=0005  t=0.971s   (early generic init)
14d6  pc=f87dd6  value=0005  t=0.971s   (2 words, same init)
14d4  pc=fb8ab6  value=0005  t=5.643s   (a second init pass)
14d4  pc=fb8ab6  value=0000  t=5.643s
14d6  pc=fb8ab6  value=0005/0000 x2     t=5.643s
14d4  pc=f87e9e  value=0000  t=10.410s  (the table-wide sweep, TASK 2)
```

**No write ever comes from `012f66` itself** (`ASR10_PTI_12FAMILY_ENTRY`:
**zero** hits across all five tapped entry points, this entire 60-second
run). Every observed write is generic initialization (values `0/5`, never
the arm-specific `0x000a`/`0x0000cb7e` pair `012f66`/`012f70` would write).
**The arm path does not execute at all** in this capture; there is
nothing to attribute a "selector" to. `879e`'s effect on the object and
whether countdown `10` is decremented or `CB7E` invoked are therefore
**unobserved, not merely unresolved** — the precondition for any of them
never occurs.

**TASK 4 — the `012f7e` `d10a` producer, traced independently:**

```
ASR10_PTI_12FAMILY_ENTRY (12f24, 12f7e, or any of the 5): 0 total
ASR10_PTI_D10A_PRODUCER: 0
ASR10_PTI_12F24_XREF / ASR10_PTI_12F7E_XREF (vs. all 16 trap vectors): no match
```

**Zero execution, independently of the timer-arm path's own zero
execution.** Per this round's own caution, no dependency between the two
is asserted — both are simply unobserved, for what appear (per TASK 1) to
be entirely separate reasons: the timer-arm tail is gated by a buffer-
fullness check inside a family that itself never runs, while `012f7e` has
no found entry edge at all. **Nearest dominating selector:** not
identified this round for either — the true entry mechanism for the
whole `012efc`-`012f94` family remains unresolved (see TASK 1/6).

**TASK 5 — the `d10a` handler at `FFFFD10A`, and `CAFA`/`CB14`/`CB7E`:**

A one-shot, unconditional (no execution required) read of high-view
`0xffd0e0`-`0xffd160` was added and captured. `CB7E`/`CAFA`/`CB14` are
**low addresses** (`$cb7e`, `$cafa`, $cb14, all under `0x10000`, in the
same loaded-RAM chunk as `012f00`'s family), **not** inside the
`0xffd0e0`-`0xffd160` high-view window at all — they cannot be entries of
whatever table or code lives at `FFFFD10A` in the sense of sharing its
address space; if `FFFFD10A`'s code *transfers* to one of them, it would
do so via an explicit absolute jump/call, not by table position. This
round's one-shot high-view dump was captured but its content was not
yet correlated against `CAFA`/`CB14` (the dump exists in the raw log for
a follow-up pass; not analyzed further here to stay within this round's
time budget). **`A6`/`A3`/`D2`/`D3` inheritance from `f89170` into the
`d10a` branch was not traced this round** — `002b14`'s `d10a` branch
(`jsr (A2)` at `2b08`, per 4.16's disassembly) never executed in any
capture this round (`89a2` is the only node ever classified there, and it
matches none of the three), so there is no live register state to trace,
and the static path was not separately hand-traced this round given the
zero-execution finding took priority. **UI/browser names are not
assigned to `CAFA`/`CB14`/`CB7E`** — correctly withheld, per the
instruction, since the mapping was not resolved.

**TASK 6 — static dispatch-reference search:**

```
$ grep -n "12f24\|12f7e" <entire ROM disassembly, entire 012c00-013200 window>
(only the two definitions themselves; no jsr/bsr/bra target anywhere)
```

No direct calls/branches into `012f00`-`012f94` were found anywhere in
the boot ROM. No long-pointer (`0x00012fxx`) or word-offset construction
resolving into this range was found via the same immediate-search
technique used successfully for `0x16`/`0x89a2`/`0xd10a` in 4.16. No
jump table with targets in this range was found. `CB7E`/`D10A`/`D110`/
`14C0` as immediate values: `D10A`/`D110` already found (4.16, this
round's TASK 1 disassembly); `14C0` found only as the `$00CA` write
target (TASK 2) and as `012f8e`'s hardcoded `#$23d4` sibling pattern (not
`14C0` itself, a different constant); `CB7E` found only as `012f70`'s own
write target, no other reference. **This family's true entry mechanism
remains an open static question** — reported honestly rather than
guessed.

**TASK 7 — 60-second live timeline, autorespond enabled:**

```
t=0.971s   $14d4/$14d6 init (f87dd6) -- generic, pre-boot-logo
t=5.394s   $00CA := 0x14c0 (fb8ab6) -- LIST ROOT LINKED (TASK 2, proven)
t=5.643s   $14d4/$14d6 second init pass (fb8ab6)
t=10.409s  panel: "TUNING KBD - HANDS OFF" begins (established, 4.15/4.16)
t=10.410s  $14d4 cleared again by the generic table-wide sweep (f87e9e,
           TASK 2) -- coincidental timing, not a targeted arm
t=10.571s  "KEYBOARD TUNED" complete; $8435 bit4 set; 0x89a2 posted to
           slot 0 (established, 4.15); classified, rejected, freed
t=10.571s -> end of 60s capture: no $00ca-list decrement/callback, no
           012f00-family entry, no CB7E/CAFA/CB14 execution, no d10a
           node construction, no browser-consumer hit -- all zero
```

**Classification: "timer linked but not serviced" is the closest fit, with an
additional, independent gap above it.** `$14C0` **is** linked into the
`$00CA` list (contradicting a "never linked" classification) — but:
- the servicing loop that would walk the list and decrement `$14c0`'s own
  countdown (`f88338`-`f88360`) **never executes at all** in any capture
  across this whole investigation (its containing tick routine never
  runs — a pre-existing, repeatedly-confirmed finding, not new this
  round), so the link is inert;
- **independently**, the `012efc`-`012f94` dispatcher family that would
  *arm* `$14c0`'s countdown to a nonzero value in the first place also
  never runs, for a *different*, still-unidentified reason (its own entry
  mechanism was not found, TASK 1/6) — so even if the servicing loop did
  run, the countdown sits at its init value of `0`, which the servicing
  loop's own `beq`-skip (4.13) treats as "disabled," and would never fire
  a callback anyway.

Both gaps are upstream of `CB7E`/`d10a`/any browser edge — the smallest
missing edge remains, as in 4.16, **immediately after calibration
completion**: nothing in any capture this round or last triggers the
`012f00` family, the `d10a` producer, or the `$00ca` list's servicing
loop.

**Verification:** build clean (RAM window `012c00`-`013200` disassembled
with zero `ILLEGAL` opcodes across 1536 bytes, confirming delta/alignment
independently of any single instruction); Gate 1 exact (`GATE1_EXIT=0`,
flagless `grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean.
Autorespond was enabled for every live run this round. Nothing forced;
not committed. No earlier retraction was overwritten.

### 4.18 The `ae10`-`ae20` loop precisely: `trap #5` always yields the caller — the "test" and the "payload" are decoupled (not yet reviewed)

**Central, corrected finding:** `trap #5` (`f880b6`) unconditionally falls
through into `trap #6`'s body (`f880d6`+, 4.14), which *always* ends in
`bra $f87f80` — the unconditional scheduler-yield continuation. This
means **every call to `trap #5` suspends the calling task immediately**;
it is never a synchronous "set a flag and return" operation from the
caller's point of view. Slot 0's own `ae10` loop therefore does not
execute `ae12`→`ae14`→`ae18` as one atomic sequence — it suspends at
`ae12` and later **resumes at `ae14`** (the instruction after `trap #5`),
via the scheduler's ordinary dispatch mechanism, in a later, unrelated
quantum. Live-proven below, not asserted.

**TASK 1/2 — first and second iteration, exact:**

```
seq  tap                pc     d0        a5_before/after  slot+0(delay) slot+10(queue-head) time
1-3  AE12_BEFORE(x3*)   ae12   00000000   000000           03e8          0000                10.5634s
[8ms gap -- slot 0 suspended inside trap #5/#6; slots 1 and 3 run
 (RTE_PRE rte_count=2,3); the calibration routine (f8f2e6-f8f2f0) posts
 node 14f4/89a2 to slot 0's mailbox +0xc via trap #9]
—    RTE_PRE            f87fc0 (n/a)     —                —             —                    resumes AT ae14, NOT ae10
1-2  AE1A_TAKEN(x2*)    ae1a   00000080   0014f4           03e8          0000 (post-pop)      10.5713s
     node+2=89a2 (the already-known node)
—    [trap #4 x3, 002b14 classify -- unchanged from 4.15/4.16]
4-6  AE12_BEFORE(x3*)   ae12   00000000   0014f4           03e8          0000                 10.5713s
[no further AE1A/AE20 hit for the rest of the 60s capture -- slot 0
 suspends again inside this SECOND trap #5 call and is never redispatched]

(*) each group is 3 (or 2) log lines ~250ns apart for what is structurally
one logical event -- a known MAME core artifact (established earlier this
investigation) where a single PC-gated write/read fires the tap more than
once; not three/two genuine separate iterations.
```

**Exact reason bit 7 of D0 differs between "iterations":** it does
**not** come from the *same* `trap #5` call's own dequeue logic at all.
`trap #5`/`#6`'s `f880e0: move.w ($10,A2),D0` executes **before** the
suspend, using slot 0's own `+0x10` field — captured live at **`0000`
(empty)** both immediately before (`ae12_before`) and immediately after
(`ae1a_after_trap5_6`) this exact sequence. Since `D0` starts at exactly
`0` (`moveq #0,D0` at `ae10`) and the empty-queue branch (`f880e4: beq
f880fc`) skips the dequeue entirely, `D0` **should** remain `0` through
to `ae14` for *this* call — consistent with the "before" snapshot. But
because the resume happens in a **later, separate quantum** (after the
suspend), `D0` at the actual `ae14` resume is **not preserved** across
that boundary (established mechanism, 4.14/4.18) — it is whatever value
`D0` happened to hold from *other, unrelated* code that ran during the
8ms gap (slots 1/3, the calibration routine). The captured value,
`0x00000080` exactly (upper three bytes already zero, low byte `0x80`),
is consistent with leftover state from that intervening execution, not
with anything shaped like a node address (`0x14f4`, `0x23d4`, etc., all
seen elsewhere in this investigation with non-zero high bytes). **`A5`,
by contrast, is set correctly and meaningfully** — via the standard,
separate dispatch-time mailbox reload (`f87fb4: movea.w ($c,A2),A5`,
4.14) that runs for *every* dispatch into *any* slot, independent of
`D0`'s fate.

**Why the first evaluation calls `87f2`:** `D0`'s leftover value (`0x80`)
happens to have bit 7 set, so `and.b #$80,D0` is non-zero and the `beq`
is not taken — a **coincidence of inherited register state**, not a
deliberate "node is present" signal. **Classification: (E) — unresolved
by design**, more precisely: **the test is decoupled from the payload.**
It is not (A) (a node *is* available — `A5=0014f4` — so "no node"
would be the wrong read), not (B) (there is no live "status bit 7 of a
node" being tested — `D0` here is a leftover scalar, not a node
identity), not (C) (no timeout/threshold field was involved in this
specific branch), not (D) in the sense of "another trap #6 status" being
deliberately checked — the honest characterization is that this
particular check's outcome, in this capture, was accidental.

**TASK 3 — `ae20` RTS: never reached, this capture (a valid negative).**
Because `trap #5` always suspends, `ae14`/`ae18`'s evaluation never
happens as part of one continuous, synchronous call — and in the one
resume observed, the `beq` was *not* taken (bit 7 was set), so `ae20` was
never approached. **`ASR10_PTI_AE20_FULL`: 0 hits, entire 60-second
capture.** No post-RTS continuation, no original JSR/BSR resolution, and
no "true continuation" trace are possible from this capture — reported
as a gap, not fabricated. The second `trap #5` call (`seq=4-6`) *also*
suspends (no further hits at all afterward), meaning slot 0 is left
suspended inside `trap #5`/`#6` for the remaining ~49 seconds of the
capture, exactly mirroring 4.15's original finding that slot 0 never
runs again after this point — now additionally explained mechanically
(it suspends *inside the trap*, not after a clean loop exit).

**TASK 4 — system-wide snapshot, reconstructed from the pre-existing
dispatcher instrumentation already active in this same capture (this
round's own `AE20`-triggered snapshot did not fire, since `ae20` never
executed):**

| slot | addr | +00 | +06 (saved PC) | +0a (SR) | +0c (mailbox) | +0e (USP) | +10 (head) | +12 (tail) | +14 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | `0023d4` | `03e8` | `00ae14` | `0004` | `0000` | `1e20` | `0000` | `0000` | `0000` |
| 1 | `0023ea` | `0000` | `ffffc85a` | `0000` | `0000` | `1e88` | `0000` | `0000` | `0000` |
| 3 | `002416` | `0000` | `ffff90f4` | `0000` | `0000` | `1fb4` | `0000` | `0000` | `0000` |
| 4 | `00242c` | `0000` | `000068a8` | `0000` | `0000` | `1fdc` | `0000` | `0000` | `0000` |
| 5 | `002442` | `0000` | `0000779c` | `0000` | `0000` | `2054` | `0000` | `0000` | `0000` |

`$b6c.w` (free-list root) `=0x14f4` throughout (the node keeps getting
freed and immediately re-something — consistent with 4.15's ×3 `trap #4`
frees); `$b7f.w` (alloc count) `=0x0000`/`0x0001` (transient). **This
distinguishes the two states directly: slot 0 has real, specific saved
state (a delay value `03e8`, a real resume PC) — it is not "empty" in
the sense of never having run — but its own queue (`+0x10`/`+0x12`) and
mailbox (`+0xc`) are genuinely empty at every sampled moment. Slots 1/3/
4/5 are uniformly blank (`+00`/`+0c`/`+10`/`+12`/`+14` all zero) —
consistent with "system globally idle" rather than "slot 0 idle while
real work exists elsewhere."** `$14c0`'s own `+0x14`/`+0x16` and `$00CA`
were already established this round (4.17): `$00CA=0x14c0`, `+0x14=0`
(disarmed).

**TASK 5 — mechanisms that can wake/re-enter slot 0:**

Statically/live, confirmed this round and last: **`trap #9`** (posts an
explicit node to an explicit `A1`; when `A1=0x23d4`, delivers directly to
slot 0's mailbox `+0xc` on the "gate already set, mailbox empty" path,
4.14/4.15 — this is the *only* mechanism observed live to have actually
reached slot 0 in any capture); **any code writing slot 0's own
`+2`/`+3`** directly (the generic `ASR10_PTI_SLOT0_READY_BIT_WRITE` net,
4.15-4.17, remains the broadest catch-all — no new writer beyond the
already-known `trap #5/#6/#9` family was found this round); a **second
call to `trap #5` from within slot 0's own resumed code** (would
re-arm/re-test, but slot 0 never gets that far again). **Armed at loop
exit:** none of `trap #9`'s known callers (the `89a2` producer at
`f8f2e6`, already fired and is one-shot per `bclr #4,$8435.w`; the three
`0x16` producers and the `0xd10a` producer, 4.16/4.17, none of which
executed) remain live for a *second* firing in this capture. **Executed
in the subsequent 60 seconds:** none.

**TASK 6 — `trap #12` (`f88174`), complete, and its relation to `$14c0`:**

Full body (4.11/4.14, unchanged, reconfirmed against this round's
questions): uses queue-header offsets `+6`(head)/`+8`(tail)/`+0xa`
(limit)/`+0xc`(count)/`+0xe`(aux)/`+0x10`(enable gate)/`+0x12`(aux2/
callback-pointer-adjacent) on its **explicit `A1` argument** — **never**
`+0x14`/`+0x16` (the timer-record countdown/callback offsets). It *can*
post to a scheduler slot (if the caller's `A1` happens to equal a slot's
own address — not proven for any specific caller this round); on the
"gate disabled" cold-start path it *can* activate a callback (`movea.l
(A1),A0 / jsr (A0)`, using `A1+0` as a function pointer — a *different*
field position than `$14c0`'s own `+0x16` callback); on the overflow
path it *can* invoke `f87f3e` (`bsr` at `f881d0`, 4.14). **It does not
manipulate `+0x14`/`+0x16` at all, on any branch** — no address or data
flow connects `trap #12` to the `$14c0` timer-shaped record. Confirmed,
not merely asserted: the offset overlap between trap #12's own fields
and `$14c0`'s fields is coincidental (both structures reuse small offsets
like `+0xe`/`+0x10`/`+0x12` for *different* purposes), and this round's
static re-check found no case where `trap #12` is called with
`A1=$14c0`.

**Revised smallest missing edge:** unchanged in *location* from 4.16/4.17
(still immediately after calibration completion), but now understood
more precisely: slot 0's own `ae10`-`ae20` "gate" is not a meaningful
decision point in this boot path at all — it suspends unconditionally on
every `trap #5` call, and whether it "exits" via `ae20` or "continues" via
`ae1a` is governed by **coincidental leftover register state**, not by
the queue/node content the loop appears to be checking. The actual,
meaningful decision (whether the posted node routes anywhere useful) has
already been fully traced (4.15/4.16): it happens entirely downstream, at
`002b14`'s three-way classify, and fails there. This round narrows the
`ae10` loop from "an unresolved gate" to "a mechanism whose live outcome
this round was accidental" — it is not part of the causal chain to the
browser/LOAD path either way.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. Autorespond
enabled for every live run. Nothing forced; not committed. No earlier
retraction overwritten.

### 4.19 Milestone: the complete verified baseline reaches a new firmware error — `NO INST OR BANK FILES` → `EFFECT DOWNLOAD FAILED` → `ERROR 032`, ERROR 130 confirmed gone (not yet reviewed)

**This section supersedes 4.15/4.16/4.17/4.18's read of "the blocker" as far
as the *specific* stall point goes.** Those sections' node/scheduler/timer
findings (`89a2`'s wake-and-reject cycle, the `012f00` family, `$00CA`→
`$14C0` linkage, the `ae10` loop's decoupled test) are **not retracted** —
they remain accurate descriptions of mechanisms that do run — but they
were all captured under an **incomplete flag baseline** (missing
`ASR10_EXPERIMENT_DUART_COUNTER_TIMER`, `ASR10_EXPERIMENT_ES5506_HOST`,
and — critically — `ASR10_EXPERIMENT_PAR_DIAGNOSTIC` alongside
`ASR10_DIAG_PAR_VALUE`). Under the complete baseline, the boot **does not
stall silently** — it runs much further and reaches a new, later, explicit
firmware error.

**Mandatory baseline, verified from `ASR10_RUN_CONFIG_HEADER` (every flag
below shown `effective=1` in the actual captured log, not assumed):**

```
ASR10_DIAG_PANEL_AUTORESPOND=1
ASR10_EXPERIMENT_DUART_COUNTER_TIMER=1
ASR10_EXPERIMENT_ES5506_HOST=1
ASR10_DIAG_PAR_VALUE=0x200
ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1   <- REQUIRED; without it, ASR10_DIAG_PAR_VALUE
                                        is inert (read_port_cb stays unbound) and
                                        the run falls back to the ERROR 130 path.
ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE=1
ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE=1
ASR10_EXPERIMENT_TUNING_STALL_TRACE=1
ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE=1
```

**Full panel timeline (180s capture, `V161.img`):**

```
10.41s   boot logo / LOADING SYSTEM
10.53s   TUNING KBD - HANDS OFF
11.65s   KEYBOARD TUNED
11.66s   NO INST OR BANK FILES          <- first live browser/directory output
                                            in this entire investigation
12.04s   EFFECT DOWNLOAD FAILED
14.13s   ERROR 032 - REBOOT ?           <- final stable state, holds for the
                                            remaining ~166s of the 180s capture
```

- `ASR10_ES5506_HOST event=par_diag_read source=diagnostic_constant
  value=200` — **PAR reads confirmed live at `0x200`** on every one of
  1836 PAR-register accesses.
- **ERROR 130: zero occurrences** in the full 180s, 14.8M-line capture —
  confirmed absent, not merely "not yet seen."
- **Live browser/directory scan:** 37 `ASR10_FSB_ENTRY` events
  (`fdc_command_issue` ×7, `fb8c6e_seek_wrapper` ×20,
  `fb895a_range_reader` ×10) — genuine, not synthesized.
- **Live `8810`-tagged node processing:** `AE1A_TAKEN` captures
  `node+2=8810` ×4 (producer not yet identified — the three
  previously-known static `0x16` producers, 4.16/4.17, still show zero
  hits; some other producer creates these live) and `node+2=89a2` ×2 with
  6 `trap #9` posts total (up from 1 under the incomplete baseline).
- `ASR10_ERROR_CONTEXT source=panel_error032_text ... error_number=20
  (0x20=32) ... last_ST1_end_of_cylinder=1` — the error transition
  carries genuine FDC status fields (analyzed in 4.20 below, where this
  framing is itself corrected).

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. Nothing forced;
not committed.

### 4.20 The FDC transaction record, examined precisely: ST1.EOC is universal and harmless; the real failure has no FDC command at all (not yet reviewed)

**Correction to this round's own starting premise:** the "Established"
framing going into this round ("the current blocker is a genuine FDC
result with ST1 End-of-Cylinder set") does **not** survive contact with
the actual transaction log. The evidence below shows EOC is present on
**every single** floppy read in the capture — including all the ones that
demonstrably succeeded — and that **zero FDC commands occur anywhere near
the `EFFECT DOWNLOAD FAILED`/`ERROR 032` transition**. This is reported as
a correction, not a new unproven theory: it is read directly off the
pre-existing `ASR10_FDC_CMD46` transaction-summary instrumentation
already built into this driver, which logs the complete command packet,
result packet, and every status bit per transaction.

**TASK 2/3 — the complete FDC operation timeline, all 20 transactions
(all `command=0x46`, READ DATA; two AUX-register writes, `0x88`
data-rate-select and `0xF3` precompensation, are configuration, not
reads):**

| txn | C,H,R,N,EOT | bytes | ST0 | ST1 | ST2 | tc_asserted |
|---|---|---|---|---|---|---|
| 1 | 00,00,01,02,01 | 512 | 40 | 80 | 00 | 0 |
| 2 | 00,00,02,02,02 | 512 | 40 | 80 | 00 | 0 |
| 3 | 00,00,03,02,03 | 512 | 40 | 80 | 00 | 0 |
| 4 | 00,00,04,02,05 | 1024 | 40 | 80 | 00 | 0 |
| 5 | 00,00,06,02,06 | 512 | 40 | 80 | 00 | 0 |
| 6 | 00,01,05,02,06 | 1024 | 44 | 80 | 00 | 0 |
| 7 | 00,01,07,02,14 | 7168 | 44 | 80 | 00 | 0 |
| 8 | 01,00,01,02,14 | 10240 | 40 | 80 | 00 | 0 |
| 9 | 01,01,01,02,14 | 10240 | 44 | 80 | 00 | 0 |
| 10 | 02,00,01,02,03 | 1536 | 40 | 80 | 00 | 0 |
| 11 | 02,00,04,02,14 | 8704 | 40 | 80 | 00 | 0 |
| 12 | 02,01,01,02,14 | 10240 | 44 | 80 | 00 | 0 |
| 13 | 03,00,01,02,14 | 10240 | 40 | 80 | 00 | 0 |
| 14 | 03,01,01,02,07 | 3584 | 44 | 80 | 00 | 0 |
| 15 | 00,00,07,02,07 | 512 | 40 | 80 | 00 | 0 |
| 16 | 03,01,08,02,14 | 6656 | 44 | 80 | 00 | 0 |
| 17 | 04,00,01,02,14 | 10240 | 40 | 80 | 00 | 0 |
| 18 | 04,01,01,02,10 | 8192 | 44 | 80 | 00 | 0 |
| 19 | 04,01,11,02,11 | 512 | 44 | 80 | 00 | 0 |
| 20 | 00,00,06,02,06 | 512 | 40 | 80 | 00 | 0 |

**Every transaction, without exception, ends `ST0=40`/`44` (abnormal
termination, head-select reflected), `ST1=80` (bit 7, End-of-Cylinder,
set — no other ST1/ST2 bit ever sets), `tc_asserted=0`.** All 20
completed by (line-order confirmed) roughly line 1,044,313 of the
capture — and the **first** panel timeline entry (`LOADING SYSTEM`) does
not appear until line 1,044,523, ten lines later. **All 20 CMD46
transactions are the early OS-loader read phase — they finish before the
boot logo even displays**, and no further FDC command of any kind occurs
for the rest of the 180-second capture (confirmed by grep: zero
`ASR10_FDC_CMD` lines after line ~1.04M in a 14.8M-line log).

- **Which operation produces `NO INST OR BANK FILES`:** none of these 20
  directly — the message is produced by the browser code (37
  `ASR10_FSB_ENTRY` events, 4.19) working over data these reads already
  delivered. The reads themselves all completed with the *same*
  ST0/ST1/ST2 pattern as every other read in the set, indistinguishable
  from "success."
- **Which operation produces `EFFECT DOWNLOAD FAILED`/`ERROR 032`:**
  **none** — there is no 21st FDC command. The failure occurs with zero
  new disk activity, roughly 1.5-3 seconds of *emulated* time after the
  last actual read, using only data/state already resident from the
  OS-load phase.
- **Classification: (C) — unrelated to any specific FDC operation.**
  Not (A): the directory/browser scan is not what fails — 4.19 already
  shows it completing and rendering a coherent (if disappointing) result.
  Not (B) in the literal sense of "a failing download *read*" — there is
  no distinguishable download-read command in the log at all; whatever
  "EFFECT DOWNLOAD" means to the firmware, it is decided here **without**
  issuing a new FDC transaction.

**TASK 3 — control-group comparison:** every one of the 20 transactions
*is* already "the successful OS-loader read group" (4.19 places all of
them before the boot logo) — there is no separate, later "failing read"
to contrast them against. The closest thing to a parameter difference
across the set is single-sector (`R==EOT`, e.g. txn 1/2/3/5/15/19/20)
vs. multi-sector (`R<EOT`, e.g. txn 8/9/11/12/13/17, up to 10240 bytes);
**both classes show identical ST0/ST1/ST2/tc_asserted** — sector count is
not what produces EOC, since EOC is present unconditionally.

**TASK 4 — the "failing" transaction, precisely:** there isn't one to
capture beyond what the table above already shows in full (command
bytes, result bytes, every status bit, per-transaction). **EOC is the
only status bit ever set, on every transaction, successful or not** —
confirming per-transaction that it is not "accompanied by" anything else
diagnostic, because it is not itself diagnostic of failure here.

**TASK 5 — terminal-count causality, resolved: (D).** "The command
legitimately requests through EOT and EOC is unrelated" — directly
proven, not inferred, by the uniform pattern across all 20 transactions
(11 of which are multi-sector and unambiguously succeeded, since the
browser produced a coherent result from their data).

`ASR10_EXPERIMENT_FDC_SYNTH_TC`, read from source before use, per
instruction:

- **Trigger:** only inside an active `CMD46` (READ DATA) transaction,
  once per transaction, after the full 9-byte command packet has been
  written.
- **Transfer length assumed:** derived from the command's own `N` byte
  alone (`128 << N`) — i.e. **exactly one sector**, regardless of the
  command's actual `R..EOT` range.
- **Timing:** fires the instant the FIFO byte-count within the *current*
  transaction reaches that one-sector threshold — effectively immediate,
  not separately scheduled.
- **Pulses or holds:** **pulses** (`tc_w(false)` then `tc_w(true)`, a
  transient edge), not held.
- **Which operation classes it affects:** `CMD46` only, and only
  correctly for single-sector reads (7 of the 20 transactions). For the
  13 multi-sector transactions, this would force TC after only the
  *first* sector of a request for up to 10240 bytes — a genuine mismatch
  with the command's own `EOT`.

**TASK 6 — controlled experiment, run and labeled as a causal test, not a
fix:** same complete baseline, `ASR10_EXPERIMENT_FDC_SYNTH_TC=1` added.
Header confirmed `effective=1`. **Result: the boot regresses, badly.**
324 synthetic TC pulses fire (`transferred_bytes=512` each, confirming
the one-sector-only behavior predicted above), and the panel **never
reaches `LOADING SYSTEM`** — it loops `"PLEASE INSERT DISK"` for the
entire 180-second capture, a *much earlier*, worse stall than either the
old ERROR 130 baseline or the complete baseline's ERROR 032. **The exact
changed hardware edge:** every multi-sector `CMD46` read (13 of the 20
OS-loader transactions) now gets its data phase truncated to the first
512 bytes by a premature TC pulse, instead of running the full
`R..EOT` range the firmware's own command packet requested — corrupting
the OS-load phase itself before the disk is even recognized as valid.
**ST1.EOC does not "disappear" in any useful sense** (the transactions
that would have shown it never complete far enough to be compared
against the working baseline) and `EFFECT DOWNLOAD FAILED`/`ERROR 032`
are not reached at all — there is no later panel text to compare, only
the earlier prompt-loop. This is not "it progressed"; it is a
demonstrably worse, earlier failure, with a precisely identified cause.

**TASK 7 — disk geometry and image contents:** confirmed from the
transaction log: `density=hd`, `data_rate=500000` (500kbps MFM),
`decoded_sector_size=512` (`N=02` on every command), `drive_sides=2`,
cylinders `0`-`4` observed in this read set (the OS-loader phase does not
need to range further). `format=none` in every log line — MAME's
concrete track/sector-count geometry object was not resolved through this
logging path this round (a gap, not a finding). **Whether `V161.img`
contains instrument/bank files:** the browser's own rendered result,
`"NO INST OR BANK FILES"`, together with zero FDC activity for the
`EFFECT DOWNLOAD` decision, is strong, direct evidence that **this image
is OS/boot content only** — no instrument, bank, or effect-download data
was found or read. Kept separate from the FDC error analysis, per
instruction: this is a content/image-contents conclusion, not a
hardware-fault one.

**REPORT, in the order requested:**

1. **Last fully successful disk operation:** `CMD46` txn 20 (or,
   ambiguously, any of the 20 — all are equally "successful" by every
   observable status bit).
2. **First failing disk operation:** **there is none** — no FDC command
   follows the OS-loader phase before the error.
3. **Directory scanning completed normally:** **yes**, confirmed by 37
   live `ASR10_FSB_ENTRY` events and a coherent rendered result.
4. **Does the failure belong to effect download:** it belongs to
   whatever decision process produces `"EFFECT DOWNLOAD FAILED"`, but
   that process issues **no FDC command** — so in the FDC-transaction
   sense, no, it does not "belong" to a download read; it is a
   downstream, in-memory/logical consequence, most plausibly of the
   already-established "no matching files" result.
5. **Full failing command/result packet:** none exists; the fullest
   detail available is the complete 20-transaction table above.
6. **Natural TC source/missing edge:** no natural TC is ever asserted
   (`tc_asserted=0` throughout) and none is needed — classification (D),
   proven.
7. **Controlled synthetic-TC outcome:** strictly worse — regresses to an
   infinite `"PLEASE INSERT DISK"` loop, caused by truncating every
   multi-sector OS-loader read to one sector.
8. **Smallest implementation defect remaining:** unchanged from 4.16's
   framing, now sharpened — the boot reaches a real, content-driven
   dead end (`"NO INST OR BANK FILES"` → `"EFFECT DOWNLOAD FAILED"` →
   `"ERROR 032"`) with the FDC fully exonerated; the open question is
   what in-memory decision produces `EFFECT DOWNLOAD FAILED` with no
   further disk read, and whether a disk image containing actual
   instrument/bank/effect files would avoid it — not an FDC/TC defect at
   all.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. Both captures
(natural baseline and the synthetic-TC experiment) are preserved as raw
logs. Nothing forced; not committed.

### 4.21 The exact `EFFECT DOWNLOAD FAILED`/`ERROR 032` producer, and a strong ES5510 host-interface fingerprint at `FC3000` (superseded in part by 4.22 — corrections below, original text preserved as historical audit material)

**SUPERSEDING CORRECTIONS (added after 4.22's deeper trace; the section
below is kept verbatim as historical audit material, not rewritten):**

- **`ffc896` is not the first/original failure predicate.** It is a
  **post-failure retry-policy decision**: `ffc88e jsr f973f0` /
  `ffc894 bcc success` means `f973f0` (or a callee) has **already**
  returned carry set by the time `ffc896` runs. `ffc896`
  (`cmpi.l #$fff9bca0,$e8e.w`) only decides *whether to retry the same
  already-failed transfer*, based on source-pointer identity — it does
  not decide pass/fail itself. See 4.22 for the actual failing predicate.
- **`fff9bca0` is now confirmed, live, to be a valid built-in object**
  (4.22), consistent with a boot-ROM-resident effect record table — but
  its backing memory type in the *current address map* should be read as
  "consistent with ROM/high-view residency," not restated here as
  settled without that address-map confirmation (see 4.22 for what was
  actually checked).
- **`f884bc`/`f884b6` are described below as "producers"** without full
  surrounding control-flow decode of the MC68302 dispatcher body between
  them and `f89c48`. Treat this as "immediate message-selection call
  sites," not a fully-proven higher-level control-flow chain.
- **The `FC2D40`-`FC2D7C` negative ("zero accesses") applies only to the
  captured download path in that specific run** — it is not a global
  statement about every possible code path in the firmware.

**Original section 4.21 text follows, unmodified:**

**Scope note (per instruction):** `V161.img` is **not** independently
proven OS-only in this section — its directory contents were not decoded
separately. 4.19/4.20's "no instrument/bank/effect data found" reading
stands as the browser's own rendered conclusion, not a directory-level
proof, and is not restated as settled fact here.

**Baseline:** the complete nine-flag set (4.19) plus
`ASR10_EXPERIMENT_DOWNLOAD_TRACE=1`, all ten flags confirmed `effective=1`
in `ASR10_RUN_CONFIG_HEADER`. Panel timeline unchanged from 4.19/4.20.

**TASK 1 — exact producers, traced past the generic character writer:**

```
ASR10_ERROR_CONTEXT source=error_number_write_00c0
  pc=f88284 previous_pc=ffc8a4 d0=00000020 ...
ASR10_ERROR_CONTEXT source=panel_effect_download_failed_text
  pc=f89c48 previous_pc=f884bc ...
ASR10_ERROR_CONTEXT source=panel_error032_text
  pc=f89c48 previous_pc=f884b6 ...
```

- **`EFFECT DOWNLOAD FAILED`'s higher-level producer:** a message-selector
  reached from `previous_pc=f884bc` — immediately adjacent to `f884be`,
  already established (4.13/4.14) as the entry of the MC68302
  hardware-status-byte IRQ dispatcher. `ERROR 032`'s own text is selected
  from `f884b6`, a few bytes earlier in the *same* dispatcher. Both route
  through the shared low-level panel-character writer `f89c48`.
- **The error number (`0x20`=32) itself is written into lowmem `$00c0`
  at `f88284`**, called from **`previous_pc=ffc8a4`** — inside the exact
  retry routine disassembled below.
- **Error-number source: immediate**, not a table or inherited state —
  traced to one specific `move.b #$20,D0` instruction (below).

**TASK 2/3 — the retry routine, disassembled in full (`ffc840`-`ffc8c0`,
captured via a new one-shot high-view code dump gated on
`ASR10_EXPERIMENT_DOWNLOAD_TRACE`, added this round):**

```
ffc884  movea.l $e8e.w,A0        ; A0 := source pointer (set once, at ffc850,
                                 ; from the incoming node's own +4 field)
ffc888  movea.w ($26,A0),A3      ; A3 := length/offset field from that source
ffc88c  adda.l  A0,A3            ; A3 := end address
ffc88e  jsr     $fff973f0.l      ; *** the actual transfer/verify call ***
ffc894  bcc     $ffc8d6          ; success: branch away (not in this window)
ffc896  cmpi.l  #$fff9bca0,$e8e.w ; *** THE FAILING COMPARE ***
ffc89e  bne     $ffc8ac          ; source != the one known-retriable sentinel:
                                 ; skip the retry entirely
ffc8a0  subq.b  #1,$e9d.w        ; source == sentinel: consume one retry
ffc8a4  bpl     $ffc884          ; retries remain: loop back and retry
ffc8a6  move.b  #$20,D0          ; retries exhausted: D0 := 32 (decimal)
ffc8aa  trap    #$0              ; hard trap -- $00c0 gets 0x20 downstream
ffc8ac  trap    #$3              ; (non-retry path) allocate a node
ffc8ae  move.w  #$87f6,($2,A5)   ; tag it +2 := 0x87f6 (new identity, not
                                 ; yet cross-referenced against 002b14)
ffc8b4  move.l  #$fff840b6,($4,A5) ; +4 := another pointer-shaped constant
ffc8bc  movea.w #$23d4,A1        ; hardcoded slot-0 address (same pattern
                                 ; as the d10a producer, 4.16)
ffc8c0  trap    #$9              ; post to slot 0
```

**This is the load-bearing result.** The compare at `ffc896` is
`cmpi.l #$fff9bca0,$e8e.w` — comparing the transfer's own source pointer
(set once, before the loop, from the caller's node) against a single
fixed sentinel address, `0xfff9bca0`. It is **not** a hardware
status/ready compare — it is a "is this the one source I know how to
retry" gate. Both branches from it are fully traced: the non-matching
path skips straight to the `trap #3`/`0x87f6`-node/`trap #9` sequence
(most plausibly *this* is what produces `EFFECT DOWNLOAD FAILED`'s node,
though `0x87f6` was not cross-referenced against `002b14`'s three-way
check this round — a remaining gap); the matching path retries until
`$e9d.w` (the same retry counter the pre-existing `ASR10_TASK3_
DOWNLOAD_TRACE` instrumentation already tracks as
`retry_counter_0e9c_0e9d`) goes negative, at which point `D0 := 0x20`
and `trap #0` fires — a hard trap, separate from the graceful
node-posting path, and the direct, exhaustively-traced origin of the
literal `32` in `ERROR 032`.

**Correction to my own initial read:** `ffc840`'s and `ffc876`'s
`jsr $a268.w` do **not** target boot-ROM `f8a268` — `0xa268` has bit 15
set, so per the already-established absolute-short sign-extension rule
this targets high-view `$ffa268`, not examined this round (flagged, not
guessed at).

**TASK 4 — effect-data source, classified (B): loaded overlay/OS RAM.**
The transfer's source pointer (`$e8e.w`) is set once, from the incoming
node's `+4` field, and the only sentinel the retry logic recognizes is
`0xfff9bca0` — a high-view (`f9xxxx`) address, in the same "loaded
overlay" address space as the transfer routine itself (`f973f0`,
independently confirmed live in the original `ASR10_TASK3_DOWNLOAD_TRACE`
sample as a real executed PC). This is **not** one of the floppy-read
destination addresses from the 20 OS-loader transactions (4.20) — no
overlap is evidenced; the two are different address spaces (fixed
high-view constant vs. FDC DMA/PIO buffer). First/last bytes, exact
length, and the routine that originally populated `0xfff9bca0` were not
traced this round.

**TASK 5 — ES5510 source read (`es5510.h`/`es5510.cpp`), fingerprint
comparison:**

The device exposes an 8-bit host interface, `host_r(space, offset)`/
`host_w(offset, data)`, with this register map (source-level, verbatim):

| offset | register |
|---|---|
| 0x00-0x02 | GPR latch bytes [2][1][0] |
| 0x03-0x08 | INSTR latch bytes [5]..[0] |
| 0x09-0x0b | DIL latch (read-only) |
| 0x0c-0x0e | DOL latch |
| 0x0f-0x11 | DADR latch |
| 0x12 | Host Control — **`host_r` case 0x12 is hardcoded `return 0;`** |
| 0x14 | RAM select (I/O vs. delay-line) |
| 0x16 | Program Counter (read, test-only, hardcoded `0x27`) |
| 0x18 | Host Serial Control |
| 0x1f | Halt Enable (write) / Frame Counter (read) |
| 0x80/0xa0/0xc0 | GPR+INSTR select/write-select registers |

**Observed ASR-10 bus addresses touched at `FC3000`-`FC303E`** (via the
existing, previously-rate-limited `ASR10_CLUSTER_TRACE
event=fc3000_*` taps, already in the driver): `fc3000, fc3002, fc3004,
fc3006, fc3008, fc300a, fc300c, fc300e, fc3010, fc3024, fc3030, fc303e` —
**every one of these matches `offset = (address - 0xFC3000) / 2` against
the table above exactly**: `fc3000`→GPR[2], `fc3002`→GPR[1],
`fc3004`→GPR[0], `fc3006`→INSTR[5], `fc3008`→INSTR[4], `fc300a`→INSTR[3],
`fc300c`→INSTR[2], `fc300e`→INSTR[1], `fc3010`→INSTR[0],
**`fc3024`→0x12 Host Control**, **`fc3030`→0x18 Host Serial Control**,
**`fc303e`→0x1f Halt Enable**. The captured write values are consistent
with real ES5510 init sequences, not noise: `fc3024` written `0x03` then
`0x02` (host-control bits 0-1, matching the device's own documented
"RAM clear" self-clearing behavior); `fc3030` written `0x48` (a plausible
Master/Sony/serial-direction byte); `fc303e` written `0x02` then `0xff`
(Halt Enable). Access width is consistently a byte value carried in a
16-bit CPU transaction (`mem_mask=00ff`), matching the ES5506 host's own
already-established even/odd byte-lane convention on this same bus
(4.16).

- **Evidence for the match:** twelve distinct addresses, spanning nearly
  the device's entire low register bank *and* three separate named
  control registers (Host Control, Serial Control, Halt Enable), all
  landing exactly on the word-strided offset formula with no exceptions;
  write values structurally consistent with genuine init sequences for
  those specific registers.
- **Evidence against / unresolved:** `host_r` offset `0x12` (Host
  Control) is hardcoded to always return `0` in MAME's own device model —
  this is a real gap in the *MAME device itself*, separate from whether
  the ASR-10 driver maps anything there at all (currently, `0xfc2080`-
  `0xfc3fff` is plain `.ram()` — no device is attached, so today's actual
  return value is whatever was last written to that RAM cell, not the
  device's own stub). No read-back/busy-poll sequence was captured at
  full instruction resolution this round (the existing tap is
  power-of-2 rate-limited); the exact host-control *read* that a
  handshake loop would depend on was not directly observed.

**TASK 6 — strongest fingerprint: (B) `FC3000`-`FC30xx`.** Confirmed by
access ordering (register offsets touched in a sequence consistent with
device initialization, not random) and by register *semantics*
(Host Control/Serial Control/Halt Enable specifically, not just numeric
proximity). `FC2D40`-`FC2D7C`: **zero accesses in the entire 180-second
capture** — ruled out, not merely unfavored.

**TASK 7 — recommendation only, nothing implemented this session:**
**(D) collect more evidence before either A/B/C**, specifically: a
full-resolution (non-rate-limited) capture of every `FC3000`-range access
around the download window, to (a) find the actual host-control *read*
a handshake would depend on, if one exists, and (b) confirm no other
register (e.g. DADR/DOL, for direct DRAM access) is also touched. The
fingerprint is strong enough to justify this as the near-term next step,
not strong enough to justify instantiating the device blind.

- **Proposed flag:** `ASR10_EXPERIMENT_ES5510_HOST` (unset by default,
  matching this driver's established convention).
- **Proposed address map:** `FC3000`-`FC303F`, `offset = (address -
  0xFC3000) >> 1`, byte-wide (`mem_mask=00ff`) accesses only, matching
  the pattern observed.
- **Access-width adaptation:** the ASR-10 bus presents 16-bit
  transactions with the real byte in the low half (`mem_mask=00ff`); an
  adapter would extract/insert that byte and call `host_r`/`host_w`
  directly, mirroring the already-working `es5506_device` adapter pattern
  in this same driver.
- **Reset wiring:** none evidenced yet — `device_reset()`'s effect on
  ASR-10 boot timing was not characterized this round.
- **IRQ/handshake wiring:** none evidenced — no ES5510 IRQ line is
  referenced anywhere in this driver, and no busy/ready read was captured
  at instruction resolution.
- **Minimum success criterion for the next run:** a full-resolution
  capture showing the specific host-control (or other) *read* the retry
  loop's `f973f0` call depends on, with its actual vs. expected value —
  i.e., completing TASK 3's "expected vs. actual" cell that this round
  could only bound structurally, not pin to one instruction.

**TASK 8 — documentation, this section.**

**REPORT:**

1. **`EFFECT DOWNLOAD FAILED` producer:** message selector near `f884bc`
   (inside the established MC68302 IRQ dispatcher), via `f89c48`.
2. **`ERROR 032` producer:** message selector near `f884b6` (same
   dispatcher), via `f89c48`; the numeric error code `0x20` is written to
   `$00c0` at `f88284`, called from `ffc8a4`.
3. **First failing compare:** `ffc896: cmpi.l #$fff9bca0,$e8e.w` —
   gates retry eligibility by source-pointer identity, not a hardware
   status bit.
4. **Complete device-access sequence:** `FC3000`+`2×offset` writes/reads
   spanning GPR/INSTR latches, Host Control, Serial Control, and Halt
   Enable, in an initialization-consistent order (full list in TASK 5/6).
5. **Effect-data source:** classified (B), loaded overlay/OS RAM, source
   pointer set from the caller's node `+4` field; length and exact bytes
   not traced this round; no evidenced overlap with the 92,672
   floppy-read bytes.
6. **ES5510 fingerprint verdict:** strong positive match on register
   offsets and semantics; MAME's own `host_r` stub for Host Control
   (offset `0x12`) always returns `0`, a modeling gap independent of
   whether the ASR-10 driver even maps the device yet.
7. **Most likely mapped bank:** `FC3000`-`FC30xx`, word-strided register
   offsets; `FC2D40`-`FC2D7C` ruled out (zero accesses).
8. **Smallest next implementation:** not implemented this session, per
   instruction — recommended next step is (D), a full-resolution
   capture of the `FC3000` handshake read before instantiating anything.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. New
instrumentation this round: a one-shot, gate-conditioned high-view code
dump of `ffc840`-`ffc8c0` under the existing `ASR10_EXPERIMENT_DOWNLOAD_
TRACE` flag (no new flag added, no behavior change when the flag is off).
Nothing forced; not committed.

### 4.22 The exact live carry source, found: a write/verify-readback mismatch inside the ROM effect-object transfer loop, `f97574`, live-confirmed (not yet reviewed)

**Baseline:** the complete ten-flag set (4.21) plus the *pre-existing*
`ASR10_EXPERIMENT_FC3000_VERIFY_TRACE=1` (discovered this round, not
newly written — a prior round of this same investigation had already
built and gated this exact diagnostic, including a comment reading
"proven: `ffc896` compares `$0e8e` against this literal"). All eleven
flags confirmed `effective=1` in `ASR10_RUN_CONFIG_HEADER`. **No new
flag was added; no rate-limiting was removed from the shared `FC3000`
taps — the existing one-shot, table-matched capture already isolated the
correct invocation.**

**TASK 2 — complete live call graph of `f973f0`, disassembled in full
(`f973f0`-`f977ee`, static ROM disassembly, all addresses in the 256KB
boot ROM region, not the disk-loaded overlay):**

```
f973f0  move.l A3,$e7e.w          ; entry: save A3
f973f4  bsr    f9764e             ; helper: arithmetic/scaling (below)
f973f8  bsr    f97744             ; helper: ESP reset/init (below)
f97400  bsr    f973e0             ; (not examined this round -- new callee)
f97402  movea.l $e7e.w,A3         ; restore A3
        ... [outer record loop, calls f97450 to validate+route each
             record, f976ec/f97776/f976fa per byte-block, f9744e on the
             0xFF end-sentinel] ...
f9744e  bra    f97496             ; ALWAYS falls through into the verify
                                  ; pass -- f973f0 itself never rts's on
                                  ; the normal path; f97496's own rts is
                                  ; what actually returns to ffc894.

f97450  (record-type validator)
  cmp.b #1,D3 / bcs f97490         ; D3<1: invalid
  cmp.b #4,D3 / bhi f97490         ; D3>4: invalid
  movea.l #$fffc3001,A4            ; type 1: A4=FC3001,A5=FC3011(+0x10),D4=0x1c0
  [type 2: A4=FC3007, D4=0x180]
  [type 3/4: A5=FC3005(+0x4), D4=0x140]
  cmp.w D0,D0                      ; canonical explicit-clear-carry idiom
  rts                              ; CARRY EXPLICITLY CLEARED (valid)
f97490  ori.b #1,CCR / rts         ; CARRY EXPLICITLY SET (invalid record type)
                                  ; -- consumed locally by f97422's own
                                  ; `bcs f97412` skip/rescan; confirmed
                                  ; NOT propagated to f973f0's return.

f97496  (verify/readback pass -- SECOND parse of the SAME object)
  ... re-parses records the same way, calling f97450 again ...
  f974fc..f97572: rebuilds an expected byte D2 via type/position-
                  dependent OR-masking (constants 0x3f/0x07/0x03/0x0f/
                  0xff depending on D1 ranges) -- NOT simply re-reading
                  the raw source byte.
f97574  cmp.b (A6),D2             ; *** THE ACTUAL FAILING COMPARE ***
                                  ; (A6) = readback from the FC3000-range
                                  ; device window; D2 = firmware's own
                                  ; computed expected value
f97576  beq    f9759c             ; match: continue
f97578  bsr    f976fa             ; mismatch: kick, then...
f97580  addq.b #1,$e8c.w          ; increment retry counter (already-
                                  ; tracked "record_scratch" field)
f97584  cmpi.b #$a,$e8c.w
f9758a  bcs    f9740a              ; <10 retries: redo the WHOLE outer
                                  ; transfer from the top
f9758e  st     $e8a.w              ; 10 retries exhausted: set
                                  ; loop_done_flag_0e8a
f97592  bsr    f976fa
f97596  ori    #1,CCR              ; *** CARRY EXPLICITLY SET -- FINAL FAILURE ***
f9759a  rts                        ; returns to ffc894 with carry set
```

**Helpers, roles and carry classification (TASK 2 continued):**

| helper | role | carry at return |
|---|---|---|
| `f9764e` | arithmetic/scaling (D3-controlled loop, tail-`jmp $8030.w`) | not carry-relevant on the main path |
| `f97744` | ESP reset/init: `bsr f977b0`→`bsr f9776e`→MOVEP `#$ffff0000`→`FC3141:=0xf5`→`FC3025:=0x02`→`trap #8` (26-tick sleep, **requires the DUART counter**)→`trap #7` | preserved from callees; no explicit set/clear found |
| `f976ec` | mask+save: `move SR,D0`→save to `$e82.w`→OR `#$700`→**Line-A `A000`**→rts | carry inherited from whatever the Line-A service leaves (see TASK 4) |
| `f976fa` | restore+kick: reload `$e82.w`→**Line-A `A000`**→bounded poll on `FC3025` bit 2 (≤21 iters, `dbeq`, result unused)→write `0xff` to `FC3101`→rts | carry not explicitly touched; inherited |
| `f97776` | handshake/transfer core: level-wait on `FC302D` (`cmpi.b #$28`, `bcc` loop), write data byte through `(A0,D4.w)`, handshake-wait on `FC3025` bit 2 (`dbeq`), on true timeout (`dbf` outer loop exhausted) `move.b #$21,D0`/`trap #0` (a *different* hard trap, 0x21=33, not reached on the live path this round) | timeout tail sets no explicit carry (goes to `trap #0` instead); normal-path return carry not explicitly set |
| `f97490` | invalid-record-type exit | **explicitly set** (local to the validator, confirmed not propagated) |
| `f9748c`/rts | valid-record-type exit | **explicitly cleared** (`cmp.w D0,D0` idiom) |
| `f97596` | verify-pass retry-exhaustion exit | **explicitly set — this is the one that reaches `ffc894`** |

**TASK 3/4/5/6 — live confirmation, the exact mismatch, captured by the
pre-existing `ASR10_FC3000_VERIFY_HANDSHAKE` tap (one-shot, gated to the
exact `$e8e.w==0xfff9bca0` table):**

```
ASR10_FC3000_VERIFY_HANDSHAKE pc=f97574 address=fc300f offset=fc300e mem_mask=00ff
  observed=f0 expected_d2=90 sr=0709
  outer_retry_remaining=2 internal_retry_count=0
  table_base_0e8e=fff9bca0 record_type_d3=02 record_count_d5=59
  record_param_d1=00 record_index_d6=00
  a0=000000 a3=fff9bd4e a4=fffc3007 a5=fffc3011 a6=fffc300f
  last_accesses="[pc=f9770e addr=fc3101 W data=ffff],[pc=f97792 addr=fc3101 W data=0000],
    [pc=f97796 addr=fc3025 R data=0002],[pc=f97792 addr=fc3141 W data=0000],
    [pc=f97796 addr=fc3025 R data=0002],
    [pc=f97574 addr=fc3007 R data=00ff],[pc=f97574 addr=fc3009 R data=00ff],
    [pc=f97574 addr=fc300b R data=00ff],[pc=f97574 addr=fc300d R data=00ff],
    [pc=f97574 addr=fc300f R data=00f0]"
```

**This is the load-bearing live result.** At `f97574`, comparing readback
`(A6)=0xf0` against the firmware's own computed expected value
`D2=0x90` — **a genuine mismatch, live-confirmed, not inferred.** Four
of the five positions read back in this record (`fc3007/09/0b/0d`) show
`0x00ff`, and only the fifth (`fc300f`) shows `0x00f0` — **not the
"unmodeled device returns zero" pattern the naive model predicted**,
exactly as the working hypothesis going into this round warned. The
handshake poll at `fc3025` reads `0x0002` (bit 2 clear), so the `dbeq`
loop exits on its first iteration every time — **the bounded poll passes
trivially, confirmed live, consistent with 4.21's structural read of
plain `.ram()` backing**, but this is a *different* fact from what
actually fails: the failure is the **content mismatch at `f97574`**, not
a stuck handshake bit.

- **Decision PC:** `f97574`. **Instruction:** `cmp.b (A6),D2`.
- **Helper/invocation:** the verify pass (`f97496`+), first mismatch of
  the live `0xfff9bca0`-table invocation (one-shot capture; not
  necessarily the very last retry before exhaustion, but representative
  of the mismatch class that drives every retry).
- **Expected value:** `D2=0x90`, computed by the verify pass's own
  position/type-dependent OR-masking logic (`f974fc`-`f97572`) — **not**
  simply the raw source byte re-read from the ROM object.
- **Actual value:** `0xf0`, read back from `FC300F` — i.e. whatever is
  currently resident at that `.ram()` cell.
- **Branch:** `bne` (mismatch) taken → retry-then-exhaust path →
  `f97596: ori #1,CCR` → `rts` with carry set → `ffc894`'s `bcc` not
  taken → falls into the code 4.21 already traced (`ffc896` onward).
- **Classification against TASK 6's candidates:** **(D) — verify/readback
  pass mismatch**, specifically at `f97574`. Not (A)/(B): the level-wait
  and handshake-wait *both* pass trivially per the ring buffer. Not (C):
  the timeout-exhaustion tail in `f97776` is not what's hit here (that
  path leads to a *different* trap, `0x21`, not observed). Not (E): the
  record type (`0x02`) is within the valid 1-4 range, confirmed by
  reaching the verify pass at all. **`ffc896` (TASK 6's explicit
  exclusion) is confirmed, again, to be retry policy only — not
  reported as the failure predicate.**

**TASK 7 — the verify/readback pass, characterized:** yes, a genuine
**second pass over the same ROM effect object** (confirmed:
`table_base_0e8e=fff9bca0` matches the first pass's own source). Writes
happen in the *first* pass (`f97432: move.b (A3)+,(A6)`, upload); the
*second* pass (`f97496`+) only reads back and compares — a real
verify-after-write design, not a coincidence. The compare is against a
**computed/masked expected value**, not a raw re-read, a raw checksum, a
GPR/instruction-content compare, or a record-count/pointer check — those
alternatives from the task list are not what `f97574` does.

**TASK 8 — the ROM effect object at `fff9bca0`, live-confirmed fields:**
`table_base_0e8e=fff9bca0` (the exact address, matching the pre-existing
tap's own hard-coded gate and this round's static prediction from
4.21). `record_type_d3=02` (type 2, using `A4=FC3007`/`D4=0x180` per
`f97450`'s routing — matches the live `a4=fffc3007` exactly).
`record_count_d5=59` (89 decimal — a real, substantial record count, not
a trivial/empty object). Full header-word decode, complete name-string
decode, and the type 1/3/4 D4-offset mappings (`0x1c0`/`0x140`) were
**not** re-derived live this round beyond what 4.21's static read
already found (`"3 1   V O I C ..."` header text) — this remains
consistent with, not independently re-proven beyond, that static reading.

**TASK 9 — ES5510 fingerprint, extended:** `FC3101`/`FC3141` (TASK 10,
below) now also fit the word-stride formula precisely: `FC3101` →
`offset=(0x101)/2=0x80` = the device's own "Read select - GPR+INSTR"
trigger register; `FC3141` → `offset=0xA0` = "Write select - GPR". Both
match `es5510.cpp`'s `host_w` cases `0x80`/`0xa0` exactly — **this closes
the gap TASK 10 asked about.**

**TASK 10 — role of `FC3101`/`FC3141`: (A), part of the same ES5510 host
decode, using the identical word-stride mapping** — not external glue,
not a second device, not unresolved. Confirmed by both static
disassembly (`f97776`'s `D4=0x100`/`D4=0x140` calls write through
`(A0,D4.w)`, i.e. `FC3101`/`FC3141` relative to `A0=FC3001`) and the live
ring buffer (`fc3101`/`fc3141` writes immediately preceding the transfer,
exactly where the disassembly says they'd occur).

**TASK 11 — recommendation only, nothing implemented this session,
updated from 4.21's (D):** the exact failing predicate is now known
(TASK 6), so the next step can be sharper than "collect more evidence" —
but per instruction, **no device is instantiated this round.**
Recommended next action remains evidence-gathering, now narrowly scoped:
confirm what value `FC300F` (and its four preceding positions) *should*
hold after a correct upload, by tracing the *first-pass write* at
`f97432` for the same record/byte-index live, and comparing it against
the verify pass's computed `D2` — this determines whether stock
`es5510_device`'s real GPR/INSTR-latch bit-packing (which MAME's model
does implement, per 4.21's source read) would make the two agree, or
whether the mismatch is purely a "nothing is instantiated, so nothing
transforms the byte the way `D2`'s masking logic expects" gap. **Minimum
success criterion for that next run:** the first-pass write value at the
same address/index is captured and compared directly against this
round's `expected_d2=0x90`/`observed=0xf0`, rather than only the
post-hoc readback.

- **Proposed flag (unchanged):** `ASR10_EXPERIMENT_ES5510_HOST`.
- Address map, byte-lane behavior, MOVEP handling: unchanged from 4.21,
  now with two additional confirmed registers (`0x80`/`0xa0` triggers).
- **Reset wiring:** `f97744`'s init sequence (`FC3141:=0xf5`,
  `FC3025:=0x02`, `trap #8` 26-tick sleep) is a real, ROM-driven reset
  sequence — **this confirms the DUART counter/tick engine is a hard
  prerequisite for ESP init to even complete**, independent of whatever
  the eventual verify-pass outcome is.
- **IRQ/handshake wiring:** still none evidenced.

**TASK 12 — documentation, this section.**

**Line-A note (TASK 4's special requirement):** `A000` appears in
exactly the two helper entry/exit points already known
(`f976ec`/`f976fa`), both using it purely as "replace resumed SR with
D0" (4.21's own established reading, reconfirmed against the fuller
`f973f0` call graph this round). No live per-invocation D0/stacked-SR/
resumed-SR/resulting-CCR trace was captured this round (the existing
`ASR10_FC3000_VERIFY_HANDSHAKE` tap does not instrument Line-A directly)
— this remains a specific, narrow gap for a future round if the Line-A
carry-preservation question needs closing beyond the structural reading
already given.

**REPORT, in the order requested:**

1. **Corrected `f973f0` call graph:** given in full above (TASK 2) —
   `f973f0` never `rts`s on its own; it tail-branches into `f97496`'s
   verify pass, whose own `rts` is what returns to `ffc894`.
2. **Five helper roles + live hit counts:** roles given in the table
   above; live per-helper invocation counts were not separately tallied
   this round (the capture is a single gated snapshot, not a running
   counter) — a gap, not a claim of zero.
3. **Carry classification per return site:** table above; `f97450`'s two
   exits are explicit (clear/set), `f97596` is explicit (set, the one
   that matters), the four `f976xx`/`f977xx` helpers do not explicitly
   touch carry on their normal paths.
4. **Exact last carry-changing instruction before `ffc894`:**
   `f97596: ori.b #1,CCR`, reached after 10 exhausted retries of the
   `f97574` mismatch.
5. **Exact expected vs. actual:** `D2=0x90` (computed) vs. `(A6)=0xf0`
   (read back from `FC300F`), live-confirmed.
6. **Exact register/bank involved:** `FC300F` (`A6`, mid-transfer window
   address within the type-2 `FC3007`-based range), part of the
   `FC3000`-`FC303F` ES5510 host bank.
7. **Line-A effect on the live path:** structurally "restore SR via
   Line-A," not independently re-traced per-invocation this round (gap
   noted above).
8. **Verify/readback-pass result:** a genuine second pass, real content
   mismatch, not a stuck handshake or timeout.
9. **Decoded `fff9bca0` object:** confirmed live as the source table for
   this transfer, type-2 record with 89 sub-items; full byte-level
   decode not repeated beyond 4.21's static read this round.
10. **Stock `es5510_device` compatibility verdict:** strong positive
    fingerprint, now extended to `FC3101`/`FC3141` as the device's own
    `0x80`/`0xa0` select registers; the specific content-mismatch defect
    is not yet attributed to a specific MAME-model gap vs. an
    ASR-10-driver mapping gap — that determination needs the first-pass
    write trace recommended in TASK 11.
11. **Role of `FC3101`/`FC3141`:** (A), same device, same word-stride
    map, GPR/INSTR read-select and write-select triggers.
12. **Smallest justified implementation:** not implemented this session;
    next step is the narrow first-pass-write-vs-verify-pass-read
    correlation described in TASK 11, before any instantiation decision.

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. No new flag
added — the pre-existing `ASR10_EXPERIMENT_FC3000_VERIFY_TRACE` flag was
enabled for this capture, discovered (not written) this round. ES5510
not instantiated. Nothing forced, patched, or synthesized; nothing
committed.

### 4.23 The exact mismatch mechanism, found: a shared-window RAM-reuse collision, not a register-width mask — stock ES5510 has the missing piece (not yet reviewed)

**The arithmetic hint in this round's own brief (`0xF0 & 0x9F = 0x90`,
predicting a hardware bit-mask) is directly falsified by live evidence
below.** It is recorded here as a disproven hypothesis, not deleted —
per this document's own discipline of correcting rather than erasing.
`ffc896` remains retry policy only (4.22's finding, unchanged);
`f97574` remains the exact failing compare (4.22, unchanged). This
section adds the missing link 4.22 left open: *why* the readback differs
from the write.

**Baseline:** the complete eleven-flag set (4.22) baseline, unchanged.
All flags reconfirmed `effective=1` in `ASR10_RUN_CONFIG_HEADER` for this
capture.

**New instrumentation (TASK 1):** one new tap, `log_esp_first_pass_write`,
added inside the *existing* `hook_fc3000_write_tap` (no new flag — gated
on the pre-existing `ASR10_EXPERIMENT_FC3000_VERIFY_TRACE` and the
pre-existing `fc3000_verify_table_match()` helper), firing on every
`f97432` upload-loop write while the `0xfff9bca0` table is active. This
directly answers TASK 1's "correlate the exact first-pass write" without
guessing from timestamps.

**The exact answer, live-captured, unambiguous:**

```
seq=5     record_index_d6=00  dest=fc300f  written=90  source_rom=f9bd4d
seq=11    record_index_d6=01  dest=fc300f  written=90  source_rom=f9bd53
...       (57 more iterations, all writing to the SAME dest=fc300f)
seq=16442 record_index_d6=56  dest=fc300f  written=90
seq=16448 record_index_d6=57  dest=fc300f  written=90
seq=16454 record_index_d6=58  dest=fc300f  written=f0   <- LAST write, matches
                                                            4.22's observed=0xf0 EXACTLY
```

**All 2670 writes to `FC300F` target the identical CPU-visible address.**
`record_index_d6` runs 0 through at least 58 (of `record_count_d5=59`
total), each one uploading a *different* record's data through the *same*
five-byte window (`FC3007`-`FC300F`, the instruction-latch bytes for a
type-2 record, per 4.21/4.22's offset mapping). **The very last write
before the verify pass runs (`record_index_d6=58`) writes exactly
`0xf0`** — the identical byte 4.22's verify-pass readback later observed.
**This is not a register-width mask. It is a plain last-write-wins RAM
collision**: `FC3007`-`FC3011` is unconditionally `.ram()` (4.21), so
every one of the 59 records' uploads overwrites the same five bytes, and
whatever the *final* record leaves behind is what the verify pass reads
back when it later checks *every* record in turn — including record 0,
whose own byte (`0x90`) was long since overwritten.

**TASK 2/6 — reconstructing why this shouldn't collide on real
hardware, from `es5510.cpp` source semantics (reasoned directly from the
cited source, not run as a separate test harness — sufficient given the
mechanism below is unambiguous):**

The static disassembly (4.22) already showed `f97776` (the transfer core)
executing `move.b D1,(A0,D4.w)` with `D1` = the per-record index/param
and `D4` varying by call site (`0x100`, `0x140`, `0x180`, `0x1c0`
depending on record type and pass). Cross-referencing against the
`es5510_device` register map (4.21/4.22):

- `D4=0x180` → `offset=0xc0` = **"Write select - INSTR"**
  (`es5510.cpp` `case 0xc0: if (data<0xa0) instr[data]=instr_latch...`) —
  the firmware's own upload-pass call (record type 2 uses `D4=0x180`,
  matching 4.22's static read of the type-2 routing) commits the just-
  built 48-bit `instr_latch` (from the six `FC3007`-`FC3011` byte writes)
  into **`instr[D1]`** — a *distinct, indexed* internal instruction slot
  per record, on stock hardware/stock `es5510_device`.
- `D4=0x100` → `offset=0x80` = **"Read select - GPR+INSTR"**
  (`case 0x80: if (data<0xa0) instr_latch=instr[data]; ...`) — the
  verify pass's own call (`f974ea`, unconditionally reached for every
  record type including 2) writes `D1` (the *same* per-record index)
  here **before** looping over `FC3007`-`FC3011` at `f97574` — i.e. the
  firmware *does* select record `D1`'s own stored instruction before
  reading it back.

**On stock hardware/stock `es5510_device`, this protocol is exactly
right: each record's 48-bit instruction is committed to its own
`instr[D1]` slot on upload, and re-selected by the same `D1` before
verification — no collision, because the "many slots" live inside the
device's own `instr[]`/`gpr[]` arrays, not in the shared five-byte host
latch window.** The **only** reason this collides in the current
ASR-10 driver is that `FC3000`-`FC31FF` is plain, passive `.ram()` — the
select writes to `FC3101`/`FC3141` are inert, so nothing ever routes a
given record's bytes to a per-record location; all 59 records' worth of
uploads simply land in the same five RAM cells, and the verify pass
reads back whichever record wrote there *last*, regardless of which
record it's currently checking.

**TASK 3 (offline stock-device classification):** based on directly
reading `es5510_device::host_w`'s `case 0xc0`/`case 0x80` logic (cited
above) against this round's proven write-then-select sequence, the
classification is **(A) — stock `es5510_device`, correctly wired, would
return the *correct*, non-collided per-record value** (specifically:
`instr[0]`'s own committed bytes when record 0 is re-selected, not
record 58's leftover bytes). This is reasoned directly from the cited
source against the live-proven call sequence, not asserted from the
device's name alone — TASK 3's own caution against forcing a match
otherwise is respected: the match is on *behavioral protocol*
(select-commit-reselect-read), not on address proximity.

**TASK 4/5 (byte-lane and MOVEP mapping):** not separately re-verified
this round beyond 4.21/4.22's existing `mem_mask=00ff` observations
(every access in the new `ASR10_ESP_FIRST_PASS_WRITE` capture is
byte-wide, consistent with prior rounds); no MOVEP instruction was
specifically isolated in the `f97432` write path this round (the write
there is a plain `move.b`, not a MOVEP) — MOVEP behavior (used elsewhere
in `f97744`'s init sequence, per 4.22) remains characterized only as far
as 4.22 already established, not extended here.

**TASK 7 — full arrow-by-arrow correlation, with evidence type:**

| step | evidence |
|---|---|
| ROM source byte (`0x90` at `f9bd4d`, record 0) | live (`ASR10_ESP_FIRST_PASS_WRITE` `seq=5`) |
| → written to `FC300F` | live (same event) |
| → (record 0's write is later overwritten by records 1-58) | live (2670 total writes to the same address, `seq=5..16454`) |
| → committed to `instr[D1]` (stock semantics) | static (`es5510.cpp` `case 0xc0`), reasoned |
| → re-selected via `FC3101` write of `D1` before verify | static (`f97776`/`f974ea` disassembly, 4.22) + stock semantics (`case 0x80`), reasoned |
| → host readback at `FC300F` | live (4.22, `observed=0xf0`) — **but this is record 58's byte, not record 0's, because nothing is instantiated to honor the select** |
| → firmware-computed expected `D2=0x90` (record 0's own value) | live (4.22) |
| → `f97574` mismatch | live (4.22) |

**TASK 8 — implementation verdict: (A), instantiate stock `es5510_device`
with a word-stride/byte-lane adapter — strongly justified by this
round's evidence, not merely "likely."** The collision is fully and
specifically explained by the *absence* of the device's own per-record
internal storage; no gap in `es5510_device`'s own modeled behavior was
identified this round (unlike the still-open `host_r` offset `0x12`
stub noted in 4.21, which is unrelated to this specific `instr[]`
collision path). **Not implemented this session, per instruction.**

**TASK 9 — minimum next-run success criteria (unchanged in spirit from
4.22, now concretely testable against this exact record):** after a
future instantiation, record 0's `FC300F` verify read should return
`0x90` (not `0xf0`), `$e8c.w` should not increment for that record,
and — critically — the *same* check for record 58 should independently
also pass (proving the fix generalizes across records, not just record
0), before treating `EFFECT DOWNLOAD FAILED`/`ERROR 032`'s disappearance
as meaningful.

**TASK 10 — documentation, this section.**

**Superseding note for 4.22's TASK 11 recommendation:** 4.22 recommended
tracing the first-pass write as the next step "before any instantiation
decision." That trace is now complete (this section); the recommendation
sharpens from 4.22's "(D)-adjacent, collect more evidence" to this
section's **(A), with a specific, falsifiable prediction** — still not
acted on this session.

**REPORT, in the order requested:**

1. **Exact matching first-pass write PC:** `f97432`, `move.b (A3)+,(A6)`.
2. **Source ROM address and byte (record 0):** `f9bd4d` → `0x90`.
3. **Object/record/byte identity:** table `0xfff9bca0`, type 2, record
   index 0 (and, for the *actual* value later read back, record index
   58), byte position 5-of-6 within the instruction-latch window
   (`FC300F`, offset `(0xF-1)/2` within the `FC3007`-based window).
4. **Complete sequence:** upload write (`f97432`) → per-record commit
   via `FC3180` select (offset `0xc0`, stock semantics) → re-select via
   `FC3101` (offset `0x80`) before verify → readback at `f97574`.
5. **Current plain-RAM shadow behavior:** the select writes are inert;
   `FC3007`-`FC3011` is a single shared RAM window, overwritten by every
   one of 59 records in turn.
6. **`FC300F`-derived host offset:** `(0xF00F-0xFC3000... )` — i.e.
   `(0x300F-0x3000)/2 = 0x07` within the instruction-latch bank (INSTR
   latch byte position, per 4.21's table), consistent throughout.
7. **Stock internal value after commit:** would be `instr[0]`'s own
   committed 48-bit value (containing byte `0x90` at this position),
   distinct from `instr[58]`'s.
8. **Stock `host_r` predicted byte:** `0x90` for record 0, reasoned
   directly from `es5510.cpp`'s `case 0x80`/latch-byte-read logic against
   the live-proven select sequence — not independently run as a separate
   test binary this round.
9. **Does stock return 0x90:** **yes, per direct source reasoning** —
   classification (A) from TASK 3/6 above.
10. **Exact mask/packing rule:** **none — the `0xF0`/`0x9F`/`0x90`
    arithmetic coincidence from this round's own brief is disproven.**
    The real mechanism is RAM-window reuse across 59 records, not a
    bit-width truncation.
11. **`FC3000` vs `FC3001` logical base:** unchanged from 4.21/4.22 —
    `offset=(address-0xFC3000)>>1`; not re-litigated this round.
12. **Ordinary byte-lane transformation:** unchanged, `mem_mask=00ff`
    throughout, confirmed again in this round's own capture.
13. **MOVEP transformation:** not separately re-verified this round (see
    TASK 4/5 note above) — remains as characterized in 4.22.
14. **Smallest justified implementation:** **(A)**, per TASK 8 — not
    implemented this session.
15. **Next-run success criteria:** per TASK 9 above — record 0 *and*
    record 58 must both verify correctly, not just "the boot progresses."

**Verification:** build clean; Gate 1 exact (`GATE1_EXIT=0`, flagless
`grep -c "ASR10_FDC_TC"` = `0`); `git diff --check` clean. One new tap
added (`log_esp_first_pass_write`), gated entirely on the pre-existing
`ASR10_EXPERIMENT_FC3000_VERIFY_TRACE` flag and the pre-existing
`fc3000_verify_table_match()` gate — no new flag, no behavior change
when that flag is off. ES5510 not instantiated. Nothing forced, patched,
or synthesized; nothing committed.
