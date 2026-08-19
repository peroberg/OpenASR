# IRQ1 / IMR Unmask Probe

## Question

Following up on `irq1-storage-completion-probe.md` — the naive unconditional
`FDC INTRQ -> MC68302 external IRQ1` wiring broke boot-to-FILE1 with firmware
`ERROR 129 - REBOOT`, because RECALIBRATE also completes during the OS's own
polled boot-time disk load. The hypothesis under test here: **the mask is the
gate** — on real hardware, INTRQ does not reach the CPU directly but through
MC68302's interrupt controller, where `IMR` holds the source masked until
firmware unmasks it.

**Question:** does firmware unmask an external-IRQ1 bit in MC68302 `IMR`
(`$FC6816`) anywhere between `FILE 1` and the instrument-load stall?

This note observes only. Nothing was built.

## Step 0 — reference-point mismatch

The task's "known observed IMR writes" reference list —
`$00, $2B, $09, $00, $00, $2B` from PCs `$00B014, $F88400, $F88448, $F8A048,
$00B07A` — does not describe MC68302 `IMR` (`$FC6816`). Cross-checked against
`docs/asr10/investigations/panel-input-model.md:116`:

```text
$5  $FC480B  ISR/IMR  ROM reads $F884C2; writes #$00 $F88400, #$2B $F88448,
                       #$09 $F8A048; OS writes #$00
```

`$FC480B` is the **DUART's** (SCN2681) ISR/IMR register, a different chip at
a different address, documented separately from the MC68302 SIB window. The
PCs and values in the task's reference list match this table exactly. This
is not a measurement to reproduce against `$FC6816` — it is evidence about a
different register. Not treated as a calibration failure; noted and set
aside.

The two reference points that genuinely describe MC68302 `IMR` do check out
against `docs/asr10/reference/mc68302-status.md`:

- ROM: `IMR |= $C080` at `$F87F0A` (unmasks PB9/PB10/PB11).
- OS: `IMR |= $2400` at (high-view) `$F0BF1A` (unmasks SCC1+SCC2).

Neither involves an external-IRQ1 bit, because — see Step 0.5 below — there
isn't one.

## Step 0.5 — chip-architecture cross-check, before measuring

`docs/mc68302/interrupt-source-map.md`, already in this tree, draws a hard
line between two mechanisms:

- **INRQ** (internal level-4 sources: PB8-11, SCC1-3, SDMA, IDMA, Timer1-3,
  SMC1-2, SCP) — these have `IPR`/`IMR`/`ISR` bits. `IMR` masks these.
- **EXRQ** (external IRQ7/IRQ6/IRQ1) — a separate table, "External Request
  Sources". Its own "Clear path" column reads **"external source must
  clear"**, and the source-map's clear-and-end-of-service summary states EXRQ
  is **"not represented in INRQ ISR"**.

In other words: per the manual as already captured in this repository,
external IRQ1 has no `IMR` bit to unmask in the first place. It is a raw CPU
IPL line, gated (if at all) by the 68000 core's own SR interrupt-priority
mask or by board-level logic — not by MC68302's internal `IMR`. This is
`[Verified chip]`, already on record, not re-derived here; it predicts the
measurement below independent of running it.

## Instrument Failure — the tap doesn't survive a BAR write

The first run of the probe, tap installed at script start (before boot),
found **zero** writes to `$FC6812`/`$FC6816`/`$FC6818` for the entire run —
including the whole pre-`FILE 1` boot. Per
`docs/asr10/reference/methods-static-analysis.md` §8 ("kalibrera
instrumentet innan mätningen tolkas"), a zero this total is not a result
until it's shown the same search hits something known.

`docs/asr10/lua/archive/imr_probe_calibration_check.lua` widened the tap to
the entire SIB window `$FC6800-$FC68FF` (which the ROM's own
PBCNT/PBDDR/PADAT/PBDAT writes, documented in `mc68302-status.md`, should
land in) and still found zero writes anywhere in that window through the
whole boot. That ruled out "wrong three addresses" and pointed at the tap
mechanism itself.

`docs/asr10/lua/archive/imr_probe_calibration_check2.lua` installed the same
wide tap after a fixed emulated delay instead of at script start. Installed
at `t=2s`: still zero. Installed at `t=15.5s`: **immediate** traffic —
`$FC6829` (PBDAT low byte, the PAR/analog-mux channel select) writes appear
within one millisecond of tap installation and continue every ~2ms
thereafter.

Root cause: `mc68302_device::install_internal_window()`
(`src/devices/machine/mc68302.cpp`) calls `remove_internal_window()` then
re-issues `install_readwrite_handler()` for `$FC6000-$FC6FFF` on every BAR
write. That call silently drops any Lua write tap installed on that range
beforehand — the tap and the device's dynamically-installed handler don't
compose across a re-install. A tap installed before the relevant BAR write
is invisible from that point on; a tap installed after it works normally.

This is a durable instrument note for any future probe of the MC68302
internal window, not specific to this question — logged here and worth
carrying into `methods-static-analysis.md`.

## Measurement

`docs/asr10/lua/archive/imr_unmask_probe.lua`: tap on
`$FC6812`/`$FC6816`/`$FC6818` installed immediately after `FILE 1` is
reached (i.e. after the window is known-stable, per the calibration above),
then the `0A, 23, 02` instrument-load sequence is pressed, with the tap left
running 10 seconds past the known stall point (`LOADING JM DIGI SYN` at
t≈17.8s in this run; the FDC command phase itself goes quiet by t≈18.3s per
`instrument-load-v350.md`).

```text
IMRPROBE start wait_for=FILE1
IMRPROBE_FILE1 t=16.300000 events_so_far=0
IMRPROBE_TAP_INSTALLED t=16.300000
IMRPROBE_LOADING t=17.790000
IMRPROBE_SUMMARY total_events=0 after_file1_events=0 file1_t=16.300000 load_seen_t=17.790000 final_display="L0ADING JM DIGI 5YN   "
```

[Verified dynamic] **Zero** writes to `$FC6812` (GIMR), `$FC6816` (IMR), or
`$FC6818` (ISR) occur between `FILE 1` (t=16.3s) and ten seconds past the
instrument-load stall (through t≈27.8s). This zero is calibrated: the
identical tap, installed at the identical point in boot, catches `$FC6829`
traffic immediately in the control run.

Coverage: this measurement only covers `FILE 1` onward, per the task's
question. It does not — cannot, with this tap-install-timing constraint —
directly observe whether `IMR |= $C080` (ROM, PB9/10/11) executes earlier
during the boot's own disk-load phase; that would need a tap installed after
whatever the *last* pre-FILE1 BAR write is, which was not located here. Not
needed for this question, but a gap for anyone answering a different one.

### Post-hoc witness: was the tap actually alive for the whole window?

The positive control above only proves the tap was alive **at the moment it
was installed** (t=16.3s) — not that it survived to t≈27.8s. Since the
failure mode is *silent* drop (no Lua error, just no further callbacks), a
zero result and a dead tap are indistinguishable without a witness spanning
the full window. `docs/asr10/lua/archive/imr_probe_bar_witness.lua` taps BAR
itself (`$0000F2`) from script start (t=0) through the same full run:

```text
BARWITNESS_WRITE t=0.000004 addr=0000F3 value=C6 mask=FFFF pc=000016 after_imr_tap_point=false
BARWITNESS_WRITE t=5.395043 addr=0000F2 value=0F mask=FF00 pc=FB8ABE after_imr_tap_point=false
BARWITNESS_WRITE t=5.395061 addr=0000F3 value=C6 mask=00FF pc=FB8ABE after_imr_tap_point=false
BARWITNESS_SUMMARY total_bar_writes=3 after_reference_point_t_gt_16.3=0
```

[Verified dynamic] BAR is written exactly three times in the whole run —
once at reset (`t≈0`) and twice more at `t≈5.395s` (`PC=$FB8ABE`, inside the
boot's own FDC polled-read loop) — settling to `$0FC6` (window base
`$FC6000`, matching every other reference in this tree). **Zero** BAR writes
occur after `t=5.395061s`, more than ten seconds before `FILE 1` (`t=16.3s`)
and more than twenty seconds before the IMR/GIMR/ISR tap was installed. The
tap that produced the zero-writes result was never ripped mid-measurement —
there was nothing left to rip it. BAR's own map
(`mc68302_device`'s static `bootstrap_map`, installed once in
`device_start()`) is not itself subject to the dynamic-reinstall problem
that hit the SIB window, so this witness tap does not need the same
after-the-fact install-timing workaround.

**Status upgrade:** `[DISPROVEN]` (below) stands as originally written, now
with explicit coverage witnessed for the full measurement window, not only
its first instant.

## Answer

**`[DISPROVEN]`** — firmware does not unmask an external-IRQ1 bit in
MC68302 `IMR` between `FILE 1` and the instrument-load stall.

This rests on one measured line, now witnessed for its full coverage
window, not on two independent lines:

1. **Measured, primary evidence:** zero `IMR`/`GIMR`/`ISR` writes between
   `FILE 1` and ten seconds past the instrument-load stall, on a tap proven
   to work at the same install point *and* now shown (via the BAR-write
   witness above) to have remained alive for the entire window — the SIB
   window's last reconfiguration was at `t≈5.4s`, over ten seconds before
   `FILE 1` and over twenty before the tap was installed.
2. **Supporting, not independent:** `docs/mc68302/interrupt-source-map.md`
   documents `ExternalIrq1` specifically as an `EXRQ` source with "external
   source must clear" and "not represented in INRQ ISR" — no `IMR` bit for
   *that specific mechanism*. This is **not** a general "external
   interrupts have no `IMR` bit" law: PB8-PB11 are physically external pins
   that firmware *does* mask/unmask through real `IMR` bits (`IMR |= $C080`
   at `$F87F0A`, this same document, is exactly that), because they are
   routed through the chip's internal level-4 `INRQ` machinery rather than
   through the three dedicated `EXRQ` pins (`IRQ7`/`IRQ6`/`IRQ1`). The
   manual's claim is narrow — specifically the `IRQ1` mechanism bypasses
   `IMR` — and should be read as corroboration of the measurement, not as a
   second, independently-sufficient proof standing on its own.

Per the task's outcome branches: no controller is built. If a gate exists
for this specific `IRQ1` path, it is not `MC68302 IMR` — that register
genuinely was not written in this window, on a witnessed-live tap.

## What Actually Opens The Path To Vector `$51`, Then

Not established by this probe — genuinely open, not guessed at. What is
ruled out now:

- Not MC68302 `IMR`/`ISR` masking (this probe).
- Not "wire INTRQ to IRQ1 unconditionally, always" (`irq1-storage-completion-probe.md`).

What remains open, consistent with `docs/mc68302/interrupt-source-map.md`'s
own EXRQ model ("external source must clear", not ISR-represented):

- The 68000 core's own SR interrupt-priority mask (`I0-I2`) — firmware could
  run the boot's polled disk-load phase at a mask level that ignores level-1
  entirely, then lower the mask once the OS's async storage state machine is
  armed for the instrument-load path. Not measured here.
- A board-level gate between INTRQ and MC68302 IRQ1 that isn't a bare wire —
  PAL/GAL glue, a latch enabled by some other write, or a different physical
  source than uPD72069 INTRQ altogether. Still `[OPEN]` per
  `storage-completion-dispatch.md`.
- `ASR-10`-specific: FDC ND (DMA/interrupt mode) bit, which the OS's SPECIFY
  command already toggles between the boot path (`09`, polled) and the
  instrument-load path (`08`, DMA/interrupt) — real hardware may gate the
  physical INTRQ signal itself on ND, even though MAME's
  `upd765_family_device::command_end()` currently asserts
  `intrq_wr_callback()` regardless of ND.

The SR-mask direction is the cheapest of these to check next: it needs only
reading the CPU's own status register around the two boot-time RECALIBRATEs
versus the instrument-load one, no new tap-timing workaround, and no
assumption about undiscovered board glue.

## Verification

- `make SUBTARGET=mame -j4`: not needed, no C++ changed.
- `docs/asr10/regression-test.sh`: 5/5 before, 5/5 after. Lua-only scripts,
  none referenced by the regression suite; unaffected as expected.
- No `mem_map` changes. No `-log`. No `getenv`. No commits made.
- `git diff --check`: clean.

## Line Count

- C++: none changed.
- `docs/asr10/lua/archive/imr_unmask_probe.lua`,
  `imr_probe_calibration_check.lua`, `imr_probe_calibration_check2.lua`:
  written, run, archived — their questions are answered.
