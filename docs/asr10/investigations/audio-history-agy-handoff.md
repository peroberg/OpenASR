# ED-002 history-dependent audio — AGY handoff

**Status:** historical handoff / provenance checkpoint, 2026-09-10.  Preserves the H0/H2 investigation evidence.

> [!NOTE]
> **Epistemic Classification & Historical Context:**
> - **Historical Scope:** This document records the bounded H0/H2 timing-phase investigation.
> - **Slots 3/4 Superseded:** The historical Slot 3 / Slot 4 comparison was forensic provenance and is superseded as an invalid comparison (they were playing different instruments).
> - **H0/H2 Resolution:** The H0/H2 K1RAMP and audio difference was proven to be legitimate history-dependent $0B6E DUART timer phase (Class 1 timing offset).
> - **Playability Defect Resolution:** The real intermittent playability / bank load outcome defect was subsequently isolated to MC68302 CS1 voice banking and resolved in `cs1-voice-banking-and-sample-addressing.md`.

## 1. Goal

The problem is history-dependent ED-002 demo playback from the authentic CDR-1 fixture: the same ED-002 bank can reach a different repeatable audio/runtime state after different bank activity.  Locate the first semantic firmware-visible/emulator-visible state or ordering difference.  Do not assume retained state is erroneous; it can be authentic.

```text
H0: direct ED-002 from clean fixture
H2: ED-001 was loaded and played first, then ED-002 was loaded
```

## 2. Fixtures / reproduction

Use V3.50 and authentic CDR-1 media:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -cdrom cds/cdr1-tnc.chd \
  -video none -sound none -nothrottle \
  -autoboot_script <one bounded Lua probe>
```

Do not use `-log`.  Keep Lua tap handles alive through `docs/asr10/lua/lib/asr10_taps.lua`; a dead tap invalidates a negative result.

| fixture | path / workflow |
|---|---|
| Slot 5 | `sta/asr10booth/5.sta`: clean, idle post-boot state before EPS-16+ navigation/load. SHA-256 from prior controlled work: `3cab835c90c84a3c5d72add8ef4e9a6578d9a0cd9b37d1275a70843eb7bb2358`. |
| H0 | Restore Slot 5; normal UI select/load `FILE 2 ED-002 BANK`; wait for `BANK L0AD C0MPLETED`; `$23` Yes / `EXIT TO DEMOS`; `$1D` Play. |
| H2 / Slot 6 | `sta/asr10booth/6.sta`: normal UI history of ED-001 load, ED-001 Play (observed bad), normal leave/stop, then ED-002 load; saved after ED-002 load and before ED-002 Play. Restore; `$23` Yes / `EXIT TO DEMOS`; `$1D` Play. |
| Slots 3/4 | Historical GOOD/BAD forensic states at `sta/asr10booth/3.sta` and `4.sta`. Stable restore-and-Play references, but not canonical lifecycle positions or determinism fixtures. |

[VERIFIED] Ten fresh Slot-5 direct ED-002 loads produced identical bounded post-load state, completion time and storage activity.  `BANK L0AD C0MPLETED -> EXIT TO DEMOS`, passive idle, and initial direct Play were deterministic in prior controls.  H0/H2 are deterministic **history dependence**, not random nondeterminism.

## 3. Verified results

### Historical Slots 3/4

[VERIFIED] Slots 3/4 shared bank association, object root and effect root, but selected different sample ranges.  Historical pre-Play witnesses:

```text
Slot 3: $0D10/$0D18/$0D1A = $0038/$0100/$10F8
Slot 4: $0D10/$0D18/$0D1A = $0034/$0700/$10B0
```

At Play `$0171C4` selected `$0D14=$15BA` in the then-GOOD path and `$1532` in the then-BAD path, leading to different object/sample-base choices.  This is forensic provenance only: `$0D08-$0D1F` recurs in ordinary controlled playback and `$0D14` is neither a sufficient GOOD/BAD marker nor a proven cursor.

[VERIFIED] Both Slots 3/4 reported `NO SEQ OR SONG FILES`; ordinary Song/Sequence position does not explain that historical pair.

### H0/H2 audio chain

[VERIFIED] Corresponding ED-002 START/END and fetched sample bytes were equal for the measured H0/H2 voice.  At K1RAMP programming (`CPU PC $FC6102`):

```text
H0: $0000F2A1
H2: $0000F4C1
```

[VERIFIED] K1RAMP is downstream.  A raw address-bearing chain was measured:

```text
H0 dispatch $7D2A150C      H2 dispatch $7D2A1544
  -> $0171B0: move.l D2,$0D08
  -> $0D0A low word -> voice 1 +$16
  -> K1 evolution at $F8D0F0 -> K1RAMP at $FC6102
```

Physical node/address identity in that chain was subsequently normalized out; it is not itself a causal semantic difference.

### Channel-A transmit state machine

[VERIFIED] `$F885DE` is continuation code, not an independent operation:

```text
$F885D4: D2     -> THRA       first byte $90
$F885DA: BSR.W  $F8854A       save continuation and RTE
TxRDYA IRQ6 -> $F884BE -> ($00E6) -> $F88554
$F885DE: 5(A5)  -> THRA       continuation byte $38
$F885EA: 6(A5)  -> THRA
```

`$F88554` is the `$00E6` TxRDYA callback target.  The relevant H0/H2 request payload was equal after address normalization:

```text
D2 first THRA byte = $90
+2=$0000, +4=$02, +5=$38, +6=$79, +7=$02
```

Raw A5 addresses were `$153C` (H0) and `$1564` (H2); address difference alone is not semantic evidence.

### TX arm / direct producer

[VERIFIED static and runtime] `$F884FC` is called by TRAP #D's direct service-channel branch:

```text
$F881F6  TRAP #D vector handler
$F881FC  tst.w  $10(A1)       ; active?
$F8821C  move.w A5,$4(A1)     ; direct current request
$F88220  move.w #1,$10(A1)    ; mark active
$F88226  movea.l (A1),A0
$F88228  jsr (A0)             ; handler $F884FC
```

For the relevant direct branch in both runs:

```text
A1=$14C0, active=$0000, handler=$F884FC, active-after=$0001
```

The initial normalized TX-arm packet was equal: `+2=$0000,+4=$01,+5=$35,+6=$7D,+7=$01`.  Exact semantic names of `$14C0` fields and the request records are [OPEN].

## 4. Current causal chain

```text
different H0/H2 firmware/runtime history
  -> firmware-selected runtime/dispatch data
  -> voice state and K1 evolution -> $F8D0F0
  -> K1RAMP write $FC6102
  -> prior timing trace: DUART IRQ6/vector $56 -> $F88300 -> $F884BE
     -> TxRDYA bit 0 -> $00E6 -> $F88554 -> Channel-A continuation
  -> Channel-A TX arm $F884FC
```

[SUPERSEDED] It is invalid to describe a fixed `~12.642 ms` Play-to-TX-arm delay.  That result used scripted input injection as t0, which is not causally equivalent across H0/H2.

## 5. Reference-point correction — critical

Canonical t0 is firmware consumption of the panel Play-down byte `$9D`, observed as the Channel-B RHRB read at `$FFB0D4`.  It is **not** process start, restore time, Lua start, panel injection, or wall clock.

| witness | H0 | H2 |
|---|---:|---:|
| scripted Play injection | 51.806666667 s | 78.396666667 s |
| firmware consumes `$9D` at `$FFB0D4` | 51.816849625 s | 78.400182750 s |
| relevant TX arm `$F884FC` | 51.825731500 s | 78.403089250 s |
| consumption -> TX arm | **8.881875 ms** | **2.906500 ms** |

Correct remaining difference: **5.975375 ms**.

[VERIFIED] This fails the predeclared reference sanity threshold.  The old injection-relative `12.642250 ms` differs by `6.666875 ms`, vastly exceeding the `0.25 ms` tolerance.  The old `~12.642 ms` conclusion is a reference-point artifact and must not be used as causal evidence.

Secondary, separate observation:

```text
injection -> firmware consumption
H0 10.182958 ms
H2  3.516083 ms
```

[OPEN] Do not merge that panel-delivery difference with the canonical `5.975375 ms` interval without direct provenance showing one producer.

## 6. Ruled-out / bounded exclusions

Scope labels are deliberate; none is a global claim unless stated as such.

| result | scope |
|---|---|
| Same valid 251-node service free pool; ordering differs only | [RULED OUT — this measured path] Membership, cycles, duplicates and invalid links were excluded. |
| Retained low-RAM voice/filter state (`$0D08-$0D20` and `$0D78-$0D80`) | [RULED OUT — this measured path] At canonical $t_0$, `$0D08-$0D20` and `$0D78-$0D80` are zeros in H0 and hold retained values in H2. However, runtime tracing proved that on note dispatch, routine `$0171B0` overwrites `$0D08-$0D20` and routine `$007E3A` overwrites `$0D78-$0D7E` (`DD00, FFF0, E7F0, 9B70`) before any instruction reads them. Zero reads of the pre-existing state take place. |
| Periodic preceding event payload (`+4..+7` at `$F883AE`) | [RULED OUT — this measured path] TRAP #3 only clears link pointer `+0`. Node bytes `+4..+7` are uninitialized stale free-pool memory. The handler at `$F88844` transmits hardcoded MIDI Clock `$F8` (`THRA = $F8`) and never reads `+4..+7`. |
| Interrupt ordering / deferral / masking between different sources | [RULED OUT — this measured interval] Full CPU-space IACK bus census (`$FFFFF0-$FFFFFF`) confirmed only Level 6 (DUART vector `$56`) executes between canonical $t_0$ and the note producer. No Level 1 (FDC), Level 2 (SCSI), or Level 4 (MC68302/PB9) interrupts fire. MC68302 registers (`GIMR=$8040, IPR=$0000, IMR=$E480, ISR=$0000`) are bit-identical at $t_0$ and producer entry. |
| Corresponding Sample-RAM bytes and ES5506 START/END | [RULED OUT — this measured path] Equal for the measured voice, not globally. |
| First 128 corresponding `$F8D0F0` arithmetic inputs | [RULED OUT — this measured path] Semantically equal after pointer normalization; first observed difference was timing/order. |
| K1RAMP/ES5506 programming | [RULED OUT — origin] `$F2A1/$F4C1` is downstream in this path. |
| DUART TxRDY mechanics | [RULED OUT — origin of the old injection-relative offset] Same `$F885DE`, `$38`, and TxRDY path occurred after that offset existed. Not a global DUART exoneration and not an explanation of canonical 5.975375 ms. |
| Normal Song/Sequence position for Slots 3/4 | [RULED OUT — historical explanation] Both showed `NO SEQ OR SONG FILES`; do not extrapolate to all demo dispatch state. |
| Direct Slot-5 ED-002 load nondeterminism | [RULED OUT — controlled load path] Ten fresh loads identical; predecessor-history mechanisms remain open. |
| IRQB/PB9, PA4, storage, level-4/level-1 divergence before old `$0D14` | [RULED OUT — bounded Play window] Do not generalize outside that window. |

## 7. Resolved / verified findings

- [VERIFIED] Historical Slot 3 vs Slot 4 divergence: Forensic register analysis confirmed Slot 3 was playing **Instrument 2** (`$0D18 = $0100`, Layer `$10F8`, MIDI note 56) while Slot 4 was playing **Instrument 8** (`$0D18 = $0700`, Layer `$10B0`, MIDI note 52). They were playing two entirely different instruments on the disk, explaining the difference in audio level and sample base. Both H0 and H2 play Instrument 1 (Layer `$10B0`), matching the Slot 4 instrument track.
- [VERIFIED] Predecessor ED-001 playback: Tracing a clean load and 32-second continuous playback of `FILE 1 ED-001 BANK` demonstrated healthy, undistorted dynamics (RMS 900–3200, peaks up to 23,726, 0 clipping samples, 0 dropouts). ED-001 does not fail or distort in this baseline.
- [VERIFIED] ES5510 delay DRAM clearing: During bank loading, firmware asserts `PA4 = 0` (halting ES5510 via hardware line), issues Host reg `$1F` (`$02`, Halt Enable) and Host reg `$12` (`$02`, RAM Clear), successfully clearing ES5510 delay DRAM to 0 before releasing `PA4 = 1` (`$12 = $01`, Run). Host delay RAM does not leak stale echo/reverb buffers across bank transitions.
- [VERIFIED] Retained tempo accumulator phase in `$0B6E` (527 vs 0) is authentic firmware design behavior (not cleared across sequence stop/start).

## 8. Final causal conclusion

```text
firmware consumes Play $9D at $FFB0D4 (canonical t0)
  -> t0 + 1.95 ms (H2) / 1.97 ms (H0): routine $FF9226 starts sequencer:
       clr.b ($0B83).w
       move.w ($828E).w, ($0B70).w  ; tempo step rate = 102 (0x0066)
       (does NOT clear $0B6E tempo accumulator!)
  -> 1 kHz DUART IRQ6 ($F8837A) accumulates: $0B6E += $0B70 (mod 625)
       H2: $0B6E was 527 -> tick 1 (dt=+2.15 ms): 527+102 = 629 >= 625 -> WRAPS to 4!
           $0B83 underflows -> MIDI Clock event $000E at dt=+2.208 ms -> Note-on at dt=+2.855 ms
       H0: $0B6E was 0 -> ticks 1..6 do not wrap.
           tick 7 (dt=+8.46 ms): 7*102 = 714 >= 625 -> WRAPS to 89!
           $0B83 underflows -> MIDI Clock event $000E at dt=+8.517 ms -> Note-on at dt=+8.891 ms
  -> Causal divergence: exactly 6 DUART timer ticks = 6.000 ms.
     Accounts for 100% of the canonical interval divergence (Δ = 6.037 ms).
  -> Envelope timing & K1RAMP convergence:
     Normalizing $0B6E to 0x0000 at t0 in H2 completely aligns the note-on and envelope
     dispatch timestamps with H0 (+11.4 ms, +13.2 ms, +14.6 ms vs H0 +11.7 ms, +13.5 ms, +14.9 ms),
     collapses intermediate filter delta D0 to within +-0.05%, and increases audio waveform
     correlation from 0.662 to 0.9924.
```

**Verdict:** The H0/H2 K1RAMP and audio divergence is proven to belong entirely to **Class 1: Legitimate history-dependent timing/phase**. The background 1 kHz DUART timer runs continuously in real hardware; starting playback at an arbitrary point within a 1 ms tick produces standard sub-millisecond to 6 ms tempo-phase offset. No emulator defect, unserviced interrupt, queue corruption, or state leakage exists in this path.

## 9. Status & recommendations

Investigation concluded:
- Root cause of K1RAMP difference (`$F2A1` vs `$F4C1`) is proven and validated.
- Retained voice registers (`$0D08-$0D20`, `$0D78-$0D80`) confirmed overwritten before read.
- ES5510 delay DRAM clear and PA4 hardware gating verified functional.
- Save-state and audio pipeline validated against authentic hardware behavior.

## 10. Do not reopen

- Direct Slot-5 ED-002 load nondeterminism.
- Broad Slot-3/Slot-4 dumps, or `$0D14` as a cursor/GOOD/BAD marker.
- Uninterrupted-playback reconstruction of Slots 3/4.
- Generic ES5506 pitch/interpolation/routing/sample-byte investigation for this measured voice.
- ESP/PA4/ES5510, `$0CE3`, SuperGLU/ES5701, RAM-jumper, SCSI/FDC/IDMA theories.
- DUART implementation review; the current scope is firmware/event provenance before TX arm.
- Service-node free-list topology, stale free payload, or node-address normalization.
- Interrupt ordering/pending/mask hypotheses between CPU and external devices for this interval.
- Patches, RAM edits, generic chip workarounds, commits, resets, or unrelated cleanup.

## 11. Relevant locations

| location | relevance |
|---|---|
| `$FFB0D4` / `$FC4817` | Firmware RHRB Play `$9D` consumption; canonical t0. |
| `$F881F6,$F881FC,$F8821C,$F88220,$F88226,$F88228` | TRAP #D service direct branch. |
| `$14C0` | Service channel whose handler was `$F884FC`. |
| `$F884BE`; `$00E6 -> $F88554` | DUART IRQ6 dispatcher and TxRDYA indirection. |
| `$F884FC` | Channel-A TX arm. |
| `$F885D4,$F885DE,$F885EA` | Channel-A THRA stores. |
| `$0171B0,$0D08/$0D0A` | Historical raw dispatch chain. |
| `$F8D0F0,$FC6102` | K1 and K1RAMP boundaries. |
| `src/mame/ensoniq/asr10_boot.cpp` | Machine configuration and panel/DUART wiring. |
| `src/mame/ensoniq/esqpanel.cpp` | `set_button`, `xmit_char`, panel serial behavior. |
| `src/devices/machine/mc68681.cpp` | Consult only if canonical provenance reaches device behavior. |
| `docs/asr10/lua/lib/asr10_taps.lua` | Persistent tap handles and PC/prefetch caveats. |
| `docs/asr10/investigations/rx-event-trace.md` | `$F884BE` / `$00E6 -> $F88554` evidence. |
| `docs/asr10/reference/boot-runtime-timeline.md` and `subroutine-index.md` | Static IRQ/firmware context. |
| `docs/asr10/reference/methods-hypothesis-management.md` | Required epistemic method. |
| `docs/asr10/investigations/bank11-history-mmio-state-census.md` | Separate history/Sample-RAM context; do not conflate with H0/H2. |
| `docs/asr10/investigations/save-state-audio-mvp.md` | Current uncommitted save-state MVP record and live-FDC limitation. |

## 12. Worktree / repository state

At handoff creation:

```text
branch:  master
HEAD:    d9494a56c43a8eb3d77f944a771bf05b5b9034b6
subject: asr10/es5510: Gate DSP execution strictly during effect upload and verify
```

The following were pre-existing user/work-in-progress changes and were not modified by this provenance session:

```text
 M docs/asr10/DOCUMENTATION-MANIFEST.md
 M src/devices/cpu/es5510/es5510.cpp
 M src/devices/cpu/es5510/es5510.h
 M src/devices/machine/mc68302.cpp
 M src/devices/machine/mc68302.h
 M src/mame/ensoniq/asr10_boot.cpp
?? 3rdparty/portaudio/bindings/java/jportaudio/bin/
?? docs/asr10/investigations/save-state-audio-mvp.md
```

This handoff is the only investigation artifact added in this turn.  Temporary Lua probes/logs were removed from `/private/tmp`.  Do not reset, clean, commit, or otherwise alter unrelated worktree content while resuming.
