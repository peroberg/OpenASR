# Effect-object floppy byte provenance — ASR-10 V3.50

## Question

For loaded `44LUSH PLATE`, identify only the current-MAME FDC source position
of the three already established current-effect bytes:

```text
$0062B664 <- $82
$0062B665 <- $83
$0062B666 <- $01
```

The scope stops at FDC C/H/R/N plus byte offset.  It does not identify a
filesystem file, an effect record, or the semantics of `+$66`.

## Established producer boundary

The preceding investigation verified the current-model data path:

```text
uPD72069 `dma_r()`
  -> ASR board `idma_drq_w()`
  -> MC68302 external IDMA `idma_transfer_in(data)`
  -> incrementing DAPR/current-effect RAM
```

It also established that CPU-PC snapshots beside the RAM writes are scheduler
context, not CPU writer provenance.  This pass does not repeat that question.

## Method

The normal physical `Load -> Effects -> Enter/Yes` selection of `44LUSH
PLATE` was used after a live `FILE 1  TUT0RIAL BNK` boot witness.  A temporary,
state-neutral C++ observation was necessary because Lua cannot read the FDC's
private active READ DATA state at the instant `dma_r()` pops the FIFO.  It
captured, only while DAPR was `$0062B660-$0062B66A`:

* DAPR and the byte returned by `dma_r()`;
* the current external-IDMA count and its transfer-relative index;
* the command C/H/R/N and drive/head selection; and
* the FDC's matched on-disk ID header and the current data-byte slot.

The FDC context was sampled immediately before `dma_r()` and the same local
`data` variable was passed immediately to `idma_transfer_in(data)`.  The
temporary accessors, output, and Lua trigger were removed, followed by a clean
rebuild.  This is a current-MAME model observation, not an assertion about
physical ASR bus timing.

## Relevant FDC command

The target window belongs to one `READ DATA` command:

| item | observed value |
|---|---:|
| command | `$46` (MFM READ DATA in the current uPD765 model) |
| drive/head select byte | `$00` |
| drive | 0 |
| selected head | 0 |
| command C/H/R/N | `76 / 0 / 15 / 2` |
| matched on-disk ID C/H/R/N | `76 / 0 / 15 / 2` |
| sector size | 512 bytes |
| external-IDMA raw BCR | 3073 |
| active transfer length | 3072 bytes (`BCR - 1`, existing model convention) |

The match between command CHRN and the scanned on-disk ID header is the
positive control that the byte offsets below belong to the intended data field,
not merely a command request that happened to be pending.

## Exact byte mapping

The FDC data-slot index is zero-based within the current sector.  `transfer
index` is likewise zero-based within this external-IDMA arm.  The bounded
neighbour window was:

| FDC C/H/R/N, offset | FDC byte | IDMA transfer index / remaining before | DAPR destination |
|---|---:|---:|---:|
| `76/0/15/2`, 96 | `$20` | 96 / 2976 | `$0062B660` |
| `76/0/15/2`, 97 | `$20` | 97 / 2975 | `$0062B661` |
| `76/0/15/2`, 98 | `$00` | 98 / 2974 | `$0062B662` |
| `76/0/15/2`, 99 | `$00` | 99 / 2973 | `$0062B663` |
| `76/0/15/2`, 100 | `$82` | 100 / 2972 | `$0062B664` |
| `76/0/15/2`, 101 | `$83` | 101 / 2971 | `$0062B665` |
| `76/0/15/2`, 102 | `$01` | 102 / 2970 | `$0062B666` |
| `76/0/15/2`, 103 | `$00` | 103 / 2969 | `$0062B667` |
| `76/0/15/2`, 104 | `$00` | 104 / 2968 | `$0062B668` |

Thus the requested three-byte provenance is:

```text
drive 0, head 0, cylinder 76, sector 15, N=2 (512 bytes), offset 100: $82
  -> FDC `dma_r()` -> external IDMA -> $0062B664
drive 0, head 0, cylinder 76, sector 15, N=2 (512 bytes), offset 101: $83
  -> FDC `dma_r()` -> external IDMA -> $0062B665
drive 0, head 0, cylinder 76, sector 15, N=2 (512 bytes), offset 102: $01
  -> FDC `dma_r()` -> external IDMA -> $0062B666
```

## Sector-boundary and byte-preservation verdict

`+$64`, `+$65`, and `+$66` are in the same sector; no sector boundary is
crossed.  The nine-byte surrounding sequence has matching FDC, IDMA and DAPR
order.  This rules out attributing the alignment to a lone coincidental `$01`.
The witness establishes byte preservation at this current-model boundary:

```text
FDC sector byte
  -> `idma_transfer_in` argument
  -> DAPR byte at the same transfer index
```

## Hypotheses

| hypothesis | verdict | reason |
|---|---|---|
| H1 `+$64/+65/+66` are consecutive FDC bytes | SUPPORTED | offsets 100/101/102 and IDMA indexes 100/101/102 |
| H2 all three are in one floppy sector | SUPPORTED | same verified `76/0/15/2`, offsets inside 512-byte field |
| H3 exact sector-relative source offset for `+$66` can be established | SUPPORTED | offset 102, with command and matched ID-header witness |
| H4 FDC -> IDMA -> RAM is byte-preserving here | SUPPORTED | bounded nine-byte neighbour correlation through the same call chain |
| H5 this sector location is already an effect-file field | OPEN | no filesystem, file allocation, record, or field analysis was performed |

## What this establishes, and what it does not

**[Verified current-MAME transport provenance]** the `$82/$83/$01` object
bytes arrive from drive 0 / head 0 / cylinder 76 / sector 15 at offsets
100/101/102 through the existing FDC-to-external-IDMA path.

It does **not** establish which file owns that sector range, whether these are
serialized object bytes or loader-transformed bytes, or any meaning for
`+$66=$01`.  In particular, no claim about `$0CE3`, ACTV, pitch, sample rate,
ES5701, or physical clocking follows from this transport measurement.

```text
TRANSPORT PROVENANCE: VERIFIED (current MAME model)
FILE-FORMAT FIELD: OPEN
FILE-FORMAT HANDOFF: JUSTIFIED
CONSTRUCTION CLASS: unchanged / UNKNOWN
MODEL CHANGE JUSTIFIED: NO
```

The handoff is justified only because the transport question is now complete.
The next domain is a bounded filesystem/file-record mapping from
`C76/H0/R15/offset 100..102`; it must not assume that the bytes are an explicit
sample-rate field.

## Single next experiment

Map this verified C/H/R/N/offset range to the V3.50 filesystem allocation and
the selected 44LUSH effect record, stopping before assigning field semantics.
