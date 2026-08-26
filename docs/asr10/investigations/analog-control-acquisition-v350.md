# ASR-10 V3.50 analog/control input acquisition

Date: 2026-08-26

Baseline: `048451aef1e` (`asr10: validate display workflow stability`)

Method: static V3.50/boot-ROM disassembly plus retained Lua bus taps; no
firmware writes, no C++ behavior changes and no MAME `-log`.

## Question and evidence boundary

Question:

> Does V3.50 acquire PITCHWHL, MODWHEEL, PEDAL, VOLUME, MR. KNOB and
> REFRENCE through the ES5506's 10-bit PAR/POT input, with an external
> selector such as U55 MC74HC4051N?

The board inventory contains U29 ES5506, U55 MC74HC4051N (8:1 analog
multiplexer), U56 LM358 and U57 LM393. U30/U33 MC74HC4053 and U4
MC74HC4066 also exist. Component presence and chip capability are separate
from routing:

| Claim | Status before this run |
|---|---|
| U55 is physically present | `[Verified hardware]` |
| ES5506 exposes a 10-bit PAR/POT input | `[Verified chip capability]` |
| U55 is the controller-input mux | `[OPEN]`, strong candidate |
| U55 common pin feeds ES5506 POT_IN | `[OPEN]` |
| U55 select pins are driven by MC68302 PB2-PB0 | `[OPEN]` |
| selector-to-physical-control wiring | `[OPEN]` |

The [ASR-10 Service Manual](../sources/ASR10_service_manual.pdf), ASR Test
Procedure pages 35-36, independently names
the diagnostic controls and workflow: Command, Env 1, Right to `EXAMINE
ANALOG INPUT`, Enter, then Up through PITCHWHL, MODWHEEL, PEDAL, VOLUME,
MR. KNOB and REFRENCE. Footswitch and Patch Select appear later on the same
page but are digital tests and are not treated as PAR channels here.

## Falsifiable hypotheses

1. **Multiplexed ES5506 acquisition.** V3.50 repeatedly writes more than one
   three-bit selector value, waits, reads ES5506 PAR and stores the result.
   No PAR reads, or a serial producer writing the displayed values instead,
   would falsify this for the measured workflow.
2. **Passive diagnostic viewer.** The same scan continues at the same rate and
   with the same selector sequence in idle and every diagnostic selection;
   changing the page changes only a RAM-table reader. A page-dependent hardware
   selector or PAR rate would falsify this.
3. **U55 identity.** The runtime pattern may make U55 the likely physical mux,
   but cannot verify pin-level identity. A schematic/continuity trace from U55
   select/common pins is required for `[Verified physical routing]`.

## Exact ES5506 host-access derivation

The current ASR memory map binds the ES5506 byte host interface at
`$FC2000-$FC207F` with `.umask16(0x00ff)`. Firmware's DPRAM thunk at
`$FC60B0` is:

```text
movep.l ($68,A0),D2       ; callers set A0=$FC2001
```

The CPU-visible byte sequence is therefore:

```text
$FC2069  latch byte 31:24 = $00
$FC206B  latch byte 23:16 = $00
$FC206D  PAR bits 9:8
$FC206F  PAR bits 7:0
```

In MAME, the first host offset of a four-byte group calls the selected
register reader and the remaining three offsets drain `m_read_latch`.
Register offset `$68/8 = $0D` is PAR in low, high and test page readers, so
no PAGE write is required to identify this global register. The callback is
masked to ten bits. Lua's 16-bit address-space tap reports the aligned offsets
`$FC2068/$6A/$6C/$6E` plus the low-byte `mem_mask`; those four callbacks are
the same odd-byte MOVEP transaction, not four PAR conversions.

This derivation prevents two old errors: `$FC2069 == 0` does not mean PAR is
zero, and four bus bytes do not mean four ADC reads.

## Probe and validity controls

The temporary probe retained all handles through `lib/asr10_taps.lua` and
installed MC68302-window taps only after `FILE 1 TUTORIAL BNK` was visible.
An initial attempt installed them too early and lost the SIB taps when the
internal window was reinstalled; its PBDAT/serial zero results were discarded
under the project's section 8.5 rule. Every retained measurement below had a
continuously firing low-RAM write witness.

Captured domains:

- the complete four-byte ES5506 PAR MOVEP transaction;
- PBDAT low-byte reads/writes at `$FC6828/$FC6829`;
- low-RAM writes and reads around `$0D80-$0DFF` and viewer state
  `$0EA4-$0EA7`;
- narrow DUART channel A/B data-register accesses;
- MC68302 SCC register accesses;
- display text and fixed-duration phase labels.

PC values attached to memory taps are **access provenance**, not instruction
read-tap execution claims. The code identities below additionally come from
static instruction decoding. PBDAT write callbacks report the post-access PC
(`$0069AC/$0069B2`); the actual decoded instructions begin at
`$0069A2/$0069AA`.

## Service entry: raw `$0C` correction and raw `$0D`

After loading TUTORIAL BNK and selecting Instrument/Track 1:

```text
$06 Command     -> CREATE NEW INSTRUMENT
$0C candidate   -> QUANTIZE TRACK, then Track-command pages
```

Therefore `$0C = Env1/service entry` is `[DISPROVEN in this context]`.
It was not promoted merely because the old layout labeled it.

The ROM raw-to-mapped table gives raw `$0D -> mapped $31`, the next bounded
parameter-code candidate. One targeted test, not a sweep, produced:

```text
$06 Command
$0D             -> NO COMMANDS ON PAGE
Right           -> CALIBRATE KEYBOARD
Right           -> SOFTWARE INFORMATION
Right           -> EXAMINE DOS STATUS
Right           -> EXAMINE ANALOG INPUTS
Enter           -> PITCHWHL 64
```

This matches the service manual's Command + Env 1 diagnostic family.
`$0D = Env1` is consequently `[Verified runtime, Command/service context]`.
It is not a claim that raw `$0D` has the same action in every firmware state.

## Raw runtime summaries

The stable phase summaries were:

| Phase | seconds | complete PAR reads | PAR/s | PBDAT writes | DUART A RX/TX | SCC R/W | PAR value |
|---|---:|---:|---:|---:|---:|---:|---:|
| normal idle | 8.000 | 3,975 | 496.9 | 7,923 | 0 / 0 | 0 / 0 | `$200` |
| diagnostic menu | 4.000 | 2,000 | 500.0 | 4,000 | 0 / 0 | 0 / 0 | `$200` |
| PITCHWHL | 3.000 | 1,501 | 500.3 | 3,002 | 0 / 0 | 0 / 0 | `$200` |
| MODWHEEL | 3.000 | 1,500 | 500.0 | 3,000 | 0 / 0 | 0 / 0 | `$200` |
| PEDAL | 3.000 | 1,500 | 500.0 | 3,000 | 0 / 0 | 0 / 0 | `$200` |
| VOLUME | 3.000 | 1,500 | 500.0 | 3,000 | 0 / 0 | 0 / 0 | `$200` |
| MR. KNOB | 3.000 | 1,499 | 499.7 | 2,998 | 0 / 0 | 0 / 0 | `$200` |
| REFRENCE | 3.000 | 1,498 | 499.3 | 2,996 | 0 / 0 | 0 / 0 | `$200` |

The idle interval began close enough to residual calibration work to include
some non-steady counts; the diagnostic-menu interval is the clean 500.0/s
rate witness. Every complete read had bus provenance PC `$FC60B2`, within the
decoded MOVEP thunk. All four host bytes had identical counts.

On an analog value page, DUART channel B showed about 700 reads and 700 writes
per 3 seconds. This is the already-verified panel/display channel and follows
the roughly 9.3 Hz diagnostic redraw. It is absent on stable idle/menu screens
and is not a competing acquisition producer. Channel A remained zero. SCC
register reads and writes remained zero in every stable phase while the RAM,
PBDAT and PAR witnesses were live.

## Selector sequence and timing

The steady V3.50 loop decodes as:

```text
$006950  selector 0 -> $F8D920
$00695A  selector 2 -> $F8D992
$006964  selector 5 -> $F8D9DE
$00696E  selector 3 -> $F8DA58
$006978  selector 4 -> $F8DA98
periodically selector 7 -> $F8DB4C and reference-factor update
conditionally selector 1 -> $0172EC
```

`$0069A2` clears PBDAT bits 2:0, `$0069AA` ORs in D0, then the code yields
through `trap #8/#7` before the handler reads PAR. PB3 is the independently
toggling LRCLK input, so raw bytes alternate between (for example) `$10/$18`
for selector 0; masking with 7 gives the stable selector.

In the four-second diagnostic-menu phase, final writes (the OR instruction,
not the preceding clear) were:

| selector | reads/selections | per second |
|---:|---:|---:|
| 0 | 399 | 99.75 |
| 2 | 399 | 99.75 |
| 5 | 398 | 99.50 |
| 3 | 398 | 99.50 |
| 4 | 399 | 99.75 |
| 7 | 7 | 1.75 |
| total | 2,000 | 500.00 |

The normal five-channel sequence therefore repeats at approximately 100 Hz;
the periodic reference adds one extra conversion every 60 loops and is
observed at about 1.7 Hz. Its extra settle interval reduces the ordinary
channels slightly below 100/s, leaving the aggregate near 500/s. The measured
final-selector-write to completed-PAR-read delay in the
stable diagnostic-menu phase was mean 1,980.1 us, min 1,864.8 us and max
2,099.9 us. This is runtime evidence for settle/yield timing, not a vendor
specification for U55.

## Channel-to-firmware-state mapping

The background producer continuously updates 14-byte blocks. The diagnostic
viewer uses `$0EA4` as an index, reads one byte from the selected block in
`$F8DCD6-$F8DD02`, and stores the formatted value at `$0EA6`. The tap offsets
below are aligned 16-bit callbacks; the actual byte address follows from the
mask.

| UI selection | PBDAT selector | producer/transform | source byte read by viewer | observed tap tuple | `$0EA4` | `$0EA6` | display |
|---|---:|---|---:|---|---:|---:|---|
| PITCHWHL | 0 | `$F8D920` | `$0D8F` | `$0D8E`, mask `$00FF`, `$40` | 0 | 64 | `PITCHWHL 64` |
| MODWHEEL | 2 | `$F8D992` | `$0D9D` | `$0D9C`, mask `$00FF`, `$7F` | 1 | 127 | `MODWHEEL 127` |
| PEDAL | 4 | `$F8DA98` | `$0DAB` | `$0DAA`, mask `$00FF`, `$7F` | 2 | 127 | `PEDAL 127` |
| VOLUME | 3 | `$F8DA58` plus `$0069DA` smoothing | `$0DB9` | `$0DB8`, mask `$00FF`, `$6D` | 3 | 109 | `VOLUME 109` |
| MR. KNOB | 5 | `$F8D9DE` | `$0DC7` | `$0DC6`, mask `$00FF`, `$00` | 4 | 0 | `MR. KNOB 0` |
| REFRENCE | 7 | periodic `$0068C8` path | `$0DD6` | `$0DD6`, mask `$FF00`, `$80` | 5 | 128 | `REFRENCE 128` |

Each stable page made 28 viewer reads/writes in 3 seconds. The observed viewer
write provenance was `$F8DD06`; the decoded instruction is the preceding
`move.b D0,$0EA6` at `$F8DD02`. The additional reader PCs at `$004850` and
`$0049DA` consume `$0EA6` for display formatting.

This is the requested RAM provenance: the display does not read PAR directly.
It reads one byte selected from an already-maintained table and copies that
byte through `$0EA6`.

## A/B/A and active-versus-passive verdict

Two deterministic loops were held for two seconds per state:

```text
PITCHWHL -> MODWHEEL -> PITCHWHL
  $0D8F/$40 -> $0D9D/$7F -> $0D8F/$40

VOLUME -> MR. KNOB -> VOLUME
  $0DB9/$6D -> $0DC7/$00 -> $0DB9/$6D
```

Every window had 1,000 complete PAR reads (one MR. KNOB boundary window had
999), 2,000 PBDAT writes, the same selector distribution, the same `$200` PAR
value and approximately 1.98 ms selector-to-read delay. Only `$0EA4`, the
selected source read, `$0EA6` and display text followed the UI choice.

**[Verified runtime] `EXAMINE ANALOG INPUTS` is a passive viewer of a
continuous background acquisition table.** It does not direct the mux to the
currently displayed control and does not change PAR rate. This result is
bounded to V3.50 and the six analog pages measured.

## 10-bit to 7/8-bit transformations

The common raw reader returns 10-bit PAR, then `asl.w #6` makes a left-justified
16-bit value. V3.50 does not implement the candidate transformations as simple
`>>3` or `>>2` alone. Static code tied to the runtime table shows:

- selector 0 / PITCHWHL: reference calibration, boot-calibrated center dead
  zone `$0DDE/$0DE0`, signed scale by `$01A4`, clamp `-64..63`, then add 64;
- selector 2 / MODWHEEL: subtract `$1130`, multiply by `$0148`, take the high
  word and clamp to `0..127`;
- selector 4 / PEDAL: threshold handling around `$9AB0`, otherwise subtract
  `$0604`, multiply by `$01D8`, take the high word and clamp to `0..127`;
- selector 3 / VOLUME: subtract `$0604`, multiply by `$00E5`, high word,
  clamp `0..127`, then the OS loop slews `$0DB8` toward `$0DE4` one count at a
  time;
- selector 5 / MR. KNOB: subtract `$0604`, multiply by `$01EE`, high word and
  clamp to `0..255`;
- selector 7 / REFRENCE: the viewer uses the high byte of the filtered
  left-justified value.

These formulas are `[Verified firmware arithmetic]`. The measured current
model returned only `$200`, so real-controller endpoint curves, tolerances and
dead zones remain `[OPEN physical/runtime with real input]`.

## Current MAME input-model boundary

The ASR panel currently exposes only three adjusters:

| host adjuster | emulator channel written |
|---|---:|
| Data Entry | 3 |
| Input Level | 4 |
| Volume | 5 |

There are no ASR panel inputs for pitch bend, modulation wheel, pedal or patch
select. More importantly, `asr10_boot_state::analog_r()` selects
`m_analog_values[m_duart_io & 7]`, while V3.50 selects acquisition channels
through MC68302 PBDAT bits 2:0. The current source-level channel labels also
conflict with the firmware mapping above (firmware 3=VOLUME, 4=PEDAL,
5=MR. KNOB).

Controlled Data Entry and Volume injection at 0, 512 and 1023 did not change
PAR (`$200` throughout), the selected RAM cell, or diagnostic display. This is
`[Verified current-model limitation]`, not evidence that physical controls do
not work. The existing adjusters do not currently exercise the firmware's
measured PBDAT-selected acquisition path.

## U55 / physical conclusion

Runtime now provides this chain:

```text
continuous firmware scan 0,2,5,3,4 (+ periodic 7)
    -> PBDAT PB2-PB0 write
    -> about 1.98 ms settle/yield
    -> one ES5506 PAR read
    -> channel-specific filter/scale block
    -> persistent RAM table
```

Together with a physically present 8:1 HC4051, this is **strong evidence for a
multiplexed ES5506-PAR controller acquisition path**. U55's status advances to
`[Likely acquisition mux]`, not `[Verified acquisition mux]`. Runtime cannot
establish which PCB trace reaches which pin.

The minimum physical closure is a power-off continuity/pin trace:

```text
U55 pin 3 COM        -> ES5506 POT_IN or intervening analog stage?
U55 pins 9/10/11 S0-S2 -> MC68302 PB0/PB1/PB2 or latch/glue?
U55 pin 6 enable     -> fixed level or controlled source?
U55 X0-X7            -> wheel/pedal/slider/reference connectors?
```

U56/U57 and the 4053/4066 parts must not be assigned roles from placement
alone. A schematic or continuity measurement, not another firmware trace, is
the falsifier for U55 identity.

## Final status table

| Claim | Result |
|---|---|
| V3.50 reads ES5506 global PAR `$0D` | `[Verified runtime + source derivation]` |
| PAR reads occur in normal idle | `[Verified runtime]` |
| PAR reads occur in diagnostic menu/pages | `[Verified runtime]` |
| PBDAT PB2-PB0 supplies repeating selector values | `[Verified firmware + runtime]` |
| scan is 0,2,5,3,4 with periodic 7 | `[Verified firmware + runtime]` |
| aggregate conversion rate is 500/s | `[Verified runtime, current model]` |
| ordinary per-channel rate is about 100/s; reference about 1.7/s | `[Verified runtime, current model]` |
| diagnostic actively selects acquisition hardware | `[DISPROVEN for measured V3.50 workflow]` |
| diagnostic passively selects a RAM-table entry | `[Verified runtime + firmware]` |
| serial DUART/SCC path supplies analog values | `[DISPROVEN for measured windows]`; live witnesses, DUART B activity is display traffic |
| simple universal 10-bit `>>3`/`>>2` mapping | `[DISPROVEN firmware]` |
| per-control calibrated 7/8-bit transformations | `[Verified firmware arithmetic]` |
| U55 is the acquisition mux | `[Likely]` |
| U55 COM/select physical wiring | `[OPEN]` |
| current host adjusters exercise the measured path | `[DISPROVEN current model]` |

## Smallest next experiment

Do not implement a mux yet. First trace U55 COM and select pins on a board or
schematic. A continuity result from U55 pins 9/10/11 to PB0/PB1/PB2 and pin 3
to ES5506 POT_IN would promote the physical identity; a different destination
would falsify it while leaving the verified firmware scan intact. Only after
that should a separate implementation task replace the DUART-selected callback
with a PBDAT-selected, board-mapped input path.
