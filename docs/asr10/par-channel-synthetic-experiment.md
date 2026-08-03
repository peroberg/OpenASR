# PAR channel synthetic experiment

2026-08-03. Follow-up to `par-channel-select.md`, `par-is-an-adc.md`,
`loop840-and-divisor-source.md`, and `duart-opr-dynamic.md`.

## Static channel select

`[Verified]` V350's RAM-loaded routine at `$0067EC-$006892` programs
MC68302 PBDAT low bits before each PAR consumer:

| Call | Write sequence | PB2:0 at `bsr $006864` | Consumer |
|---|---|---:|---|
| 1 | `ori.b #$07,$FC6829` | 7 | Stores result at `$0DD6`, then `DIVU.W D2,D0` at `$006800` |
| 2 | `andi.b #$F8,$FC6829`; `ori.b #$05,$FC6829` | 5 | Post-DIVU PAR consumer |
| 3 | `andi.b #$F8,$FC6829`; `ori.b #$00,$FC6829` | 0 | Post-DIVU PAR consumer |

`[Verified]` The ROM init writes `PBCNT=0x0080`, `PBDDR=0xF097`, and
`PBDAT=0x0007`. PB2:0 are therefore GPIO outputs, not peripheral pins.

`[Verified]` Static searches of V350 and the interleaved ROM found no
writer to `$FC6829` between each channel-select write and the following
`bsr $006864`. Other ROM references to `$FC6829` found in this pass were
`btst #3,$FC6829` reads.

`[Verified]` The loop at `$006864` performs eight PAR samples with
`jsr $FFFC60B0`, scales each 10-bit value into `D2` with `asl.w #6`;
`lsr.w #3`, and accumulates into `D6`. The first caller then enters
`DIVU.W D2,D0` with `D2` equal to the eight-sample sum.

## ES5506 wavetable RAM

`[Verified]` `ASR10_EXPERIMENT_ES5506_HOST=1` previously produced
1,875,158 `unmapped bank0` messages in a V350 run to `ERROR 130`, with
average speed around 294.85%.

`[Verified]` Mapping a separate ES5506 wavetable RAM space through
`set_addrmap(0, ...)` and `.ram()` removes those `unmapped bank0`
messages. The same V350 run still reaches `ERROR 130`, and average speed
in the measured run rose to about 402.26%.

`[Verified]` No ES5506 `read_port_cb` analog binding is part of that code
change.

## Temporary PAR injection experiment

Temporary instrumentation, now removed, bound synthetic PAR values only
for this measurement:

| PB2:0 | Synthetic PAR |
|---:|---:|
| 7 | `$300` |
| 5 | `$280` |
| 0 | `$200` |
| other | `$000` |

Command used:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_ES5506_HOST=1 \
ASR10_EXPERIMENT_PAR_CHANNEL_DIAGNOSTIC=1 \
SDL_VIDEODRIVER=dummy \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 60 -log
```

`[Verified]` The first three `$006864` consumers used the expected
channels:

| Read range | PB2:0 | Synthetic PAR | Resulting pattern |
|---:|---:|---:|---|
| 1-8 | 7 | `$300` | `D2=$C000` at the first `$006800` DIVU |
| 9-16 | 5 | `$280` | eight channel-5 samples |
| 17-24 | 0 | `$200` | eight channel-0 samples |

`[Verified]` PBDAT bit 3 toggled during these reads (`$17/$1F`,
`$15/$1D`, `$10/$18`), but PB2:0 remained stable for each consumer.

`[Verified]` The run executed two `$006800` DIVU instructions. Both saw
PB2:0 equal to 7 and `D2=$0000C000`; the previous zero-divisor path was
therefore avoided for the first calibration call.

`[Verified]` No `ERROR 130` panel text was observed in the 60-second
synthetic run. The run did not establish a useful later boot baseline:
after the first 7/5/0 sequence, firmware read additional PAR channels
including 2, 3, and 4, which this intentionally narrow experiment left at
zero.

## Conclusions

`[Verified]` The observed V350 boot path does not use a single fixed PAR
input for all consumers in this routine. It explicitly selects channels
7, 5, and 0 with MC68302 PB2:0.

`[Likely]` PB2:0 are a channel select for an external analog mux feeding
ES5506 PAR. This follows from GPIO output configuration, immediate
channel writes before PAR consumers, and stable PB2:0 through each
consumer. The board-level signal identity is still not proven here.

`[Hypothesis]` Later PAR channels 2, 3, and 4 are part of the same analog
scan family. They need real source identification before any permanent
ADC model or callback is added.

`[Rejected]` OPR-only channel selection for the `$006864` path. OPR is
constant at this point (`internal=$D1`, physical output `$2E`) and does
not change across the eight PAR reads.

## All-channel synthetic table experiment

2026-08-03. Temporary instrumentation, now removed, bound a named synthetic
10-bit value for every PB2:0 channel. This was an experiment only; these
values are not physical claims.

| PB2:0 | Synthetic name | Synthetic PAR |
|---:|---|---:|
| 0 | `channel_0_boot_center` | `$200` |
| 1 | `channel_1_midscale` | `$200` |
| 2 | `channel_2_midscale` | `$200` |
| 3 | `channel_3_midscale` | `$200` |
| 4 | `channel_4_midscale` | `$200` |
| 5 | `channel_5_filter_seed` | `$280` |
| 6 | `channel_6_midscale` | `$200` |
| 7 | `channel_7_reference` | `$300` |

Command used:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_ES5506_HOST=1 \
ASR10_EXPERIMENT_PAR_CHANNEL_TABLE=1 \
SDL_VIDEODRIVER=dummy \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 60 -log
```

`[Verified]` The run made 72 PAR reads, all from read PC `$FC60B0`.
Observed channels were 7, 5, 0, 2, 3, and 4. Channels 1 and 6 were not
requested in this 60-second path.

`[Verified]` The first two `$006800` DIVU instructions both saw
PB2:0 = 7 and `D2=$0000C000`, so the previous divide-by-zero cause was
removed for the channel-7 calibration path.

`[Verified]` Final state at the 60-second stop:

| Field | Value |
|---|---|
| final PC | `$F87F86` |
| previous distinct PC | `$F88118` |
| panel text | empty string |
| PAR reads | 72 |
| `$006800` DIVU count | 2 |
| `KEYBOARD TUNED` | not reached |
| panel error code | none observed |
| classification | hang/max-poll stop |

`[Verified]` The log's `ASR10_ERROR_ENTRY_STUB pc=f882de` is not the
first cause of this stop. Disassembly shows `$F882DE` is called normally
from `$F87EAC` during initialization and clears scheduler/list state. Its
diagnostic label is misleading for this run.

`[Verified]` `ASR10_ERROR139_D0_CANDIDATE` is also a diagnostic candidate
line, not an emitted panel error. No `ERROR 139 - REBOOT ?` or other
panel error text appeared.

`[Likely]` With all observed channels nonzero, the next blocker is no
longer the initial PAR divide-by-zero. The run now stalls in the
ROM scheduler/dispatcher path around `$F87F86`/`$F88118`, after the
second calibration pass and after the PAR scan includes channels 0, 2,
3, 4, and 5.
