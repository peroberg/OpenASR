# ASR-10 panel input model

Date: 2026-08-10. Scope: static verification before stimulation, starting from
`f102b959c60`.

No code changes were made. `INPUT_PORTS_START(asr10_boot)` remains empty at
`src/mame/ensoniq/asr10_boot.cpp:4610-4611`.

## Verified: current harness path

The existing harness injects panel ACK/status bytes through SCN2681 channel B, not
through MAME input ports.

Code path:

```text
panel_autorespond_fire()
  -> panel_c_queue_rx(0xff, "autorespond_fc4817_write", write_pc)
      -> m_duart->m_chanB->rx_fifo_push(data, 0)
```

Relevant code:

- `src/mame/ensoniq/asr10_boot.cpp:1872-1883`: `panel_c_queue_rx()` pushes one byte
  directly into `m_duart->m_chanB->rx_fifo_push(data, 0)`.
- `src/mame/ensoniq/asr10_boot.cpp:1894-1905`: `panel_autorespond_fire()` always injects
  byte `$FF` as the current autoresponse.
- `src/mame/ensoniq/asr10_boot.cpp:2927-2934`: panel output bytes written by firmware
  schedule the autoresponse timer.
- `src/mame/ensoniq/asr10_boot.cpp:2810-2816`: reads from `$FC4816/$FC4817` pop channel B
  RHRB when SRB had RxRDY.

Firmware dispatch path:

```text
panel byte -> DUART channel B RX-FIFO -> SRB RxRDY -> ISR bit 5 -> IRQ6
  -> $F884BE -> ($00DE).w handler -> RHRB pop
```

Supporting code/documentation:

- `$F884BE` reads DUART ISR `$FFFC480B`; bit 5 dispatches through `($00DE).w`.
- `reference/vector-map.md` and `reference/subroutine-index.md` already describe the
  channel B RX path as verified.

## Verified: raw byte mapping exists

Provenance of the earlier claim: `$F82484` and the example pairs first came from the
current driver's diagnostic log plus targeted extraction from the interleaved ROM image,
not from an independently decoded ROM index instruction.

- `src/mame/ensoniq/asr10_boot.cpp:2840-2850` reads raw RHRB byte at `$F89CEA`,
  indexes ROM table `$F82484 + raw`, and logs the mapped byte.

The ROM now has an independent static proof for that table:

```text
$F89CEA  move.b  $FFFC4817.l,D1      ; read DUART channel B RHRB
$F89D9C  movea.l #$FFF82484,A0
$F89DA2  move.b  (A0,D1.w),D1        ; raw -> mapped byte
```

Search coverage:

- Absolute bases in `$F82480-$F82490`: one hit, `$F89D9C movea.l #$FFF82484,A0`.
- PC-relative `lea (d16,PC),An` whose target lands in `$F82480-$F82490`: zero hits.
- `lea abs.{w,l}` and `movea.l #imm,An` bases in the same interval: only the hit above.

Addressing mode and table shape:

- Base load: absolute immediate long, `movea.l #$FFF82484,A0`.
- Indexing: `(A0,D1.w)`.
- Index register: `D1.w`, cleared by `moveq #0,D1` before the byte read at `$F89CEA`.
- Entry size: 1 byte.
- Lookup guard: no upper-bound `cmp`/`cmpi`, no mask, and no normalization was found
  before `$F89DA2`. The only branch before lookup is `$F89D94 b23c 0023` /
  `$F89D98 6602`, which returns on raw `$23` and falls through otherwise.
- High byte: `D1` is guaranteed zero at lookup because `$F89CE8 7200` (`moveq #0,D1`)
  precedes `$F89CEA 1239 fffc 4817` (`move.b $FFFC4817.l,D1`).
- Data-object bound: `$F824AA` is separately named by ROM code as the `ERROR ` string
  base (`$F89D4A 247c fff8 24aa`, `movea.l #$FFF824AA,A2`). Therefore the verified
  mapping prefix is `$F82484-$F824A9`, i.e. raw `$00-$25`.
- Runtime implication: if hardware or a future harness presents raw `$26-$FF`, ROM code
  would index beyond the verified mapping prefix into adjacent ROM data. No guard was
  identified in this routine.

Prediction test against targeted extraction:

| raw byte | mapped byte |
|---:|---:|
| `$15` | `$16` |
| `$0F` | `$17` |
| `$21` | `$23` |
| `$03` | `$40` |

The in-bound predictions match. The earlier `$3B -> $23` example was an artifact of
reading past the verified mapping prefix: raw `$3B` addresses `$F824BF`, inside the
adjacent `" - REBOOT ?"` ROM string, not inside the panel mapping table.

The full generated dump is `docs/asr10/static/panel-raw-map.csv`
(`raw,mapped,status,note`). It retains all 256 dumped bytes, but marks only raw
`$00-$25` as `status=mapping`; all later bytes are `status=beyond-verified-bound`.

[Likely] The 38 verified mapping entries have a grouped structure:

| high nibble | count | low nibbles |
|---:|---:|---|
| `$0x` | 8 | `0-7` |
| `$1x` | 9 | `0-8` |
| `$2x` | 6 | `0-5` |
| `$3x` | 12 | `0-B` |
| `$4x` | 3 | `0, 1, 3` |

The missing `$42` is unresolved. It may be an unpopulated position, or a value whose raw
code lies outside the verified prefix; the latter would argue that a larger logical
input range exists despite the adjacent data-object boundary. Do not name this structure
as a keypad matrix or scancode table until firmware or vendor documentation supplies
that terminology.

This proves that user/panel bytes are not necessarily consumed as raw bytes. The ROM
applies a translation table before later semantics.

## Verified: mapped-value consumer cluster

The first consumer is in the same ROM routine immediately after the table lookup:

```text
$F89D94  cmp.b  #$23,D1              ; raw value, before mapping: early exit
$F89DA6  cmp.b  #$40,D1              ; mapped value
$F89DAC  st     $0C4A.w
$F89DB0  cmp.b  #$17,D1              ; mapped value
$F89DB6  movea.l A7,A6
$F89DB8  cmp.b  #$16,D1              ; mapped value
$F89DBE  move    USP,A6
$F89DC0  cmp.b  #$30,D1              ; mapped digit lower bound
$F89DC6  cmp.b  #$39,D1              ; mapped digit upper bound
$F89DD4  sub.b  #$30,D1
$F89DD8  lsl.w  #3,D1
$F89DDA  adda.w D1,A5
```

The digit path is a computed record/slot access after mapping, not a general jump table
for all mapped panel values. This pass did not identify a separate mapped-value jump
table for `$16`, `$17`, `$23`, or `$40`.

`static/routines.csv` does not contain a routine interval covering `$F89D94-$F89DDA`;
the cluster therefore falls outside all currently named routines.

Secondary static clusters:

- ROM `$F8BB14-$F8BBDC` compares several mapped-table values (`$25`, `$23`, `$16`,
  `$30`, `$24`, `$22`, `$20`, `$21`). This is a candidate cluster only; this pass did
  not establish dataflow from the panel table to it.
- `V161.img` raw disk offsets around `$005178-$005380` contain a dense small-constant
  comparison cluster including `$17`, `$24`, `$22`, `$14`, `$11`, and digit-like
  values. As raw disk offsets these fall outside all known `static/routines.csv`
  intervals.
- `V350.img` raw disk offsets `$016AD2-$016B38` contain a similar comparison cluster.
  As raw disk offsets these also fall outside all known `static/routines.csv` intervals.

## Likely: user input uses the same channel B receive transport

[Likely] Front-panel user input enters through the same DUART channel B receive path as
panel ACK/status:

- Existing documentation (`investigations/panel-protocol.md`) describes panel input
  keys/ACK/status as arriving on RHRB and routed through the OS RX parser.
- The ROM code around `$F89CEA` explicitly treats RHRB data as a raw panel input byte and
  maps it through `$F82484`.
- No MAME input ports exist, and no separate current source for panel user events is
  implemented.

This is still not [Verified] for file-browser keys specifically. ACK/status bytes and
button events may share transport while using different packet/state contexts.

## Open: key semantics

The exact `DOWN`, `UP` and `ENTER` byte values are not identified.

Known mapped values under current focus are `$16`, `$17`, `$23` and `$40`, but the
current evidence still does not bind any of them to `DOWN`, `UP` or `ENTER` in the
V3.50 file browser. `$23` is also explicitly tested as a raw value before mapping at
`$F89D94`, so it must not be treated as only a post-table semantic code.

Therefore no `INPUT_PORTS` implementation should be written yet. A guessed mapping would
create false `executed=observed` edges in the call-graph database.

## Service documentation check

Searched vendor material only under `/Users/paroberg/develop/asr10/docs/sources/`:

- `ensoniq/deepsonic/ensoniq_asr10_manual.extracted.txt`
- `ensoniq/r-massive/service/Ensoniq_ASR10_ASR88_Service_Manual.pdf`
- `ensoniq/r-massive/schematics/Ensoniq_EPS16Plus_Schematics_Mainboard_PSU_Keypad_Display_KPC_Memory_Output.pdf`
- `ensoniq/r-massive/hardware/Ensoniq_SuperGLU_ES5701_Technical_Specification.pdf`
- R-Massive index/article/software text files

Search terms covered scancode/scan code/raw code/key code/button code/panel code,
keypad/button matrix, front panel, and named UP/DOWN/ENTER button phrases.

[OPEN] No vendor table of ASR-10 panel scancodes was identified by this method. The
service manual mentions the Enter button as a user feature and describes front-panel
mechanical service, but does not document the raw bytes or mapped bytes seen by the ROM.

## Minimal input-port shape once mapping is known

The smallest useful MAME surface is probably:

```text
PORT_NAME("Down")
PORT_NAME("Up")
PORT_NAME("Enter")
```

But each press must be converted to the authentic raw panel byte, not directly to a
post-table mapped byte, unless later evidence proves the harness should bypass the raw
mapping table.

The conversion point would need to be before channel B FIFO insertion:

```text
MAME input field transition
  -> ASR-10 raw panel byte for that key
  -> channel B RX FIFO insertion equivalent to panel_c_queue_rx()
  -> DUART RxRDYB/IRQ6 path
```

In code terms, the eventual implementation needs to bridge from an input-port transition
to the same receive side currently reached at
`src/mame/ensoniq/asr10_boot.cpp:1877`, while preserving that firmware performs its own
raw-to-mapped lookup at `$F82484`.

## Next evidence needed

Do not add input ports until at least one of these is true:

- a real or trusted ASR-10 panel trace identifies raw bytes for DOWN/UP/ENTER
- runtime instrumentation observes a file-browser key event and its raw RHRB byte
- static analysis identifies the consumer that labels mapped bytes as browser
  DOWN/UP/ENTER

Until then, the deterministic file-browse test remains blocked by missing input
semantics, not merely by missing `INPUT_PORTS` syntax.
