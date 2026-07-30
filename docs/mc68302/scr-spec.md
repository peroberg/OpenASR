# MC68302 System Control Register

Flyttad från `~/develop/mc68302/docs/mc68302/scr-spec.md` 2026-07-30,
oförändrad i sak — hela dokumentet är redan i specifikationsform (inga
"fungerar nu"-påståenden om sidoprojektets kod hittades). Se
`docs/mc68302/README.md` för triagelistan.

This document records the MC68302 System Control Register semantics.
Primary source: MC68302 User's Manual, sections 2.7 and 3.8.1 through
3.8.8.

## Register

| Property | Value |
|---|---|
| fixed bootstrap address | `0x000000F4` |
| access space | supervisor data, function code 5 |
| width | 32 bits |
| reset value | `0x00000F00` |
| normal internal offset | bootstrap-only configuration register |
| byte access | not modeled; bootstrap path accepts aligned word halves |
| word access | aligned high and low halfword writes |
| long access | represented as two word writes on the 68000 bus |

The manual states that BAR, SCR and CKCR are reset only by total system
reset, and that chip selects are not asserted for accesses to BAR or SCR.

## Bit Map

| Bits | Name | Reset | Access | Meaning |
|---:|---|---:|---|---|
| 31..28 | reserved/status zero | `0` | read as zero | no documented function |
| 27 | IPA | `0` | W1C status | M68000 core has unmasked interrupt request |
| 26 | HWT | `0` | W1C status | hardware watchdog timeout occurred |
| 25 | WPV | `0` | W1C status | chip-select write-protect violation |
| 24 | ADC | `0` | W1C status | chip-select address decode conflict |
| 23 | reserved | `0` | read as zero | no documented function |
| 22 | ERRE | `0` | RW | external RISC request enable; documented with clock control |
| 21 | VGE | `0` | RW | vector generation enable in disable-CPU mode |
| 20 | WPVE | `0` | RW | assert BERR on write-protect violation |
| 19 | RMCST | `0` | RW | special TAS/read-modify-write cycle treatment |
| 18 | EMWS | `0` | RW | extra wait state for external-master accesses |
| 17 | ADCE | `0` | RW | assert BERR on chip-select address decode conflict |
| 16 | BCLM | `0` | RW | use internal interrupt pending to assert bus-clear |
| 15 | FRZW | `0` | RW | freeze software watchdog timer when FRZ is asserted |
| 14 | FRZ2 | `0` | RW | freeze Timer 2 when FRZ is asserted |
| 13 | FRZ1 | `0` | RW | freeze Timer 1 when FRZ is asserted |
| 12 | SAM | `0` | RW | synchronous external-master access mode |
| 11 | HWDEN | `1` | RW | hardware watchdog enable |
| 10..8 | HWDCN | `111` | RW | hardware watchdog timeout count |
| 7 | LPREC | `0` | RW | destructive low-power recovery when set |
| 6 | LPP16 | `0` | RW | low-power divider input is main clock/16 |
| 5 | LPEN | `0` | RW | enables low-power entry on STOP |
| 4..0 | LPCD | `00000` | RW | low-power clock divider select |

Confidence: confirmed for the field layout and reset value.

## Status Bits

`IPA`, `HWT`, `WPV` and `ADC` are write-one-to-clear status bits. Writing
zero leaves each bit unchanged. Writing one clears the corresponding
latched event. The upper reserved nibble remains zero.

`IPA` is set when the core has an unmasked interrupt request. If `BCLM`
is set, `BCLR` and the internal bus-clear signal to IDMA are asserted.
The ASR-10 bootstrap configuration leaves `BCLM=0`, so clearing IPA has
no deferred bus-clear release effect in that configuration.

`HWT` is set by the hardware watchdog, not by the software watchdog
Timer 3/WRR/WCN block.

`WPV` and `ADC` may assert BERR if `WPVE` or `ADCE` are set. The
ASR-10 bootstrap configuration leaves both enables clear.

## Control Effects

`VGE` affects interrupt vector generation only in disable-CPU mode. A
device running the on-chip M68000 core keeps `VGE=1` retained but with
no active effect in this configuration.

`RMCST=0` keeps normal M68000 locked read-modify-write cycle timing.
`EMWS=0` leaves external-master wait states at the default policy.
`BCLM=0` disables the low-interrupt-latency bus-clear behavior.
`SAM=0` selects asynchronous external-master access to internal
registers/RAM. With no external master present, normal CPU-owned bus
operation is unchanged.

`HWDEN=0` disables the hardware bus-cycle watchdog. This is distinct
from the software watchdog at WRR/WCN. The ASR-10 bootstrap write clears
the reset default `HWDEN=1, HWDCN=111`.

`LPEN=0` disables low-power entry on STOP. `LPREC`, `LPP16` and `LPCD`
are retained but inactive while `LPEN=0`.

`FRZW=FRZ2=FRZ1=0` leaves freeze control disabled for the software
watchdog and timers.

## Observed ASR-10 Bootstrap Write

The ROM executes:

`MOVE.L #$0F200000,$00F4.W`

This appears on the bus as:

| Halfword | Address | Value | Effect |
|---|---:|---:|---|
| high | `0x000000F4` | `0x0F20` | clear `IPA/HWT/WPV/ADC`; set `VGE` |
| low | `0x000000F6` | `0x0000` | clear `FRZ*`, `SAM`, `HWDEN/HWDCN`, low-power fields |

With the documented W1C status semantics, final SCR readback is:

`0x00200000`

not the raw written value `0x0F200000`.

## Current Configuration Classification

| Field | Selected value | Runtime meaning |
|---|---:|---|
| IPA/HWT/WPV/ADC | write one | clear latched status bits |
| VGE | `1` | retained; active only in disable-CPU mode |
| WPVE/ADCE | `0` | no BERR assertion for WPV/ADC, though status bits can still latch |
| RMCST | `0` | normal RMC/TAS treatment |
| EMWS | `0` | no added external-master wait state |
| BCLM | `0` | no IPEND-driven bus clear |
| FRZW/FRZ2/FRZ1 | `0` | freeze inputs do not stop watchdog/timers |
| SAM | `0` | asynchronous external-master internal access mode |
| HWDEN/HWDCN | `0/000` | hardware bus-cycle watchdog disabled |
| LPEN | `0` | low-power entry disabled |
| LPREC/LPP16/LPCD | `0` | retained but inactive while LPEN is clear |

## Implementation Scope

External-master bus cycles, DISCPU mode, FRZ input assertion and
low-power STOP entry remain explicit future checkpoints: the ASR-10
runtime does not activate them, and fas 3 step 1 does not model them
(only the W1C status bits and the plain RW fields needed so the
bootstrap write above round-trips correctly are in scope).
