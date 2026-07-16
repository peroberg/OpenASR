# ASR-10 Evidence Tree — Consolidated 2026-07-15

Companion to `architecture.md`, `panel-protocol.md`, `current-blocker.md`. This
document classifies every claim made across the DUART counter/timer
investigation (asr10-claude-3, commits `ba7f83917dd`..`947ffb66eec` plus
uncommitted diagnostics) into five strict categories. Every entry cites its
source: a run/log event name, a ROM/RAM address, a MAME source file+line, or
another doc.

Evidence levels used below: **[PROVEN]** — reproduced, disassembled, or cited
from source directly. **[STRONG]** — consistent, repeated, non-coincidental
but not fully closed. **[HYP]** — plausible, evidence-compatible, unproven.
**[RETRACTED]** — actively disproven or superseded. **[OPEN]** — contradiction
or unresolved gap.

---

## 1. Proven facts

- **DUART timer-mode IRQ chain works end-to-end.** Sequence dynamically
  proven: ACR=0x60 write (timer mode) → nonzero CTUR:CTLR preload →
  `duart_counter_check_implicit_start()` arms a periodic MAME timer →
  `duart_counter_terminal_count()` sets ISR bit 3 → `panel_c_update_irq6()`
  asserts IRQ6 (`counter_active=08`) → `maincpu_iack_r` returns vector 0x56
  (also independently confirmed by the pre-existing `ASR10_M68K_IACK`
  tracer) → firmware dispatches to `f88300` (34+ watched-PC hits) → `f88300`
  reads the STOP-counter register, clearing ISR bit 3 → cycle repeats.
  Source: `error.log` events `ASR10_DUART_COUNTER event=implicit_scn2681_timer_start/terminal_count`,
  `ASR10_DIAG_PANEL_AUTORESPOND event=irq6_route`, `ASR10_M68K_IACK`,
  `ASR10_DUART_COUNTER event=watched_pc role=bit3_handler_entry_f88300`.

- **`f88300` is the scheduler timeout/tick engine**, fully disassembled
  (live ROM, `unidasm -arch m68000 -xchbytes`):
  ```
  f88300: move.b  $fffc481f.l, D0      ; STOP-counter read, acks ISR bit3
  f88306: movea.w $c6.w, A0            ; primary slot table base [$00c6]
  f8830a: moveq   #0, D0
  f8830c: move.w  (A0), D1             ; slot+0 = tick countdown
  f8830e: beq     $f8831e
  f88310: subq.w  #1, D1
  f88312: move.w  D1, (A0)
  f88314: cmp.w   ($14,A0), D1         ; vs slot+0x14 = threshold
  f88318: bhi     $f8831e
  f8831a: bclr    D0, ($2,A0)          ; clear pending bit0 (slot+2)
  f8831e: adda.w  #$16, A0             ; next slot (stride 0x16)
  f88322: cmpa.w  $c8.w, A0            ; vs table end [$00c8]
  f88326: bcs     $f8830c
  f88328: addq.b  #1, $b82.w           ; mod-10 tick counter
  f8832c: cmpi.b  #$a, $b82.w
  f88332: bcs     $f88362              ; not yet 10th tick -> skip secondary walk
  f88334: clr.b   $b82.w
  f88338: movea.w $ca.w, A0            ; secondary table base [$00ca]
  f8833c: move.w  ($14,A0), D1         ; entry+0x14 = countdown
  f88340: beq     $f88358
  f88342: subq.w  #1, D1
  f88344: move.w  D1, ($14,A0)
  f88348: bne     $f88358              ; only fire when countdown hits 0
  f8834a: movea.l ($16,A0), A1         ; entry+0x16 = 32-bit callback ptr
  f8834e: movem.l A0/A3, -(A7)
  f88352: jsr     (A1)                 ; the callback invocation
  f88354: movem.l (A7)+, A0/A3
  f88358: adda.w  #$1a, A0             ; next secondary entry (stride 0x1a)
  f8835c: cmpa.w  $cc.w, A0            ; vs table end [$00cc]
  f88360: bcs     $f8833c
  ```
  Confirms: primary-slot stride 0x16, slot+0=countdown, slot+2=pending-bit
  state (bit0 cleared on threshold), and a **secondary table** rooted at
  `[$00ca.w]`/end `[$00cc.w]`, stride 0x1a, entry+0x14=countdown,
  entry+0x16=**32-bit callback pointer**, invoked via `jsr (A1)` at exactly
  `f88352` (verified: `f8834a: movea.l ($16,A0),A1` immediately precedes
  it — no other candidate instruction loads A1 in this routine). The
  secondary walk runs only every 10th primary tick
  (`$0b82` mod-10 counter, `f88328`/`f8832c`/`f88334`).

- **TRAP #7 and TRAP #8 — mechanism proven by disassembly, not by name.**
  Trap table at ROM `f82080` re-verified by direct dump (7 of 16 entries
  independently matched previously-documented values, confirming table
  alignment): TRAP #7 → `0xfff88108`, TRAP #8 → `0xfff8812c`.
  ```
  TRAP #7 (f88108): movea.w $b6a.w,A2 ; move.w D0,($14,A2) ; moveq #0,D1
                     ori #$700,SR ; cmp.w (A2),D0 ; bcs..
                     bclr/bset D1,($2,A2) ; bset D1,($3,A2)
                     bra $f87f80          <- falls into the dispatcher body,
                                             NO rte
  TRAP #8 (f8812c): ori #$700,SR ; movea.w $b6a.w,A0 ; move.w D0,(A0) ; rte
  ```
  Proven: neither trap touches D2/D6/D7. Neither accesses hardware. TRAP #7
  is a genuine **scheduling** primitive (sets active-slot+0x14 threshold,
  conditionally adjusts the pending bits by comparing D0 against the
  current countdown, then jumps directly into the dispatcher — never
  returns via rte). TRAP #8 is a plain **synchronous state write**
  (sets active-slot+0, `rte`, no dispatcher entry). This retires the
  "sleep/yield" naming guess entirely in favor of disassembly-proven
  behavior. Source: this session's static analysis, cross-checked against
  `architecture.md` §5 trap table (7/16 entries matched exactly:
  #2=f88066, #3=f88078, #4=f880a2, #6=f880d6, #9=f88138, #10=ffff88e8,
  #11=ffff88e2, #12=f88174).

- **FC60B0 thunk verified byte-for-byte at runtime.** Live read:
  `0548 0068 4e75` = `movep.l ($68,A0),D2 ; rts`, exact match to the
  pre-decoded image evidence (`match=1`, zero divergence). No runtime
  patch/relocation. Source: `ASR10_FC2001_TRACE event=fc60b0_verify`.

- **The divide-by-zero root cause chain is fully traced.** `00686e`
  (8-iteration loop, `moveq #7,D7`) calls `FC60B0` eight times
  (fire_counts 149/153/157/161/165/169/173/177 — 177 is the exact fault
  tick). Every one of the 32 individual byte reads (FC2069/6B/6D/6F × 8)
  returned `0x00`, with zero variance across the entire pass — the only
  measurement pass observed in any run. Since every input is 0, the
  arithmetic (`asl.w #6`, `lsr.w #3`, accumulate into D6) is fully
  determined without needing per-instruction hooks: D6 never changes, and
  since `move.w D6,D2` immediately precedes the fault with D2=0 (captured
  in the exception frame), D6 both started and ended at 0. Fault site:
  `00680a: divu.w D2,D0` (dividend D0=0xa3480000, an immediate load two
  instructions earlier; divisor D2=0, a direct register operand — not a
  memory address, so no write-trace applies). Source: `ASR10_DIVZERO_FRAME
  event=exception_frame`, `ASR10_FC2001_TRACE event=fc2xxx_read` (32/32
  `data=0000`).

- **`0x6800` is not a call target — it is fallthrough after an internal
  `bsr`.** Disassembly of the live byte dump immediately preceding the
  fault: `0067fe: bsr $686e` (2-byte `bsr.s`) ends exactly at `0x6800`.
  The stacked long at SP+6 (claimed candidate for "return address to
  0x6800's caller") is `0x00000000` — falsifying the "0x6800 was jsr'd"
  premise directly, consistent with 0x6800 simply being mid-routine
  straight-line code. Source: `ASR10_DIVZERO_FRAME event=stack_frame_dump`,
  `event=caller_return_bytes` (skipped: address was 0).

- **MOVEP register-offset match to ES5506 PAR/IRQV/PAGE is now a fully
  closed mathematical chain**, not just an unforced coincidence — see
  `es5506-chain-verification.md` for the complete proof. Three shipped,
  independent MAME drivers (`esqkt.cpp:184`, `nmk/macrossp.cpp:847`,
  `seta/ssv.cpp:392`) wire `es5506_device::read/write` via
  `.umask16(0x00ff)` over exactly an 0x80-byte CPU window for a 0x40-byte
  (64-entry) device register file — i.e. CPU-displacement = 2 ×
  device-offset, proven by the range arithmetic itself (0x80 CPU bytes ÷ 2
  = 0x40 device bytes, matching the register file size exactly). Applying
  that identical, precedented convention to our observed CPU displacement
  +0x68 gives device-offset 0x34 = 52 decimal → `case (52/4)=13` inside
  `reg_read_test`, which is written in source as `case 0x68/8:  // PAR`
  (`es5506.cpp:1421,1498`) — an exact algebraic identity
  (`CPU_disp/8 == device_offset/4` when `device_offset = CPU_disp/2`),
  not a scaled/forced match.

## 2. Strong evidence (not fully closed)

- **FC2000-FC3FFF is plain, unmodeled `.ram()` in this harness**
  (`asr10_boot.cpp` `mem_map()`: `map(0xfc0000, 0xfc3fff).ram();`) — the
  proximate reason all four FC2069/6B/6D/6F reads return 0. No custom
  device handler exists for this range in the current driver.

- **A "device access library" of MOVEP thunks exists in the DPRAM chunk**
  (FC6028-FC6136, ~20 entries, full table in `movep-library.md`), with
  read AND write variants at multiple offsets, register-width diversity
  (D0/D1/D2/D3), and 3 exact offset hits (+0x68/+0x70/+0x78) against
  ES5506's global TEST-bank registers (PAR/IRQV/PAGE) in **both** low and
  high per-voice banks identically. Strong circumstantial support for an
  ES5506-compatible register layout underlying this library, but static
  callers are proven for only ONE thunk (FC60B0, called from `00686e`) —
  the other ~19 thunks' callers are not captured in any run to date.

- **FC3000-FC31FF cluster is genuinely runtime-active**, 32 writes
  observed, PC range `f9734e`-`f973aa`, using the **identical MOVEP-via-A0
  convention** (`f97360: movep.l D0,($0,A0)`) as the FC2001 library — but
  this activity completes **very early** in boot (log line ~460, vs. the
  fault at line ~394000) with no temporal or causal link to the divide.
  No shared A0 base value was captured connecting the two clusters.
  Source: `ASR10_CLUSTER_TRACE event=fc3000_write` (32/32 hits, 0 hits on
  FC2D40-FC2D7F or FC222E/FC226E in the same run).

- **Node 14f4/89a2 is posted and consumed (once, under the counter/timer
  experiment).** Free-list head (`$0b6c`) observed advancing 14f4→14fc
  (allocation) then returning to 14f4 with `$0b7f` back to 0 (release,
  trap #4) within one dispatcher pass — direct proof the scheduler's
  node-consumption path completes cleanly once the panel/ring stall is
  removed. This was previously only hypothesized; see
  `asr10-panel-slot0-handoff-2026-07-13.md` for the pre-experiment state.

- **Corrected MC68302 Port A/B register map** (internal base `FC6000`):
  `FC681E`=PACNT, `FC6820`=PADDR, `FC6822`=PADAT, `FC6824`=PBCNT,
  `FC6826`=PBDDR, `FC6828`=PBDAT (low byte `FC6829`=PB7..PB0), reserved
  `FC682A/682C/682E`, and `FC6830-FC683E`=BR0/OR0/BR1/OR1/BR2/OR2/BR3/OR3
  (chip-select base/option registers). Source: RTEMS `m68302.h` register
  structure + MC68302 User's Manual, independently anchored by this
  session's already-observed `FC6830-FC683E` chip-select initialization
  writes (`architecture.md` §3). This **retracts** an informal,
  never-committed doubt from an earlier session that Port B might live at
  `FC6834/6835` instead — those are confirmed BR1/OR1, not Port B.
- **MC68302 Port B bits 2:0 are configured as GPIO outputs.** Runtime-proven
  via `ASR10_EXPERIMENT_MC68302_GPIO_TRACE=1`: `PBCNT=0x80` (bits 2:0 = 0 →
  GPIO mode, not peripheral IACK7/6/1) and `PBDDR=0x97` (bits 2:0 = 7 → all
  three configured as outputs), both set at `fb8e16`/`fb8e1e`, `fire_count=0`.
  Reproduced identically across two independent captures (45s and a longer
  run). This satisfies the Phase 2 Stage 1 gate exactly as specified.

- **`00686e` fully disassembled** (`006870-00689c` extended range): confirms
  `moveq #7,D7` loop, `movem.l D6-D7,-(A7)`, `move.w #4,D0`/`trap #8`/
  `moveq #0,D0`/`trap #7` (cooperative yield to the dispatcher **between
  every one of the 8 samples**), `movea.l #$fc2001,A0`/`jsr $fffc60b0`,
  `asl.w #6,D2`/`lsr.w #3,D2`/`add.w D2,D6`, `dbra D7,...`, `move.w D6,D2`,
  `rts`. Source: `ASR10_TASK2_00686E_DUMP` (live RAM read).
- **`006800-006820` (the divider) fully disassembled**: `move.w D2,$0dd6`,
  `divu.w D2,D0` (dividend `$a3480000`), a zero-quotient clamp to `$FFFF`
  (`bne.s`/`move.w #$ffff,D0`), `move.w D0,$0df2`, then
  `andi.b #$f8,$fc6829`/`ori.b #$05,$fc6829`. The clamp-to-`$FFFF` pattern
  supports classifying `$0DF2` as a timer/period reload value rather than
  a generic diagnostic. Source: `ASR10_DIVIDER_TASK2` (live write taps at
  `$0DD6`/`$0DF2`) + `ASR10_FC681X_CODE_DUMP`.
- **`FC60B0` has (at least) four additional static ROM call sites**:
  `f8db04`/`f8db24`/`f8db36`/`f8db52` (all literal `jsr $fffc60b0`),
  found via an exhaustive live word-scan of `0xf80000-0xfbfffa`. Two of
  the four (`f8db30`, `f8db4c`) implement an exponential-smoothing filter
  against a per-instance state cell at `(A2+6)` — architecturally
  consistent with periodic sampling of a noisy physical input. **None of
  the four have been observed executing in any capture** (every captured
  PAR-read burst still totals exactly 32 events, matching only `00686e`'s
  own loop) — consistent with these being event-driven (e.g.
  touch-triggered) callbacks that don't fire during an unattended boot.
  Source: `ASR10_TASK1_STATIC_JSR_SCAN` + `ASR10_CODE_DUMP
  tag=task2_f8db_armed_callback_range`.

- **Divider branch at `00680c` corrected to `BVC`, not `BNE`.** Re-verified
  directly from the live opcode word `$6804` (`0110 1000 00000100`: Bcc
  class, condition field `1000`=VC). The clamp to `$FFFF` at `00680e`
  fires on **quotient overflow** (divisor too small), not on a zero
  quotient — divide-by-zero is a separate, full CPU exception excluded
  entirely from this branch. Retracts the `BNE`/"zero-quotient clamp"
  description from the previous report (chat-only, never committed to
  this file, but recorded here to close the gap).
- **Minimum safe (non-overflowing) divisor is `0xA349`, not `0xA348`.**
  `ceil(0xA3480000 / 0xFFFF) = 0xA349` (at `D2=0xA348` exactly, the true
  quotient is `0x10000`, one past the 16-bit max — this rounds down to
  `0xA348` if using floor instead of ceil, an off-by-one in the earlier
  report). Implied valid raw PAR range (8 identical samples,
  `D2=raw<<6`): **`raw` in `[0x28E, 0x3FF]`** (654-1023 decimal), not
  `[0x28D, 0x3FF]`.
- **Diagnostic PAR test (`ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1
  ASR10_DIAG_PAR_VALUE=0x300`) confirms the full propagation chain**,
  raw=0x300 chosen inside the valid range above: `D2=0xC000` (exact
  `raw<<6`) -> `divu.w` quotient=`$D9B5`, remainder=`$4000` (no overflow)
  -> `$0DD6=$C000`, `$0DF2=$D9B5` -> `andi #$f8,$fc6829` then
  `ori #$05,$fc6829` (PBDAT 0x0f->0x08->0x0d, confirming the previously-
  truncated `ori` operand) -> firmware reaches a **new, stable** terminal
  state: `"EFFECT DOWNLOAD FAILED"` then `"ERROR 032 - REBOOT ?"`
  (`troubleshoot.md`: 032 = bad download), replacing the old ERROR 130/
  PAR=0 blocker. Reproduced identically in independent 45s and 180s
  captures (both stop progressing at the same `read_count=459`,
  `fire_count=1453`). No channel/resting-value semantics are claimed for
  0x300. See `subsystems.md` for the full disassembly and byte-level
  trace.

- **ERROR 032 ("EFFECT DOWNLOAD FAILED" / "bad download") origin, fully
  traced.** With the diagnostic PAR bridge active
  (`ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1 ASR10_DIAG_PAR_VALUE=0x300`), the
  firmware reaches a new, stable blocker. Traced via live disassembly and
  targeted taps (`ASR10_ERROR_CONTEXT source=error_number_write_00c0`,
  `ASR10_TASK3_DOWNLOAD_TRACE`, `ASR10_CODE_DUMP tag=task1_error032_caller`):
  - Error-code storage: lowmem `$00C0` (already an established "current
    error number" cell — see the pre-existing `error_number_write_00c0`
    hook). Assignment site: `f88284` (`move.w D0,$00c0`), inside the ROM's
    shared common-error routine (`f88260-f882a0`, first documented in
    Phase 1B), called via `TRAP #0` with `D0` preloaded.
  - Caller: a retry loop at `ffc89e-ffc8aa` (in the `.ram()` window
    `0xfc6900-0xffffff`, i.e. genuine separate RAM, not a low-memory
    mirror): `subq.b #1,($0e9d).w` / `bpl.s $ffc884` (retry while the
    counter stays non-negative), falling through to
    `move.b #$20,D0` / `trap #0` once retries are exhausted.
  - The retried body (`ffc884-ffc8a4`) calls `jsr $fff973f0` — a
    byte-coded "record table" interpreter (ROM `f973f0-f97460+`): reads a
    record-type byte into D3 (`0xFF` = terminate), then a count/param
    pair, then loops calling further subroutines per record. For record
    types `0x01-0x04` it loads `movea.l #$fffc3001,A4` — the **same**
    FC3000-cluster window already proven runtime-active very early in
    boot (`movep-library.md`/`subsystems.md`).
  - Panel strings: `"EFFECT DOWNLOAD FAILED"` lives at ROM `f840b6`
    (0xFF-terminated) and, separately, as loaded content at lowmem `392`
    (preceded by two panel control bytes at `38e-391`); `"ERROR "` is a
    shared template at ROM `f824aa`, `"REBOOT ?"` at `f824b4` (immediately
    adjacent — the numeric part is formatted between them, not a canned
    "ERROR 032" string). No literal `"ERROR 032"` string exists anywhere
    in ROM or lowmem.
  - **Last FDC hardware status before failure, and for every single READ
    DATA (CMD 0x46) transaction in the entire run (20/20, including the
    very first, single-sector txn=1 with `EOT=1`)**: `ST0=0x40` (abnormal
    termination), `ST1=0x80` (End of Cylinder) — `read_source=
    upd72069_device`, `format=none`, `image_geometry=not_exposed`. Every
    read, regardless of requested cylinder/head/sector/EOT, terminates
    abnormally at the first sector boundary.
  - The record interpreter's own trace (`ASR10_TASK3_DOWNLOAD_TRACE`)
    shows its source pointer (`A3`, mirrored to lowmem `$0E7E`) pointing
    first into lowmem RAM (`$010722`) for one record, then into **ROM**
    (`$F9BD3E`) for subsequent records — the interpreted table is not
    exclusively disk-sourced. Record types observed (`D3` low byte)
    increment sequentially (0, 1, 2) with 10 sub-iterations each,
    consistent with well-formed (not obviously garbage) data for the
    records actually captured. The retry-counter cell (lowmem `$0E9C/9D`)
    was **not** captured due to an odd-byte-address gap in this
    session's tap (word-aligned checks only) — an acknowledged
    instrumentation gap, not a claim that no decrement occurs (the
    disassembly proves the `subq.b`/`bpl` retry loop exists).
  - **Classification (Task 5): the evidence favors a disk-read/format
    gap over an ES5510-protocol failure.** Every FDC READ DATA command
    in the run — not just ones near the failure — abnormally terminates
    with End-of-Cylinder, including a single-sector request that should
    trivially succeed. Combined with `format=none`/`image_geometry=
    not_exposed`, this points at the raw `.img` floppy-format support
    (already flagged as incomplete in `running.md`) rather than a
    protocol gap in whatever chip FC3001 belongs to. **Not fully
    closed**: no direct proof yet ties this specific interpreter
    invocation's source data to the specific failed FDC transaction.

- **Root cause of every uPD72069 READ DATA abnormal-termination/End-of-
  Cylinder result, proven from source.** `src/devices/machine/upd765.cpp`
  `read_data_continue()`, `SECTOR_READ` state (~line 2038-2055): when the
  just-completed sector equals `EOT` (`command[4]==command[6]`), the core
  checks `tc_done` (set only by a `tc_done=true` transition inside
  `tc_w(true)`, upd765.cpp:410-420). If `tc_done` is false, it sets
  `ST0_FAIL|ST1_EN` (exactly the observed `ST0=0x40/ST1=0x80`); if true,
  it completes normally. **`asr10_boot.cpp` never calls `m_fdc->tc_w()`
  anywhere** (confirmed by exhaustive grep) — only `auxcmd_w()` (vendor
  AUX commands: reset/motors/rate/precompensation, none of which are a
  TC equivalent) and `fifo_r()`/`msr_r()` are used. This means `tc_done`
  is permanently false, so **every** READ DATA command whose last
  requested sector reaches `EOT` (which is every observed command in
  this driver's usage — every read requests exactly one sector, `R==EOT`
  from the start) reports abnormal/EOC regardless of image content or
  geometry.
  - Real-hardware precedent for the missing signal: `src/mame/akai/
    mpc60.cpp` (closely related NEC uPD7206x-family FDC usage) wires a
    dedicated host I/O write address to `fdc_tc_w()`, which does exactly
    `m_fdc->tc_w(0); m_fdc->tc_w(1);` — a software-driven "TC strobe"
    port distinct from the FDC's own 4 registers. ASR-10 almost
    certainly has an equivalent board-level TC-strobe address that
    `asr10_boot.cpp` has not yet identified/wired.
  - **Directly measured for the minimal C=0/H=0/R=1/EOT=1 transaction**
    (`ASR10_FDC_CMD46` summary, new `total_fifo_reads`/`data_phase_reads`/
    `result_phase_reads` fields): `total_fifo_reads=519`,
    `data_phase_reads=512`, `result_phase_reads=7`. **All 512 requested
    data bytes are transferred in full** before the (incorrectly
    abnormal) result phase — this is not a truncated or partial
    transfer.
- **Floppy format/geometry, checked and NOT implicated as a separate
  bug.** `asr10img_format` (`src/lib/formats/esq16_dsk.cpp`, a
  `upd765_format` subclass) declares `{FF_35, DSHD, MFM, 1000, 20, 80,
  2, 512, ..., 1, ...}` — 20 sectors/track, 80 tracks, 2 heads, 512
  bytes/sector, sector IDs starting at 1. `20*80*2*512 = 1,638,400`
  bytes, an **exact** match to `floppies/asr10booth/V161.img`'s actual
  file size. `upd765_format::identify()`/`load()` (verified by reading
  the shared base class) read **flat, linear per-(track,head) sector
  data** via `read_at()` then synthesize the MFM track programmatically
  — the same convention `esqimg_format` uses for the VFX-SD/EPS-16
  family — so this format should (and, per the correct C/H/R/N result
  bytes, does) load and serve the file correctly logically.
  - **However**, direct inspection of the file and of the actual decoded
    bytes the FDC returns for C=0/H=0/R=1 (`ASR10_TASK5_SECTOR_DATA`,
    captured from the live 512-byte data phase, not read at a guessed
    file offset) shows **track 0 (both heads, file offset 0 through
    20479) is uniformly the repeating byte pair `6D B6`**. File offset
    20480 (`track=1,head=0`, per the format's own
    `(track*head_count+head)*track_size` addressing) contains
    unambiguous 68000 code (`247c fff8 2188 4eb8 ...` — a `movea.l
    #$fff82188,A3`-style pattern matching this session's other
    disassembly). Offsets at 512000, 819200 (halfway), 1000000, and the
    last 32 bytes of the file are **all** the same `6DB6` fill pattern.
  - **Corrected 2026-07-16 — retracts "unformatted"/"MFM gap-fill"
    framing.** `asr10img_format`/`upd765_format::load()` reads these
    bytes directly as **decoded sector payload** via `read_at()` (see
    source above) — it does not parse or interpret raw flux/MFM cells
    at all. Calling `6D/B6` an "MFM gap pattern" would require an
    independent flux-level analysis this session has not done. The
    accurate, unembellished statement is: **track 0 is logically
    readable and contains uniform `6D`/`B6` filler bytes; no structured
    payload has yet been identified there.** Since the full OS clearly
    already loaded successfully earlier in boot (reaching "LOADING
    SYSTEM" and later prompts) via reads that must have hit track 1+,
    this specific C0/H0/R1 read is most likely the retry loop
    (`ffc89e`, see below) probing a location whose content this session
    has not identified — not necessarily "the effect file's real
    location," and not necessarily meaningless either.
  - **Classification (Task 5, revised): the TC omission is a proven,
    100%-reproducible bug independent of image content (it would
    misreport EVEN a perfectly good read); the track-0 filler content is
    a second, separate, and also-real fact about this specific test
    image that a TC fix alone will not paper over** — after fixing TC, a
    C0/H0/R1 read would complete with *normal* status and 512 bytes of
    `6DB6` filler, which downstream firmware may or may not accept.

- **The firmware's READ DATA wrapper already tolerates ST0-abnormal +
  ST1-End-of-Cylinder as a non-error outcome.** Full, tool-verified
  disassembly (`unidasm`, not hand-decoding) of `fb8a5a-fb8aec` (the
  READ DATA / CMD46 dispatcher, confirmed by its own `move.b #$46,(A0)+`
  command-byte setup) shows its completion check is:
  ```
  fb8ace  move.b  $4c6.w,D2      ; D2 = ST0
  fb8ad2  andi.b  #$c0,D2         ; mask Interrupt Code bits
  fb8ad6  beq     fb8aec          ; IC==0 (normal) -> return, no error
  fb8ad8  btst    #7,$4c7.w       ; else test ST1 bit 7 (End of Cylinder)
  fb8ade  bne     fb8aec          ; EOC set -> ALSO return, no error
  fb8ae0  move.b  #$0d,$49d.w     ; only reaches here if abnormal AND !EOC
  fb8ae6  move.b  #$28,$4ae.w
  fb8aec  rts
  ```
  For the exact observed txn=1 result (`ST0=0x40` -> IC=`01`≠0, `ST1=0x80`
  -> bit7 set), this **takes the "no error" path** — the missing
  `tc_w()` does not, by itself, cause this specific firmware routine to
  flag a failure. The `WRITE DATA` wrapper (`fb8aee-fb8b80`, command
  byte `0x45`) and a third `READ DATA` variant (`fb8b82-fb8c10`, reads
  into D3 instead of a buffer) share the **identical** tolerant
  tail — confirming Task 3's expectation that a real TC-adjacent
  mechanism (or its absence) is handled uniformly across command
  families, not specially for CMD46.
- **The data-phase-to-result-phase transition is driven entirely by the
  MSR EXM bit dropping, not by any host byte counter.** The transfer
  loop (`fb8aa2-fb8abe`) polls `$FFFC4001` (MSR) every iteration; once
  EXM (bit 5) reads 0, it branches straight to the result-byte reader
  (`fb8ac2`/`fb8d78`) — this happens **regardless of `tc_done`**, since
  MAME's `upd765_family_device::read_data_continue()` transitions
  `main_phase` to `PHASE_RESULT` on both the `tc_done` true and false
  paths. The only host-visible counter in this loop (`D0`, preloaded to
  `0x3D0900` ≈ 4,001,536) is a generic worst-case safety timeout
  decremented on *every* poll attempt (whether or not a byte was read),
  not a 512-byte countdown.
  - **No memory-mapped access outside `$FFFC4001`/`$FFFC4003` occurs
    anywhere between the last data byte and the first result byte** —
    confirmed directly from the disassembly (every instruction in
    `fb8aa2-fb8db2` touches only those two addresses or CPU
    registers/lowmem scratch cells). No candidate TC-strobe write
    exists in this firmware revision's CMD46/CMD45 paths.
  - **The FDC transfer is confirmed genuine programmed I/O**, not
    MC68302 IDMA: the loop is a tight CPU poll-then-move-byte sequence
    with no DMA register of any kind touched, addressed, or referenced
    anywhere in the disassembled range. No MC68302 DMA register access
    was found near CMD46 dispatch in this session's captures.
  - **Implementation gate (Task 6) not met**: none of proof-A (a
    specific guest access = TC strobe), proof-B (verified MC68302 DMA
    completion drives TC), or proof-C (documented board wiring) was
    established. Per the task's own gating, `tc_w()` is **not** wired
    as a permanent implementation this session. `mpc60.cpp`'s
    `fdc_tc_w()` (`m_fdc->tc_w(0); m_fdc->tc_w(1);` on a dedicated I/O
    write) remains supporting-only precedent for a *sibling* uPD7206x
    board, not proof for ASR-10's own memory map.
- **Diagnostic-only synthetic TC experiment
  (`ASR10_EXPERIMENT_FDC_SYNTH_TC=1`) reveals a regression, not a fix.**
  Pulsing `tc_w(false);tc_w(true)` purely from the host's own verified
  512-byte data-phase count (labeled `source=synthetic_host_completion`,
  gated, never committed as a hardware claim) makes CMD46 report
  genuinely normal status (`ST0=00 ST1=00`, confirmed in the log) and,
  per the `upd765_family_device` source, causes the completed sector's
  cylinder to auto-advance (`command[2]++`) on the now-true `tc_done`
  path — result `C` changes from a constant `00` (every transaction,
  no-TC) to `01` (57 of 76 transactions, with-TC). **This does not
  progress the boot further — it regresses it**: the machine gets stuck
  repeating `"PLEASE INSERT DISK"` (76 CMD46 transactions in 45s, zero
  `"LOADING SYSTEM"` events), never reaching the previously-observed
  `EFFECT DOWNLOAD FAILED`/`ERROR 032` stage at all. This strongly
  suggests some **earlier** disk-presence/validity check depends on the
  exact previous (broken) status/cylinder-unchanged behavior, and the
  real blocker remains the track-0 content (uniform `6D`/`B6` filler,
  see above), not the FDC status path. The experiment served its
  intended diagnostic purpose (Task 6's "diagnostic fallback") and is
  not recommended for permanent use as-is.
- **Reclassification (2026-07-16): the missing FDC TC is no longer
  treated as a proven ASR-10 bug.** The cumulative evidence above — no
  TC-strobe access anywhere in the firmware, no MC68302 DMA
  participation, both the READ and WRITE wrappers explicitly coding
  "abnormal + End-of-Cylinder" as an accepted success case, and the
  synthetic-TC experiment *regressing* boot (stuck repeating "PLEASE
  INSERT DISK") rather than advancing it — is more consistent with an
  **intentional programmed-I/O completion convention** than with an
  emulation gap. Restated precisely: **MAME's `upd765_family_device`
  requires an asserted TC for a formally normal completion status, but
  the ASR-10 firmware appears designed to omit TC entirely and to treat
  End-of-Cylinder completion as the expected, successful outcome for a
  single-sector (`EOT==R`) transfer.** No permanent TC source should be
  investigated or wired until this is revisited; `tc_w()` remains
  uncalled by design pending further evidence, and
  `ASR10_EXPERIMENT_FDC_SYNTH_TC` must stay disabled by default
  (diagnostic-only, already gated off).
- **The actual disk-acceptance signature checks for the fb92ce device-
  probe sequence, fully identified (tool-disassembled).** `fb92ce`
  (called from the outer "insert disk" loop at `fb917a`) issues three
  separate single-sector reads via `fbb518`/`fbb55a`/`fbb4ec`, each into
  a different lowmem buffer, each checked against a literal ASCII
  signature at a fixed offset:
  - `fbb518` → buffer `$4FE`, `R`=1 (or 2 on retry, from `$4ED`), `EOT`=`R`.
    Checked by `fb80e8`: `cmpi.w #$4944,($26,A0)` — buffer offset `0x26`
    must equal `"ID"` (0x4944). Mismatch -> `$49d`=`0x11`.
  - `fbb55a` → buffer `$526`. Checked by `fb80c0`:
    `cmpi.w #$4f53,($1c,A0)` — offset `0x1c` must equal `"OS"` (0x4f53).
    Mismatch -> `$49d`=`0x12`.
  - `fbb4ec` → buffer `$544`. Checked by `fb809e`:
    `cmpi.w #$4452,($3fe,A0)` — offset `0x3fe` must equal `"DR"`
    (0x4452). Mismatch -> `$49d`=`0x13`.
  - A **separate, later** routine (`fb94ec`, called only after all
    three probes above already succeed) reads a fourth buffer (`$944`)
    and checks a **6-byte** signature at buffer offset `0x10` (`fb95e0`:
    `cmpm.w` loop against ROM `$fb90ac-$fb90b1` = bytes
    `49 33 32 35 56 4d`, i.e. ASCII `"I325VM"`). Mismatch ->
    `$49d`=`0x25` (`fb9614`).
  - Each of the three checks in `fb80xx` is itself gated on `$49d==0` at
    entry — if the previous probe already failed, later probes skip
    their own comparison entirely (this reproduces the FDC-family
    read-then-verify-signature pattern seen with `"NO SCSI DEV"` /
    `"R SCSI DEV\0\0"` text immediately preceding the `"I325VM"` table in
    ROM — these are almost certainly a **generic boot-device probe**
    trying floppy/SCSI/other candidates in turn, not the primary OS
    loader).
  - **Directly confirmed: the `"ID"`/`"OS"`/`"DR"` signature bytes are
    genuinely examined against our C0/H0/R1(/R2/R3...) sector content**
    (Task 2's question). Since that content is uniform `6D`/`B6` filler
    at every offset checked, **every one of these comparisons fails
    identically in both the no-TC and synthetic-TC captures** — the
    signature checks themselves are not sensitive to FDC status at all,
    only to buffer content.
- **The no-TC vs. synthetic-TC divergence is NOT the signature checks —
  it is an anomaly in the FDC's own result registers under the
  synthetic pulse.** Direct comparison of the first 4 CMD46 transactions
  (`decoded_R`=1,2,3,4 requested in both captures identically):
  - **No-TC**: `result_R_byte` tracks the requested `R` each time
    (1,2,3,5) and `result_C_byte` stays `00` throughout — consistent
    with `fi.pcn`/`command[]` being freshly matched to each new command.
  - **Synthetic TC**: `result_C_byte` jumps to `01` after the first
    completed transaction and **stays there**; `result_R_byte` gets
    **stuck at `01`** for the next two transactions despite `R`=2 and
    `R`=3 being freshly requested each time. This does not match either
    "real hardware" or "no-TC" behavior, and is most plausibly an
    artifact of this session's simplistic host-byte-count-triggered
    `tc_w()` pulse interacting awkwardly with `upd765_family_device`'s
    internal `command[]` array (which the source shows is mutated
    in-place — `command[2]++; command[4]=1;` — on the `tc_done`-true
    completion path), **not evidence about how real, correctly-timed
    hardware TC would behave.** This stuck-register anomaly is very
    likely why the synthetic-TC full-boot run reached additional,
    different signature checks (`0x13` at `fb80b4`) that the no-TC run
    never reached, and should not be read as "TC breaks disk
    acceptance" in any hardware-meaningful sense.
  - **Conclusion for Task 1**: the direct branch selecting accepted vs.
    rejected for a single probe is the `cmpi.w`/`cmpm.w` signature
    compare in `fb80e8`/`fb80c0`/`fb809e`/`fb95e0` — ST0/ST1/ST2 are
    **not** inputs to that comparison at all; only the returned C/H/R/N
    matter insofar as a stuck/wrong `C` (an observed *side effect* of
    the synthetic-TC pulse, not of TC itself) could route a later probe
    to compare against the wrong buffer/cylinder. This is a diagnostic
    artifact, not a hardware finding.
- **Task 4/6 (early read sequence, image identity)**: every CMD46
  transaction observed in both captures uses `C=0,H=0`, `N=2` (512
  bytes/sector), and monotonically increasing `R`/`EOT` (single-sector
  reads, `R==EOT` always) — consistent with the `fb92ce` device-probe
  sequentially trying sectors 1, 2, 3, 4, 5... of track 0, cylinder 0,
  head 0 looking for a recognizable boot/device signature, not with a
  directory or FAT-style structure. Every probed sector's payload is
  the same uniform `6D`/`B6` filler (see the disk-signature evidence
  above and the earlier track-0 audit), so **classification F (disk
  data is correct and ERROR 032 is a later, separate subsystem issue)
  cannot yet be fully distinguished from classification E (wrong disk
  image/track for this probe)** — no independent evidence identifies
  whether track 0 is *supposed* to hold real boot-device descriptors on
  a genuine ASR-10 boot disk, or whether `V161.img`'s repository
  provenance marks it as something else (e.g. an OS-only or effects-only
  image where track 0 is legitimately blank). This remains open.

## 3. Plausible hypotheses (unproven)

- **FC2001 is (or is modeled on) an ES5506/ES5505-family device**, based
  on the register-offset match and the shared MOVEP convention with the
  FC3000 cluster. Not proven: no ASR-10-specific address_map precedent
  exists anywhere in MAME (`esqasr.cpp`'s own skeleton wires zero devices
  into its address map), so the CPU-side base-address wiring is inferred,
  not cited.
- **The three static clusters (FC2001-relative, FC2D40-FC2D7C,
  FC3001-FC31xx) belong to one device family**, given the shared MOVEP
  idiom — explicitly **not** merged into one device per instruction, since
  no common initialization, paging, or device-select evidence has been
  found. Classification remains E (unresolved) leaning D (one device +
  glue latches) per the fit-matrix in the ES5701 wiring study.
- **PAR-style host-port semantics would explain the "8 time-spaced
  samples" pattern architecturally** (real hardware would read a slowly
  varying analog/external value 8 times and scale-accumulate it) — this
  is consistent with, but not proof of, the real ASR-10's design intent.

## 4. Disproven / retracted

- **Global FF/low RAM alias/mirror model.** Retracted in `architecture.md`
  §1.2/§10 — a clean non-aliased run proves low (`00xxxx`) and high
  (`FFxxxx`) are separate loader destinations/backings, not mirrors. The
  alias-induced `CA7E → FC6000 → ERROR129` cascade was an artifact of this
  retracted model, not a clean-run blocker.
- **`FF0AE2` as a dispatch table entry / `89a2` signed-offset dispatch
  model.** Retracted in `architecture.md` §8/§10 — `FF0AE2` is never
  written in a clean run; `img[0x30e2]` is proven low-system callback data
  (`$00E2`/`$00E6`), unrelated to node type `89a2`.
- **"8-sample average."** Explicitly retracted this session (per direct
  instruction) — the routine at `00686e` performs a **scaled accumulation**
  (`asl.w #6` then `lsr.w #3` per sample, summed into D6 across 8
  iterations), not a divide-by-8 average. No divide-by-eight instruction
  has been observed anywhere in the traced routine.
- **"Zero literal `$fc60xx` references in the boot ROM" (movep-library.md,
  prior session).** Retracted this session as a search-methodology error:
  a differently-targeted literal search missed the `4eb9 fffc 60b0`
  (absolute-long `JSR`) encoding actually used by the four static callers
  at `f8db04/24/36/52`. Not a claim that new hardware was found — the
  ROM content was always there; the earlier scan simply didn't match it.
- **FC222E/FC226E block copies.** Cannot be verified — exhaustive
  disassembly of the entire static ROM (`0xf80000`-`0xfbffff`, ~97,000
  instructions) found **zero** literal references to either address, and
  bounded runtime taps (32-access cap) recorded **zero** hits on either
  address in the 45s run. This is listed under retracted/unconfirmed
  rather than proven, since the evidence this session directly
  contradicts being able to locate or trace it. See §5 for the open
  question this leaves.
- **TRAP #7/#8 as "sleep/yield" (name-based inference).** Retracted as a
  *naming* guess and **promoted to §1 Proven facts** with the actual
  mechanism: TRAP #7 is a scheduling primitive (dispatcher-entry via
  `bra`), TRAP #8 is a synchronous state write (via `rte`). Neither is a
  blocking wait in the OS sense.
- **install_read_tap as a precise per-instruction hook for opcode
  fetches.** Attempted and disproven this session: `m68kmusashi.h:213`
  types `m_oprogram32` as a `::cache` accessor (fast-path, bypasses
  passthrough-tap dispatch), while `m_program32` (data reads/writes,
  line 216) is `::specific` (dispatch-backed, tappable). Confirmed
  empirically — four opcode-fetch taps at proven-executed addresses
  (`f8834a`, `f88352`, `006800`, `00680a`) recorded zero hits across two
  separate runs, while data-read taps at the same session's other
  addresses fired correctly.

- **MC68302 Port B bits 2:0 as an ES5506 PAR analog-mux channel select.**
  Tested via `ASR10_EXPERIMENT_MC68302_GPIO_TRACE=1` (Phase 2, Stage 2) and
  **not supported**: bits 2:0 are written exactly once at boot (`fb8e2e`,
  value `0b111`) and never rewritten again in either a 45s or a longer
  capture. The routine at `0067f6` (previously guessed as a "bits 0-2
  strobe set") executes exactly once (`fire_count=142`) and, empirically,
  only ORs in bit 3 (the separately-tracked LRCLK candidate); it does not
  touch bits 2:0. Every observed `00686e`/`FC60B0` PAR-measurement pass
  (fire_counts 146/150/154/158/162/166/170/174, identical across both
  captures) reads bits 2:0 as a constant `7` and PAR as a constant `0`. The
  required evidence for this hypothesis — at least two distinct bits-2:0
  values, each stable during its own measurement pass — does not exist.
  Verdict: **B** (GPIO outputs confirmed, no PAR correlation observed).
  Stage 3 (diagnostic analog-mux model) was not implemented, since it is
  gated on Stage 2 passing.

## 5. Unresolved contradictions / open gaps

- **FC222E/FC226E**: given as "proven evidence" in a prior instruction,
  but this session's static (full-ROM) and runtime (bounded-tap) search
  both return zero hits. Either the routine lives in a boot phase never
  reached before the divide-by-zero crash (most likely, consistent with
  FDC_OS also never firing), or the addresses/context were from a
  different investigative branch not available here. Flagged, not
  guessed.
- **Static callers for 19 of the 20 MOVEP library thunks are unknown** —
  only `FC60B0`'s caller (`00686e`) is proven. Whether the FC3000 cluster
  and FC2001 cluster share a common caller family, a shared A0-load site,
  or are fully independent remains open (see `subsystems.md`).
- **Whether ASR-10's real board wires ES5506 at FC2xxx via the odd-lane
  convention is inferred, not cited** — the mathematical chain (§1, Task 2)
  proves the *register arithmetic* is consistent with that wiring; it does
  not prove the *board* actually did it that way. No ASR-10-specific
  schematic or MAME driver confirms the base-address choice.
