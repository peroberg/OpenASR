# Effect-object installation producer — ASR-10 V3.50

## Question

Loaded `44LUSH PLATE` establishes the current-effect object at `$0062B600` as
a byte stream, including `+$66=$01`.  This pass identifies the producer of
that stream and records the source-side state for the three neighbouring bytes
around `+$66`.  It does not revisit `$0CE3`, ACTV, pitch, ES5510, clocks, or
the effect-file format.

## Method and controls

The normal physical `Load -> Effects -> Enter/Yes` transition selected
`FILE 16 44LUSH PLATE`.  The committed non-display witness was
`$0E92=$0062B600`, `$0CE3=$01`, `$0062B666=$01`.

A bounded Lua write tap covered only `$0062B650-$0062B679`.  Its CPU-PC column
was concurrency context, never writer attribution.  Lua cannot identify a
common program-space write caller, so a temporary, state-neutral C++ observation
was added only to the existing MC68302 IDMA write paths and removed after the
run.  `idma_transfer_in()` was the external-source byte-feed path; the first
four ordinary boot transfers printed from that method were a live positive
control (fixed source `$FFFC5803`, destination `$000944`, changing FDC bytes).

## Destination stream

The focused Lua witness received 42 byte-lane writes in ascending address
order.  The local shape is a byte stream through the 16-bit program map:

| destination | Lua lane/value | CPU PC snapshot |
|---:|---:|---:|
| `$0062B664` | high `$82`, low `$83` | `$F87FA2`, `$F87FCC` |
| `$0062B666` | high `$01`, low `$00` | `$F87FCC`, `$F87FA0` |
| `$0062B668` | high `$00`, low `$00` | `$F87FA0`, `$F87FCA` |

The rotating `$F87Fxx` snapshots remain scheduler/interrupt-return context;
they neither identify nor exclude CPU code on their own.

## Producer witness

The temporary observation inside existing `mc68302_device::idma_transfer_in()`
reported the exact same descending-count, incrementing-destination sequence:

| nominal SAPR | destination | byte | remaining before write |
|---:|---:|---:|---:|
| `$FFFC5803` | `$0062B664` | `$82` | 2972 |
| `$FFFC5803` | `$0062B665` | `$83` | 2971 |
| `$FFFC5803` | `$0062B666` | `$01` | 2970 |

This method calls `m_s_program->write_byte(m_idma_dest, data)` and increments
DAPR.  It is therefore a direct current-model writer witness, not an inference
from the CPU PC.  The separate internal RAM-to-RAM IDMA routine did not report
an overlapping transfer.

```text
FDC `dma_r()` byte in current ASR board callback
  -> MC68302 external IDMA `idma_transfer_in(data)`
  -> DAPR `$0062B666`
  -> byte `$01`
```

`$FFFC5803` is the firmware-programmed SAPR value captured by the device.  In
the current external-IDMA model it is a fixed peripheral-source identifier; the
model deliberately does not dereference it as RAM.  The byte comes from
`m_fdc->dma_r()`, so neither a RAM source offset nor an on-disk source offset
may be invented here.

## CPU and bus-master verdict

The earlier Lua-only zero arm counter was not a sufficient negative test: it
did not observe the device's actual write call.  The direct device-path witness
supersedes it for this narrow question.

| candidate | result | evidence |
|---|---|---|
| CPU explicit copy/store | DISPROVEN for this witnessed current-model stream | matching writes originate in `idma_transfer_in()`, not a PC-correlated CPU instruction sequence |
| MC68302 external IDMA | SUPPORTED | exact DAPR/data/remaining sequence at `+$64/+65/+66` |
| MC68302 internal RAM copy | UNSUPPORTED | no overlapping internal-transfer witness |
| other RAM bus master | UNSUPPORTED in the current model | the existing IDMA method owns matching writes |
| RAM staging buffer | DISPROVEN in the current external-IDMA model | this method takes FDC callback data and does not dereference SAPR |

This identifies current MAME/firmware-model behavior only.  It does not prove
physical ASR-10 bus arbitration or `$FFFC5803` physical wiring.

## Source correlation

```text
fixed peripheral-source identifier `$FFFC5803`
  -> FDC DMA byte `$82` -> DAPR `$0062B664`
  -> FDC DMA byte `$83` -> DAPR `$0062B665`
  -> FDC DMA byte `$01` -> DAPR `$0062B666`
```

This is a verified transfer-interface correlation, not a sequential RAM-source
mapping.  The source identifier remains fixed while DAPR increments.  Mapping
the FDC bytes to sectors, file records, or effect-format fields is expressly
outside this pass.

## Hypotheses

| hypothesis | verdict | reason |
|---|---|---|
| H1 CPU produces stream | DISPROVEN for witnessed current-model transfer | direct IDMA writer witness |
| H2 MC68302 IDMA produces stream | SUPPORTED | `idma_transfer_in()` writes exact neighbours |
| H3 other bus master produces stream | UNSUPPORTED in current model | no other writer is needed |
| H4 RAM/staging buffer source | DISPROVEN in current external-IDMA model | FDC callback source, not dereferenced RAM SAPR |
| H5 source for `+$66` can be captured locally | SUPPORTED | producer, fixed source identifier and `$01` captured |
| H6 source is serialized effect data | OPEN | FDC byte not yet mapped to an effect-file representation |

## File-format handoff and next experiment

```text
FILE-FORMAT HANDOFF: NOT YET
CONSTRUCTION CLASS: UNKNOWN
```

The object population is now verified as an FDC-to-IDMA-to-RAM transfer in the
current model.  It still does not distinguish direct serialized effect bytes
from upstream disk/file staging or transformation.  The single next experiment
is to correlate this identified transfer with its disk-sector/file-record
source, retaining the same three-byte neighbour rule.

**MODEL CHANGE JUSTIFIED: NO.**
