# Factor Of Two: Mapped, Not Explained. Clock Provenance Cleaned Up.

Scope note, as before: ES5510 stays `set_disable()`'d throughout; all
audio judgments are the dry ES5506 path. No code change this task
beyond clock-provenance comments — the ES5506 clock itself is
unchanged from `keyboard-and-sample-bridge-9.md`.

**A saved recording**: `select-and-play.lua` run under the current
(Y2, corrected) clock, `-wavwrite`'d to a scratchpad file for
listening (not committed — a session artifact, not project material).

## Del 1 — The Factor Of Two: Evidence Laid Out, No Mechanism Named

**Correction carried over from the prior turn's own self-correction**:
"MAME's FC/ACCUM fractional bits are probably interpreted a bit
position off" does not follow from anything measured. It was a
plausible-sounding mechanism promoted to an explanation before being
tested. It is retracted here, replaced with what the datasheet and
the code actually say.

### What the datasheet says (`docs/ensoniq/ES5506.pdf`, Ensoniq OTTO
Specification Rev 2.3 — a real, primary source, not a guess)

- **§4.5, The Frequency Control Register (FC)**: *"The register is a
  17-bit word, that is divided into a 6-bit integer portion and an
  11-bit fractional portion."*
- **§4.4, Accumulator (ACCUM)**: *"The integer part is 21 bits... The
  11-bit fractional part is needed to obtain proper frequency
  resolution and also for the interpolation calculation."*
- **§1, Introduction, feature list**: *"UP TO 16MHZ OPERATION."*

### What `es5506.cpp` does (read only, not modified)

```cpp
const s8 ADDRESS_INTEGER_BIT_ES5506 = 21;
const s8 ADDRESS_FRAC_BIT_ES5506 = 11;
voice->freqcount = get_address_acc_shifted_val(data & 0x1ffff);  // 17-bit mask
// get_integer_addr(): (accum & mask) >> ADDRESS_FRAC_BIT (=11)
```

**[Verified]** MAME's FC (17-bit, 6+11) and ACCUM (32-bit, 21+11)
formats are **bit-exact matches to the primary datasheet**, not an
inference from behavior. This specific mechanism — a fractional-bit
misinterpretation in MAME's model — has no supporting evidence and
positive evidence against it: the model matches the chip's own
documented register format exactly.

### The hard datapoint, checked against both alternatives

Firmware's own FC value for `$3C` is `0x38D` (909) — a value the
firmware computes from the note number, independent of whatever clock
MAME's driver config supplies. MAME's own conversion: playback rate
(words/sec) = `FC * sample_rate / 2048`.

```text
at 29,762Hz (ASR-10's documented mode):  909*29762/2048 = 13,210.3 words/sec
at 59,524Hz (current MAME config, Y2 undivided): 909*59524/2048 = 26,394.0 words/sec

freshly re-measured live (es5506-bank1-late-probe.lua, current config):
  25,600 and 27,840 words/sec across two 200ms windows
```

**[Verified]** The live measurement matches the **full-rate**
(59,524Hz) prediction within the same bucket-quantization noise
margin established in `keyboard-and-sample-bridge-8.md` (~3-5%), and
does not match the half-rate (29,762Hz) prediction at all (off by
~2x). This confirms MAME's own execution is internally self-consistent
with its own formula at the currently-configured clock, at every layer
measured so far (FC write, ACCUM write, live fetch address, live
fetched data, live output pitch) — it rules out an *additional*,
separate compensating error inside MAME's own model. It does not by
itself explain the tension below.

### `esq5505.cpp`'s crystal division, in detail

```cpp
auto clock = 30.47618_MHz_XTAL / 2;
M68000(config, m_maincpu, 30.47618_MHz_XTAL / 2);
ESQ_5505_5510_PUMP(config, m_pump, 30.47618_MHz_XTAL / (2 * 16 * 32));
ES5505(config, m_otis, 30.47618_MHz_XTAL / 2);
```

**[Verified]** The *same-named* 30.47618MHz crystal feeds the M68000,
the ES5505 ("OTIS"), and the 5505/5510 "pump" clock, **all divided by
2** before reaching any of them. This is real, committed, working code
for a closely related Ensoniq board — not a guess about ASR-10's own
board, but a genuine precedent that the identically-named crystal is
halved before reaching an OTIS-family chip in at least one other
Ensoniq product.

### The two facts that do not reconcile

1. The datasheet states OTTO is rated **"up to 16MHz."** Feeding it
   the undivided Y2 (30.476MHz) exceeds that rating by roughly 2x.
   Combined with `esq5505.cpp`'s real precedent of halving the same
   crystal, this is genuine (not speculative) support for real
   hardware receiving a divided clock (most plausibly Y2/2 =
   15.238MHz, comfortably under the 16MHz ceiling).
2. Measured pitch is correct (`keyboard-and-sample-bridge-9.md`,
   ~0.3-0.6%) **only** when MAME's ES5506 clock is set to the full,
   undivided Y2 (30.476MHz). Halving it in MAME (to match fact 1)
   would halve the modeled sample rate and, with firmware's FC
   unchanged, produce audio one octave too low — directly contradicting
   the measured-correct result.

**No candidate tested this task has direct evidence explaining both
facts at once.** Candidates considered, side by side:

| Candidate | Support | Status |
|---|---|---|
| MAME's FC/ACCUM fractional-bit width is wrong | None — datasheet confirms MAME's format is exact | **Ruled out** |
| Real ASR-10 hardware feeds ES5506 the full, undivided Y2, exceeding the chip's own generic 16MHz rating | Would directly reconcile both facts, but no board-level measurement or schematic evidence found this task; datasheet max ratings are sometimes conservative across a product family | `[OPEN]`, no direct support either way |
| A crystal-network `/2` before ES5506, matching `esq5505.cpp`, exists on the ASR-10 board too, **and** some other stage in the signal path (AD/DA, or ES5510's role when enabled) carries a compensating factor of 2 that isn't modeled at all | Would reconcile both facts without exceeding the chip's rating; no evidence for *what* the compensating stage would be | `[OPEN]`, no direct support either way |
| Some other, unidentified mechanism | — | `[OPEN]` |

**Stated as instructed, no more**: the interface behaves correctly —
pitch within 0.6%, interval ratios within 0.3%, the addressing layer
bit-exact verified. The clock `XTAL(30'476'180)` is Y2, supported by
the board's crystal complement and by an independent backwards
calculation from measured pitch. There is a factor of two between
ASR-10's documented rate and MAME's internal calculation **whose cause
is unknown**, and it stands `[OPEN]` with no mechanism named.

## Del 2 — Clock Provenance

| Device | Clock in `asr10_boot.cpp` | Classification | Basis |
|---|---|---|---|
| `MC68302` (MPU) | `XTAL(16'000'000)` | **Mätt/härledd** | Y1, board crystal list (`PLAN.md` §3); this is the crystal every other Y1-derived clock in the driver already assumes correctly |
| `UPD72069` (FDC) | `XTAL(16'000'000)` | **Gissning** | Driver's own comment: *"clock unknown; placeholder for boot tracing"* — unchanged, not resolved this task |
| `ES5510` (DSP) | `XTAL(10'000'000)` | **Gissning** | No citation found this task; device is `set_disable()`'d, so this value has never been exercised — no measurement possible while disabled |
| `SCN2681` (DUART) | `XTAL(16'000'000)/4` (device master clock) + `set_clocks(500'000, 500'000, 1'000'000, 1'000'000)` (IP2-IP5) | **Härledd, empiriskt bekräftad** | IP3=500,000Hz feeds channel A; `500,000/16 = 31,250` baud — exactly the MIDI rate this project verified byte-for-byte correct (`keyboard-and-sample-bridge-4.md`). IP5=1,000,000Hz feeds channel B; `1,000,000/16 = 62,500` baud — matches the panel channel's documented rate. Both check out against real observed protocol behavior, not just formula. |
| `ES5506` (OTTO) | `XTAL(30'476'180)` | **Härledd (bakåträkning) + familjeprecedens** | Y2, board crystal list; independently converged with a backwards calculation from measured `$3C` pitch (`keyboard-and-sample-bridge-7.md`/`-9.md`), within 0.4%. The Del 1 factor-of-two tension is attached to this entry, not hidden. |

Y2/Y3 vs. ES5510: **[OPEN]**. Y2 (30.47618MHz) and Y3 (33.8688MHz)
both feed the audio side per `PLAN.md` §3's crystal table; which one
(if either, undivided) feeds ES5510 specifically is not established
this task — the device is disabled and has never been clocked in any
measurement.

**Principle recorded, per instruction**: a clock in this machine
config is not a picture of the schematic. It is the effective input
value that makes the modeled device behave correctly. This project
does not model the board's clock-distribution network; it states an
effective clock and its provenance.

## Del 3 — Unattributed Addresses: None Found; One Confirmation, Nothing Cyclic

`catchall_cyclic_probe` (scratchpad, not archived — a straightforward
extension of the existing `catchall-ram-inventory.lua` technique, run
across the *entire* boot→load→select→play timeline rather than idle
boot alone) watched `$FC5020-$FFFFFF` (bucketed) and `$FC5000-$FC501F`
(the "SCSI candidate," byte-level) across a full ~24.6s run.

```text
CC_TOTAL_BUCKETS count=9   (identical to the original catchall-ram-inventory.md finding)
CC_SCSI_WINDOW_HITS count=8, all at t=2.948565-2.948572s (a ~7-microsecond burst)
CC_SCSI_HIT addr=FC5001 dir=w / FC5003 dir=w / FC5003 dir=r / FC5001 dir=w / ... (write, write, read-back pattern)
```

**[Verified]** No new address territory beyond the already-cataloged 9
buckets, even across a much richer scenario (instrument load, select,
and play — not just idle boot). Those 9 buckets' own characterization
(stack/system variables, per the original inventory) is unchanged.

**Correction to the task's own framing**: `$FC5000-$FC501F` is not
actually an unattributed "SCSI candidate" — `memory-map.md` already
resolved this: *"SCSI-kretsen ligger på `$FC5001`/`$FC5003` i CS3. ROM
`$FBB5C0` gör `movea.l #$00FC5001,A4` / `movea.l #$00FC5003,A3`, och
både ROM och båda OS-versionerna skriver `move.b #$18` (WD33C93
Command) följt av `move.b #$00` (Reset)."* This task's burst —
`$FC5001`(w) / `$FC5003`(w) / `$FC5001`(w) / `$FC5003`(r), repeated,
at `t≈2.95s` — matches that already-documented WD33C93 reset sequence
at the **address** level exactly (this probe did not log data values,
so the byte-level `$18`/`$00` match is inherited from `memory-map.md`,
not independently re-confirmed here). **[Verified]** This is a
confirmation of known, already-attributed SCSI-controller boot
behavior, not a new discovery — correcting the premise that this
window was unattributed. **[Verified]** Nothing in either watched
range shows sustained, regular/cyclic access consistent with a
polling scanner or status port — all traffic (catch-all and SCSI
alike) is either the 9 already-cataloged buckets or this one
already-documented, non-cyclic burst.

No previously-unattributed address or cyclic scanner was found this
task. If additional glue logic exists on the board, it is not
evidenced as doing anything this firmware touches continuously.

## Del 4 — Journal

- **Factor of two, named as a standing debt, not a closed question**:
  measured pitch requires MAME's ES5506 clocked at the full,
  undivided Y2 (30.476MHz); the chip's own datasheet rates it "up to
  16MHz," and a real, committed sibling-driver precedent
  (`esq5505.cpp`) halves the identically-named crystal before it
  reaches the same chip family. These two facts do not reconcile via
  any mechanism confirmed this task. `[OPEN]`, no mechanism named.
- **Clock provenance table**: above, Del 2. `UPD72069` and `ES5510`
  remain guesses; `SCN2681`'s derived rates are empirically confirmed
  against real protocol behavior; `ES5506`'s Y2 is doubly-converged
  but carries the open factor-of-two question.
- **Address inventory**: 9 buckets, unchanged from the original
  catalog; `$FC5000-$FC501F` traffic matches `memory-map.md`'s
  already-documented WD33C93 reset sequence at the address level —
  confirmation, not a new lead. No previously-unattributed address or
  cyclic scanner found.
- **`TUNING KEYBOARD` strings and the `MODE=$0D`/`ACT=$1F` mode
  switch**: both remain `[OPEN]`, unchanged from
  `keyboard-and-sample-bridge-9.md`.

## Verification

- `docs/asr10/regression-test.sh`: 8/8, unaffected — no clock change,
  no code change beyond provenance comments, this task.
- No `mem_map` change, no ES5510 activation, no bank 1 mapping change,
  no clock value changed. `es5506.h`/`.cpp` read (datasheet
  cross-check, divisor comparison), not modified, not committed.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` used
  for the saved listening recording and the fresh fetch-rate
  re-measurement.
- No fork with an open mandate.
- `git diff --check`: clean.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: provenance comments only, on the
  four clock lines (`UPD72069`, `ES5510`, `SCN2681`, `ES5506`) — no
  clock value changed.
- No new Lua scripts archived this task (the catch-all probe reused
  the existing technique and produced a negative/confirming result;
  the fetch-rate re-measurement reused `es5506-bank1-late-probe.lua`
  unchanged).
- `docs/asr10/reference/subroutine-index.md`, `docs/asr10/current-status.md`:
  updated with the clock provenance table and the factor-of-two
  standing debt.
