# MC68302 Vector Origin Map

Flyttad från `~/develop/mc68302/docs/mc68302/vector-origin-map.md`
2026-07-30. Triage: vektorformeln och tabellerna är hårdvarufakta,
oförändrade. Kolumnen "Current implementation" (sidoprojektets egen
kod) borttagen. ASR-10-korrelationsavsnittet är runtime-bevis, behållet
men omskrivet till att inte referera sidoprojektets interna klasser.
Inte använt i fas 3 steg 1. Se `docs/mc68302/README.md`.

This document records where interrupt vectors originate in the MC68302
model. It separates MC68302 vector generation from the 68000 vector-table
fetch and from board-specific external interrupt sources.

Primary source: MC68302 User's Manual Sections 3.2.1 through 3.2.5,
especially Table 3-5, "Encoding the Interrupt Vector".

## Vector Construction

When the MC68302 interrupt controller supplies a vector, the final 8-bit
vector is:

```text
vector = (GIMR.V7_V5 << 5) | source_low_5
```

For the ASR-10 observed `GIMR = 0x8040`:

```text
GIMR.V7_V5 = 0b010
prefix     = 0x40
```

The 68000 then computes:

```text
vector_table_address = vector * 4
handler_pc           = read32(vector_table_address)
```

The vector-table longword fetch is a CPU memory operation after IACK; it
is not the same bus cycle as the IACK vector byte.

## Internal Level-4 Sources

| Vector at `GIMR=0x8040` | Table address | Source | Trigger origin | CPU level |
|---:|---:|---|---|---:|
| `0x4F` | `0x013C` | PB11 | Port B interrupt input 3 | 4 |
| `0x4E` | `0x0138` | PB10 | Port B interrupt input 2 | 4 |
| `0x4D` | `0x0134` | SCC1 | SCC1 event register/mask | 4 |
| `0x4C` | `0x0130` | SDMA bus error | SDMA bus error reporting | 4 |
| `0x4B` | `0x012C` | IDMA | IDMA CSR normal/error event | 4 |
| `0x4A` | `0x0128` | SCC2 | SCC2 event register/mask | 4 |
| `0x49` | `0x0124` | Timer 1 | TER1 reference/capture event | 4 |
| `0x48` | `0x0120` | SCC3 | SCC3 event register/mask | 4 |
| `0x47` | `0x011C` | PB9 | Port B interrupt input 1 | 4 |
| `0x46` | `0x0118` | Timer 2 | TER2 reference/capture event | 4 |
| `0x45` | `0x0114` | SCP | SCP event | 4 |
| `0x44` | `0x0110` | Timer 3 | software watchdog timer event | 4 |
| `0x43` | `0x010C` | SMC1 | SMC1 event | 4 |
| `0x42` | `0x0108` | SMC2 | SMC2 event | 4 |
| `0x41` | `0x0104` | PB8 | Port B interrupt input 0 | 4 |
| `0x40` | `0x0100` | level-4 error | level-4 IACK with no eligible INRQ | 4 |

Example, Timer 2 under ASR-10 configuration:

```text
Timer 2 reference match
-> TER2.REF set
-> IPR bit 6 set if ORI is enabled
-> eligible only if IMR bit 6 is set and no equal/higher ISR block exists
-> CPU level 4
-> IACK returns vector 0x46
-> vector table address 0x118
-> CPU loads handler PC from memory at 0x118
```

With observed ASR-10 `IMR=0x0000` (early boot), this chain stops before
CPU level assertion: Timer 2 can set event/pending state but remains
masked. `docs/asr10/baseline-media.md` observes `IMR` (`fc6816`) later
in the boot at `0xe480` — nonzero, meaning several sources get unmasked
after this early snapshot; which write does that and when is not yet
traced in this tree.

## External Vectors

Table 3-5 defines MC68302-generated low bits only for external levels
7, 6 and 1:

| External source | CPU level | Low bits | Vector at `GIMR=0x8040` | Table address | Notes |
|---|---:|---:|---:|---:|---|
| IRQ7 / level 7 EXRQ | 7 | `0x17` | `0x57` | `0x015C` | controller supplies only when `IV7=0`; otherwise external vector/autovector |
| IRQ6 / level 6 EXRQ | 6 | `0x16` | `0x56` | `0x0158` | ASR-10's SCN2681 DUART uses this path, see `docs/asr10/duart.md` |
| IRQ1 / level 1 EXRQ | 1 | `0x11` | `0x51` | `0x0144` | controller supplies only when `IV1=0`; otherwise external vector/autovector |

In normal mode, external levels 2, 3 and 5 can be encoded on IPL pins,
but Table 3-5 gives no MC68302-generated vector for them. Those levels
require an external vector source or autovector.

## CPU-Side Resolution

The CPU-side sequence is independent of source type after a vector byte
has been returned:

```text
interrupt level is sampled
-> 68000 performs IACK cycle
-> vector byte is returned by MC68302 or external/autovector mechanism
-> 68000 pushes exception frame
-> 68000 reads vector table longword at vector * 4
-> first handler instruction is fetched at the loaded PC
```

## ASR-10 Correlation

Confirmed by `docs/asr10/duart.md` and this tree's own traces:

- External DUART IRQ is board-level IRQ6 and returns vector `0x56`,
  table address `0x158`. `asr10_boot.cpp`'s `maincpu_iack_r` already
  returns this vector unconditionally for level 6 (fixed board wiring,
  not gated on any experiment flag — see `docs/asr10/fdc-map.md`).
- MC68302 Timer 2 is configured by the ROM (`TRR2=0x3F01`,
  `TMR2=0x003B`, matching `docs/mc68302/timer2-interrupt-spec.md`
  exactly) with vector `0x46`, table address `0x118`, initially masked
  by `IMR=0x0000`.

Unknown:

- Which ASR-10 board signals, if any, drive MC68302 IRQ1 or IRQ7.
- Which ASR-10 devices, if any, use PB11/PB10/PB9/PB8 as interrupt
  inputs.
- Physical destination of TOUT1/TOUT2.
- What later unmasks `IMR` to `0xe480` and whether that changes the
  boot's current stall (`docs/asr10/baseline-media.md`).

## Status in this tree

Not implemented in fas 3 step 1 (no interrupt controller in that
step). This is the vector/table reference fas 3 step 2 needs.
