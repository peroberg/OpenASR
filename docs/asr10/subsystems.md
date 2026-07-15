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
│                       │  (fire_counts 149..177), asl.w#6/lsr.w#3/
│                       │  accumulate into D6, `move.w D6,D2` at end.
└──────────┬───────────┘  Proven: this session, live dump + disasm.
           │ FC60B0 thunk: movep.l ($68,A0),D2, A0=FC2001
           ▼
┌─────────────────────┐
│ divider                │  `006800-00680e`: move.w D2,$0dd6 (store
│                       │  rate param) ; move.l #$a3480000,D0 ;
│                       │  divu.w D2,D0  <- FAULTS when D2=0.
└──────────┬───────────┘  Proven: this session, exception frame +
           │                live disassembly.
           │ (immediately after the divide, on the non-faulting path)
           ▼
┌─────────────────────┐
│ MC68302 ports          │  `andi.b #$f8,$fc6829` / `ori.b #$05,$fc6829`
│                       │  immediately follow the divide in the routine
│                       │  given by the task context — NOT independently
│                       │  re-disassembled this session (the fault
│                       │  prevents this path from executing in any
│                       │  captured run). [OPEN].
└──────────┬───────────┘
           │ (hypothesized only)
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
