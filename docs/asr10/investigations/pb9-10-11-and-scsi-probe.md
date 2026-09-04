# PB9/PB10/PB11: two hypotheses tested, both fall (2026-08-24)

PB9/PB10/PB11 had surfaced as candidates for two unrelated mechanisms in
`handoff-2026-08-23.md`'s Del 3/Del 4: a source selector for `REC
SRC=MAIN-OUT` digital self-sampling, and SCSI-INTRQ. This task tests
both directly. Neither survives measurement — not "no evidence found"
but three independent, stacked, positively-identified reasons. `[Verified
runtime]`/`[Verified static]` unless marked otherwise.

## Del 0 — auditing the WAV-channel assumption backward

Before touching PB9-11: the previous session's own stereo verification
found `-wavwrite` captures have 3 channels (MAME's floppy-drive-noise
speaker, always present in the device tree ahead of this driver's own
`SPEAKER`), not 2. That raised a real question: did the 8th regression
test's pitch/interval measurements assume the wrong channel?

**Audited, not re-measured from scratch.** `docs/asr10/lua/check_note_audio.py`
is the only WAV-analysis script in this tree (`grep -rl "wave.open\|getnchannels"
docs/asr10/` finds nothing else); every pitch/interval claim in
`keyboard-and-sample-bridge-7/8/9/10.md` reused its `PASS note_audio_wav`
gate rather than independent code. Its channel selection is **not**
hardcoded to 2:

```python
ch = w.getnchannels()
...
chan1 = a[1::ch]  # SPEAKER left, per asr10_boot.cpp's add_route(0, "speaker", 1.0, 0)
```

`ch` is read from the file's own header, so `a[1::ch]` always lands on
absolute-interleave-position 1 regardless of how many channels exist —
which, given the floppy speaker is always enumerated ahead of the
driver's `SPEAKER`, is always the first *real* audio channel, not the
floppy channel (position 0).

**Re-ran the actual 8th-test path fresh** (`note_audio.lua` +
`check_note_audio.py`) to confirm rather than trust the formula:

```
PASS note_audio rhra=3 voice_writes=2400
PASS note_audio_wav peak=3876 freq=262.3Hz
```

Direct Python re-analysis of the same capture: `channels: 3`; channel 0
(floppy) is **zero for every single sample in the entire capture**, not
just the measurement window; channel 1 and channel 2 both show
`peak=3876` (matching the checker's reported peak exactly) and are
bit-identical (a single mono MIDI note, both output routes carrying the
same voice — expected, matches the stereo task's own mono-control
finding). Floppy noise cannot have contaminated any measurement: it is
mathematically disjoint from the interleave index used (position 1 vs
position 0), and it was empirically silent throughout, not just
theoretically excluded.

**Can floppy sound be disabled, should that become the suite default?**
Traced `floppy_connector`/`floppy_image_device` (`src/devices/imagedev/floppy.cpp`,
`.h`): `m_use_sound` **defaults to `false`** in the connector's own
constructor (`floppy.cpp:223`), and `asr10_boot.cpp` never calls
`enable_sound()` — so floppy sound is already off by its own default,
confirmed by the empirical zero above, not assumed. The extra channel
*slot* in the WAV exists regardless of that flag: `floppy_image_device::
device_add_mconfig()` (`floppy.cpp:2000-2006`) unconditionally adds a
`SPEAKER` + `FLOPPYSOUND` sub-device pair to any floppy drive's own
machine config — that happens at static mconfig-build time, before any
runtime enable/disable flag is read. Removing the channel slot itself
would require patching MAME's core `floppy.cpp`, out of scope for this
driver. **Conclusion: nothing to change.** The measurements hold; this is
already the safest configuration available without touching core MAME.

## Del 1 — PBDDR/PBDAT: what's configured, what's touched

**Static, re-confirmed not re-derived** (`mc68302-status.md`): ROM's
`$FB8E06` init writes `PACNT($FC681E)=$E000`, `PADDR($FC6820)=$FFFF`,
`PBCNT($FC6824)=$0080`, `PBDDR($FC6826)=$F097`, `PADAT($FC6822)=$18FC`,
`PBDAT($FC6828)=$0007`. `PBDDR=$F097` = `1111 0000 1001 0111`: bits
9/10/11 are **0** → configured as inputs, consistent with them being
interrupt sources rather than firmware-driven outputs.

**Dynamic measurement, this task.** A tap on `PBDDR`/`PBDAT`
(`$FC6826-$FC6829`, both directions) installed at `-autoboot_script`
start found **zero** writes across the *entire* boot->load sequence —
this includes the one-time ROM init above, which happens before any
Lua tap can attach (the init executes within the first few instructions
after reset, well before `-autoboot_script`'s Lua environment
initializes). This is a real methodological limit, not a dead tap: a
witness on `$000000-$0FFFFF` general RAM writes stayed alive and
active (843,384+ hits) for the whole window, proving the harness itself
was live; the ROM-init writes are simply earlier than any Lua tap can
ever be installed in this project's `-autoboot_script` model.

**What matters is the window *after* init**, which a live tap fully
covers. Across that window — boot, instrument load, `FILE 1`, and every
panel interaction below — **PBDAT is never read and never written
again**. This holds across four independent, witnessed probes (a
dedicated boot-trace, a REC SRC Field-2 cycling probe, a corrected
64-button sweep, and a full Data Entry Slider sweep), not one narrow
test.

## Del 2 — the `MAIN-OUT` source-selector hypothesis: PB never moves, and Field 1 turned out to be unreachable

**First methodological trap, caught before it produced a false
negative.** REC SRC's displayed text does **not** live-update when a
field's underlying value changes — it is redrawn only when
Sample•Source Select (`BTN_20`) is pressed again. Directly measured:

```
SANITY_ENTER      display="REC SRC=INPUTDRY LEFT " mode=0
SANITY_AFTER_0A   display="REC SRC=INPUTDRY LEFT " mode=1   <- text stale, RAM already changed
SANITY_AFTER_0A_2 display="REC SRC=INPUTDRY LEFT " mode=2   <- text still stale
SANITY_REENTER    display="REC SRC=INPUTDRY L+R  " mode=2   <- only now does text catch up
```

`$016F` (Field 2's known cell) updates immediately on each `BTN_0A`
press; the display string does not, until `BTN_20` is pressed again. A
first attempt at a 64-button sweep (comparing display text immediately
after each candidate press) would have — and initially did — silently
miss any button whose only effect is a state change with no immediate
redraw. Corrected methodology: reset (`BTN_20`) -> candidate -> force
redraw (`BTN_20`) -> compare against the clean baseline text, with
`$016F` also read directly as a positive-control cross-check.

**Corrected 64-button sweep result:** only `$0A`/`$0B` (Down/Up) show
any effect at all, and both only move `$016F` (Field 2 — already known).
No other button code, 0x00-0x3F, changes Field 1 or produces a
redraw-visible display change. `$0C`/`$0D` (the pilot keymap's "Left"/
"Right") do **not** move a cursor within this screen at all — they
navigate to an entirely unrelated top-level menu (`COPY AUDIO TRACK` ->
`ERASE AUDIO TRACK` -> `FILTER AUDIO EVENTS` -> `SHIFT AUDIO TRACK` on
repeated presses), confirming `panel-keymap.md`'s own disclaimer that
these are a clicking-convenience pilot mapping, not verified physical
Left/Right cursor semantics.

**Data Entry Slider, also swept** (`:panel:analog_data_entry`, an
`IPT_ADJUSTER` ioport, `$000-$3FF`, driven directly via
`field:set_value()`, redraw forced with `BTN_20` after each step, full
low-RAM window diffed): no change to the displayed Field 1 text at any
of five sampled values across the full range. The RAM cells that do
change (`$000D5C`, `$0014EE`, `$0002F4`, ...) look like generic
UI/animation counters that drift with wall-clock time regardless of the
slider (confirmed by also appearing, with different specific values,
between steps that didn't intentionally change anything) plus one cell
(`$000DDC`) that tracks the raw slider reading itself (`$0200`->`$0300`
for slider value `$300`) — a hardware-value cache, not evidence of a
Field 1 transition.

**Net result for Field 1:** it could not be driven to any value other
than `INPUTDRY` through any currently-modeled panel control (button
matrix or Data Entry Slider). This is itself a finding, filed
separately below — not folded into the PB question, because it means
the PB hypothesis was never actually tested under its real
precondition (`REC SRC=MAIN-OUT` selected). What **can** be said
directly: across everything that *is* reachable — Field 2 cycling
(proven working, used as a positive control throughout), all 64
buttons, and the full slider range — `PBDAT`/`PBDDR` never changed once.
The hypothesis falls for lack of any observed PB correlation with
anything drivable in this model.

**Separate, filed item: REC SRC Field 1 is not reachable in the current
panel model.** The manual's own two-column layout mentions "Press: 1-8"
adjacent to the REC SRC screen; whether that is a genuine numbered-page
direct-select mechanism for this specific screen or an unrelated
adjacent tip lost to PDF-reflow ambiguity was not resolved here — this
task's own instruction was to determine the PB-vs-source-selector
condition, not to complete the panel's UI model. Flagged as a
`panel-input-model.md`-class gap, not investigated further.

## Del 3 — the SCSI-INTRQ hypothesis: falls, and the real mechanism is now identified

**`boot-sequence.md` step 4 ("SCSI-avsökning") was marked "RUTIN EJ
IDENTIFIERAD"** — no one had located the code that decides whether to
show `SCSI INSTALLED`/`SEARCHING FOR SCSI DEV`. A boot-window trace
(PBDAT/PBDDR plus the SCSI stub range `$FC5000-$FC501F`, both
directions, from machine start through `FILE 1`) found it:

```
SCSI_HIT[1] t=2.948559 pc=FB92C2 dir=W offset=FC5000 data=0202 mask=00FF
SCSI_HIT[2] t=2.948560 pc=FB92C4 dir=W offset=FC5002 data=0000 mask=00FF
SCSI_HIT[3] t=2.948560 pc=FB92C6 dir=W offset=FC5000 data=0202 mask=00FF
SCSI_HIT[4] t=2.948561 pc=FB92C6 dir=R offset=FC5002 data=0000 mask=00FF
SCSI_HIT[5] t=2.948565 pc=FB92C2 dir=W offset=FC5000 data=0202 mask=00FF
SCSI_HIT[6] t=2.948565 pc=FB92C4 dir=W offset=FC5002 data=5555 mask=00FF
SCSI_HIT[7] t=2.948566 pc=FB92C6 dir=W offset=FC5000 data=0202 mask=00FF
SCSI_HIT[8] t=2.948566 pc=FB92C6 dir=R offset=FC5002 data=0000 mask=00FF
```

`mask=00FF` on every hit means only the low byte lane is live, i.e. the
real addresses touched are the odd bytes `$FC5001`/`$FC5003` — the same
index/data register pair `memory-map.md` row H1 already identified
statically (AM33C93A programming model). Decoded: write `$02` to the
index register (select register 2), write a test pattern to the data
register, re-write the index register (resets the chip's internal
auto-increment pointer to the same register), read the data register
back. Two patterns are tried: `$00` (coincidentally matches this
model's stub, which always returns 0 — read-back "succeeds" by luck)
and `$55` (mismatches: wrote `$55`, read back `$00`). At `t≈2.95s`, well
before `LOADING SYSTEM` appears at `t≈4.25s` — i.e. this genuinely is
the boot-time detection step, not a later runtime SCSI operation.

**This is a register write/readback presence test, not a PB-bit and not
an interrupt line.** `scsi_asr_candidate_r/w` (`asr10_boot.cpp:774-782`)
is a pure stub — read unconditionally returns `0`, write does nothing —
so the second test pattern's readback fails, and firmware correctly (by
its own logic) concludes no SCSI chip is present. That is exactly why
the emulated run has always skipped both SCSI boot messages
(`boot-sequence.md`'s "Emulerad körning saknar de två SCSI-stegen"),
now with a mechanism instead of just an observation. No PBDAT/PBDDR
activity occurs anywhere near this window (confirmed by the same trace,
which covered `PBDAT`/`PBDDR` for the entire run with a live witness).

**Not built**: no `WD33C93`/`AM33C93A` device, no change to the stub.
Per instruction, this establishes the condition, not the fulfillment of
it.

## Del 4 — architecture: why the model can't answer either hypothesis right now, at the code level

Read (not modified) `src/devices/machine/mc68302.cpp` to understand why
neither hypothesis could show up dynamically even in principle, beyond
"nothing was observed":

- `MODELED_IRQ_BITS = SCC_IRQ_BITS | IDMA_IRQ_BIT` (`mc68302.cpp:59`)
  does **not** include bits 7 (PB9), 14 (PB10) or 15 (PB11).
  `update_internal_irq()`'s final step,
  `eligible = pending & IMR & ~ISR & MODELED_IRQ_BITS` (`mc68302.cpp:
  414-417`), masks with `MODELED_IRQ_BITS` last — so even if something
  set an IPR bit for PB9/10/11, it could never become `eligible` and
  could never assert `IRQ4`. This is the same category, and the same
  method point, as the four-round external-IRQ1 disproof documented in
  `current-status.md`: a real architectural boundary of the *model*,
  not evidence about real hardware either way.
- `set_pb_input()` exists as a general mechanism
  (`mc68302_device::set_pb_input()` -> `m_sim->set_external_input()`,
  `mc68302.cpp:385-388`) but `asr10_boot.cpp` only ever calls it for
  bit 3 (LRCLK, `asr10_boot.cpp:504`). Bits 7/14/15 have **no
  driver-side stimulus wired at all** — nothing could pulse them even
  if the interrupt path above didn't already block them.

Together: even if real ASR-10 hardware ties PB9/10/11 to `MAIN-OUT`
selection or to SCSI-INTRQ, this emulation currently has no stimulus
wired to those pins and no interrupt path for them even if stimulus
existed. That is a precise, code-level statement of why this round's
two hypotheses were structurally untestable here, not just empirically
quiet.

## Architecture finding, filed in `interrupt-topology-gaps.md` and `scc-rx-source-and-consumer.md`

While building the boot-trace probe for Del 3, re-confirmed the
previous task's own finding: completing SCC1's descriptor completion
(vector `$4D`) fires **two** `$37A1` IDMA events in the same firmware
event, using whatever SCC2's buffer currently holds rather than waiting
for SCC2's own independent completion. Written into both reference
documents in place (see their own 2026-08-24 entries), not just here,
since it is an interrupt-delivery/IDMA-coupling fact that belongs with
those tables, not only with stereo-content verification.

## `REC SRC=MAIN-OUT`, restated for this document's own record

Already established in `stereo-round-trip-verification.md` Del 3: the
manual's `Sample•Source Select` Field 1 has `MAIN-OUT` as one of four
values, described as requiring no accessory (unlike `DIGITAL`, which
needs the optional DI-10 card) — so the digital self-sampling path, if
it exists in this model's firmware reach at all, is a base-machine
feature, not an add-on-gated one. This task adds: that feature's own
UI path could not be exercised at all through the currently modeled
panel (Del 2 above), so it remains unverified in *both* directions —
neither confirmed reachable nor confirmed to touch PB.

## What was not done

No `WD33C93`/`AM33C93A` implementation. No ADC implementation. No SCC
code change. No `mem_map` change. Factor two, clocks, bank 1, ES5510
and expanded RAM configurations were not touched.
`es5506.h`/`.cpp`/`es5510.cpp`/`esqpump.cpp` were not read this task
(not relevant to PB/SCSI). PB9/10/11's real hardware identity remains
`[OPEN hardware]`, unchanged by this task — what changed is that two
specific hypotheses about them are now retired with reasons on record,
not left to be re-guessed.

## Regression

None added. No stable, reproducible new runtime *behavior* resulted
from this task (the result is an absence — PB inactivity — and a
static/boot-timing fact about the SCSI stub, not a new pass/fail
condition this project's suite doesn't already cover). Suite unchanged:
11 tests, 12 PASS lines, 13 total lines, verified green before and
after.

## References

- `../reference/interrupt-topology-gaps.md` — 2026-08-24 entries for
  both the PB9/10/11 retirement and the SCC1/SCC2 IDMA-coupling fact.
- `../reference/boot-sequence.md` step 4 — SCSI-scan routine, now
  identified.
- `../investigations/scc-rx-source-and-consumer.md` — 2026-08-24
  addendum for the IDMA-coupling fact.
- `../reference/mc68302-status.md` — static PBDDR/PBDAT init and the
  PB9/PB10/PB11 handler disassembly this task builds on.
- `../reference/memory-map.md` row H1 — the SCSI register pair identity
  this task's dynamic trace confirms is what's actually touched.
- `stereo-round-trip-verification.md` — Del 0's WAV-channel audit
  builds directly on this document's own channel-count finding.
