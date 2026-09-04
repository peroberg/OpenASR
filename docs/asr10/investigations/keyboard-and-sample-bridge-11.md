# ES5510: What Firmware Asks For, Before Anything Is Turned On

Scope note, as before: this task's Del 3 briefly enabled ES5510 in a
scratch build, measured, and reverted via `git checkout` before
finishing — the committed tree has ES5510 `set_disable()`'d
throughout, unchanged from every prior turn.

## Parked, Verbatim, Per Instruction

*Vid verkliga hårdvaruvärden — CLKIN 15,238 MHz, utgångstakt
29 762 Hz — producerar MAME:s ES5506 halva tonhöjden mot vad riktig
hårdvara producerar för samma FC. FC- och ACCUM-bredderna matchar
databladet. Vår dubblade klocka kompenserar exakt. Orsaken är
okänd.*

Not investigated further this task, per instruction.

## Del 1 — What Does Firmware Ask ES5510 For?

`es5510_traffic_probe` (scratchpad; device left `set_disable()`'d the
entire time — only the host-register bridge already wired in an
earlier turn was watched) logged every access to `$FC3000-$FC31FF`
from reset through `FILE LOADED` and a played note (~25.5s, 26,395
total events).

### Host Control: polled constantly, always reads 0 — and the datasheet says that's fine

```text
ES5510_HOST_CONTROL_READ_COUNT count=6705
  pc=F9779A count=2914
  pc=F97684 count=1107
  pc=F9770A count=2684
```

**[Verified]** Three call sites poll Host Control (offset `0x12`,
`$FC3024`/`$FC3025`) continuously across the *entire* run — not a
one-time boot check — at a cadence matching this project's own
already-established ~12ms background-scheduler tick (adjacent read
clusters land exactly 12ms apart). Every single read returns `0`,
matching `es5510.cpp`'s hardcoded stub exactly.

**Real ES5510 datasheet** (`docs/ensoniq/ES5510.pdf`, Ensoniq ESP Spec
Rev 2.4, §5.1.2 "Host Access OK/"): *"the Host must always check that
the Host Access OK/ bit in the Host Control register is low or set to
0... When this data is valid, the Host Access OK/ bit will go low
again, and the data can be read by the Host."* The `/` suffix is the
signal's own active-low naming convention: **0 means ready/OK, not
busy.** **[Verified]** MAME's hardcoded-zero stub therefore reports
"always ready," not "always busy" — the *benign* direction for a stub
to fail in. This is precisely why nothing hangs: firmware's real
handshake protocol (check OK/, request, wait for OK/ to go low again)
degenerates to "always immediately ready," so every request completes
instantly and firmware proceeds without ever blocking. This is not a
timeout being silently absorbed; there is nothing to time out —
firmware's own success condition is met on every check.

### A real, complete instruction-memory download — the address ranges match the datasheet exactly

`es5510_instr_detail_probe` (scratchpad) isolated the `INSTR`-latch
writes (`$FC3006`-`$FC3010`, offsets `0x03`-`0x08`) and the two SELECT
registers that commit them:

```text
DETAIL_SELECT (gi=true, "Write Select: GPR+INSTR", $FC31C1):
  160 distinct index values, spanning t=0.001368s to t=21.783s (the
  whole boot-to-load window, not a single burst)
DETAIL_SELECT (gi=nil, "Write Select: INSTR", $FC3181):
  22 distinct index values (0x3E-0x58), in two short bursts at
  t≈16.26s and t≈21.78s (matching the FILE-1-to-FILE-LOADED window)
```

**Datasheet cross-check** (§6.1 Host Interface Memory Map): *"Write
Select: INSTR ... Address Ranges: INSTR $00-$9F"* — **160 addresses,
$00 through $9F**. The measured 160 distinct GPR+INSTR-select indices
match this **exactly** — not approximately. §1 Introduction, separately:
*"program lengths from about 64 to 160 microinstructions at typical
sample rates"* — **160 is also the documented maximum program
length.** **[Verified]** This is a real, complete download covering
the entire addressable instruction memory, not noise or a partial
patch — the measured extent matches the chip's own documented address
range and maximum program length at the same time, independently.

The smaller, later 22-position sweep (on the plain `INSTR`-select
register rather than the combined `GPR+INSTR` one) is **[Likely]** a
narrower, in-place patch of a sub-range of already-loaded instructions
around the file-load boundary — consistent with the datasheet's own
description (§5.1.2) of using the write-select registers to update a
single instruction's field without re-downloading the whole program —
not independently confirmed as to *which* field or why.

### Does firmware proceed regardless?

**[Verified]** Yes, unconditionally through this task's entire
measurement window: boot completes, `FILE 1` is reached, the
instrument loads, `FILE LOADED` appears, the instrument is selected,
and a note plays — all while Host Control has read `0` on every one
of 6,705 checks and the full 160-word instruction download has
completed. Nothing in this path depends on the device being real.

## Del 2 — Which Clock?

`XTAL(10'000'000)` has no ASR-10-specific citation. What's now
available:

- **Datasheet, §6.2.4 Supplies (clock and power)**: *"VDD +5.0 volt
  power supply < 100mA @ **10MHz clock**"* — 10MHz is a real,
  datasheet-cited operating point for this exact part, not merely a
  number borrowed from a sibling driver.
- **Datasheet, §7 Timing Diagram, "ESPR7 TIMING SPECIFICATIONS"**:
  full timing tables for **three** speed grades — 8MHz, 10MHz, 12MHz.
  10MHz is one of three official, documented configurations for the
  `ESPR7` die revision specifically (which revision the ASR-10's own
  chip is stamped with is not established here).
- **Datasheet, §1 Introduction**: *"nominal instruction cycle is
  250ns"* = exactly 4MHz instruction rate. Handwritten margin notes on
  the block-diagram page give `Instr Rate = MCLK/3`; `4MHz * 3 =
  12MHz` — the *nominal* figure in the intro corresponds most exactly
  to the 12MHz grade, not 10MHz, though 10MHz remains independently
  datasheet-valid per the two points above.
- **Family precedent, corrected**: `esq5505.cpp` contains **two**
  different clocking conventions for different board variants in the
  same file, not one. One variant halves a named `30.47618_MHz_XTAL`
  crystal for the M68000/ES5505/pump (the pattern cited in the prior
  turn). A **separate** variant (a different board config in the same
  file, lines ~762-834) clocks the M68000, `ES5510` (`set_disable()`'d,
  exactly matching this driver), `ES5505`, and an `HD63450` DMAC all
  from a **flat, undivided `10_MHz_XTAL`** — no halving at all. This
  second pattern is the closer match to what `asr10_boot.cpp` already
  does (`ES5510` at `10'000'000`, disabled), not merely a coincidence
  of borrowing a round number.
- **Sample-rate handwritten note** (same block-diagram page): *"Sample
  Rate = MCLK/512"* — the same divisor shape as ES5506's own
  `clock/(16*32)` formula. At `MCLK=10MHz`, this gives ~19.5kHz,
  inside the datasheet's own stated *"typical sample rates of between
  10kHz and 50kHz."* Were ES5510 instead fed the undivided Y2
  (30.476MHz, as ES5506 now is), the same formula gives ~59.5kHz —
  **outside** that stated typical range. This is a mild point against
  ES5510 sharing ES5506's undivided clock, independent of the Del-1-
  parked factor-of-two question.

**Classification: familjeprecedens + datablad (delvis), inte mätt.**
10MHz is real and documented for this chip, and matches a genuine
family-precedent board configuration structurally closer to this
driver's own than the halved-crystal one. It is still not established
which of 8/10/12MHz — or Y2/Y3, divided or not — the actual ASR-10
board wires to its own ES5510. `[OPEN]`, upgraded from pure guess to
partially-evidenced guess.

## Del 3 — Scratch Experiment: Enabled, Measured, Reverted

`es5510_host.set_disable()` commented out in a scratch build (never
committed; reverted via `git checkout -- src/mame/ensoniq/asr10_boot.cpp`
immediately after measurement, confirmed clean — `git status`/`git diff
--stat` empty, rebuilt and re-verified 8/8 before writing this up).

```text
PASS boot
PASS display
FAIL button no_file2 final_display="EFFECT D0WNL0AD FAILED"
PASS button_upper
PASS nodisk
PASS file_loaded
PASS mc68302_guards
PASS note_audio (rhra=3 voice_writes=2400)
PASS note_audio_wav (peak=3834 freq=262.3Hz)
FAIL regression failures=1
```

**[Verified]** Enabling the device does **not** hang MAME — the
failure mode is a graceful, already-understood one:
`filesystem-browser-map.md` §4.20-4.24 (prior, legitimate project work)
already traced `EFFECT DOWNLOAD FAILED`/`ERROR 032`'s exact producer
(`f884b6`/`f89c48`), its retry-limit trigger
(`ffc896: cmpi.l #$fff9bca0,$e8e.w`), and its hard-trap exit
(`D0:=0x20`, `trap #0`) in full, months before this task. Enabling the
real device changes *something* about the timing or content the real
handshake now exposes, tripping that already-documented retry limit —
a real functional difference from the stub, not a new failure mode.
`note_audio` (a different, shorter button sequence + MIDI note, not
the same path `button.lua` exercises) still passed with unchanged
pitch/amplitude (`262.3Hz`, `peak≈3834` vs. `3873` disabled — within
normal vibrato-measurement noise). **[OPEN]** why `button.lua`'s
sequence fails while `note_audio.lua`'s doesn't was not chased
further — different button sequences, not re-run under multiple
seeds to check for raciness.

## Del 4 — What Would ES5510 Add? Structurally, Nothing Reaches The Speaker Yet

**[Verified]** `src/devices/cpu/es5510/es5510.h`: `class es5510_device
: public cpu_device` — **not** a `device_sound_interface`. No
`sound_stream`, no `add_route()`, nothing in this device class
participates in MAME's audio mixer at all. `asr10_boot.cpp` never
wires one either way (confirmed — no `add_route` call referencing
`m_es5510_host` anywhere in the driver). Consistent with this: the
scratch experiment's `note_audio_wav` measurement showed no pitch or
amplitude change with the device fully enabled and (per Del 1) a real
160-instruction program loaded.

**Answer to the task's own question**: no, nothing audible would
change on the single note this project currently tests, **and it
couldn't, structurally**, regardless of whether the downloaded program
is a real reverb/chorus algorithm or garbage — there is no signal path
from ES5510 to the mix bus in the current model. This is not "the
effect is too subtle to hear" — it is "there is nowhere for it to go."
**This is a strong argument for prioritizing other work over wiring
ES5510 further**, exactly as the task anticipated: building a real
audio path for a device with no measured audible effect on the one
signal this project currently exercises is a larger investment than
its near-term payoff justifies. Recorded as the answer, not implied.

## Verification

- `docs/asr10/regression-test.sh`: 8/8 before this task's scratch
  experiment, 8/8 after (the experiment's own 7/8 result was measured,
  reported, and reverted — never left in a failing state).
- No permanent code change. The Del 3 scratch experiment
  (`set_disable()` commented out) was built, measured, and reverted
  via `git checkout` before this document was written; `git status`/
  `git diff --stat` on `asr10_boot.cpp` confirmed clean before the
  final regression run above.
- No `mem_map` change, no clock change, no bank 1 change.
  `es5506.h`/`.cpp` and `es5510.cpp`/`.h` read (device-class check),
  not modified, not committed.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` used
  for the scratch-experiment pitch/amplitude comparison.
- Factor of two not investigated this task, per instruction — parked
  verbatim above.
- No fork with an open mandate.
- `git diff --check`: clean.

## Line Count

- No permanent C++ change (scratch experiment built, measured,
  reverted).
- No new archived Lua scripts this task (two scratchpad-only probes,
  `es5510_traffic_probe.lua` and `es5510_instr_detail_probe.lua`, used
  for measurement and not committed, consistent with this project's
  practice of not archiving every exploratory script when the
  finding is fully captured in the investigation document).
