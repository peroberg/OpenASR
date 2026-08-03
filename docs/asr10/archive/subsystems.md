# ASR-10 Subsystem Graph — Relations, Not Addresses

Purpose: group the known code families into functional blocks and show how
control/data flows between them, plus the three clock domains involved.
Addresses are cited only to anchor each block to its evidence — the graph
itself is about relationships.

## Functional blocks

```
┌─────────────────────┐
│ f87exx               │  Scheduler core (dispatcher, 6-slot table at
│ scheduler core        │  $23d4-$2458, trap #2/3/4/6/9/D/10/11/12
│                       │  release/alloc/post/promote semantics).
└──────────┬───────────┘  Proven: architecture.md §5-6.
           │ TRAP #7 (bra, no rte) re-enters dispatcher body directly
           │ TRAP #8 (rte) writes active-slot+0, returns to caller
           ▼
┌─────────────────────┐
│ f883xx               │  Tick engine (f88300): STOP-counter ack, walks
│ tick engine           │  primary slot table (stride 0x16, +0=countdown,
│                       │  +0x14=threshold, +2=pending bit0), then every
│                       │  10th tick ($0b82 mod-10) walks the secondary
│                       │  table (stride 0x1a, +0x14=countdown,
│                       │  +0x16=32-bit callback ptr).
└──────────┬───────────┘  Proven: this session, full disassembly.
           │ jsr (A1) at f88352, when a secondary entry's own
           │ countdown reaches exactly 0
           ▼
┌─────────────────────┐
│ f88bxx-f890xx         │  Callback bodies invoked from the secondary
│ (armed callbacks)      │  table. NOT independently disassembled this
│                       │  session — flagged as unverified membership,
│                       │  included here only because it's the obvious
│                       │  address-range neighbor of the tick engine and
│                       │  the finalizer/armed-callback blocks below.
└──────────┬───────────┘  [OPEN] — no direct evidence gathered this
           │                session; do not cite as proven.
           ▼
┌─────────────────────┐
│ f8cexx                │  Slot2 finalizer (f8ce3a/f8ce4c, per prior
│ finalizers             │  session's hint — not re-verified this
│                       │  session).
└──────────┬───────────┘  [OPEN] — carried forward from task context,
           │                not independently confirmed here.
           ▼
┌─────────────────────┐
│ f8dbxx                │  Callback armed by the rate/measurement
│ armed callbacks        │  routine (f8db00-f8db4e, per prior session's
│                       │  hint) — [OPEN], not re-verified this session.
└──────────┬───────────┘
           │
           ▼
┌─────────────────────┐
│ f973xx-f977xx         │  Calibration + GPIO + counter-adjacent code.
│ calibration/GPIO       │  PROVEN runtime-active this session: 32 writes
│                       │  to FC3000-FC31FF cluster, PC range
│                       │  f9734e-f973aa, using the SAME MOVEP-via-A0
│                       │  convention as the FC2001 library
│                       │  (`f97360: movep.l D0,($0,A0)`). Runs very
│                       │  early in boot (log line ~460), no temporal
│                       │  link to the divide-by-zero (line ~394000).
└──────────┬───────────┘  Proven: ASR10_CLUSTER_TRACE event=fc3000_write.
           │
           ▼ (separately, NOT shown as sequentially connected --
             see caveat below)
┌─────────────────────┐
│ 0x67xx-0x68xx          │  OS measurement routine. PROVEN: `00686e`
│ OS measurement         │  (moveq #7,D7 loop) calls FC60B0 eight times
│                       │  (fire_counts 149..177 in one capture; 146-174
│                       │  in another), asl.w#6/lsr.w#3/accumulate into
│                       │  D6, `move.w D6,D2` at end.
└──────────┬───────────┘  Proven: full live disassembly, `00686e-00689c`:
           │              ```
           │              00686e  moveq   #7,D7
           │              006870  moveq   #0,D6
           │              006872  movem.l D6-D7,-(A7)   ; loop top
           │              006876  move.w  #4,D0
           │              00687a  trap    #8
           │              00687c  moveq   #0,D0
           │              00687e  trap    #7
           │              006880  movem.l (A7)+,D6-D7
           │              006884  movea.l #$fc2001,A0
           │              00688a  jsr     $fffc60b0
           │              006890  asl.w   #6,D2
           │              006892  lsr.w   #3,D2
           │              006894  add.w   D2,D6
           │              006896  dbra    D7,$006872
           │              00689a  move.w  D6,D2
           │              00689c  rts
           │              ```
           │              The loop body invokes TRAP #8 then TRAP #7
           │              (D0=4, then D0=0) **between every sample**,
           │              i.e. it cooperatively yields to the dispatcher
           │              once per iteration — this is why each of the 8
           │              PAR samples lands several ticks apart rather
           │              than in a tight loop, and why unrelated bus
           │              traffic (e.g. the f8cf00-f8d010 voice-init
           │              burst) is observed interleaved between samples
           │              in the same capture.
           │ FC60B0 thunk: movep.l ($68,A0),D2, A0=FC2001. **Also has
           │ four additional static ROM call sites** (f8db04/24/36/52,
           │ see movep-library.md) that never fire in any capture taken
           │ so far — retracts the earlier "zero literal ROM callers"
           │ claim.
           ▼
┌─────────────────────┐
│ divider                │  `006800-006820`, fully disassembled and
│                       │  **corrected 2026-07-16** (the branch at
│                       │  `00680c` is `BVC`, not `BNE` — re-verified
│                       │  directly from the live opcode word `$6804`:
│                       │  `0110 1000 00000100` = Bcc class, condition
│                       │  field `1000` = VC, displacement `+4` ->
│                       │  `006812`):
│                       │  ```
│                       │  006800  move.w  D2,$0dd6      ; raw rate param
│                       │  006804  move.l  #$a3480000,D0
│                       │  00680a  divu.w  D2,D0          ; FAULTS if D2=0
│                       │  00680c  bvc.s   $006812         ; V clear (no overflow) -> store directly
│                       │  00680e  move.w  #$ffff,D0      ; V set (overflow) -> clamp to $FFFF
│                       │  006812  move.w  D0,$0df2       ; final divider result
│                       │  006816  andi.b  #$f8,$fc6829   ; clear PBDAT bits2:0
│                       │  00681e  ori.b   #$05,$fc6829   ; set PBDAT bits2:0=5 (confirmed
│                       │                                    by live execution, see below)
│                       │  ```
│                       │  Semantic distinction (corrected): divide-by-zero
│                       │  is a full CPU exception (excluded entirely, not
│                       │  reachable via this branch); the clamp to $FFFF
│                       │  fires on **quotient overflow** (divisor too
│                       │  small for the quotient to fit in 16 bits), not
│                       │  on a zero quotient. This is the classic "avoid
│                       │  an unrepresentable reload count" pattern,
│                       │  consistent with $0DF2 feeding a hardware
│                       │  **timer/period reload value**. $0DD6 keeps the
│                       │  raw (unscaled-by-divide) rate parameter.
│                       │
│                       │  **Confirmed live** via the gated diagnostic PAR
│                       │  test (`ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1
│                       │  ASR10_DIAG_PAR_VALUE=0x300`, NOT a channel/
│                       │  resting-value claim): raw=0x300 -> D2=0xC000
│                       │  (matches `raw<<6` exactly) -> `divu.w` computes
│                       │  quotient=$D9B5, remainder=$4000 (no overflow,
│                       │  `bvc` taken) -> `$0DD6=$C000`, `$0DF2=$D9B5` ->
│                       │  `andi #$f8,$fc6829` (PBDAT 0x0f->0x08) ->
│                       │  `ori #$05,$fc6829` (PBDAT 0x08->0x0d, bits2:0=5,
│                       │  confirming the previously-truncated operand) ->
│                       │  firmware proceeds to a **new** blocker (see the
│                       │  MC68302-ports box below).
└──────────┬───────────┘  Proven: exception frame + this session's live
           │                disassembly (`ASR10_TASK2_00686E_DUMP`,
           │                `ASR10_FC681X_CODE_DUMP`) + live diagnostic
           │                run (`ASR10_DIVIDER_TASK2`, `ASR10_GPIO_STAGE1`).
           │ (immediately after the divide, on the non-faulting path)
           ▼
┌─────────────────────┐
│ MC68302 ports          │  `andi.b #$f8,$fc6829` / `ori.b #$05,$fc6829`
│                       │  (bits 2:0 clear-then-set-to-5, i.e. 0b101 --
│                       │  a THIRD distinct PB2:0 value, different from
│                       │  the constant 0b111 observed throughout every
│                       │  unmodeled-harness capture) immediately follow
│                       │  the divide.
│                       │
│                       │  **Now confirmed executing**, via the gated
│                       │  diagnostic PAR test (raw=0x300, in the
│                       │  mathematically-valid non-overflow range, see
│                       │  the divider box above): PBDAT goes
│                       │  0x0f->0x08 (`andi #$f8`)->0x0d (`ori #$05`,
│                       │  bits2:0=5, confirming the address operand).
│                       │  Immediately after (fire_count=206, a
│                       │  previously-unreached point), the adjacent
│                       │  `006840-00686c` routine runs its own
│                       │  `andi #$f8`/`ori #$00` pair on the SAME byte
│                       │  (PBDAT 0x0d->0x08->0x08, bits2:0=0 -- a
│                       │  **fourth** distinct value), immediately
│                       │  before falling into `00686e`'s next
│                       │  measurement pass. No physical/channel meaning
│                       │  is claimed for any of these values.
│                       │
│                       │  Firmware then proceeds past this point (up to
│                       │  fire_count=1453 observed, both 45s and 180s
│                       │  captures reach the identical stable end state)
│                       │  to a **new terminal blocker**: panel text
│                       │  progresses through "LOADING SYSTEM"->
│                       │  "EFFECT DOWNLOAD FAILED"->"ERROR 032 - REBOOT ?"
│                       │  (`troubleshoot.md`: 032 = "bad download",
│                       │  digital-board-class error), then idles waiting
│                       │  for a reboot-confirm keypress that never comes
│                       │  in this headless harness. This replaces the
│                       │  old ERROR 130/PAR=0 blocker entirely for this
│                       │  configuration.
└──────────┬───────────┘  Proven: live diagnostic run, `ASR10_GPIO_STAGE1`,
           │                `ASR10PANEL text=...`. Reproduced identically
           │                in independent 45s and 180s captures.
           │ (hypothesized only, for the general/uninjected case)
           ▼
┌─────────────────────┐
│ external device window │  FC2001-relative MOVEP library (20 thunks,
│ (FC2xxx)               │  see movep-library.md). PROVEN runtime-read
│                       │  path (FC2069/6B/6D/6F, all zero). Register-
│                       │  offset match to ES5506 PAR/IRQV/PAGE is
│                       │  PROVEN math (es5506-chain-verification.md),
│                       │  board wiring is HYPOTHESIS only.
└───────────────────────┘
```

**Caveat on the f973xx→0x67xx arrow:** these two blocks are drawn adjacent
above because they're both "calibration/measurement"-flavored and both use
the MOVEP-via-A0 convention, but **no evidence in this session shows one
calls the other, or that they share a common caller.** The FC3000 cluster
activity is temporally isolated to very early boot; the FC60B0 measurement
pass happens much later, at fire_count 149-177. Treat them as parallel,
independently-triggered subsystems sharing a bus convention, not a proven
sequential pipeline, until a shared caller or shared A0-load site is
found.

## Three clock domains

1. **CPU clock (16 MHz nominal, `M68000(config, m_maincpu, XTAL(16'000'000))`
   in `esqasr.cpp` — the only clock citation available for this CPU in any
   driver in the tree; our own `asr10_boot.cpp` does not itself declare an
   XTAL for the maincpu in a way surfaced this session).** Drives
   instruction execution throughout every block above.

2. **DUART timer tick (provisional, ~1.085ms period in this experiment;
   `docs/asr10/current-blocker.md`-adjacent sessions established the
   IRQ6/vector-56 delivery chain, this session established the periodic
   re-arm model).** This is the clock that **drives** the tick engine
   (`f88300`) — every terminal-count fire is one entry into the primary
   slot walk, and every 10th such tick drives one pass of the secondary
   table (and hence, indirectly and intermittently, the OS measurement
   routine at `00686e`, whichever secondary-table entry's callback that
   turns out to be — not directly proven, since the specific secondary
   entry's callback pointer at the moment of the fatal call was not
   captured; see `evidence-tree.md` §5).

3. **Device sample clock (hypothetical, for whatever real chip underlies
   FC2001).** This is what the measurement routine is trying to
   **measure** — the entire purpose of `00686e`'s 8-sample scaled
   accumulation, read via FC60B0, appears architecturally consistent with
   measuring a *rate* derived from this domain (per the task's own
   framing: "measured rate parameter"). Not proven: no evidence exists in
   this session that FC2001 has a real, ticking clock behind it in the
   current harness (it's plain `.ram()`, returns constant 0 — see
   `evidence-tree.md` §2).

**Summary relation:** `DUART timer tick` **drives** `tick engine` **drives**
(every 10th tick) `secondary-table callbacks` **possibly-drives**
`OS measurement routine` **reads** `external device window` (intended to
measure the `device sample clock` domain, but currently reads constant
zero) **feeds** `divider` (which faults when that zero propagates through
unchanged) → (on the non-faulting path, unreached in any capture)
`MC68302 ports`.
