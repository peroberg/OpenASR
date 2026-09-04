# MC68302 SIB Register Map

Flyttad från `~/develop/mc68302/docs/mc68302/sib-register-map.md`
2026-07-30, med triage: hårdvarufakta oförändrade, sidoprojektets egen
implementationsstatus ("Current Implementation Coverage" i originalet)
borttagen — den beskriver `~/develop/mc68302`s kod, inte den här
MAME-enheten. Se `docs/mc68302/README.md` för fullständig triagelista.

This document maps the MC68302 system integration block (SIB) registers
relevant to the ASR-10 boot analysis. It is a documentation artifact
only; it does not define emulator behavior on its own.

## Sources

- Primary: MC68302 User's Manual (OCR'd text in the source side
  project, not carried into this tree).
- Runtime evidence: ASR-10 ROM traces from
  `roms/asr10booth/asr-65e0-hi-1.5b.bin` and
  `roms/asr10booth/asr-648c-lo-1.5b.bin`.

Confidence labels:

- `confirmed`: present in the MC68302 manual and observed or implemented consistently.
- `strongly supported`: derived from manual-defined fields and runtime values.
- `tentative`: plausible but not fully decoded from a clean manual table.
- `unknown`: not enough evidence yet.

## Internal Base

The MC68302 internal register window is a relocatable 4 KiB block. The
ASR-10 ROM writes bootstrap BAR with:

| CPU address | Width | Value | Result |
|---:|---:|---:|---|
| `0x000000F2` | word | `0x0FC6` | internal base `0x00FC6000` |

After this write, SIB offsets map as:

```text
CPU address = 0x00FC6000 + internal offset
```

The SIB register area observed in the current ROM run is mainly `base + 0x0800..0x0859`, i.e. CPU addresses `0x00FC6800..0x00FC6859` — matches `asr10_boot.cpp`'s existing `m68302_internal_r/w` range exactly.

## Register Table

| Offset | Address at BAR `0x0FC6` | Register | Width | Reset | Block | Access class | Observed |
|---:|---:|---|---:|---:|---|---|---|
| `0x0800` | `0x00FC6800` | reserved | 16 | - | IDMA | reserved | no |
| `0x0802` | `0x00FC6802` | CMR | 16 | `0x0000` | IDMA | config/status | no |
| `0x0804` | `0x00FC6804` | SAPR high | 16 | unknown | IDMA | address | no |
| `0x0806` | `0x00FC6806` | SAPR low | 16 | unknown | IDMA | address | no |
| `0x0808` | `0x00FC6808` | DAPR high | 16 | unknown | IDMA | address | no |
| `0x080A` | `0x00FC680A` | DAPR low | 16 | unknown | IDMA | address | no |
| `0x080C` | `0x00FC680C` | BCR | 16 | unknown | IDMA | count | no |
| `0x080E` | `0x00FC680E` | CSR | 8 | `0x00` | IDMA | event/status | no |
| `0x0810` | `0x00FC6810` | FCR | 8 | unknown | IDMA | function code | no |
| `0x0812` | `0x00FC6812` | GIMR | 16 | `0x0000` | interrupt controller | config | yes |
| `0x0814` | `0x00FC6814` | IPR | 16 | `0x0000` | interrupt controller | event/status clear | yes |
| `0x0816` | `0x00FC6816` | IMR | 16 | `0x0000` | interrupt controller | mask config | yes |
| `0x0818` | `0x00FC6818` | ISR | 16 | `0x0000` | interrupt controller | in-service clear | yes |
| `0x081E` | `0x00FC681E` | PACNT | 16 | `0x0000` | parallel I/O | config | yes |
| `0x0820` | `0x00FC6820` | PADDR | 16 | `0x0000` | parallel I/O | direction config | yes |
| `0x0822` | `0x00FC6822` | PADAT | 16 | unknown | parallel I/O | data latch | yes |
| `0x0824` | `0x00FC6824` | PBCNT | 16 | `0x0080` | parallel I/O | config | yes |
| `0x0826` | `0x00FC6826` | PBDDR | 16 | `0x0000` | parallel I/O | direction config | yes |
| `0x0828` | `0x00FC6828` | PBDAT | 16 | unknown | parallel I/O | data latch | yes |
| `0x0830` | `0x00FC6830` | BR0 | 16 | `0xC001` | chip select 0 | config | yes |
| `0x0832` | `0x00FC6832` | OR0 | 16 | `0xDFFD` | chip select 0 | config | yes |
| `0x0834` | `0x00FC6834` | BR1 | 16 | `0xC000` | chip select 1 | config | yes |
| `0x0836` | `0x00FC6836` | OR1 | 16 | `0xDFFD` | chip select 1 | config | yes |
| `0x0838` | `0x00FC6838` | BR2 | 16 | `0xC000` | chip select 2 | config | yes |
| `0x083A` | `0x00FC683A` | OR2 | 16 | `0xDFFD` | chip select 2 | config | yes |
| `0x083C` | `0x00FC683C` | BR3 | 16 | `0xC000` | chip select 3 | config | yes |
| `0x083E` | `0x00FC683E` | OR3 | 16 | `0xDFFD` | chip select 3 | config | yes |
| `0x0840` | `0x00FC6840` | TMR1 | 16 | `0x0000` | timer 1 | config | no |
| `0x0842` | `0x00FC6842` | TRR1 | 16 | `0xFFFF` | timer 1 | reference | no |
| `0x0844` | `0x00FC6844` | TCR1 | 16 | `0x0000` | timer 1 | capture | no |
| `0x0846` | `0x00FC6846` | TCN1 | 16 | `0x0000` | timer 1 | counter | no |
| `0x0849` | `0x00FC6849` | TER1 | 8 | `0x00` | timer 1 | event/status | no |
| `0x084A` | `0x00FC684A` | WRR | 16 | `0xFFFF` | watchdog | config/reference | yes |
| `0x084C` | `0x00FC684C` | WCN | 16 | `0x0000` | watchdog | counter | no |
| `0x0850` | `0x00FC6850` | TMR2 | 16 | `0x0000` | timer 2 | config | yes |
| `0x0852` | `0x00FC6852` | TRR2 | 16 | `0xFFFF` | timer 2 | reference | yes |
| `0x0854` | `0x00FC6854` | TCR2 | 16 | `0x0000` | timer 2 | capture | no |
| `0x0856` | `0x00FC6856` | TCN2 | 16 | `0x0000` | timer 2 | counter | no |
| `0x0859` | `0x00FC6859` | TER2 | 8 | `0x00` | timer 2 | event/status | no |

Note: `0xFC6860` (the register `FC6860_CLEAR_BUSY` compensates for, see
`docs/asr10/experiment-flags.md`) is **not** in this manual-sourced
table — it falls outside the documented SIB register set covered by
the side project's audit. Its identity is still open; step 1 of fas 3
models its observed busy-bit behavior without claiming a manual
register name for it.

## Interrupt Controller

Manual-confirmed registers:

- `GIMR`: global interrupt mode. Important fields include `MOD`, `IV7`, `IV6`, `IV1`, `ET7`, `ET6`, `ET1`, and vector prefix bits `V7..V5`.
- `IPR`: interrupt pending register. Event/status register; in polled mode bits are cleared by writing one. Some event-register-backed sources clear through their own event registers.
- `IMR`: interrupt mask register. A set bit enables the corresponding internal interrupt source; clearing a bit masks that source. Bit 0 is undefined and ERR cannot be masked.
- `ISR`: in-service register. Bits are cleared by writing one.

The source-bit table has been verified against the PDF text:

| Bit | Source |
|---:|---|
| 15 | PB11 |
| 14 | PB10 |
| 13 | SCC1 |
| 12 | SDMA bus error |
| 11 | IDMA |
| 10 | SCC2 |
| 9 | Timer 1 |
| 8 | SCC3 |
| 7 | PB9 |
| 6 | Timer 2 |
| 5 | SCP |
| 4 | Timer 3 |
| 3 | SMC1 |
| 2 | SMC2 |
| 1 | PB8 |
| 0 | ERR for IPR, undefined in IMR, always zero in ISR |

## Parallel I/O

Manual-confirmed semantics:

- `PACNT`: one bit per Port A pin; `0 = GPIO`, `1 = dedicated peripheral`.
- `PADDR`: one bit per Port A pin; `0 = input`, `1 = output` when the pin is GPIO.
- `PADAT`: Port A data/output latch.
- `PBCNT`: controls PB7..PB0 dedicated function selection; PB11..PB8 are always general-purpose I/O.
- `PBDDR`: Port B direction; `0 = input`, `1 = output`.
- `PBDAT`: Port B data/output latch.

Important dedicated pin references from the manual:

- Port A includes SCC2/SCC3 pins and IDMA pins such as `DREQ`, `DACK`, and `DONE`.
- Port B includes `IACK7`, `IACK6`, `IACK1`, `TIN1`, `TOUT1`, `TIN2`, `TOUT2`, and `WDOG` on PB7..PB0.
- PB11..PB8 can generate interrupt-controller requests on high-to-low transitions.

## Chip Selects

Manual-confirmed BR fields:

- `FC2..FC0`: function-code compare value.
- `BASE ADDRESS`: A23..A13.
- `RW`: read/write compare value.
- `EN`: enable.

Manual-confirmed OR fields:

- `DTACK`: internal wait-state generation or external DTACK.
- `BASE ADDRESS MASK`: mask for A23..A13.
- `MRW`: masks or enables RW comparison.
- `CFC`: masks or enables function-code comparison.

RW/MRW behavior, DTACK timing, write-protect violation, and external
DTACK semantics are manual-documented but not yet modeled by any MAME
device (they are also out of scope for fas 3 step 1).

Decoded ASR-10 values from the side project's observed run (not yet
re-verified against this tree's own ROM trace beyond what
`docs/asr10/` already establishes for CS2/CS3 = FDC/DUART):

| CS | BR | OR | Enabled | Start | End exclusive | Size | FC compare | FC | RW | MRW | DTACK |
|---|---:|---:|---|---:|---:|---:|---|---:|---|---|---:|
| reset CS0 | `0xC001` | `0xDFFD` | yes | `0x000000` | `0x002000` | 8 KiB | yes | 6 | read | masked | 6 wait |
| reset CS1-3 | `0xC000` | `0xDFFD` | no | `0x000000` | `0x002000` | 8 KiB | yes | 6 | read | masked | 6 wait |
| CS0 final | `0x1F01` | `0x3F82` | yes | `0xF80000` | `0xFC0000` | 256 KiB | no | ignored | read | compared | 1 wait |
| CS1 final | `0x1FEF` | `0xFFFE` | yes | `0xFF6000` | `0xFF8000` | 8 KiB | yes | 0 | write | compared | external |
| CS2 final | `0x1F85` | `0xFFFC` | yes | `0xFC2000` | `0xFC4000` | 8 KiB | yes | 0 | read | masked | external |
| CS3 final | `0x1F89` | `0x7FFC` | yes | `0xFC4000` | `0xFC6000` | 8 KiB | no | ignored | read | masked | 3 wait |

## Timers and Watchdog

Manual-confirmed timer fields:

- `TMR`: `PS` prescaler, `CE` capture edge/interrupt enable, `OM`, `ORI`, `FRR`, `ICLK`, `RST`.
- `TRR`: reference value.
- `TCR`: capture register.
- `TCN`: counter.
- `TER`: event register; event bits clear by writing one. Bit 1 is `REF`, bit 0 is `CAP`, bits 7-2 are reserved.

For `TMR`, confirmed values include:

- `RST = 0`: reset timer; `RST = 1`: enable timer.
- `ICLK = 00`: stop count.
- `ICLK = 01`: master clock.
- `ICLK = 10`: master clock / 16.
- `ICLK = 11`: corresponding TIN falling edge.
- `FRR = 0`: free run.
- `FRR = 1`: restart when reference reached.
- `ORI = 1`: interrupt on reference reached.

Manual-confirmed watchdog fields:

- `WRR` is a 16-bit reference register.
- Bit 0 is `EN`; clearing `EN` resets/disables watchdog counting.
- Reset value is `0xFFFF`.

See `docs/mc68302/timer2-interrupt-spec.md` for the normative Timer 2
register/bit/vector spec, and `docs/mc68302/watchdog-spec.md` for the
watchdog counting/interrupt model. Neither is implemented in fas 3
step 1.
