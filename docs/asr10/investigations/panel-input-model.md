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

## Verified: 68681-compatible register layout at `$FC4801`

Static ROM/OS evidence fits a 68681-compatible register file at odd byte addresses,
base `$FC4801`, stride 2. No direct even-address accesses in `$FC4800-$FC481F` were
identified in `asr10.bin`, `V161.img`, or `V350.img`; no `$00FC48xx` long-address form
was identified either. The observed long-address form is `$FFFC48xx`, which is ordinary
24-bit peripheral addressing, not low-RAM mirroring.

Register-relative coverage matters here: ROM loads `$FFFC4801` into `A0` at `$F88450`
and uses offsets from that base in the init path. Those accesses would be missed by an
absolute-literal-only search.

| reg | address | 68681-compatible role | ROM R/W | V1.61 R/W | V3.50 R/W | observed sites and immediate writes |
|---:|---:|---|---:|---:|---:|---|
| `$0` | `$FC4801` | MR1A/MR2A | 0/0 | 0/0 | 0/0 | base load only: `$F88450 207c fffc 4801` |
| `$1` | `$FC4803` | SRA/CSRA | 2/0 | 0/0 | 0/0 | reads `$F8845E`, `$F8896E` |
| `$2` | `$FC4805` | CRA | 0/7 | 0/2 | 0/2 | ROM writes `$40,$50,$20,$01,$04,$08,$20`; OS writes `$08,$04` |
| `$3` | `$FC4807` | RHRA/THRA | 3/7 | 0/2 | 0/2 | ROM reads `$F8845A,$F88480,$F8899C`; writes include `$F88844 #$F8`; OS writes D0 |
| `$4` | `$FC4809` | IPCR/ACR | 4/1 | 0/0 | 2/0 | ROM writes `$F88440 #$60` via `(8,A0)`; reads `$FB7BEE,$FB7C30,$FB7C84,$FB8154` |
| `$5` | `$FC480B` | ISR/IMR | 1/3 | 0/2 | 0/2 | ROM reads `$F884C2`; writes `#$00` `$F88400`, `#$2B` `$F88448`, `#$09` `$F8A048`; OS writes `#$00` |
| `$6` | `$FC480D` | CUR/CTUR | 0/1 | 0/0 | 0/0 | `$F88438 303c 07d0; 0188 000c` writes CTUR/CTLR as MOVEP word `$07D0` |
| `$7` | `$FC480F` | CLR/CTLR | 0/1 | 0/0 | 0/0 | low byte of same MOVEP word `$07D0` |
| `$8` | `$FC4811` | MR1B/MR2B | 0/2 | 0/0 | 0/0 | `$F884A0` loads base; `$F884A6 #$13`, `$F884AA #$0F` |
| `$9` | `$FC4813` | SRB/CSRB | 4/1 | 3/0 | 3/0 | ROM reads `$F89C56,$F89CD8,$F8B95E,$F8B96C`; writes `$F884B0 #$EE` via `(18,A0)` |
| `$A` | `$FC4815` | CRB | 0/6 | 0/0 | 0/0 | `$F88486` loads base; writes `$20,$30,$40,$50,$10`; `$F884B6 #$05` |
| `$B` | `$FC4817` | RHRB/THRB | 4/6 | 2/0 | 2/0 | reads include `$F89CEA`; writes include panel TX at `$F89AA4,$F89C48,$F89CB0,$F89CF6,$F89D0E,$F89D22` |
| `$C` | `$FC4819` | IVR | 0/0 | 0/0 | 0/0 | no identified access |
| `$D` | `$FC481B` | IP/OPCR | 1/1 | 0/0 | 0/0 | read `$FB7CA8`; clear/write `$FB90BA 4239 fffc 481b` |
| `$E` | `$FC481D` | start counter / set OP bits | 0/4 | 0/1 | 0/1 | ROM writes D0, `#$9F`, `#$D9`; OS writes D0 |
| `$F` | `$FC481F` | stop counter / reset OP bits | 1/4 | 0/1 | 0/1 | ROM reads `$F88300`; writes D0, `#$60`, `#$26`; OS writes D0 |

OS direct-access sites:

- V1.61: `$015658`/`$015678` write CRA `#$08/#$04`; `$004DAA`/`$01562A` write THRA
  from D0; `$01276C`/`$0127D2` write IMR `#$00`; `$00D82A`, `$0122F6`, `$012308`
  read SRB; `$00D842`, `$012312` read RHRB; `$014C2C` writes start/set-OP from D0;
  `$014C24` writes stop/reset-OP from D0.
- V3.50: `$0188A2`/`$0188C2` write CRA `#$08/#$04`; `$004DE0`/`$018874` write THRA
  from D0; `$013B8C`/`$01BD4E` read IPCR/ACR bit 5; `$013A14`/`$013A7A` write IMR
  `#$00`; `$00D6BC`, `$0125CA`, `$0125DC` read SRB; `$00D6D4`, `$0125E6` read RHRB;
  `$0175AA` writes start/set-OP from D0; `$0175A2` writes stop/reset-OP from D0.

Chip select: `reference/memory-map.md` decodes CS3 as `$FC4000-$FC5FFF`, read/write,
3 wait states. `$FC4801-$FC481F` is therefore inside CS3, below the SCSI candidate
window at `$FC5001/$FC5003`.

Channel A/B observations:

- Channel B is the panel path: `$F89CEA` reads `$FFFC4817` after polling `$FFFC4813`
  bit 0; panel output writes `$FFFC4817`.
- Channel B init writes MRB `$13,$0F`, CSRB `$EE`, CRB `$20,$30,$40,$50,$10,$05`.
  CSRB `$EE` selects selector `$E` for both Rx and Tx on channel B.
- Channel A init is explicit:
  `$F88410 117c 0013 0000` writes MR1A `#$13`, `$F88416 117c 0007 0000` writes
  MR2A `#$07`, and `$F8841C 117c 00ee 0002` writes CSRA `#$EE`.
- ACR is written at `$F88440 117c 0060 0008`, i.e. `$FC4809 <- #$60`; CTUR/CTLR are
  written by `$F88438 303c 07d0; $F8843C 0188 000c`, i.e. MOVEP word `$07D0`.
- [Verified] CSRA = `$EE` väljer selector `$E` för både Rx och Tx.
             På 68681-kompatibel registerlayout innebär selector `$E` IP2/16.
             Kanal A använder därmed extern klocka via IP2 och inte den
             interna BRG:n. ACR = `$60` sätter BRG-set 1 (utan verkan för
             kanal A), counter/timer-läge med X1/CLK som källa, och lämnar
             IP change-of-state-avbrotten avstängda.

- [Likely]   Kanal A används för MIDI. 31250 baud finns inte i den interna
             BRG-tabellen och kräver därför extern klockning; 31250 x 16 =
             500 kHz på IP2 är en naturlig kandidat.

- [OPEN]     Vilken frekvens som faktiskt finns på IP2, samt vilken fysisk
             anslutning kanal A bär.
- The common counter/timer setup (`ACR #$60`, CTUR/CTLR `$07D0`) also matches the
  already documented 1.000 ms counter tick.

Current driver mapping, kept separate from ROM evidence:

- `src/mame/ensoniq/asr10_boot.cpp:1680` maps `$FC4800-$FC481F` to
  `duart_panel_asr_candidate_r/w`.
- `src/mame/ensoniq/asr10_boot.cpp:2756-2868` translates MAME word offsets to the
  low byte lane and calls `m_duart->read(word)` / `m_duart->write(word)`.
- `src/mame/ensoniq/asr10_boot.cpp:4650-4651` instantiates `SCN2681` at
  `XTAL(16'000'000) / 4` and wires its IRQ to CPU line 6.

That driver mapping agrees with the ROM register family, but remains a model choice.
The hardware conclusion above comes from firmware address use.

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

## Verified: raw-byte helpers around `$F89C80-$F89DE0`

`$F89D46` is not the entry for the receive-byte path. A pass that defines entry only as
incoming control flow from outside the investigated interval `$F89C80-$F89DE0` shows
multiple entrypoints/helpers in the interval:

| target | incoming control flow from outside `$F89C80-$F89DE0` | interpretation |
|---:|---|---|
| `$F89C88` | `$F89C6C beq $F89C88` | boundary artifact: target is inside the interval but source is just before it |
| `$F89C94` | ROM `$F87D44`, `$F87DAA`, `$F87E3C`, `$FB94B4`, `$FB94DA`; V1.61 `$0122D2`; V3.50 `$0125A6` | panel write/helper entry |
| `$F89CA2` | ROM `$FB94D2`; also `$F89DFA bra $F89CA2` just after the interval | string-output loop helper |
| `$F89CF6` | V1.61 `$0167F8`, `$016F14`; V3.50 `$01BAAC` | panel command/handshake helper |
| `$F89D46` | ROM `$F8829A jsr $FFF89D46` | error-message formatter entry |
| `$F89D8E` | `$F89DEC bra $F89D8E`, just after the interval | boundary artifact of the raw-byte loop |

Full-long data occurrences with `$FF`/`$00` fill match those same direct-call operands:
ROM has `$FFF89C94`, `$FFF89CA2`, `$FFF89D46`; V1.61/V3.50 have `$FFF89C94` and
`$FFF89CF6`. No first-0x400 vector-table entry and no `static/os-binding-table.csv`
slot targets this interval. `static/routines.csv` has no post for the interval, so no
CSV correction was made.

Internal exits in `$F89C80-$F89DE0`: `rts` at `$F89C86`, `$F89C92`, `$F89CAE`,
`$F89CC8`, `$F89CE6`, `$F89CF4`, `$F89D44`, `$F89D9A`; no `rte` or terminal `jmp` was
identified inside the interval.

Relevant disassembly from `$F89D46` to the loop:

```text
$F89D46  6100 FE96          bsr     $F89BDE
$F89D4A  247C FFF8 24AA     movea.l #$FFF824AA,A2
$F89D50  4EBA FF42          jsr     ($F89C94,PC)
$F89D54  7000               moveq   #$00,D0
$F89D56  1038 00C1          move.b  ($00C1).w,D0
$F89D5A  347C 80C0          movea.w #$80C0,A2
$F89D5E  7203               moveq   #$03,D1
$F89D60  4EBA 009C          jsr     ($F89DFE,PC)
$F89D64  4212               clr.b   (A2)
$F89D66  347C 80C0          movea.w #$80C0,A2
$F89D6A  4EBA FF36          jsr     ($F89CA2,PC)
$F89D6E  247C FFF8 24B1     movea.l #$FFF824B1,A2
$F89D74  4EBA FF2C          jsr     ($F89CA2,PC)
$F89D78  263C 000A 0000     move.l  #$000A0000,D3
$F89D7E  4EB9 FFFB 8D6C     jsr     $FFFB8D6C.l
$F89D84  6100 FE58          bsr     $F89BDE
$F89D88  4E6D               move    USP,A5
$F89D8A  4238 0C4A          clr.b   ($0C4A).w
$F89D8E  6100 FF3A          bsr     $F89CCA
$F89D92  65FA               bcs     $F89D8E
$F89D94  B23C 0023          cmp.b   #$23,D1
$F89D98  6602               bne     $F89D9C
$F89D9A  4E75               rts
$F89D9C  207C FFF8 2484     movea.l #$FFF82484,A0
$F89DA2  1230 1000          move.b  (A0,D1.w),D1
```

The two bytes skipped by `$F89D98 6602` are exactly `$4E $75`, i.e. `rts`.
Raw `$23` therefore exits before the table lookup. This is raw protocol handling, not
post-lookup firmware semantics; raw `$23` would map to `$25` if it were allowed to reach
the table.

Framing:

- `$F89D8E` calls `$F89CCA`.
- `$F89CCA-$F89CE6` polls `$FFFC4813` bit 0 (`SRB RxRDYB`) with a timeout.
- If ready, `$F89CE8 7200` clears `D1` and `$F89CEA 1239 fffc 4817` reads one byte from
  `$FFFC4817` (`RHRB`).
- No start byte, length field, or checksum was identified in this routine. It consumes
  a single already-framed DUART channel-B receive byte after SRB says one is available.

Post-lookup destination:

- `$40` sets `($0C4A).w`.
- `$17` copies `A7` to `A6`; `$16` copies USP to `A6`.
- `$30-$39`, gated by `($0C4A).w`, selects an 8-byte record by `sub.b #$30,D1`,
  `lsl.w #3,D1`, `adda.w D1,A5`; the selected pointer is dereferenced by `$F89DEE`
  (`move.l (A5)+,D0`) and sent through `$FFF97EE8` and the panel text output path.
- Non-digit mapped values loop back to `$F89D8E`.

The mapped value is not stored into a general RAM queue by this routine. It is consumed
directly as control flow and as an index into an `A5`-relative record table, which is
invisible to ordinary absolute-reference searches.

Direct-call coverage:

- Direct incoming flow to `$F89D46` exists at `$F8829A`.
- No `static/os-binding-table.csv` slot targets `$F89D46`, `$F89D8E`, or `$F89D9C`.
- The only direct branches to `$F89D8E/$F89D9C` are loop-local or boundary-local:
  `$F89D92 bcs $F89D8E`, `$F89DEC bra $F89D8E`, and `$F89D98 bne $F89D9C`.

No alternative direct caller was found that jumps into the lookup after pre-bounding
`D1` differently. Coverage: direct absolute calls, direct PC-relative branches/calls,
vector-table scan, `$FF`/`$00`-filled longword data occurrences, and binding-table
targets. Register-indirect calls (`jsr (An)`, `jmp (An)`) are not covered, so "all call
sites" means all identified direct call sites by these methods.

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

[Verified] Rå `$23` returnerar före lookupen via kodvägen `$F89D94/$F89D98`.
`bne.s` tas när `D1 != $23`; vid likhet faller flödet till `rts`.
Tabellposten `$23 -> $25` nås aldrig via denna väg.

[Likely] Rå `$23` är panelens idle-/keepalive-/no-event-byte.

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

## Verified: receive loop is active in V3.50 FILE 1 state

[Verified] Pollvägen `$F89CCA` (SRB) -> `$F89CEA` (RHRB) -> lookup finns i ROM.

[Verified] Den kör under V3.50 i `FILE 1  TUTORIAL BNK`-läget. Observation:
en 40 s körning utan injektion, utan `-log`, aktiverade mätfönstret när displaytexten
`FILE 1  TUTORIAL BNK` observerades och räknade därefter 12 läsningar av `$FC4813`
(`SRB`) och 12 läsningar av `$FC4817` (`RHRB`) före processens tidsgräns.

[Verified] `duart_panel_asr_candidate_r/w` är en handskriven MAME-handler runt en
instansierad `SCN2681`, inte en ren stub som returnerar färdiga panelbytes. `mem_map`
kopplar `$FC4800-$FC481F` till handlern (`src/mame/ensoniq/asr10_boot.cpp:1719`),
maskinkonfigurationen instansierar `SCN2681` (`src/mame/ensoniq/asr10_boot.cpp:4738`),
och läs-/skrivhandlern går via `m_duart->read(word)` / `m_duart->write(word)`
(`src/mame/ensoniq/asr10_boot.cpp:2819`, `src/mame/ensoniq/asr10_boot.cpp:2927`).

[Verified] `ASR10_PANEL_SWEEP_RAW`-injektionen och den befintliga panelharnessens
ACK/status skriver till samma hjälpfunktion och samma kanal-B FIFO:
`panel_sweep_fire()` anropar `panel_c_queue_rx(..., "panel_sweep_raw", ...)`
(`src/mame/ensoniq/asr10_boot.cpp:1953-1957`), `panel_autorespond_fire()` anropar
`panel_c_queue_rx(0xff, "autorespond_fc4817_write", ...)`
(`src/mame/ensoniq/asr10_boot.cpp:1937-1944`), och `panel_c_queue_rx()` gör
`m_duart->m_chanB->rx_fifo_push(data, 0)` (`src/mame/ensoniq/asr10_boot.cpp:1911-1917`).

[Verified] Injektionen kördes i kontrollkörningarna: stdout innehöll
`ASR10_PANEL_SWEEP event=inject raw=23 ... before="FILE 1  TUTORIAL BNK"` och motsvarande
rad för raw `$22`. Ingen post-injection `ASR10_PANEL_SWEEP event=rhrb raw=22/23` sågs
före timeout.

[OPEN] Varför de injicerade `$22/$23`-bytena inte observerades vid `$F89CEA` trots att
receive-loopen är aktiv i samma startläge. Poll i boot och avbrott i runtime är en
möjlig förklaring, men poll/poll, avbrott/avbrott, FIFO/timing och redan-köade
autorespondbytes är också förenliga med observationerna. Ingen av dem är prövad här.

[Verified] En senare no-injection-körning i samma V3.50 `FILE 1  TUTORIAL BNK`-läge
visade att den aktiva receive-konsumenten inte är ROM-vägen `$F89CCA/$F89CEA`.
Efter att startläget observerats lästes `$FC4813` 12 gånger från `$FFB0BC` och
`$FC4817` 12 gånger från `$FFB0D4`. Alla 12 RHRB-läsningar returnerade befintlig
ACK/status-byte `$FF`; FIFO-djupet gick från 1 till 0 vid varje RHRB-läsning.
Den befintliga ACK/status-harnessen anropade `panel_c_queue_rx()` 12 gånger i samma
fönster, alltid med FIFO-djup 0 före push och 1 efter push. Inga FIFO-overrun-pushar
observerades.

[OPEN] Den aktiva runtime-rutinen runt `$FFB0BC/$FFB0D4` är ännu inte statiskt
identifierad eller namngiven. `$F89CEA`-vägen finns kvar som ROM-evidens, men den var
inte den aktiva konsumenten under FILE 1-mätningen.

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
`src/mame/ensoniq/asr10_boot.cpp:1908`, while preserving that firmware performs its own
raw-to-mapped lookup at `$F82484`.

## Next evidence needed

Do not add input ports until at least one of these is true:

- a real or trusted ASR-10 panel trace identifies raw bytes for DOWN/UP/ENTER
- runtime instrumentation observes a file-browser key event and its raw RHRB byte
- static analysis identifies the consumer that labels mapped bytes as browser
  DOWN/UP/ENTER

Until then, the deterministic file-browse test remains blocked by missing input
semantics, not merely by missing `INPUT_PORTS` syntax.
