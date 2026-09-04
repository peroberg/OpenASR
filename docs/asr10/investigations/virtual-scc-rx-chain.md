# Virtual SCC RX chain experiment

Date: 2026-08-23
OS: V3.50 (`V350.img`)
Machine: `asr10booth`

## Question

Can the existing firmware accept a controlled SCC-like receive completion,
leave `WAITING`, program IDMA, copy the received range to the recording
destination and run the normal completion handler?

This experiment does not identify the physical ADC-to-SCC wiring and does not
implement an SCC, CP, ADC, Port B source or interrupt controller.

## Boundary inventory

The current MAME boundary cannot perform this experiment in observation-only
Lua:

- SCC parameter RAM and registers are shadow storage without CP side effects;
- no existing hook fills RX descriptors or raises SCC events;
- Lua can write memory and observe IACK but cannot assert the CPU's level-4
  input with a device vector;
- `mc68302_device::idma_transfer_in()` can accept a board-supplied byte, but the
  current IDMA model does not dereference SAPR;
- the only production caller supplies FDC bytes on DRQ.

A temporary, environment-gated driver bridge was therefore used. It was active
only with `ASR10_EXPERIMENT_VIRTUAL_SCC_RX=1` and was removed after the
measurement. Normal behavior with the variable absent was unchanged.

## Controlled source

The bridge waited for sampling state `$0D04=2`, then used SCC1's firmware-owned
active descriptor. It did not substitute an object, descriptor address,
destination or state-machine value.

```text
descriptor  $FC6400
buffer      $F76600
MRBLR       $0320 (800 bytes)
status      $D000 -> $5000 (E/ownership cleared, wrap/status preserved)
length      $0000 -> $0320
SCCE1       high byte -> $01 (RX event)
IACK        level 4, vector $4D
```

The deterministic 800-byte payload used its byte offset as recognizable data,
except each 16-byte-stride sample word was zero. The sample at offsets
`$40/$41` was `$7FFF`. This makes live `$FFD54A` encounter four below-threshold
samples and one explicit above-threshold sample after 64 bytes. It also leaves
more than the measured 58-byte pretrigger extent available.

After firmware programmed IDMA, the bridge accepted only an SAPR/BCR range
fully contained in that injected descriptor. It supplied bytes from the exact
firmware-selected SAPR through the existing `idma_transfer_in()` entry point.
On that model's completion return it delivered level-4 vector `$4B`. It did not
write the recording destination directly.

Observation was separate in
`../lua/archive/virtual-scc-rx-chain-probe.lua`. SIB taps were installed after
the final BAR relocation. The positive witness counted all writes in
`$000000-$05FFFF` throughout the measured window.

## Runs

### Calibration run: 64-byte counter

The first source verified the front of the path but did not leave WAITING:

```text
level-4/$4D IACK                  1
$00643C                           reached
$0064BA                           reached
active descriptor index          0 -> 1
$0065CC / $00665C                not reached
IDMA writes                       0
other low-RAM write witness       575683
```

Static follow-up located the missing condition. Live `$FFD54A` samples one
signed word every 16 bytes and compares its magnitude with `$FFD160`;
`$00653E` then requires the
received extent to exceed `$FFD15C=58`. The counter did not provide a
controlled threshold crossing. This was an input-vector failure, not evidence
against the downstream chain.

### Discriminating run: 800-byte threshold vector

Pre-injection state:

| field | value |
|---|---:|
| sampling state `$0D04` | `$0001` (Level-Detect) |
| active SCC1 descriptor | `$FC6400`, index 0 |
| status / length | `$D000 / $0000` |
| buffer / MRBLR | `$F76600 / $0320` |
| IDMA CMR | `$0002` |
| IDMA SAPR / DAPR / BCR | `0 / 0 / 0` |
| recording destination | `0` |

Observed chain after Enter-Yes:

```text
20.856300  descriptor length <- $0320, E cleared, SCCE bit 0 set
20.856303  level-4 IACK vector $4D
20.856313  $00643C scc_rx_common
20.856328  $0064BA scc_received_range_continue
20.856445  $0065CC state <- 3
20.856511  $00B478 range_to_idma
20.856526  SAPR <- $F76606
20.856527  DAPR <- $02C110
20.856528  BCR  <- $031A
20.856529  CMR  <- $37A1
20.856600  first destination write at $02C110, value $06
20.856602  level-4 IACK vector $4B
20.856618  $00AA48 idma_complete
```

Firmware selected `$F76606`, not the beginning of the descriptor. This is
consistent with a threshold at offset 64 and 58 bytes of retained pretrigger
history: `$40-$3A=$06`. BCR `$031A` is the remaining range
`$F76606..$F76920`.

Post-completion state:

| observation | result |
|---|---:|
| display | `RECORDING 272 SEC LEFT` |
| level-4 `$4D` / `$4B` IACK | 1 / 1 |
| object destination `+$20` | `$02C110 -> $02C42A` (`+$031A`) |
| object pending range count | `0` |
| IDMA CSR | `$01` in its high byte (`$0100` word read) |
| destination writes | 793 |
| exact deterministic-byte matches | 793 |
| mismatches in the 794-byte firmware range | 1, the final byte |
| live low-RAM write witness | 581391 |

The one-byte discrepancy is not an unknown SCC stage. The present IDMA model
explicitly initializes its working count to `BCR-1`, a convention derived from
the earlier FDC transfer where raw BCR was 513 for 512 bytes. Here firmware
programmed an exact byte range of 794 bytes, so the same convention copied 793
bytes and left the final destination byte untouched. Firmware nevertheless ran
the normal `$4B/$00AA48` completion and advanced the object destination by the
full `$031A`.

`$02C110` is the verified V3.50 recording destination for this run. It is not
inside the current driver's `$100000-$1FFFFF` shared sample-RAM window. The
physical decode/banking from this CPU address to installed sound memory remains
`[OPEN]`; this experiment makes no `mem_map` claim.

## Result and status

**`[Verified runtime]`:** ASR-10 firmware accepts a CP-shaped SCC1 RX
completion, applies its real threshold/pretrigger logic, leaves `WAITING`,
reaches `RECORDING`, programs the received subrange into IDMA and runs its real
IDMA completion continuation.

**`[Verified runtime, current model]`:** the existing IDMA byte-input path wrote
793 deterministic source bytes to the firmware-selected recording destination,
and every written byte matched.

**`[OPEN device implementation]`:** CP/SDMA descriptor production itself was
emulated by the temporary bridge, not executed by an SCC model. The physical RX
source and wiring remain `[OPEN]`/`[Likely]` exactly as documented elsewhere.

**`[DISPROVEN for exact recording transfer]`:** the current globally applied
`BCR-1` IDMA convention is not correct for this SCC memory-range transfer. It
drops one byte while firmware advances by BCR.

The firmware chain is functionally present; no additional unknown firmware
stage was found between a valid SCC receive completion and recording IDMA
completion. The remaining observed blocker is the known, explicit one-byte
IDMA model convention, plus the still-unimplemented producer side of SCC/CP.

## Hypothesis revision

| hypothesis | previous | observation | new status / falsifier |
|---|---|---|---|
| firmware accepts SCC RX as recording data | `[Verified firmware path]`, live transfer `[OPEN]` | `$4D -> RECORDING -> $37A1`, 793 exact destination bytes | `[Verified runtime]` for a CP-shaped virtual completion; falsified by a repeatable valid completion that does not reach `$00B478` |
| current IDMA copies firmware BCR exactly | `[OPEN]` outside FDC | BCR 794, 793 writes, one final-byte mismatch | `[DISPROVEN]`; a corrected per-mode count must reproduce both 512/513 FDC and 794/794 SCC cases |
| SCC CP itself works in MAME | unimplemented | bridge fabricated documented CP side effects | unchanged `[OPEN device implementation]` |
| physical SCC RX source is analog-board audio | `[Likely]` | no physical source was exercised | unchanged; schematic/continuity or captured pins remain discriminating evidence |

## Reproduction provenance

The retained Lua observer is
`../lua/archive/virtual-scc-rx-chain-probe.lua`. It requires the temporary
bridge described above and is archived as measurement provenance, not as a
standalone current-machine test. No `-log` was used.

The full regression passed before the bridge was added. After the measurement,
all temporary C++ instrumentation was removed and the normal tree was rebuilt
and regressed again.
