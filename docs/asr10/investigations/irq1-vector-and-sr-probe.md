# IRQ1 Vector And SR-Mask Probe

**Superseded for root cause:** `irq1-handler-chain-probe.md` measured the
actual crash directly — a genuine 68000 Address Error from an
uninitialized `$0402` continuation pointer, reached two steps after SIS.
The SR-mask/"late SEEK" framing below was inference from timing gaps, not
a direct measurement, and Del 2's `[Likely]` guess about the source is
superseded by that document's direct `ST0` evidence. The vector-delivery
result (`$51`, correct) and the tap-lifetime finding below both stand.

## Scope

Follow-up to `irq1-storage-completion-probe.md` and
`irq1-imr-unmask-probe.md`. Two remaining questions before trusting the
`ERROR 129 - REBOOT` failure as a timing story rather than a vector
mixup:

1. When the naive `FDC INTRQ -> MC68302 IRQ1` wiring drives boot into
   `ERROR 129`, does the IACK actually fetch vector `$51`, or did the
   original falsification measure the wrong thing?
2. If it does fetch `$51`: is the CPU's own SR interrupt-priority mask the
   gate — specifically, does it stay closed through the whole window from
   RECALIBRATE completion until firmware clears INTRQ via SENSE INTERRUPT
   STATUS, *and* does that clear happen before the mask opens?

Method only. The naive wiring
(`m_fdc->intrq_wr_callback().set_inputline(m_maincpu, 1)`) was reintroduced
for these two measurements only, in the same session, and removed again
immediately after. Nothing was built.

## Del 2 — Which Vector Was Actually Fetched

`docs/asr10/lua/archive/irq1_vector_probe.lua` taps
`cpu.spaces["cpu_space"]` (the m68k IACK bus,
`m_cpu_space_config("cpu_space", ...)` in `m68000.cpp` — confirmed by
listing available space names, not guessed) at the level-1 IACK cycle and
reads the vector byte the CPU actually receives, plus the vector-table
target it dereferences to.

### Calibration trap: the tap offset is not the driver's byte address

First attempt filtered on offset `0xfffff3`, matching the byte address
`asr10_boot_state::cpu_space_map()` uses for its own level-1 `.lr8()`
handler. Zero events, despite the build unambiguously reaching
`ERROR 129`. `docs/asr10/lua/archive/irq1_vector_probe_calibration.lua`
widened the tap to log every offset seen, unconditionally:

```text
CALIB3_READ t=15.032440 offset=FFFFFC data=56 mask=FFFF
CALIB3_READ t=15.032527 offset=FFFFF2 data=51 mask=FFFF
```

`cpu_space` is a 16-bit-wide address space (`ENDIANNESS_BIG, 16, ...`); the
Lua tap reports the word-aligned base address, not the odd byte address the
driver's single-byte handler is installed at, with the actual value in the
low 8 bits of `data` (big-endian: the odd byte is the low byte of its
containing word). Level 6 at offset `FFFFFC` (word base of driver byte
address `fffffd`) returning `$56` is the already-known-correct DUART vector
— a positive control confirming the calibration, not just a guess. Level 1
is offset `FFFFF2` (word base of `fffff3`).

### Result

With the calibrated offset:

```text
IRQ1VEC t=15.032527 vector=51 target_addr=000144 target=FFFF87CE pc_before=FFA2A2 pc_after=FF87CE sr=2100 mask=1
IRQ1VEC_SUMMARY total_iack1=1 vector_51_count=1 other_vector_count=0 final_display="ERR0R 129 - REB00T    "
```

[Verified dynamic] Exactly **one** level-1 IACK occurs in the entire run,
at `t=15.032527s`, fetching vector `$51`. `target_addr=$000144` is
`$51*4`, correct. `target=$FFFF87CE` is exactly the entry chain documented
in `storage-completion-dispatch.md`
(`vector $51 -> $FFFF87CE -> binding slot $87CE.w -> jmp.l $00BAB6 -> $F114B6`)
— the CPU jumps to `$FF87CE`, which is not the final dispatcher but the
first hop of the already-documented chain toward it. `pc_before=$FFA2A2`
places execution in loaded high-RAM/OS code at the moment of interrupt, not
ROM — the OS is already resident by this point, well before `FILE 1`
displays.

**Del 2 answer:** yes, the fetched vector was `$51`. The `ERROR 129`
failure in `irq1-storage-completion-probe.md` was not a wrong-vector
artifact. That falsification's conclusion is not retracted; it is now
better supported, not just assumed.

## Del 3 — Is The SR Mask The Gate

### Instrument bug: an unreferenced tap handle gets silently reclaimed

First version of the combined FDC-dialogue / mask-transition / IACK probe
called `cpu_space:install_read_tap(...)` without keeping the returned
handle in a variable. Result: the IACK tap fired **zero** times in that run,
while an independent tap in the same script (FDC FIFO writes, whose handle
*was* kept as `local fdc_tap = ...`) worked normally throughout, and the
same IACK event was confirmed to have actually happened via the separate
Del 2 script in the identical build. This is a different failure mode from
`irq1-imr-unmask-probe.md`'s dynamic-reinstall trap — here nothing
reinstalled the handler, the Lua garbage collector reclaimed an object
nothing referenced. Logged in `methods-static-analysis.md` §8.6. Fixed by
keeping `local irq1_tap = cpu_space:install_read_tap(...)`.

### Result

`docs/asr10/lua/archive/irq1_sr_mask_probe.lua`, corrected, run from reset
through the crash, logging every FDC FIFO byte at `$FC4003`, every SR
mask-level transition (5ms polling), and the IACK event:

```text
SRPROBE_MASK_INIT t=0.000000 sr=0000 mask=0
SRPROBE_MASK_EDGE t=0.005000 pc=F89C54 sr=2700 mask=0->7
SRPROBE_IACK1     t=15.032527 vector=51 target=FFFF87CE pc=FFA2A2 sr=2100 mask=1
SRPROBE_FDC       t=15.032592 pc=FB8CF2 value=08
SRPROBE_SUMMARY   final_display="ERR0R 129 - REB00T    " t=15.440000
```

[Verified dynamic] Mask goes to `7` (fully closed, all non-NMI levels
blocked) at `t=0.005s` and **no further transition is observed** at 5ms
resolution for the rest of the ~15.4s run — the single IACK is the only
break in that pattern, and by 68k semantics the mask *must* have been `0`
in the instant(s) immediately before it (a level-1 request needs
mask `<1` to be taken at all) and became `1` at the moment of acceptance
(observed `sr=$2100`, matching).

[Verified dynamic] A single FDC byte `$08` (SENSE INTERRUPT STATUS) is
written 65 microseconds after the IACK, at the shared low-level
FDC-byte-sender PC `$FB8CF2` (the same return PC every FDC command byte in
this build goes through, regardless of caller — not a distinguishing
signal by itself). Combined with `pc_after=$FF87CE` from Del 2 (the CPU
provably left its prior execution context and entered the documented
vector-`$51` chain), this SIS write is consistent with being the *handler's
own* first action (`$F114B6`'s FDC branch calls `$FB7C5A`, which sends `08`)
— i.e. a **response to** the interrupt being taken, not a **precondition
for** it.

[Verified dynamic] The full FDC dialogue from `t≈3.7s` to `t≈15.0s` is not
two isolated RECALIBRATEs with idle time between them, as
`irq1-storage-completion-probe.md`'s narrower probe suggested. It is
**continuous** activity: RECALIBRATE at `t≈3.7s`, then a long repeating
`08 (SIS), 46 ... (READ DATA)` sequence — the OS's own multi-sector disk
load — running essentially back-to-back through `t≈15.0s`, including SEEK
commands near the end (`t≈14.15s`, `t≈14.61s`). The boot's own polled loop
issues SIS routinely as part of *its own* normal operation, not only after
RECALIBRATE.

### Answer

The task's two-condition hypothesis, taken literally, is **`[DISPROVEN]`**:
condition 2 ("firmware clears INTRQ via SIS *before* the mask releases")
does not hold as stated — the observed SIS write happens *after* the IACK,
as the handler's reaction, not as a precondition that opens the mask.
Partial answers don't clear the bar the task set, and this is a real
partial failure of the hypothesis as framed, not a rounding error.

What the same measurements do support, stated at its own (weaker, but
real) evidence level:

- **[Likely]** The CPU's SR mask is closed (effectively `7`) for
  essentially the entire boot-time window observed, opening only
  transiently (under 5ms, this instrument's resolution floor) at the one
  point an interrupt actually got taken. This is consistent with "mask
  closed most of the time" but is not a gapless proof — the 5ms polling
  cannot rule out other sub-5ms openings elsewhere in the run that
  happened to find nothing pending.
- **[Likely]** Because the FDC was continuously active with its own
  independent SIS calls throughout `t≈4s`-`t≈15s` (each of which would
  clear device-side INTRQ per the already-verified device model), the
  pending interrupt finally taken at `t=15.03s` more plausibly traces to
  the FDC's *last* completion event before that point — a SEEK near
  `t≈14.6s`-`t≈15.0s` — than to the original `t≈3.7s` RECALIBRATE this
  investigation started from. **`[OPEN]`**, not measured directly; flagged
  so it is not conflated with the `[Verified]` facts above.
- **[DISPROVEN by later measurement]** This paragraph originally guessed
  that `ERROR 129`'s proximate cause was the SIS check's `ST0 & $E0 == $20`
  comparison failing against stale status. Wrong on two counts, both
  settled by direct measurement in `irq1-handler-chain-probe.md`: the
  comparison's outcome does not gate anything — the dispatcher proceeds to
  `jmp [$0402]` regardless of whether SIS reports success or failure — and
  `ERROR 129` itself is not an application-level branch at all but a
  genuine 68000 Address Error exception from that `jmp` dereferencing an
  uninitialized pointer. And `ready-line-artifact-probe.md` further found
  the specific `ST0` value observed (`$C8`) was never a RECALIBRATE/SEEK/
  READ-DATA status to begin with — a ready-line-change artifact, not stale
  command state. Kept for the record per the standing rule that disproven
  hypotheses stay visible, not deleted.

## Competing Hypothesis — Recorded, Not Investigated

Per instruction, recorded and left open rather than chased in this run:
that FDC INTRQ reaching a CPU interrupt line at all may be the wrong
premise. Vector `$4B` is IDMA completion; vector `$51` is storage
completion. It is possible `$51`'s real physical trigger is IDMA's own
completion signal, not FDC INTRQ — in which case every INTRQ-routing
attempt in this line of investigation, including this one, has been
answering a misframed question. Not evaluated here.

## Verification

- `make SUBTARGET=mame -j4`: clean, both with the temporary wiring and
  after reverting it.
- `docs/asr10/regression-test.sh`: 5/5 before, 5/5 after. The temporary
  wiring was in place only during the two measurement runs, never during a
  regression run.
- No `mem_map` changes. No `-log`. No `getenv`. No commits made.
- `git diff --check`: clean.
- Tree state at end: naive wiring removed again, matching
  `irq1-storage-completion-probe.md`'s disposition exactly.

## Line Count

- C++: net zero (the board-policy line was reintroduced and removed again
  within this session; the comment explaining the disposition grew by a
  few lines, replacing the previous comment).
- `docs/asr10/lua/archive/irq1_vector_probe.lua`,
  `irq1_vector_probe_calibration.lua`, `irq1_sr_mask_probe.lua`: written,
  run, archived — their questions are answered.
