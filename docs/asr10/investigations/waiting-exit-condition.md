# WAITING state and recording exit condition

Date: 2026-08-23
OS: V3.50
Method: static byte-anchored disassembly, one narrow Lua observation probe, and
the ASR-10 Musician's Manual. No machine behavior was changed and no `-log` was
used.

## Result

The sampling display is maintained by V3.50 code at static RAM
`$005B6C-$005D16`. The active wait state is `$0D04=$0002`. Its event loop does
not use an SCC register, Port B bit, ES5506 register, ES5510 register, or sample
RAM in its direct exit decision. It dequeues scheduler events with `trap #5`
and has one sampling-success event tag: `$90E8`. Concurrent level-meter service
does continue to select the analog mux through PBDAT low byte and read ES5506
PAR; that traffic is not the exit branch itself.

The only literal `$90E8` producer in the V3.50 image is in the normal
continuation of `scc_rx_common`:

```text
SCC1/SCC2 interrupt
  -> $00643C scc_rx_common
  -> return to SCC1 ISR $008D56 or SCC2 ISR $008D92
  -> ISR channel gate ($016F) and active-state gate ($0D04 != 0)
  -> $0064BA received-buffer continuation
  -> received range crosses boundary $FFD15C
  -> $0065CC  $0D04 <- 3
  -> $00665C  post scheduler event tag $90E8 to target $23F6
  -> $006698  $0D04 <- 0
  -> WAITING dequeues $90E8 at $005C06
  -> branch $005C4C
  -> $005C6C recording setup
```

This is `[Verified firmware]`: the instruction chain and the unique producer
are direct code evidence. It proves that SCC receive processing can produce the
event which leaves sampling WAITING. It does **not** prove that SCC transports
bulk PCM. The SCC payload identity, physical byte source, and whether SCC1 and
SCC2 have different sampling roles remain `[OPEN]`.

## Reproduction

Probe: `docs/asr10/lua/archive/waiting-state-locator.lua`.

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 60 \
  -autoboot_script docs/asr10/lua/archive/waiting-state-locator.lua
```

Artifact hashes for the final run:

```text
probe        1300df13c1672250fcdf578238f3c9eb152a35c43dab774c46f1f4211cfdfb99
V350         2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b
mame         9b7db6d1953ba0ef9bcf5e900dea6be469f4570eb4dd895d5e5c7c448aa16b30
ROM image    fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf
```

The ROM hash is the byte-interleaved high/low image and matches the normative
manifest. Its string anchors are:

| ROM address | text |
|---|---|
| `$F80429` | `RECORDING ` |
| `$F8043E` | ` SEC LEFT` |
| `$F814CA` | `WAITING...` |

The final two-second WAITING window reported:

```text
display="WAITING...272 5EC LEFT"
waiting instruction fetches = 313
scc_rx_common/continuation fetches = 0
ROM WAITING candidate fetches = 0
$0D04 = $0002
$0CE8 = $00011013
$017C = $0014
$FFD15C = $0000003A
SCC event reads/writes = 0 / 0
PBDAT low-byte reads/writes = 4560 / 2280
ES5506 reads = 4560
ES5510 reads = 0
sample-RAM writes = 0
sibling low-RAM writes = 183878
```

The runtime taps were installed after `FILE 1`, after the last known BAR
relocation. The 4,560 reads and 2,280 writes at the live SIB PBDAT address are a
positive same-window witness for the zero SCC-event-register result. The 313
WAITING fetches and 183,878 sibling writes independently witness the CPU and RAM
taps. The earlier broad RECORD probe remains the stronger witness for level-4
IACK, descriptors, SCC buffers, and a 12-second sample-RAM window.

## Entry and display construction

The Level-Detect input loop is at `$005F48`. Its accepted internal Enter code is
`$23`:

```text
$005FA6  cmp.b  #$23,D2
$005FAC  bsr     $00605E       ; commit UI state
$005FB0  jmp     $0040D6       ; preparation/allocation path
```

`$0040D6` enters a preparation/allocation path with indirect callbacks. The
last indirect edge into `$005B6C` is not resolved by a direct encoded operand;
it remains `[OPEN]`. The state entry itself is independently anchored by both
the display tokens and the live `$0D04` write:

```text
$005BA4  movea.w #$14CA,A2     ; WAITING...
$005BA8  jsr     $8856.w
$005BAC  bsr     $005CDE       ; format time and " SEC LEFT"
$005BB0  clr.l   $0BD6.w
$005BB4  move.w  #2,$0D04.w    ; WAITING state
```

The write was observed live at PC `$FFBFBA` (the tap reports the post-store
PC), with `$0D04` changing from `1` to `2` at `t=20.856246s`.

The generic ROM routine `$F92FB0`, which also selects `WAITING...`, is a false
text match for sampling. It executed zero times. Its `$825C` display pointer
also remained at its boot value in the locator run.

## Loop and exit branch

The wait loop is:

```text
$005BD0  trap    #5             ; dequeue scheduler event
$005BD2  and.b   #$80,D0
$005BD6  beq     $005C2E       ; no event
$005BD8  jsr     $87F2.w       ; decode event into A2/D2
...
$005C06  cmpa.w  #$90E8,A2
$005C0A  bne     $005C0E
$005C0C  bra     $005C4C       ; accepted sampling event
```

Panel event tag `$8810` with button `$23` or `$25` also reaches `$005C4C`, but
that is the abort/manual-control branch while `$0D04` is still `2`. The helper
called at `$005C4C`, `$00C296`, is exactly one `rts`; it does not alter the
decision. The following comparison distinguishes the outcomes:

```text
$005C4C  jsr     $C296.w        ; no-op / RTS in this image
$005C50  cmpi.w  #2,$0D04.w
$005C56  bne     $005C6C        ; success setup
$005C58  ...                    ; state 2: leave/abort back to Level-Detect
```

The SCC producer clears `$0D04` after posting `$90E8`, so its event takes the
`bne $005C6C` success branch. A panel abort leaves `$0D04=2` and takes the other
path.

While receive processing temporarily holds `$0D04=3`, the no-event side can
already change the display:

```text
$005C2E  cmpi.w  #3,$0D04.w
$005C36  tst.l   $0BD6.w
$005C3C  movea.w #$0429,A2     ; RECORDING
$005C40  jsr     $8848.w
$005C44  bsr     $005CD0       ; update time-left display
```

Therefore the exact state result is:

```text
entry:            $005BA4 display construction; $005BB4 state <- 2
loop/state:       $005BD0-$005C48, scheduler event loop, $0D04=2
recording signal: SCC continuation sets $0D04=3 and uniquely posts $90E8
exit branch:      $005C06/$005C0C -> $005C4C; $0D04 != 2 -> $005C6C
exit destination: $005C6C recording/sample-object setup
```

### Producer preconditions

`$0064BA` is not fall-through from `$00643C`; the channel ISR calls `$00643C`,
acknowledges the source, then conditionally tail-jumps to `$0064BA`:

```text
SCC1 $008D7A  cmpi.b #1,$016F; require not equal and $0D04 != 0
SCC2 $008DB6  tst.b  $016F;    require nonzero and $0D04 != 0
```

In the observed WAIT state `$0D04=2`, so the active-state gate is open. The
continuation reads descriptor lengths and constructs an `A0..A1` received range.
At `$00651C-$00653E` it loads the long boundary at effective address `$FFD15C`
(absolute-short `$D15C`), subtracts the received extent, and takes the
`$0065CC` branch when the range passes that boundary. It may traverse additional
descriptors at `$006548-$0065C8` before the same result. Only this crossing sets
state 3; `$006654` then admits the `$90E8` post.

The measured boundary was `$0000003A` (58 bytes). Its live writer is:

```text
$00FA3E  bclr    #0,D1
$00FA42  move.l  D1,$D15C.w       ; effective $FFD15C
$00FA46  move.l  $0BD6.w,D2
$00FA4A  cmp.l   $D15C.w,D2
$00FA4E  bcc     $00FA56
$00FA50  subq.l  #2,D2
$00FA52  move.l  D2,$D15C.w       ; clamp to $0BD6 - 2
```

`$FFD15C` was rewritten to 58 after each UI change but did not change while
`$017C` rose from 2 to 20. Its exact semantic name is therefore `[OPEN]`; it is
verified mechanically as the received-range boundary, not as the panel's audio
amplitude threshold.

## Time-left value

`272` is not a WAIT timeout. `$005CF2-$005D02` loads `$0CE8`, shifts it right
eight bits, and passes the result to the numeric formatter. Runtime had
`$0CE8=$00011013`; `$00011013 >> 8 = $110 = 272`.

The low eight bits are discarded for display. No claim is made here about
their physical unit. `$0CE8` and the displayed 272 remained constant while
WAITING. The recording-side update reads the current object at `$0D00`, uses
its `+$24` field, and snapshots that in `$0BD6`.

## Threshold correction

The earlier probe called the BTN_0A endpoint "minimum threshold." That label is
`[DISPROVEN]`.

Direct observation showed:

```text
before BTN_0A x24: $017C = 2
writes:             3, 4, ... 20
after saturation:   $017C = 20
writer:              static $005EE4 (live tap reports the following PC $FFC2EA)
```

The code bounds the value to `0..20`. The Musician's Manual independently says
there are 21 threshold levels, Up raises the threshold, Down lowers it, and the
minimum/left endpoint starts sampling immediately on Enter.

The bounds and update are explicit in the Level-Detect input loop:

```text
$005F68  cmpi.w  #20,$017C.w
$005F70  moveq   #1,D7
$005F72  bsr     $005ECA
$005F7E  cmpi.w  #0,$017C.w
$005F86  moveq   #-1,D7
$005EE4  add.w   D7,$017C.w
```

`$005ECA-$005EF2` also sends the index through `trap #10` for UI rendering. No
instruction in this routine compares `$017C` with PAR or another audio value.

The MAME input label is the source of the old interpretation: BTN_0A is named
`Down` in the current host keymap, but firmware behavior identifies it as the
increasing/Up action. It moves `FILE 1` to `FILE 2` (the manual's Up/next-file
behavior) and increases `$017C`. BTN_0B is the inverse action. No input mapping
was changed in this task.

No direct CPU-side comparison of `$017C` against an audio sample exists in the
Level-Detect handler or WAITING loop. The display index and the SCC-generated
trigger event must remain separate facts. A translated SCC control field, an
external/audio-side consumer, and a special minimum-threshold path all remain
possible; the exact consumer is `[OPEN]`.

## Hardware relevance

| component | observed relevance |
|---|---|
| SCC1 | `[Verified firmware]` its ISR can enter the common receive and trigger-producer path. No runtime event occurred, so channel-specific identity is `[OPEN]`. |
| SCC2 | Same as SCC1. The common code cannot identify which physical source carries which data. |
| received-range boundary | `[Verified firmware/runtime]` `$FFD15C=58`; crossing it in `$0064BA` sets state 3 and enables `$90E8`. Its higher-level meaning is `[OPEN]`. |
| PBDAT low byte | 4,560 reads and 2,280 writes while WAITING. Static `$0069A2/$0069AA` performs `andi.b #$F8,$FC6829` then `or.b D0,$FC6829`: PB2-PB0 select the PAR/analog-mux channel. This is level-meter traffic, not the `$90E8` exit test. |
| PB9 | PBDAT is active, but the observed instructions address only its low byte (PB7-PB0), not PB9. No branch or writer link from PB9 to `$90E8` was found. Existing physical-source hypothesis remains `[OPEN]`. |
| PB10 | Same: the measured low-byte mux access does not touch PB10; global source status remains `[OPEN]`. |
| PB11 | Same: the measured low-byte mux access does not touch PB11; global source status remains `[OPEN]`. |
| ES5506 | 4,560 reads, exactly 1,140 each at `$FC2068/$6A/$6C/$6E`, all through MOVEP thunk `$FC60B0`. This is the already-verified PAR/ADC host register used for analog level measurement, not an ES5506 voice IRQ or proof of PCM transport. |
| ES5510 | Zero reads in the measured WAITING window. No static reference in the loop or SCC trigger producer. |
| ES5701 / glue | No separately identifiable host access in the loop. A physical trigger/glue role remains `[OPEN]`. |
| scheduler | `[Verified]` `trap #5` supplies events; `trap #8` supplies bounded sleeps. Scheduler activity maintains the loop but is not the success source. |
| timer | No timer register is polled by the loop. The displayed 272 is available recording time, not a timer exit condition. |

The PAR traffic is relevant to the Level-Detect meter but is not the identified
success writer. Conversely, the SCC path is the success writer but that alone
does not identify its payload as PCM.

## First post-WAIT operations and sample RAM

The success destination `$005C6C` clears `$0D04`, prepares the two sample
control objects referenced by `$12D8/$1320`, calls setup helpers, and returns
through `$8B4C.w`. `$005E30-$005E94` updates range and position metadata in an
object through `MOVEP`; it is not a bulk copy loop and contains no absolute
`$100000-$1FFFFF` access.

Before the WAIT exit, the SCC continuation records an `A0/A1` range at object
offsets `+$34/+$38` (`$0066C6/$0066CA`) and posts a type `$0E` service node to
target `$14DA`. It still does not copy the range to sample RAM. `$14DA` is a
generic service target already used elsewhere; its presence is not evidence
that storage IDMA performs recording.

No IDMA programming, SCC SDMA destination into the 16 MB sample-RAM window, or
external/glue DMA start was found in the bounded success path. The earliest
bulk writer and its bus master therefore remain `[OPEN]`:

```text
CPU metadata writes      [Verified firmware]
MC68302 CPU bulk writes  [OPEN]
MC68302 IDMA             no evidence in this path
SCC SDMA                 [OPEN], descriptors currently target the F74/F76 pools
external/glue DMA        [OPEN]
```

**Superseded follow-up, 2026-08-23:** this was correct for the bounded code
followed here but stopped one service dispatch too early. The later consumer
trace follows the type `$0E` node through `$14DA` to `$00B478`, where firmware
programs MC68302 IDMA from the SCC payload range to the recording destination.
See `scc-rx-source-and-consumer.md`. The intended bus master is now
`[Verified firmware]`; actual transfer and physical destination decode remain
unobserved/`[OPEN]` because no SCC RX completed.

## Hypothesis revision

| hypothesis | previous | observation | new status |
|---|---|---|---|
| BTN_0A x24 selected minimum threshold | assumed | `$017C` rose `2 -> 20`; manual and file navigation identify increasing action as Up | `[DISPROVEN]` |
| `$90E8` is the firmware sampling-success event | `[OPEN]` | WAITING accepts it; unique producer is the SCC receive continuation after `$0D04 <- 3` | `[Verified firmware]` |
| `$FFD15C` is the panel amplitude threshold | unclassified | it stayed 58 while `$017C` changed `2 -> 20`; code uses it as a received-range boundary | `[DISPROVEN]` for direct identity; higher-level meaning `[OPEN]` |
| SCC participates in sampling trigger/control | `[OPEN]` | direct SCC receive-to-`$90E8` chain | `[Likely physical role]`, `[Verified firmware mechanism]` |
| SCC carries bulk PCM | `[OPEN]` | no bulk copy, zero SCC buffers, zero sample-RAM writes | `[OPEN]` |
| SCC carries keyboard data | `[OPEN]` | no discriminating received bytes | `[OPEN]` |
| PB9/PB10/PB11 provide WAITING exit | `[OPEN]` | active PBDAT traffic is byte-wide PB2-PB0 mux selection; no high-byte access or static link to `$90E8` | not supported for this state; global source identities remain `[OPEN]` |

The narrower prediction `RECORD -> immediate ERROR 005/006` remains
`[DISPROVEN]`. This task does not restore it.

## Next discriminating experiment

Run exactly the same manual sequence, but replace BTN_0A x24 with BTN_0B until
the live `$017C` write reaches and saturates at `0`; then press Enter once.

This is the minimum experiment because the manual predicts immediate recording
at the minimum threshold. Observe only `$017C`, `$0D04`, PCs `$005C2E/$005C4C`,
event tag `$90E8`, SCC event/status, and sample-RAM writes. Do not force a RAM
flag or fabricate SCC/ADC data. Outcomes discriminate cleanly:

- immediate RECORDING without SCC RX: minimum threshold has a separate direct
  software path;
- continued WAITING at verified `$017C=0`: the model is missing an input or the
  host arrow identity needs further correction;
- SCC RX/`$90E8`: even minimum threshold is mediated by the SCC trigger path.

This experiment is proposed only. It was not run in this task.
