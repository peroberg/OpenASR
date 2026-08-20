# FILE LOADED Verification Probe

## Scope

`tc-reentrancy-probe.md` fixed the `tc_w()` reentrancy bug and reached
`FILE LOADED` for the first time. That display string is a firmware claim,
not proof: 32 vector-`$51` deliveries observed in that probe do not by
themselves mean 32 sectors, and nothing in the regression suite checked
that the transferred data is real or that it landed anywhere sensible.
This task: (1) verify the load independently of the display string, (2)
lock the verified fact behind a 6th regression test, (3) observe — no code
— what firmware does once loaded.

## Del 1 — What Actually Got Transferred

`docs/asr10/lua/archive/file-loaded-verification-probe.lua`: taps the FDC
FIFO command/result dialogue (opcode-length-aware framing, not a naive
byte-run counter — see method note below) and every IDMA `DAPR`/`BCR`
write, boots to `FILE 1`, presses `0A 23 02`, waits for `FILE LOADED`, then
cross-checks the transferred bytes against `floppies/asr10booth/V350.img`
read directly via Lua's `io.open()` (untested capability in this project
before this task — confirmed working).

### Method note: two parsing bugs found and fixed before trusting the data

1. **FDC command-boundary desync.** A naive 9-byte accumulator for READ
   DATA frames breaks because `R=$08` (a legitimate sector-number
   parameter) collides with the single-byte SENSE INTERRUPT STATUS
   opcode. Fixed by classifying each new frame's length from its own
   opcode (low 5 bits), matching `upd765_family_device`'s own command
   dispatch table, not by counting bytes blindly.
2. **Arm-to-command correlation direction.** First attempt correlated each
   IDMA arm to the *nearest preceding* completed READ DATA command,
   assuming firmware sets up the FDC command before arming DMA. Measured
   data (and re-cross-checked against `disk-not-responding-probe.md`'s
   already-established byte trace) shows the opposite: **the IDMA channel
   is armed first, then the FDC command bytes are written** a few hundred
   microseconds later. Preceding-match matched arm #1 to a stale
   pre-`FILE1` boot-time command and produced nonsense disk offsets.
   Fixed by matching each arm to the nearest *following* command
   (bounded to a 50ms window) and by discarding all pre-`FILE1` command
   captures explicitly (boot-time OS loading issues its own real READ
   DATA commands on the same FIFO register, which is not this task's
   subject).

### Measured totals

```text
FLV_PARSE_QUALITY raw_commands=53 valid=21 rejected_pre_file1=32 rejected_as_desync=0
FLV_TOTALS         arms=21 total_bytes=172544 disk_min_offset=3584 disk_max_offset=382464
FLV_DEST            arms=21 dest_min=00000944 dest_max=00054400
FLV_COMPARE         compared=21 mismatches=2 all_zero_sectors=0 skipped_uncorrelated=0
```

- **[Verified]** 21 IDMA arms fired for this load, each with a directly
  measured `BCR` (not assumed): 3 arms of 512 bytes (1 sector), 2 arms of
  3584 bytes (7 sectors), and **16 arms of 10240 bytes (a full 20-sector
  track) each**. Total: **172,544 bytes across 337 sectors** — not the
  16KB a naive "32 IACKs ⇒ 32 sectors" reading would suggest. The earlier
  informal count from `tc-reentrancy-probe.md` was of vector-`$51`
  deliveries across the *whole* dialogue (RECALIBRATE/SEEK/SIS/READ DATA
  together), not of sectors; most of the real transfer work happens in
  large multi-sector IDMA bursts, each triggered by a single interrupt.
  This is the direct, measured answer to "does every interrupt cover more
  than one sector" — yes, 18 of 21 do, 16 of those a full track.
- **[Verified]** total_bytes (172,544) comes entirely from the IDMA SIB
  register taps (`DAPR`/`BCR`/`CMR` writes) — a direct register read, not
  a reconstruction from the FDC command-byte heuristic. This number does
  not depend on the two parsing bugs above being fully fixed; it was
  already correct in the very first run, before either fix.
- **[Verified]** Destination range: `$000944`–`$0552FF`. Entirely inside
  low RAM. **Nothing was written to sample RAM ($100000–$1FFFFF).**
  Answers the task's question (d) directly: at this stage of firmware
  execution, the loaded instrument data lands in system RAM, not in the
  ES5506's sample memory — consistent with this being a
  parse/directory/patch-data load rather than (yet) a raw sample-data DMA;
  see Del 3 for what happens after.
- **[Verified]** Content match: of 21 transfers, 19 are byte-for-byte
  identical between the disk image and the corresponding emulated RAM at
  the moment of comparison (script end, after `FILE LOADED`). The other 2
  are not data corruption: transfers #1 and #2 both target `$000944` (a
  reused small staging/scratch buffer that a subsequent transfer, #3,
  legitimately overwrites) — comparing final memory state against transfer
  #1/#2's original disk source necessarily "mismatches" once #3's data has
  landed on top. `all_zero_sectors=0` across all 21 — no page of the
  destination is empty/uninitialized. **[Likely]** the true per-transfer
  match rate is 21/21; the reported 19/21 is an artifact of end-of-run
  comparison against a reused destination, not evidence against 2 of the
  transfers.
- **[OPEN]** The root directory entry for `JM DIGI SYN` (`$578`, 26 bytes:
  `00 03 4A 4D 20 44 49 47 49 20 53 59 4E 20 01 4E 00 01 00 00 01 9D 00 00
  00 00`) was captured, but its field layout beyond name/base/stride was
  never decoded in this project (`disk-read-path.md` only established the
  directory's own base/stride, not per-entry field semantics). No
  candidate byte pair in this entry was force-fit to the measured
  337-sector/172,544-byte total without a prior, independently verified
  decode — that would risk a spurious match. Left open rather than guessed.

### Assessment

172,544 bytes (168.5 KB) is a plausible size for a real ASR-10 instrument
patch/directory load, an order of magnitude past the ~16KB a shallow
reading of "32 deliveries" would suggest. The load is not truncated by any
measure available here: it stops cleanly after the last arm, all
transferred content matches the source disk image where checkable, and no
transferred region is blank. **[Likely]** the load is complete and
correct, not partial — nothing in the measured data (byte counts, content
match, absence of all-zero pages) points to early termination.

## Del 2 — Sixth Regression Test

`docs/asr10/lua/file_loaded.lua`, wired into
`docs/asr10/regression-test.sh`. Boots to `FILE 1`, presses `0A 23 02`,
and asserts **both** the display string (`FILE LOADED`) **and** the exact
measured IDMA total (`172544` bytes across the arm/`BCR` taps) — not the
display alone. A regression that silently truncates the load (fewer arms,
smaller `BCR` values) but still happens to land on the right display text
would be caught by the byte-count assertion; a display-only test would not
have caught it. 6/6 green (see Verification).

## Del 3 — What Happens After FILE LOADED

`docs/asr10/lua/archive/post-file-loaded-probe.lua`: taps ES5506
($FC2000-$FC207F) and ES5510 ($FC3000-$FC303F) read/write traffic across a
5-second idle window immediately after `FILE LOADED`, then presses one
more panel button (`BTN_0A`, the same button `button.lua` already uses to
cycle files) and re-measures.

```text
PFLP_LOADED       t=21.790000 display="FILE L0ADED           "
PFLP_IDLE_WINDOW  t0=21.790000 t1=26.790000 display_changes=0
                  es5506_events=12634 es5510_events=8390
PFLP_BUTTON_PROBE t=27.620000 before="FILE L0ADED" after="FILE ?  JM DRUM5"
                  changed=true es5506_delta=1660 es5510_delta=1035
```

- **[Verified]** `asr10panel_device` (`esqpanel.h`, used by
  `asr10_boot.cpp`) has **no piano-keyboard ioport at all** — only
  `buttons_0`/`buttons_32` (panel buttons) and analog data-entry/volume.
  There is no `key_down`/`key_up` wiring for this panel class (that API
  exists on the base `esqpanel_device` and is used by other Ensoniq
  drivers, not this one). A literal "press a musical key" stimulus cannot
  be produced from Lua without first modeling that input — out of scope
  this task (no ES5506/ES5510 code, no new input modeling). The closest
  available proxy is a panel button, used here instead.
- **[Verified]** ES5506 register traffic is **not gated behind any input
  at all**. 12,634 events fired during 5 seconds of complete idle (no
  button, no key) after `FILE LOADED` — and the very first ES5506 write in
  the whole log is at `t=0.000050`, essentially at reset, long before
  `FILE 1` is even reached. This activity is **not caused by the
  instrument load**; it is a continuous background voice-servicing loop
  that runs from very early boot onward regardless of load state. 37
  distinct register addresses across the full `$FC2000-$FC207F` window are
  touched, with 71 distinct written values — not a fixed pattern, not
  all-zero.
- **[Verified]** ES5510 shows the same pattern: 8,390 events during the
  same idle window, present from `t=0.001361` onward. Written values vary
  richly (`4C`, `3F`, `4B`, `10`, `FF`, `00`, ...) — **not** the
  hardcoded-zero-stub symptom the task's framing raised as a possibility.
  `$FC3025` is read repeatedly in tight bursts (5 reads within ~50µs in
  the sampled window), consistent with a busy-wait status/ready poll —
  firmware is actively driving and reading back from the real
  `es5510_device` (`asr10_boot.cpp` wires `$FC3000-$FC303F` directly to
  `es5510_device::host_r`/`host_w`, not to a driver-level stub; the "Host
  Control stub returns a hardcoded zero" description in the task did not
  match what this driver actually has wired at that address).
- **[Verified]** The panel remains responsive after `FILE LOADED`:
  `BTN_0A` changed the display (`FILE LOADED` → `FILE ?  JM DRUM5`,
  cycling to the next file, same behavior `button.lua` already exercises
  at the `FILE 1` stage) and triggered a further burst of 1,660 ES5506 and
  1,035 ES5510 events in the following 500ms — consistent with firmware
  re-initializing voice/effects state for the newly-selected file.
- **[Verified]** No annunciator/display change occurred spontaneously
  during the 5s idle window (`display_changes=0`) — the VFD line is
  static at rest, as expected; all the measured register activity is
  inaudible/invisible background servicing, not something that shows up
  on the display.

### Assessment

The premise that "firmware might never reach the voice registers" does not
hold: it reaches them constantly, independent of key input, from very
early in boot. The ES5506's missing `SPEAKER`/`add_route` and guessed
clock (flagged in the task) are therefore plausible next blockers for
*audible* sound specifically, but not for firmware reaching the register
interface at all — that path is already exercised continuously, with real
value variation, by the current build. The ES5510 Host Control path is
similarly not stalled: it returns varying data and is polled in tight
loops, not stuck on a fixed value.

## Verification

- `docs/asr10/regression-test.sh`: 6/6 (`boot`, `display`, `button`,
  `button_upper`, `nodisk`, new `file_loaded`), run before and after every
  change in this task.
- Named verification: `file_loaded.lua` checks display text `FILE LOADED`
  **and** `idma_bytes=172544` (21 arms) — not display text alone.
- No `mem_map` change. No `-log`. No ES5506/ES5510 C++ code changed this
  task — Del 1/3 are Lua-only observation; Del 2 is a Lua-only regression
  addition.
- `git diff --check`: clean.

## Line Count

- `docs/asr10/lua/archive/file-loaded-verification-probe.lua`: written,
  run, archived — its question is answered; its verified byte-count
  (172544) is what the new regression test locks in.
- `docs/asr10/lua/archive/post-file-loaded-probe.lua`: written, run,
  archived — pure observation, no follow-up code produced by this task.
- `docs/asr10/lua/file_loaded.lua`: new, kept (regression, not archived).
- `docs/asr10/regression-test.sh`: +2 lines (comment + `run_test` line).
- No C++ changed. No deletion list beyond the two probes' archival (they
  are archived, not deleted, per this project's convention that answered
  investigations are preserved for traceability).
