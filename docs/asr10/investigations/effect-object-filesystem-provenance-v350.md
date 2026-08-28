# Effect-object filesystem provenance — ASR-10 V3.50

## Question

Map the already verified transport source for the loaded `44LUSH PLATE`
current-effect bytes from physical floppy position to filesystem file position,
then stop before assigning field semantics.

The input transport boundary is:

```text
C/H/R/N 76/0/15/2, sector offsets 100/101/102
  -> $82/$83/$01
  -> current-effect $0062B664/$0062B665/$0062B666
```

## Physical to logical mapping

The established V3.50 raw-image geometry is 80 cylinders, two heads, 20
sectors per track, and 512 bytes per sector.  Sector order is direct: block
number equals the zero-based sequential sector number.  Using the already
verified mapping from `reference/os-code-extraction.md`:

```text
track_index = C * 2 + H = 76 * 2 + 0 = 152
logical block = track_index * 20 + (R - 1)
              = 152 * 20 + 14
              = 3054 = $0BEE
disk byte offset = $0BEE * 512 = $17DC00
```

The requested source positions are therefore raw V350 image offsets `$17DC64`,
`$17DC65`, and `$17DC66`.

## Static image verification

The bounded raw-image window at `$17DC56` is:

```text
$17DC56: 44 52 59 20 20 20 20 20 20 20 20 20 00 00 82 83
$17DC66: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 01 68
```

Thus the static image independently contains the exact runtime witness:

| physical source | image offset | static byte | runtime destination |
|---|---:|---:|---:|
| C76/H0/R15, offset 100 | `$17DC64` | `$82` | `$0062B664` / object `+$64` |
| C76/H0/R15, offset 101 | `$17DC65` | `$83` | `$0062B665` / object `+$65` |
| C76/H0/R15, offset 102 | `$17DC66` | `$01` | `$0062B666` / object `+$66` |

This reproduces the FDC/IDMA values without a runtime probe.  The preceding
transport investigation independently established that the bytes retain this
order through `dma_r()` and external IDMA.

## Filesystem allocation owner

The canonical V3.50 directory begins at disk offset `$600`, with 26-byte
entries:

```text
+0  type.w
+2  name[12]
+14 size.w (blocks)
+16 flags.w
+18 startblock.l
+22 reserved.l
```

Directory entry 16 at `$07A0` is:

```text
$07A0: 00 21  34 34 4C 55 53 48 20 50 4C 41 54 45  00 08
$07B0: 00 01  00 00 0B EE  00 00 00 00
```

Decoded only with the established directory layout, it is:

| field | value |
|---|---|
| type | `$0021` (effect) |
| name | `44LUSH PLATE` |
| size | 8 blocks |
| flags | `$0001` (unnamed here) |
| start block | `$00000BEE` / 3054 |

The target logical block `$0BEE` is exactly this file's start block.  No
allocation-chain traversal is needed for this target: its file-relative block
index is zero.

## File-relative and runtime correspondence

```text
44LUSH PLATE file start = block $0BEE * 512 = $17DC00

file +$64 = $82 -> runtime current-effect +$64 = $82
file +$65 = $83 -> runtime current-effect +$65 = $83
file +$66 = $01 -> runtime current-effect +$66 = $01
```

The current FDC command's transfer-relative indexes 100/101/102 and DAPR
`$0062B664..666` were verified in the preceding pass.  Since this command
starts at the directory-established file start block and the target sector is
that start block, the correspondence is direct for this field, rather than an
inference from matching values alone.

The file start is a sufficient serialized-object boundary for the requested
mapping: `+$66` is file-relative `+$66` and runtime-object-relative `+$66`.
This pass does not parse further internal records or give this byte a semantic
name.

## Hypotheses

| hypothesis | verdict | reason |
|---|---|---|
| H1 target sector belongs to 44LUSH PLATE | SUPPORTED | target block `$0BEE` equals directory entry 16 startblock |
| H2 target bytes are persistent serialized file data | SUPPORTED | raw image at file `+$64..+$66` matches the FDC/IDMA/RAM sequence |
| H3 exact file-relative offset for runtime `+$66` can be established | SUPPORTED | `44LUSH PLATE +$66 = $01` |
| H4 enclosing serialized object boundary can be established | PARTIALLY SUPPORTED | file start gives object/file-relative `+$66`; no wider internal record parse was needed or performed |
| H5 serialized byte corresponding to runtime `+$66` is directly `$01` | SUPPORTED | direct block/offset and byte-preserving transfer mapping |
| H6 byte is explicit sample-rate metadata | OPEN | provenance establishes storage location, not field semantics |

## Established boundary and semantic handoff

```text
physical C76/H0/R15/offset 102
  -> logical block $0BEE
  -> V3.50 directory entry 16, type $0021, 44LUSH PLATE
  -> file offset $66 = $01
  -> FDC / external IDMA
  -> current-effect object +$66 = $01
```

```text
SERIALIZED PROVENANCE: VERIFIED
CONSTRUCTION CLASS: SERIALIZED_DIRECT (for the three mapped bytes)
SEMANTIC HANDOFF: JUSTIFIED
MODEL CHANGE JUSTIFIED: NO
```

This establishes that the particular runtime byte is directly serialized for
this 44LUSH file.  It does **not** establish that `$01` means sample rate,
clock selection, effect algorithm class, or any other named semantic.  It also
does not generalize the direct-copy result beyond the bounded `+$64..+$66`
window without another mapping experiment.

## Single next experiment

Compare serialized `+$66` across multiple independently documented 30 kHz- and
44.1 kHz-class effect files, while separately verifying their runtime objects;
stop at the correlation table before assigning semantic intent.
