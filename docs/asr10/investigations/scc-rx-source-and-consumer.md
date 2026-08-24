# SCC RX source and sampling consumer

Date: 2026-08-23
OS: V3.50
Method: byte-anchored static disassembly, one narrow read-only Lua snapshot
probe, the [MC68302 IDMA register map](../../mc68302/idma-spec.md), and the
[ASR-10 service manual](../sources/ASR10_service_manual.pdf). No machine
behavior was changed, no SCC data was fabricated, and no `-log` was used.

## Result

The SCC receive path is not merely a sampling trigger. A completed SCC receive
descriptor is turned into a source range and submitted as a type `$0E` service
node. Target `$14DA` dispatches that subtype to `$00B478-$00B4E4`, which
programs MC68302 IDMA to copy the exact descriptor payload range into the
recording destination held at sampling-object offset `+$20`.

The simplest surviving physical-source explanation is therefore:

```text
analog-board A/D serial output                           [Likely]
  -> MC68302 SCC receive pins, direct or through glue    [OPEN wiring]
  -> SCC CP/SDMA fills RX descriptor buffer              [Verified mechanism]
  -> $00643C/$0064BA publishes completed byte range      [Verified firmware]
  -> type $0E target $14DA                               [Verified firmware]
  -> MC68302 IDMA copies range to recording destination  [Verified firmware]
```

This upgrades `SCC <-> audio input during sampling` from `[OPEN]` to
`[Likely]`. It is not `[Verified]` because no digital-board schematic or live
RX payload identifies the physical SCC pins. The narrower claim that the
sampling RX payload is keyboard protocol data is `[DISPROVEN]`: firmware copies
those same bytes, not a derived event, into the advancing recording
destination. A separate SCC/keyboard use outside this sampling transfer remains
`[OPEN]` until the keyboard link's digital-board termination is identified.

## Descriptor ownership and first valid-data point

Each RX descriptor is eight bytes:

| offset | firmware use | observed initial value |
|---:|---|---|
| `+0` | status/control word | `$D000`; last descriptor `$F000` |
| `+2` | received byte length | `$0000` before reception |
| `+4` | payload buffer address | SCC1 `$F76600...`; SCC2 `$F74B00...` |

The hardware/firmware ownership boundary is `$00647C`:

```text
$00643C  read SCC event byte
$006466  require SCC event bit 0
$006472  select current descriptor from control-object +$0C
$00647C  bset #7,(descriptor)       ; set E/ownership bit, test old value
```

For a normally completed descriptor, the old top bit is clear. `bset` both
tests that hardware-cleared state and returns the descriptor to SCC ownership.
An old set bit takes ERROR 005. The normal path also rejects status-byte bit 1
through ERROR 006 and loops on status-byte bit 0 before clearing the low status
byte at `$0064A4`. Top-byte bit 5 is the wrap bit: it resets the ring index;
otherwise `$0064B4` increments it.

The firmware initializes empty descriptors with the ownership bit set. No
firmware path found here fills the payload, writes a received length and clears
that bit. Those are receive-CP/SDMA side effects, followed by SCC event bit 0.
Consequently an external serial receiver input is required; firmware cannot
reach the accepted state merely by advancing its own ring counter.

The first address where received metadata is used is `$0064E6-$0064F0`:

```text
$0064E6  A0 <- descriptor +4         ; payload start
$0064EC  D0 <- descriptor +2         ; received length
$0064F0  A1 <- A0 + D0               ; payload end
```

No CPU instruction in `$00643C-$0066E8` reads a payload byte. The first payload
consumer is the IDMA bus master started later by `$00B4C0`.

## Buffer identity

The runtime probe resolves the two fixed control pointers and their range
tables:

```text
$12D8 -> object $F76400    descriptor table $FC6400
$1320 -> object $F74900    descriptor table $FC6500

range entries in both objects:
  +$0200..+$0520
  +$0520..+$0840
  ...
  +$17E0..+$1B00
```

These offsets close the apparent address gap exactly:

```text
$F76400 + $0200 = $F76600   SCC1 first payload buffer
$F74900 + $0200 = $F74B00   SCC2 first payload buffer
```

The eight 800-byte buffers are therefore embedded ranges of the two sampling
objects, not semantically anonymous addresses merely adjacent to them.
`$006608/$00660A` converts absolute payload start/end into object-relative
offsets, and `$0066C6/$0066CA` stores those offsets in a 20-byte-stride range
table beginning at object `+$28`.

`$FFD15C` is not a buffer address. It is one four-byte low-RAM scalar, measured
as 58, used only as a received-extent boundary in this image. The only direct
reader is `$00651C`; its direct writers are `$00FA42/$00FA52`. Adjacent
`$FFD160` is the separately computed panel threshold quantity. Crossing the
`$FFD15C` byte extent changes sampling state and posts `$90E8`; it does not
assign semantic ownership to a memory region.

## Type `$0E` consumer and payload copy

`$0066CE-$0066E6` posts a service node with:

```text
node +2 = $0E
node +3 = $00 in the current sampling state
node +4 = sampling object pointer
target  = $14DA
trap    = #13
```

Target callback `$00B08C` detects class `$0E` at `$00B0AC` and branches to
`$00B478`. Subtype zero then consumes the exact range table written above:

```text
$00B486  index <- object +$10
$00B48E  entry <- object +$28 + index*$14
$00B492  start <- entry +$0C       ; object-relative
$00B496  end   <- entry +$10       ; object-relative
$00B49A  byte count <- end-start
$00B4A2  SAPR <- object+start
$00B4A4  DAPR <- object +$20
$00B4B4  write IDMA SAPR
$00B4B8  write IDMA DAPR
$00B4BC  write IDMA BCR
$00B4C0  write CMR $37A1          ; start
$00B4C6  unmask IDMA interrupt
```

Against the current IDMA bit table, `$37A1` starts an incrementing-source,
incrementing-destination word transfer at maximum internal request rate with
normal/error completion enabled. The raw register programming proves the copy
even if individual field names remain only `[Likely silicon decode]`.

IDMA completion continues at `$00AA48` through the generic `$0402` dispatcher:

```text
$00AA50-$00AA5E  advance/wrap range index
$00AA62           load completed byte count
$00AA66           object +$20 += byte count
$00AA6A           decrement pending range count
```

Thus payload bytes are copied, and the recording destination advances by the
completed range length. This supersedes the earlier bounded conclusion that
the type `$0E` node had no identified bulk consumer.

## Runtime address classification

The narrow probe observed no RX and did not force completion. It only read the
objects at five existing UI milestones:

```text
before RECORD:  destination +$20 = $000000, remaining +$24 = $00000000
WAITING LEFT:   destination +$20 = $02C110, remaining +$24 = $00F44710
source mode:    $016F = 0
```

The destination is prepared by ROM `$F95EB2`:

```text
$F95EE8/$F95EEC  load the two sampling object pointers
$F95EF0           derive first destination as A3+D3
$F95EF4           SCC1 object +$20 <- destination
$F95F0A           SCC2 object +$20 <- destination, offset when mode is stereo
$F95F0E/$F95F12   both object +$24 <- available byte count
```

For source mode 0, SCC1's ISR continuation is enabled and SCC2's continuation
is gated off. The UI simultaneously displays `REC SRC=INPUTDRY LEFT`, so SCC1
is the verified firmware channel selected for this run. Physical left-channel
pin naming remains `[Likely]`, not schematic-verified.

The destination `$02C110` is outside the current driver's directly mapped
`$100000-$1FFFFF` sample-RAM candidate. Therefore:

- MC68302 IDMA is `[Verified firmware]` as the intended recording writer;
- the first intended destination for this run is `[Verified runtime]`
  `$02C110`;
- an actual transfer is unobserved because SCC RX never completes;
- physical sound-memory banking/address translation for `$02C110` is `[OPEN]`.

No `mem_map` conclusion follows from this observation.

## Physical-source classification

Independent observations now converge:

1. The service manual says the analog board converts analog audio to digital
   audio and passes it to the digital board.
2. The service procedure says microphone input crossing the displayed
   threshold changes `WAITING` to `RECORDING`.
3. ERROR 005/006 are documented as audio-input synchronization failures.
4. ERROR 009 names missing LRCLK input to the 68302 and says that clock comes
   from the analog board.
5. Firmware phases SCC receiver enable against PB3/LRCLK.
6. The completed SCC payload range is copied by IDMA into an advancing
   recording destination.

`Analog-board A/D serial stream -> SCC RX` is the simplest explanation that
survives all six. Direct ADC-to-SCC wiring, any intervening mux/glue, serial
word format, and SCC1/SCC2 physical stereo assignment remain `[OPEN]`.

The hypothesis would be falsified by a digital-board schematic showing another
source on the active SCC RX pins, or by captured SCC payload that is a framed
non-audio protocol and is transformed elsewhere before recording. A schematic
or logic capture showing analog-board serial data directly on those pins would
upgrade it to `[Verified]`.

## Hypothesis revision

| hypothesis | previous | triggering observation | new status | surviving alternative / falsifier |
|---|---|---|---|---|
| SCC participates in sampling control | `[Verified firmware mechanism]` | already-verified `$90E8` path, now followed into payload copy | unchanged; scope extended to data path | physical source still not implied by control flow |
| SCC carries sampling payload bytes | `[OPEN]` | type `$0E` programs IDMA SAPR from the completed SCC range; later virtual RX experiment copied 793 exact bytes | `[Verified firmware path]`; `[Verified runtime]` for a CP-shaped virtual completion | physical producer and CP implementation remain separate; see `virtual-scc-rx-chain.md` |
| sampling SCC RX source is analog-board A/D serial data | `[OPEN]` | SCC payload becomes recording data; manual A/D, threshold and LRCLK evidence | `[Likely]` | direct pin/glue wiring remains `[OPEN]`; schematic or payload capture can falsify |
| sampling SCC RX payload is keyboard protocol | `[OPEN]` | the same bytes are copied into the advancing recording destination | `[DISPROVEN]` | separate SCC/keyboard use outside this transfer remains `[OPEN]` |
| recording writer is MC68302 IDMA | `[OPEN]` | explicit SAPR/DAPR/BCR/CMR setup; virtual RX produced 793 exact destination writes and `$4B/$00AA48` completion | `[Verified firmware/runtime]` for the current IDMA input path | exact BCR length is `[DISPROVEN]` because the FDC-derived model copied BCR-1 |
| `$FFD15C` names a buffer/structure | `[OPEN]` | only one scalar reader and two scalar writers; value 58 | `[DISPROVEN]` | higher-level meaning of the byte-count boundary remains `[OPEN]` |

## Addendum, 2026-08-24: SCC1/SCC2 are IDMA-coupled for stereo, not independent

This document's model above is written from SCC1's own descriptor
completion outward and is correct as far as it goes, but silently reads
as if SCC1 and SCC2 each drive their own independent completion ->
IDMA event. Measured directly
(`../investigations/stereo-round-trip-verification.md`,
`../reference/interrupt-topology-gaps.md`'s 2026-08-24 addendum):
completing SCC1's own descriptor (vector `$4D`) fires **two** `$37A1`
IDMA start/complete events in the same firmware event, consuming
whatever is currently in SCC2's buffer at that instant rather than
waiting for SCC2's own independent descriptor completion. A sequential
per-channel feed (feed SCC1, wait for its completion, then feed SCC2)
therefore cannot produce two independently-verified channels — SCC1's
completion always drags SCC2's IDMA along with it, using
whatever SCC2 already holds. This matches a real synchronized stereo
ADC delivering L+R in lockstep and using one channel's completion as
the "pair ready" signal for both; it is not a bug in this model. The
working technique is an interleaved feed (one byte to SCC2, one byte
to SCC1, repeating) so SCC2's buffer already holds its own distinct
content by the time SCC1's completion fires the combined transfer.

## Next discriminating experiment

Perform one physical continuity/schematic check from the analog-board digital
audio output pins to MC68302 SCC1/SCC2 receive pins, including any intervening
glue or mux and PB3/LRCLK. This is the minimum experiment that can promote or
falsify the `[Likely]` source without fabricating input in MAME. Do not implement
an SCC source before that wiring boundary is known.

## Probe

`../lua/archive/scc-rx-owner-probe.lua` is observation-only. It uses the
documented panel sequence and reads only `$016F`, `$12D8/$1320`, and fields of
the two pointed objects. It passed twice with the same object bases,
destination, remaining count and source mode.

Reproduction command:

```sh
./mame asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_script docs/asr10/lua/archive/scc-rx-owner-probe.lua \
  -seconds_to_run 45
```
