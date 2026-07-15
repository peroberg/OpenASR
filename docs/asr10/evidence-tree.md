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
