# MC68302 Consolidation, Continued: Closing The Exception Guard, Tracing Vector Origin, The Five Items

## Scope

Continuation of `mc68302-consolidation.md`. Close `[OPEN]` for synchronous
exceptions where the evidence supports it, determine whether `$51`/`$56`
actually derive from GIMR state or only coincide with it, investigate
(not rebuild) five specific mc68302 model debts, and extend the 7th
regression test rather than add an 8th. No new ASR-10 functionality.

## Del 1 — Closing `[OPEN]` For Synchronous Exceptions

### Technique: tap the handler, not the vector table

`exceptionpoint_set` is still not Lua-reachable (unchanged from the
previous task). A tap on the vector table still drowns in data traffic.
But an instruction fetch is a program-space read, so a read tap on a
handler's own **first word** fires exactly when that handler executes,
without the vector table's contamination problem — code memory is not
the periodically-reused low-RAM region the vector table turned out to
be.

### Calibration, against a known case

Per instruction: verify before trusting. The naive IRQ1 wiring (ready-
line fix reverted) is the known case that produces `ERROR 129`, a
genuine vector-3 Address Error — but it happens **before** `FILE 1`
(inside the OS's own polled boot-time disk load), so the main probe's
`FILE 1` precondition can't be reused for calibration; a dedicated
calibration script with a fixed short wait was used instead.

Temporarily reverted `m_fdc->set_ready_line_connected(false);` in
`asr10_boot.cpp`, rebuilt, ran
`docs/asr10/lua/archive/sync-exception-handler-calibration.lua`:

```text
CALIB_HANDLER vector=3 addr=00F882AE t=0.500000
CALIB_RESULT hits=1 first_t=15.032688
CALIB_VERDICT technique_confirmed
```

One hit, at `t=15.032688` — matching this exact crash's already-
established timing from `irq1-storage-completion-probe.md`. **Technique
confirmed**, not assumed. Reverted the edit immediately after
(`git checkout`), rebuilt, confirmed 7/7 restored before proceeding.

A masking bug surfaced during calibration itself, not after: an
unmasked `read_u32()` of the vector table carries a nonzero top byte
(`$FFF882AE`, not `$00F882AE`), and MAME's own `install_read_tap` caught
it outright — *"start address is outside of the global address mask
ffffff, did you mean f882ae?"* Fixed by masking every handler address to
`0x00ffffff` before installing a tap on it.

### The five items, reported

Vector table read at a known-good moment (post-`FILE 1`), handler
addresses tapped, run through `FILE LOADED` + 8s:

```text
vector=2(bus_error)           addr=00F882AA  hits=0     -- ROM handler
vector=3(address_error)       addr=00F882AE  hits=0     -- ROM handler, same address calibration fired on
vector=4(illegal_instruction) addr=00FC6000  hits=32    first_t=21.799163  -- SIB window, not code
vector=8(privilege_violation) addr=00F882C2  hits=0     -- ROM handler
vector=10(line_a_emulator)    addr=00F882CA  hits=4713  first_t=16.310990 -- ROM handler
vector=11(line_f_emulator)    addr=00FC6014  hits=64    first_t=21.799169 -- SIB window, not code
```

No table relocation observed across the whole run (`SEHP_RELOCATIONS
count=0`) — the table doesn't need to be re-read mid-run for this
firmware.

1. **Vector 2 (bus error) — closed.** ROM handler, zero hits during
   normal boot+load. `[Verified]`
2. **Vector 3 (address error) — closed.** ROM handler at the exact
   address the calibration run's genuine Address Error fired on; zero
   hits on the fixed system. `[Verified]`
3. **Vector 4 (illegal instruction) — stays `[OPEN]`, but narrower.**
   The table slot resolves to `$FC6000` — the SIB window's own base
   address, not ROM or RAM code space. `sib-coverage-inventory.lua`
   (previous task) already proved that exact address carries heavy
   ordinary DPRAM read traffic (128+ reads in its own measurement
   window). The 32 hits recorded here, all starting at the instant
   `FILE LOADED` appears, are far more consistent with coincidental data
   reads than a genuine illegal-instruction exception — nothing else in
   this whole project's history suggests firmware crashes 32 times
   during a successful load. **Not resolved either way**: this is not
   "we can't measure" (Del 1's original problem) but "the vector table
   itself does not appear to hold a real, deliberately-installed handler
   here" — a different, more specific kind of open question.
4. **Vector 8 (privilege violation) — closed.** ROM handler, zero hits.
   `[Verified]`
5. **Vectors 10 and 11 (line A/line F emulator) — split outcome.**
   Vector 10 resolves to a real ROM handler and fires 4,713 times
   starting essentially at `FILE 1` (`t=16.31`, matching `FILE 1`'s own
   `t=16.3` almost exactly) — consistent with a genuine, heavily-used
   A-line-trap-based OS syscall mechanism (some 68000-era OSes use A-line
   traps as their primary syscall path; this project's own earlier
   blanket vector-table measurement independently saw similar-magnitude
   A-line activity, ~8,714 over a comparable window — corroborating, not
   proving). `[Likely]` legitimate, not a bug. Vector 11 has the *same*
   SIB-window-address problem as vector 4 (`$FC6014`) and the same
   `FILE-LOADED`-instant timing coincidence — **stays `[OPEN]`** for the
   identical reason.

### Guard

`install_sync_exception_guard` (`asr10_guards.lua`) taps only vectors 2,
3, 8 — the three that closed cleanly. Vector 10 is deliberately excluded
(expected, frequent, not an anomaly worth an aggregated alarm); vectors 4
and 11 are deliberately excluded (guarding on an ambiguous signal would
alarm on noise, not on a real regression). Fault-injection tested:
temporarily including vector 10 in the guarded set produced exactly one
aggregated alarm despite thousands of real hits, and the test correctly
failed.

## Del 2 — Where `$51`/`$56` Actually Come From

`irq1_ack_vector()`/`irq6_ack_vector()` (`mc68302.h:94/104`) hardcode the
formula's "`(GIMR.V7_V5 << 5)`" contribution as the literal constant
`0x40`, per their own comment citing "GIMR bits 7-5" — `mc68302.cpp`
never reads a modeled GIMR value at all (GIMR itself, `$FC6812`,
classifies `known_unimplemented`; there is no `m_gimr` member anywhere in
the device).

**Measured, not assumed:** `docs/asr10/lua/archive/gimr-origin-probe.lua`
polls GIMR directly (`read_u16()`, immune to the SIB-window tap-drop trap
since a plain read always goes through whatever mapping is currently
live — no installed tap to tear down):

```text
GOP_SAMPLE t=0.000000 value=0000   -- reset default
GOP_SAMPLE t=0.002000 value=8040   -- written once
```

Firmware writes GIMR `$8040` once, at `t≈0.002s`, and — confirmed by both
fine (1ms) polling through the first 8s and coarse (100ms) polling
through `FILE 1` and `FILE LOADED` — **it never changes again for the
rest of a normal run.**

### The hand calculation

`$8040` = `1000 0000 0100 0000`. Bits 7-5 = `010` (bit 6 is the only one
of the three set). `(0b010 << 5) = 0x40` — **exactly** the hardcoded
constant.

**What the same formula gives for a different GIMR:** if firmware had
left GIMR at its own reset default (`$0000`, per `sib-register-map.md`),
bits 7-5 would be `000`, and `irq1_ack_vector()`'s real formula would give
`(0<<5)|0x11 = $11`, not `$51`. If firmware had instead written, say,
`$8060` (bit 5 also set), bits 7-5 would be `011`, giving `(3<<5)|0x11 =
$71`. **The hardcoded implementation would deliver `$51` in either
hypothetical case, silently wrong both times.**

### Answer

The machine works today — but the chip model has exactly the internal
contradiction the task predicted. `irq1_ack_vector()`/`irq6_ack_vector()`
do not derive `$51`/`$56` from GIMR state firmware actually writes; they
return a fixed constant that happens to equal what the documented formula
would produce for the *one* GIMR value this firmware has ever been
observed to program, and never changes from. This is coincidence backed
by stability, not correctness backed by implementation. **Not building
the interrupt controller in response**, per instruction — the fix for a
firmware that varies GIMR is a real `GIMR`/`IPR`/`IMR`/`ISR` model, out of
scope here. Documented instead, and made loud: `check_gimr_vector_basis`
(`asr10_guards.lua`) polls GIMR bits 7-5 at each of the regression test's
own checkpoints (guard install, `FILE 1`, `FILE LOADED`) and alarms
(aggregated, once) if they are ever anything other than `0b010`. Fault-
injection tested: a scratch copy expecting `0b101` instead produced
exactly one alarm against the real, unchanged `0b010` and correctly
failed.

**Side effect of this measurement, folded into the SIB coverage
allowlist:** the previous task's `sib-coverage-inventory.lua` calibration
run (tap installed at `t=7s`) never saw a single access to `$0812`
(GIMR) — not because firmware never touches it, but because the one
write happens at `t≈0.002s`, entirely inside the documented `t=0-7s`
coverage gap. `$0812` is added to `asr10_guards.lua`'s calibrated
`known_unimplemented` allowlist on this new evidence — the gap
manifesting concretely, not a new access to explain away.

## Del 3 — The Five Items

### 1. Catch-all RAM: `map(0xfc5020, 0xffffff).ram()`

`docs/asr10/lua/archive/catchall-ram-inventory.lua` taps the full ~716KB
range (excluding `$FC6000-$FC6FFF`, which the SIB window's dynamic
install always takes priority over regardless of what the static map
declares underneath), bucketed at 4KB granularity, over a normal
boot+load:

```text
touched_buckets=9 of possible=57
FF7000  writes=128   (no reads)
FF8000  reads=81323  writes=54834
FF9000  reads=1195   writes=4096
FFA000  reads=12229  writes=4099
FFB000  reads=13505  writes=4096
FFC000  reads=4051   writes=4209
FFD000  reads=43276  writes=12869
FFE000  writes=4096  (no reads)
FFF000  writes=4096  (no reads)
```

**[Verified]** Only 9 of 57 possible 4KB buckets are touched at all, and
every one of them sits at the very top of the address space
(`$FF7000-$FFFFFF`, the top ~36KB) — the classic 68000 layout for
supervisor stack plus OS working variables, immediately below the vector
table's own IACK region. **The remaining ~680KB of the catch-all
(`$FC5020-$FF6FFF`) is completely untouched** during a normal boot and
instrument load. Not touched by any of this task's changes (`mem_map`
untouched, per rule); this is a measurement of the existing map only.

### 2. LRCLK: edge frequency vs. period frequency

Source, not guessed: `machine_reset()` arms `m_lrclk_timer` at
`attotime::from_hz(44100)`, and `lrclk_toggle()` **flips** `m_lrclk_level`
on every callback (`m_lrclk_level = !m_lrclk_level; m_maincpu->
set_pb_input(3, m_lrclk_level);`). **[Verified]** the callback (edge) rate
is 44.1kHz; a full period (low→high→low) takes two callbacks, so the
period rate is 22.05kHz — exactly the ambiguity the task named.

Whether this is "right" depends on whether firmware counts edges or
periods when it consumes PB3. Checked the cheapest available signal: the
previous task's own `PBDAT` ($FC6828) read-count measurement, ~14,169
reads over ~22s ≈ 644 reads/sec — nowhere near either 44.1kHz or
22.05kHz. This rules out "firmware tightly polls PB3 at the LRCLK rate"
as an explanation, but doesn't resolve edge-vs-period on its own; PB3 is
one bit within a word read for other reasons at a much slower
housekeeping cadence, not something a read-count measurement alone can
disambiguate. Determining which requires tracing the actual PB3-consuming
code path (disassembly), out of scope for observation this task, and
`current-status.md` already lists "PB3 LRCLK board frequency" as an open
item independently. **`[OPEN]`**, as the task permits, backed now by a
measurement that narrows what it isn't rather than nothing at all.

### 3. `m_sim` double-construction — fixed, not just journaled

Investigated whether the second allocation (`device_start()`) is
provably redundant before touching anything. The constructor's own
comment cited a specific justifying scenario: *"the driver's own
`machine_start()` can query `cs0_covers()` (via `read_loaded_word()`)
before this device's `device_start()` runs."* Checked directly:
`read_loaded_word` does not exist anywhere in `asr10_boot.cpp` (removed
in an earlier cleanup — the comment is stale), and
`asr10_boot_state::machine_start()` (read in full) does not call
`cs0_covers()` or reference `m_sim` at all. The only current caller of
`cs0_covers()`, `low_rom_or_lowmem_r()`, is a memory read handler that
cannot fire before both `device_start()` calls complete regardless
(instruction execution only begins after every device finishes
starting). `cs0_covers()`'s only state, `m_cs[0]`, is never mutated
between construction and `device_start()` — nothing writes `BR0`/`OR0`
before the SIB window exists, which itself postdates `device_start()`.

**Confirmed trivial and risk-free, not just plausibly so — removed the
redundant `device_start()` allocation**, kept the constructor's (the
early-availability guarantee it provides may still matter for a future
caller even though none currently needs it before `device_start()`).
Comments at both sites updated to record why, not just what. Built,
regression 7/7 before and after.

### 4. Save-state: incomplete, noted as a candidate

`mc68302.cpp`'s own existing `FIXME` already says it plainly: *"m_shadow,
m_offset_access_count, and mc68302_sim internals are not registered, so
save/load restores incomplete device state."* Confirmed still accurate
by reading `device_start()`'s `save_item()` calls directly — `m_shadow`
(the generic unimplemented-register shadow array), `m_offset_access_count`
(the access-counting array this task's own Del 2 partly duplicates in
Lua), and everything inside `mc68302_sim` (Port B state, BR/OR chip-select
registers) are all absent from the save-state list. Noted as a candidate
for a future task, not touched — the task's own scope for this item was
observation, and unlike item 3, no equivalently tight proof of
zero-risk was available without a much larger investigation into what
currently depends on save-state fidelity for this device (nothing in the
regression suite exercises save/load at all).

### 5. BAR relocation: given the specified status

`install_internal_window()`'s `TODO` (`mc68302.cpp`) updated in place,
verbatim status text as instructed: *"BAR relocation works for observed
boot usage, but underlying-map restoration semantics remain unverified."*
Not left as a bare status line — resolved what could be resolved first:
partial (byte-masked) BAR writes are **no longer** an open half of this
TODO. `sib-coverage-inventory.lua` (previous task) already measured this
driver's own boot sequence writing BAR via two separate byte writes
(high byte, then low byte, both at `t≈5.395s`), correctly combining via
`COMBINE_DATA` to the same final value a single word write had already
produced at `t≈0.000004s` — `window_base` came out correct both times,
confirmed by the resulting SIB coverage table. What remains genuinely
untested: whether `unmap_readwrite()` correctly restores whatever the
*static* `mem_map` declared underneath a `window_base` the SIB window
relocates *away* from. This driver's own BAR sequence only ever
relocates *to* `$FC6000` — never elsewhere, never away from it — across
its entire exercised path, so that restoration code path has never
actually run here. Untested, not proven safe either way; the status line
says exactly that.

## Del 4 — The Gate, Confirmed Wired

Extended the existing 7th regression test (`docs/asr10/lua/
mc68302_guards.lua`), not a new 8th — the task's own instruction ("om det
inte redan finns"; it already existed from the previous task). Now
installs five guards total: `install_exception_guard` (unchanged),
`install_sib_coverage_guard` (allowlist extended with `$0812`),
`install_idma_guard` (unchanged), and the two new ones from this task,
`install_sync_exception_guard` and `check_gimr_vector_basis`. Confirmed
these run **in the suite**, not only as standalone scripts: `sh
docs/asr10/regression-test.sh` invokes `docs/asr10/lua/mc68302_guards.lua`
by name as test 7 of 7, and that file is the one that installs all five
guards — there is no separate, unwired guard script.

**Both new guards fault-injection tested**, same discipline as the
original four: a scratch copy expecting the wrong GIMR bits-7-5 value
produced exactly one alarm against the real, stable value; a scratch
copy that temporarily included vector 10 (known to fire 4,713 times) in
the synchronous-exception guard produced exactly one aggregated alarm,
not thousands, and the test correctly failed both times.

Real suite, both after the `m_sim`/BAR-comment C++ changes and after the
guard-library additions: **7/7**.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, checked after the `m_sim` C++
  change, after the BAR-comment C++ change, and after the final guard
  additions — three separate green checkpoints, not one.
- `mem_map` untouched (catch-all inventory is read-only observation of
  the existing map).
- No ES5506/ES5510 change; `es5506.h` not read this task.
- No keyboard, no new ASR-10 functionality.
- Bank 1 untouched, still `[OPEN]`.
- The two C++ changes (`mc68302.cpp`'s `m_sim` double-construction and
  the BAR-relocation TODO status) are the task's own named exception for
  Del 3 item 3 ("städa om det är trivialt och riskfritt") and item 5
  ("ge den statusen") — not a departure from the Lua-first rule for
  guards, which stayed entirely in Lua.
- CMR bit table remains `[Likely]`, not upgraded — no new evidence
  arrived this task that would justify `[Verified]`.
- No `-log`.
- `git diff --check`: clean.

## Line Count

- `src/devices/machine/mc68302.cpp`: net **-4 lines** (one redundant
  allocation removed; both remaining comments grew to record the
  investigation, not shrink, but the code itself got smaller).
- `docs/mc68302/idma-spec.md`: unchanged this task (CMR table already
  landed in the previous one).
- `docs/asr10/lua/lib/asr10_guards.lua`: +2 guards
  (`install_sync_exception_guard`, `check_gimr_vector_basis`), +1
  allowlist entry (`$0812`).
- `docs/asr10/lua/mc68302_guards.lua`: extended in place, not
  duplicated — same 7th test, five guards instead of three.
- `docs/asr10/lua/archive/`: four new calibration/investigation scripts
  (`sync-exception-handler-calibration.lua`,
  `sync-exception-handler-probe.lua`, `gimr-origin-probe.lua`,
  `catchall-ram-inventory.lua`) — written, run, archived.
