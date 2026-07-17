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
- The filesystem shape question (section 3) remains open.
