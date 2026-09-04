# MC68302 Timer 2 Interrupt Specification

Flyttad från `~/develop/mc68302/docs/mc68302/timer2-interrupt-spec.md`
2026-07-30, oförändrad i sak — redan i specifikationsform. Ersätter
`timer2-implementation-blockers.md` (lämnad kvar i sidoprojektet, se
`docs/mc68302/README.md`) som var en historisk logg över samma
utredning, nu överflödig. Inte använt i fas 3 steg 1 (ingen timer i det
steget) — sparad som facit för en senare fas 3-delfas.

This is the normative reference for the MC68302 Timer 2
implementation, based on the MC68302 User's Manual.

## Manual Audit

| Topic | Manual section | Manual page | Verified interpretation | Confidence |
|---|---|---:|---|---|
| EXRQ/INRQ levels | 3.2.2.1, Table 3-3 | 3-19 | INRQ sources are fixed at interrupt level 4 | confirmed |
| INRQ priorities | 3.2.2.2, Table 3-4 | 3-19 | Timer 2 is below PB9 and above SCP within level 4 | confirmed |
| Vector encoding | 3.2.4, Table 3-5 | 3-23 | Timer 2 low vector field is `00110` | confirmed |
| GIMR | 3.2.5.1 | 3-24 | bits 7-5 are vector prefix `V7..V5` | confirmed |
| IPR | 3.2.5.2 | 3-26 | Timer 2 source bit is bit 6 | confirmed |
| IMR | 3.2.5.3 | 3-27 | Timer 2 mask bit is bit 6; `1` enables, `0` masks | confirmed |
| ISR | 3.2.5.4 | 3-28 | Timer 2 in-service bit is bit 6; bit 0 is always zero | confirmed |
| TMR1/TMR2 | 3.5.2.1 | 3-37 | PS bits 15-8, CE 7-6, OM 5, ORI 4, FRR 3, ICLK 2-1, RST 0 | confirmed |
| TRR1/TRR2 | 3.5.2.2 | 3-38 | reference reached when TCN increments to equal TRR | confirmed |
| TCR1/TCR2 | 3.5.2.3 | 3-38 | capture register is read-only and reset to zero | confirmed |
| TCN1/TCN2 | 3.5.2.4 | 3-39 | 16-bit up-counter, readable/writable; write resets counter and prescaler | confirmed |
| TER1/TER2 | 3.5.2.5 | 3-39 | bit 1 is REF, bit 0 is CAP, bits 7-2 reserved | confirmed |

## Timer 2 Registers

All offsets below are relative to the MC68302 internal base. For the
ASR-10 BAR value `0x0FC6`, the internal base is `0x00FC6000`.

| Register | Offset | Address at ASR-10 base | Width | Reset | Access | Notes |
|---|---:|---:|---:|---:|---|---|
| `TMR2` | `0x0850` | `0x00FC6850` | 16 | `0x0000` | read/write | mode register |
| `TRR2` | `0x0852` | `0x00FC6852` | 16 | `0xFFFF` | read/write | reference value |
| `TCR2` | `0x0854` | `0x00FC6854` | 16 | `0x0000` | read-only | capture latch |
| `TCN2` | `0x0856` | `0x00FC6856` | 16 | `0x0000` | read/write | up-counter; write resets counter and prescaler |
| `TER2` | `0x0859` | `0x00FC6859` | 8 | `0x00` | read/write-one-to-clear | event register |

Byte writes to word registers update the addressed byte lane. `TER2` is
the odd byte at `base + 0x0859`; `base + 0x0858` is reserved.

## TMR2 Bit Layout

| Bits | Field | Meaning |
|---:|---|---|
| 15-8 | `PS` | prescaler value; divider is `PS + 1` |
| 7-6 | `CE` | capture edge and capture interrupt enable |
| 5 | `OM` | output mode: `0` active-low pulse, `1` toggle output |
| 4 | `ORI` | output reference interrupt enable |
| 3 | `FRR` | `0` free run, `1` restart when reference reached |
| 2-1 | `ICLK` | `00` stopped, `01` master clock, `10` master clock / 16, `11` TIN falling edge |
| 0 | `RST` | `0` reset timer, `1` enable timer |

Writing `RST=0` performs a timer software reset including clearing
`TMR`, `TRR` and `TCN`, and clears the Timer 2 event state.

## Counter Semantics

The timer input clock feeds the prescaler. The prescaler divides the
selected clock by `PS + 1`; each prescaler output increments `TCN2`.

The reference value is not reached until `TCN2` increments to equal
`TRR2`. With `TCN2=0`, `PS=0`, master clock input and `TRR2=N`, the
first reference event occurs after `N` master-clock cycles. In restart
mode, the counter resets immediately after the reference is reached.

External `TIN2` input requires an explicit external-edge signal; nothing
drives it without board wiring. `ICLK=11` therefore does not advance
from CPU cycles alone.

## TER2

| Bit | Name | Meaning |
|---:|---|---|
| 1 | `REF` | `TCN2` reached `TRR2` |
| 0 | `CAP` | capture event occurred |
| 7-2 | reserved | write zero |

`TER2` bits are set when events occur regardless of the interrupt
enable bits in `TMR2`. A bit is cleared by writing one to that bit;
writing zero does not change it. The timer negates its INRQ
contribution only when all unmasked timer event causes are cleared.

`ORI=1` makes the `REF` event contribute to the Timer 2 INRQ source.
For capture events, any nonzero `CE` mode enables capture on the
documented TIN2 edge and makes `TER2.CAP` contribute to the same Timer
2 INRQ source.

## Interrupt Source Mapping

Timer 2 is an internal request (INRQ) source at fixed CPU interrupt
level 4.

| Register | Timer 2 bit | Polarity |
|---|---:|---|
| `IPR` | 6 | `1` means pending |
| `IMR` | 6 | `1` enables interrupt request to the core, `0` masks it |
| `ISR` | 6 | `1` means in service |

`IPR` is W1C for polled operation. In vectored operation, IACK clears
the IPR bit unless the source has an event register. Timer 2 has
`TER2`, so clearing the unmasked event bit clears Timer 2 pending
state.

## Vector

```text
vector = (GIMR.V7..V5 << 5) | sourceLow5
```

Timer 2's low five-bit source vector is `00110` (`0x06`). For the
observed ASR-10 `GIMR = 0x8040`, `V7..V5 = 010`, so the Timer 2 vector
is `(0b010 << 5) | 0b00110 = 0x46`. This is distinct from the
board-level SCN2681 DUART vector `0x56` (external IRQ6 path, see
`docs/asr10/duart.md`).

## ASR-10 Decode

Observed ROM writes (confirmed live in this tree too —
`docs/asr10/baseline-media.md`'s dispatcher dump shows `fc6850=003b`
`fc6852=3f01`, an exact match):

| Register | Value | Decode |
|---|---:|---|
| `GIMR` | `0x8040` | dedicated mode, internal vectors, vector prefix `010` |
| `IMR` | `0x0000` (early); `0xe480` observed later, see `vector-origin-map.md` | |
| `TRR2` | `0x3F01` | reference value 16129 |
| `TMR2` | `0x003B` | `PS=0`, `CE=00`, `OM=1`, `ORI=1`, `FRR=1`, `ICLK=01`, `RST=1` |

At 16 MHz with master clock input and prescaler divide by one:

```text
cycles per reference = TRR2 * (PS + 1) = 16129
period = 16129 / 16,000,000 = 0.0010080625 s
```

Because `IMR bit 6 = 0` at this point, the ASR-10 configuration lets
Timer 2 run and set `TER2.REF`/Timer 2 pending state, but it must not
assert CPU level 4 until software unmasks Timer 2 by setting IMR bit 6.

## Difference From SCN2681

MC68302 Timer 2 is an internal SIB timer with `TER2`, `IPR`, `IMR`,
`ISR` and GIMR-derived level-4 vectors. The SCN2681 counter/timer (now
a real `scn2681_device` in this tree, see `docs/asr10/duart.md`) is an
external CS3 device with board glue to IRQ6 and vector `0x56`. They do
not share counters, event bits, pending bits, vectors, or acknowledge
paths.

## Status in this tree

Not implemented in fas 3 step 1 by design. Reserved for a later fas 3
sub-step alongside the interrupt controller.
