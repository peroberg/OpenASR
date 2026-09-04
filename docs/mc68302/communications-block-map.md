# MC68302 Communications Block Map

Flyttad från `~/develop/mc68302/docs/mc68302/communications-block-map.md`
2026-07-30, hårdvarufakta oförändrade. "Implementation Status"-tabellen
(sidoprojektets kod) borttagen. Relevant för fas 2:s
kommunikationsprocessor-fråga (`docs/asr10/PLAN.md`), inte för fas 3
steg 1. Se `docs/mc68302/README.md`.

Maps the MC68302 communications processor areas that produce internal
interrupts or board-visible pins. Does not implement protocol engines.

Primary source: MC68302 User's Manual Section 4, Appendix E protocol
chapters, Table 2-9 and interrupt-controller Tables 3-4 and 3-5.

## Parameter RAM

The communications processor uses parameter RAM for buffer descriptors
and protocol state:

| Range from base | Owner | Purpose |
|---|---|---|
| `0x0400..0x047F` | SCC1 | Rx/Tx buffer descriptors |
| `0x0500..0x057F` | SCC2 | Rx/Tx buffer descriptors |
| `0x0600..0x065F` | SCC3 | Rx/Tx buffer descriptors |
| `0x0660..0x067F` | SMC/SCP/SCC shared/internal | SMC buffers, SCP Rx/Tx BD, SCC bus-error channel number, revision |

The first future access to these ranges should be reported as
parameter RAM or buffer descriptor access, not as unknown generic
internal RAM.

## SCC Register Ranges

| Channel | Base offset | Registers | Reset highlights | Interrupt source |
|---|---:|---|---|---|
| SCC1 | `0x0880` | `SCON1`, `SCM1`, `DSR1`, `SCCE1`, `SCCM1`, `SCCS1` | `SCON=0x0004`, `SCM=0x0000`, `DSR=0x7E7E`, `SCCE=0`, `SCCM=0`, `SCCS=0` | `SCC1`, bit 13, vector `0x4D` at `GIMR=0x8040` |
| SCC2 | `0x0890` | `SCON2`, `SCM2`, `DSR2`, `SCCE2`, `SCCM2`, `SCCS2` | same pattern | `SCC2`, bit 10, vector `0x4A` |
| SCC3 | `0x08A0` | `SCON3`, `SCM3`, `DSR3`, `SCCE3`, `SCCM3`, `SCCS3` | same pattern | `SCC3`, bit 8, vector `0x48` |

`SCCE` is an event register on the high byte of the 16-bit bus and
uses write-one-to-clear semantics. `SCCM` enables individual SCC
events; mask bits set to one enable corresponding `SCCE` events.

Supported protocols documented by the manual include HDLC/SDLC, UART,
BISYNC, DDCMP, transparent and V.110/V.120-related modes. Full
protocol engines are outside this document.

## SMC and SCP Registers

| Offset | Register | Owner | Purpose |
|---:|---|---|---|
| `0x08B0` | `SPMODE` | SCP/SMC | mode and clock control |
| `0x08B2` | `SIMASK` | serial interface | serial interface mask |
| `0x08B4` | `SIMODE` | serial interface | serial interface mode |

SMC and SCP also use parameter RAM/buffers in the `0x0660..0x067F`
region. Their interrupt sources:

| Source | IPR/IMR/ISR bit | Priority rank | Vector at `GIMR=0x8040` |
|---|---:|---:|---:|
| SCP | 5 | 11 | `0x45` |
| SMC1 | 3 | 13 | `0x43` |
| SMC2 | 2 | 14 | `0x42` |

## SDMA Bus Error

```text
source    = SDMA bus error
bit       = 12
priority  = rank 4
vector    = 0x4C at GIMR=0x8040
```

## Pin Functions

The communications block can drive or sample many pins depending on
mode: SCC NMSI pins, multiplexed PCM/IDL/GCI pins, SCP transmit/
receive/clock/enable-related pins, SMC IDL/GCI auxiliary channels,
SDMA bus interactions. The MC68302 pin functions are documented chip
behavior; ASR-10 board destinations for these pins are unknown unless
proven by separate board evidence.

## ASR-10 Status

`docs/asr10/PLAN.md` fas 2 asks whether ASR-10 uses the MC68302's
communications processor (SCCs, dual-port RAM, buffer descriptors) at
all, given the board's MIDI-in optocoupler (U42) and external SCN2681
appear to cover serial I/O without it. That question is **open** and
is decided independently of this map — see fas 2 in `PLAN.md`, not this
document.

## Status in this tree

Not implemented in fas 3 step 1 or any near-term step. This is
reference material for whichever later step fas 2's answer requires
(possibly none, if fas 2 concludes ASR-10 doesn't use the
communications processor).
