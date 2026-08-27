# ASR-10 V3.50: ACTV audio-frame timing differential

## Question and boundary

This pass measures the current emulator after the already-proven effect-mode transition. It does not alter clocks, serial wiring, ES5510, ES5701, audio routing, PB3 generation, or firmware state.

The physical ASR-10 PB3 signal may be LRCLK, but that is not a physical conclusion here. In this driver it is a synthetic, firmware-facing GPIO stimulus; its board source and edge-vs-period meaning remain open.

## Starting evidence and method

Earlier V3.50 work establishes:

```text
effect descriptor +$66 -> $0CE3 -> $0D5E -> D3/D4
                         -> $00E826 -> ES5506 ACTV
```

Temporary `lua/audio_frame_timing_actv_temp.lua` used only ordinary physical panel edges. It first loaded 44LUSH, then used the verified FX selector to establish ROM-01 HALL REVERB. The measured A -> B -> A sequence was:

| phase | committed source/cursor | `$0CE3` | stable ACTV |
|---|---|---:|---:|
| A | `FFF9B626` / `FFF9B948` (ROM HALL REVERB) | `00` | `$1F` |
| B | `0062B600` / `0062C342` (44LUSH PLATE) | `01` | `$17` |
| A2 | `FFF9B626` / `FFF9B948` (ROM HALL REVERB) | `00` | `$1F` |

44LUSH is independently documented as 44.1 kHz / 23 voices. User-facing 31/23 voices versus OTTO's 32/24 slots remains `[Likely]`, not an allocator proof.

The probe retained its taps. It counted only host reads in the known `$F97574-$F9757A` verification window, comparing the device byte with D2. B produced 549 comparisons / 0 mismatches; A2 produced 822 / 0. Both had `$00C0=$00` and `$0E8C=$00` (error/retry). This is a positive firmware-commit witness, not a display-only oracle.

For each stable phase it read firmware-visible PBDAT bit 3 every 5 us for one emulated second: 200,000 samples. A narrow write tap on `$FC205E-$FC205F` captured the ACTV host byte lane and writer PC.

## Runtime measurements

| phase | samples | window (s) | PB3 transitions | edge cadence (Hz) | full high-low cycle (Hz) | high fraction |
|---|---:|---:|---:|---:|---:|---:|
| A | 200,000 | 0.999995 | 44,099 | 44,099.220 | 22,049.610 | 0.500000 |
| B | 200,000 | 0.999995 | 44,099 | 44,099.220 | 22,049.610 | 0.500000 |
| A2 | 200,000 | 0.999995 | 44,099 | 44,099.220 | 22,049.610 | 0.500000 |

The one-edge shortfall from nominal 44,100 is the sampled-window endpoint: `44,099 / 0.999995 = 44,099.220`. It is identical in all phases and consistent with the configured 44,100-Hz toggle callback. Thus this is a live runtime witness that the current PB3 stimulus is invariant across the reversible ACTV transition.

| transition | time (s) | post-write PC | D4 | bus data |
|---|---:|---:|---:|---:|
| setup -> A | 19.850526 | `$00E82A` | `$1F` | `1F1F` |
| A -> B | 25.954016 | `$00E82A` | `$17` | `1717` |
| B -> A2 | 29.850525 | `$00E82A` | `$1F` | `1F1F` |

`$00E82A` is the post-instruction PC for the actual store at `$00E826`. The preceding `$00E7FA` writes `$00` during reinitialisation; it is not the stable mode value. Observed order is effect upload/readback -> commit state (`$0CE3`) -> `$00E826` ACTV store -> stable PB3 measurement.

## Current ES5506 stream model

`src/devices/sound/es5506.cpp` makes ACTV a runtime stream-rate control:

```text
m_active_voices = data & 0x1f
m_sample_rate = m_master_clock / (16 * (m_active_voices + 1))
m_stream->set_sample_rate(m_sample_rate)
```

The sound update loop iterates `v = 0 .. m_active_voices` once per stream sample. This is `[Verified MAME code]`; the live `$00E826` writes prove both paths are reached. The stream's private rate is not exposed to Lua, so Lua did not directly count generated samples (`-sound none` was also used).

With the current direct `XTAL(30'476'180)` ES5506 configuration:

| phase | ACTV / slots | MAME stream rate (Hz) | ratio to A |
|---|---:|---:|---:|
| A | `$1F` / 32 | 59,523.7890625 | 1 |
| B | `$17` / 24 | 79,365.0520833 | 4/3 |
| A2 | `$1F` / 32 | 59,523.7890625 | 1 |

These are derived arithmetic from the verified generic implementation and runtime ACTV writes, not physical-ASR sample-rate measurements.

## Differential, consequence, and verdict

`asr10_boot.cpp` arms `m_lrclk_timer` at 44,100 Hz on reset and its callback only flips `m_lrclk_level` then calls `set_pb_input(3, ...)`. It has no effect-mode input. ES5506 is directly configured at Y2 and its ACTV handler has no PB3 input. The current emulator is therefore internally consistent, but splits timing domains:

```text
firmware mode -> ACTV -> generic ES5506 stream rate: changes (4/3)
driver PB3 timer -> MC68302 input: invariant (44.1k toggles/s; 22.05k cycles/s)
```

No additional reversible board/frame-control write was found in the narrow ACTV window. Earlier targeted mode-consumer work also found no mode-dependent MC68302 GPIO/PBDAT, CS1, ES5510-control, or other ES5506 global-register write beside ACTV. This is negative evidence only for the examined current paths, not proof physical ASR-10 has no board clock control.

**[PARTIAL / current MAME model]** Firmware's ACTV change automatically changes MAME's generic ES5506 stream rate, but the driver-level PB3/frame stimulus does not follow it. This supports neither a physical oscillator selection nor an ES5701 role; both remain `[OPEN]`.

A global rate mismatch can plausibly contribute to pitch/duration error. The mode-0 current stream rate is about 2x the documented 29.7619-kHz class: +1200 cents / one octave and half duration if that compared rate is the relevant playback clock. It does not explain wrong instrument/sample assignment or arbitrary blips. The B-to-A current-MAME ratio is 4/3, not 44.1/29.7619, so MAME does not represent the documented pair as one coherent two-mode timing system.

## Evidence status and next minimal experiment

- `[Verified firmware/runtime]` A/B/A `$0CE3` and ACTV `$1F/$17/$1F`, plus successful ES5510 host verification.
- `[Verified MAME runtime]` PB3 is live and invariant at 44,099.220 sampled toggles/s (22,049.610 full cycles/s) in A, B and A2.
- `[Verified MAME code]` PB3 derives from an independent 44,100-Hz timer; ACTV recomputes and applies ES5506's stream sample rate.
- `[Derived arithmetic]` 59,523.7890625 / 79,365.0520833 Hz and 4/3 ratio.
- `[Hardware inference]` none: PB3 source and all board clock/frame routing remain open.

Do not change a clock. The next smallest experiment is to identify one firmware consumer of PB3 with a measurable edge/period contract, then compare that contract with documented mode timing. Only a consumer-proven board-facing control can justify a future clock/frame model.
