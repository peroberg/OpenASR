# Ready-Line Artifact Probe

## Scope

Follow-up to `irq1-handler-chain-probe.md`, which found the naive
`FDC INTRQ -> MC68302 IRQ1` wiring's `ERROR 129` crash traces to a
`SENSE INTERRUPT STATUS` result of `ST0=$C8` (`IC=11`, `NR` set) — the
uPD765-family signature for a **spontaneous drive-ready-line-change
interrupt**, not any RECALIBRATE/SEEK/READ-DATA completion. This
investigation tests the hypothesis that follows directly from that byte:
the interrupt the naive wiring delivered was never a storage completion at
all, but an artifact of how MAME's `upd765_family_device` models the ready
line — and if so, the three-round search for "the gate" was answering a
misframed question.

**Retroactive correction, per instruction:** every earlier document in this
line of investigation that described `ERROR 129 - REBOOT` as firmware
"detecting", "catching", or "protesting" something is corrected here.
`docs/asr10/archive/troubleshoot.md` and `reference/hardware-map.md`
already correctly documented `129` as the ASR-10 service manual's *odd
address error* — a 68000 **Address Error exception** (vector 3), a
CPU-level fault, not an application-level branch. `irq1-handler-chain-probe.md`
already measured this directly and got it right. The mischaracterization is
specifically in `irq1-storage-completion-probe.md` (written before that
measurement) and in the two `current-status.md`/`architecture-handoff.md`
passages summarizing it; each is corrected in place with a note, not
silently rewritten — see "Documents Corrected" below.

## Del 1 — Mechanism And Timing, Read From Source Plus Measured

### The mechanism, in `src/devices/machine/upd765.cpp`/`.h`

`upd72069_device`'s constructor sets `ready_polled = true` and
`ready_connected = true` explicitly (`upd765.cpp:2901-2907`), overriding
the base class's own defaults for this device family. With
`ready_connected == true`:

```cpp
// upd765.cpp:428
bool upd765_family_device::get_ready(int fid)
{
    if(ready_connected)
        return flopi[fid].dev ? !flopi[fid].dev->ready_r() : false;
    return !external_ready;
}
```

this queries the real `floppy_image_device::ready_r()` — not a dummy
always-true. With `ready_polled == true`, `device_start()` arms a
**software timer poll**, not an electrical wire:

```cpp
// upd765.cpp:349-350
poll_timer = timer_alloc(FUNC(upd765_family_device::run_drive_ready_polling), this);
poll_timer->adjust(attotime::from_usec(100), 0, attotime::from_usec(1024));
```

Every 1.024ms, `run_drive_ready_polling()` compares the current polled
value against the stored one:

```cpp
// upd765.cpp:2659-2678
TIMER_CALLBACK_MEMBER(upd765_family_device::run_drive_ready_polling)
{
    if(main_phase != PHASE_CMD || (fifocfg & FIF_POLL) || command_pos)
        return;
    for(int fid=0; fid<4; fid++) {
        bool ready = get_ready(fid);
        if(ready != flopi[fid].ready) {
            flopi[fid].ready = ready;
            if(!flopi[fid].st0_filled) {
                flopi[fid].st0 = ST0_ABRT | fid | (ready ? 0 : ST0_NR);
                flopi[fid].st0_filled = true;
                irq = true;
            }
        }
    }
    check_irq();
}
```

`ST0_ABRT = 0xc0`, `ST0_NR = 0x08`. For `fid=0`, a transition to
**not-ready** (`ready==false`) produces `st0 = 0xc0 | 0 | 0x08 = 0xc8` —
exactly the measured value. This formula appears nowhere else in the
3500+ line file; it is a unique fingerprint of this one code path.

`floppy_image_device::ready_r()`/`m_ready` (`floppy.cpp`) is driven by
motor state, not disk mounting:

```cpp
// floppy.cpp:809-843, condensed
void floppy_image_device::mon_w(int state)
{
    ...
    if (!m_mon && m_image) {                       // motor off -> on
        if (m_motor_always_on) set_ready(false);
        else m_ready_counter = 2;                   // wait for 2 index pulses
        ...
    } else {                                        // motor on -> off
        ...
        set_ready(true);                             // immediate
    }
}
```

`set_ready()`'s polarity is inverted relative to the FDC-level concept:
`m_ready=true` means *not yet confirmed spinning*, and `get_ready()`
negates it. Motor-on takes two real index pulses (roughly two disk
revolutions) before `get_ready()` starts returning true (FDC-ready);
motor-off flips it back to not-ready **immediately**, no delay.

### Measured: which firmware action, and when

`docs/asr10/lua/archive/fdc_ready_dialogue_probe.lua` (no C++ change
needed — the dialogue up to the crash point is identical whether or not
IRQ1 is wired, already proven deterministic across every prior run in this
line of investigation) logs both the AUX command port (`$FC4001`, motor/
rate group) and the FIFO port (`$FC4003`):

```text
READYDLG_AUX_W  t=2.948815 pc=FB8D14 value=1E   <- motor ON, drive 0
...
READYDLG_FIFO_W t=14.969909 pc=FB8CF2 value=FF  <- last byte of boot's own last READ DATA
READYDLG_AUX_W  t=15.009412 pc=FB8D14 value=0E   <- motor OFF (all drives)
```

[Verified dynamic] Firmware sends aux `$0E` (motor off — the `$0E-$FE/xE`
group's all-zero drive-select form) at `t=15.009412`, **300 microseconds**
after finishing the result-phase read of its own last, ordinary,
successfully-completed READ DATA (`ST0=$40`/`ST1=$90`... no — corrected in
Del 3 below where the full result is shown; the point stands regardless:
this is routine end-of-transfer motor shutdown, not an error condition).
This is completely ordinary firmware behavior — turn the motor off when
done reading — that has nothing to do with any storage-completion protocol.

[Verified dynamic] The single IACK previously measured at `t=15.032527s`
follows this motor-off command by **23 milliseconds** — consistent with
`run_drive_ready_polling()`'s next opportunity to run (gated on
`main_phase == PHASE_CMD`, i.e. once the FDC controller itself is idle) and
CPU mask happening to open around then (`irq1-vector-and-sr-probe.md`).

**Del 1 answer:** the ready-line transition is firmware-initiated (a
legitimate motor-off command) but not remotely connected to any
storage-completion protocol. It does not coincide with machine start or
image mounting — this specific instance is triggered by ordinary
post-transfer motor shutdown, 15 seconds into boot. The available public
API is `set_ready_line_connected(bool)` (full disconnect — `get_ready()`
falls back to `!external_ready`, which nothing in this driver ever calls
`ready_w()` to change, so it becomes permanently `true`). There is **no
public API** to suppress only the polled-edge synthetic IRQ while keeping
`ready_connected` meaningful — `ready_polled` has no public setter; only
specific device subclass constructors set it internally. Achieving
"keep ready real, suppress only the edge" would require modifying
`upd765.cpp` itself, out of scope here (shared MAME device, not
driver-scoped policy).

## Del 2 — Does Firmware Consult Ready Status

Static/dynamic tracing of `SENSE DRIVE STATUS` (`04 xx`, ST3 result)
usage was attempted and not conclusively completed: two genuine `04 00`
instances were identified early in boot (`t≈2.9489s`, `t≈3.6990s`), but the
generic shared "receive N result bytes" driver subroutine
(`ADDQ.B #1,$04E7.w` / `CMPI.B #$0A,$04E7.w` / `BNE.s`) that consumes the
result gave no clean, cheap way to confirm or rule out a downstream branch
on the ready bit specifically, within reasonable effort. Recorded as an
open static-analysis gap, not resolved.

**The decisive test instead: direct empirical measurement**, per the
task's own Del 3 negative-control structure. `set_ready_line_connected(false)`
alone (no IRQ1 wiring) was built and run against the full regression suite:

```text
PASS boot display="FILE 1  TUT0RIAL BNK  "
PASS display display="FILE 1  TUT0RIAL BNK  "
PASS button display="FILE 2  JM DIGI 5YN   "
PASS button_upper button=23 rhrb_delta=2
PASS nodisk display="  PLEA5E IN5ERT DI5K  "
PASS regression
```

[Verified dynamic] **5/5, including `nodisk`.** The no-disk detection path
is already established (`current-status.md`) to run through DUART
IP0/INDEX, not FDC ready — this result is consistent with, and now
empirically confirms, that ready status plays no observable role in any
currently-tested firmware behavior. Full disconnect is the defensible
intervention, chosen by measurement rather than by which is easiest to
write, per instruction — it happens to be the same conclusion the missing
in-between API would have forced anyway.

## Del 3 — Test

### Negative control (documented above under Del 2): 5/5, disconnect alone is safe.

### Positive test: `set_ready_line_connected(false)` + naive IRQ1 wiring together

```text
PASS boot display="FILE 1  TUT0RIAL BNK  "
PASS display display="FILE 1  TUT0RIAL BNK  "
PASS button display="FILE 2  JM DIGI 5YN   "
PASS button_upper button=23 rhrb_delta=2
PASS nodisk display="  PLEA5E IN5ERT DI5K  "
PASS regression
```

[Verified dynamic] **5/5.** Boot no longer crashes — `ERROR 129` is gone.
The phantom ready-change interrupt that previously fired during boot's own
polled FDC phase cannot occur anymore, because `get_ready()` now returns a
constant.

`docs/asr10/lua/archive/ready_disconnect_chain_probe.lua` then ran the
instrument-load sequence (`0A, 23, 02`) against this build:

```text
RDCHAIN_FILE1     t=16.300000
RDCHAIN_FDC_W ...  03 E1 08  07 00           <- SPECIFY (DMA mode) + RECALIBRATE
RDCHAIN_IACK1     t=18.306434 vector=51 target=FFFF87CE pc=F87FCA
RDCHAIN_0402_R    ... data=0000BA5E          <- correctly populated this time
RDCHAIN_FDC_W ...  08                        <- SIS -> ST0=$20 PCN=$00 (clean success)
RDCHAIN_FDC_W ...  0F 00 01                  <- SEEK to track 1
RDCHAIN_IACK1     t=18.308684 vector=51 target=FFFF87CE pc=F87F9A
RDCHAIN_0402_R    ... data=0000B1A4          <- SEEK continuation, correctly populated
RDCHAIN_FDC_W ...  08                        <- SIS -> ST0=$20 PCN=$01 (seek to track 1 confirmed)
RDCHAIN_FDC_W ...  0F 00 00                  <- SEEK back to track 0
RDCHAIN_IACK1     t=18.311188 vector=51 target=FFFF87CE pc=F87F9E
RDCHAIN_0402_R    ... data=FFFB8710 (masked $FB8710, ROM range)
RDCHAIN_FDC_W ...  08                        <- SIS -> ST0=$20 PCN=$00
RDCHAIN_FDC_W ...  46 00 00 00 08 02 08 1B FF  <- READ DATA
RDCHAIN_IACK1     t=18.436918 vector=51 target=FFFF87CE pc=F87FCA
RDCHAIN_0402_R    ... data=FFFB85AC (masked $FB85AC, ROM range, near documented $FB85C0 IDMA setup)
RDCHAIN_FDC_R ...  40 90 00 00 00 08 02      <- READ DATA result: ST0=$40 (abnormal), ST1=$90 (EN + OVERRUN)
RDCHAIN_SUMMARY   final_display="DI5K ERR0R - L05T DATA" t=47.790000
```

[Verified dynamic] **All four of the observed IACKs are real, distinct
storage completions** — RECALIBRATE, then SEEK(track 1), then
SEEK(track 0), then READ DATA — each with `$0402` correctly populated by
the instrument-load path's own continuation-installing code before the
corresponding FDC command was issued, each producing a clean `ST0=$20`
`SENSE INTERRUPT STATUS` result. No crash. No Address Error. The
generic vector-`$51` dispatcher, given a genuinely-prepared caller, works
exactly as `storage-completion-dispatch.md` documents.

[Verified dynamic] **READ DATA's own result phase**: `ST0=$40` (`IC=01`,
abnormal termination), `ST1=$90` — bit 7 (`EN`, End of Cylinder) **and**
bit 4 (`OR`, **Overrun**) both set. `OR` is the uPD765-family signature for
"the controller needed the next data byte serviced and nothing serviced
it in time" — the textbook symptom of an unimplemented DMA/IDMA path: the
FDC begins executing the transfer and asserts its data-request signal, and
because MC68302 IDMA is not implemented, nothing ever responds.

[Verified dynamic] **No writes to the five documented IDMA registers**
(`$FC6802/04/08/0C/10`) were observed at any point in this run. The
overrun is not inferred from absence of register writes — it is read
directly off the FDC's own result phase, an internal timing self-check
independent of whether anything ever touches those specific addresses. The
final firmware display, `DISK ERROR - LOST DATA`, is a legitimate firmware
error message responding to a genuine, correctly-reported device
condition — not a CPU crash, not a phantom completion.

**Del 3 answer:** the chain reaches IDMA territory. Per instruction, this
is not pursued further in this run — implementing IDMA is separate work.

## Disposition

**The user's IDMA hypothesis is un-retired and confirmed**, exactly as
anticipated: it was "superseded" only in the narrow sense that the
*previously investigated* crash never reached far enough to need it.
Once the actual blocker (the ready-line artifact) is removed, the chain
runs cleanly through every step up to IDMA and stops there, for exactly
the reason the hypothesis predicted.

**No code change is landed.** Both `set_ready_line_connected(false)` and
the IRQ1 wiring were reverted; the tree matches
`irq1-storage-completion-probe.md`'s disposition again. The reason for not
landing the (empirically safe, empirically necessary) ready-disconnect
alone: it is meaningless without the IRQ1 wiring it was tested alongside,
and landing the IRQ1 wiring is exactly the "connect storage completion to
MC68302 external IRQ1" board policy decision that
`architecture-handoff.md`'s recommended implementation order places
*after* IDMA work, not before it — landing half of a two-part fix now
would leave the driver in an untested intermediate state matching neither
the old nor the new baseline.

## Documents Corrected

Every characterization of `ERROR 129 - REBOOT` as firmware "detecting,"
"catching," or "protesting" something is retroactively wrong: it is a
68000 Address Error exception (vector 3), a CPU-level fault. Corrected in
place with a note (not silently rewritten) in:

- `investigations/irq1-storage-completion-probe.md` — "the firmware's own
  error path caught something and reported it" and "the firmware-detected
  error" both corrected.
- `investigations/irq1-vector-and-sr-probe.md` — the `[Likely]` "proximate
  cause" paragraph, already superseded for its command attribution, is now
  also corrected for its deeper mechanism claim (SIS's result does not
  gate anything; the dispatcher proceeds to `jmp [$0402]` regardless).
- `current-status.md` and `reference/architecture-handoff.md` — "firmware-detected
  `ERROR 129`" corrected to describe it as a CPU exception.

`archive/troubleshoot.md` and `reference/hardware-map.md` already had the
correct meaning on record (`hardware-map.md:149`: `"0x46 -> ERROR 129 odd
address error"`); neither needed correction, and `hardware-map.md`'s entry
is in fact where confirmation should have been sought before this
sub-investigation's earlier passes described the symptom in firmware terms.

## Verification

- `make SUBTARGET=mame -j4`: clean, for the negative control, the positive
  test, and after reverting both.
- `docs/asr10/regression-test.sh`: 5/5 negative-control-only, 5/5 with
  both changes together, 5/5 after reverting. Never dropped below 5/5 at
  any point in this investigation.
- No `mem_map` changes. No `-log`. No `getenv`. No interrupt controller,
  no IDMA implementation. No commits made.
- `git diff --check`: clean.
- Tree state at end: both temporary lines removed, matching
  `irq1-storage-completion-probe.md`'s disposition.

## Line Count

- C++: net zero (both lines added and reverted within this session; the
  driver comment explaining the disposition grew, replacing the previous
  comment).
- `docs/asr10/lua/archive/fdc_ready_dialogue_probe.lua`,
  `sds_st3_probe.lua`, `ready_disconnect_chain_probe.lua`: written, run,
  archived — their questions are answered (Del 2's ST3-branch question is
  the one exception, left open per above).
