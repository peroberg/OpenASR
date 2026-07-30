# MC68302 Watchdog Model

Flyttad från `~/develop/mc68302/docs/mc68302/watchdog-spec.md`
2026-07-30, oförändrad i sak. Inte använt i fas 3 steg 1 (ingen
watchdog i det steget). Se `docs/mc68302/README.md`.

Records the software watchdog semantics from the MC68302 User's
Manual section 3.8.6.

## Registers

| Register | Offset | Width | Reset | Semantics |
|---|---:|---:|---:|---|
| WRR | `0x084A` | 16 | `0xFFFF` | watchdog reference; bit 0 enables counting |
| WCN | `0x084C` | 16 | `0x0000` | watchdog counter; writing either byte services the watchdog |

`WRR` is readable and writable. Clearing `WRR.EN` disables the
watchdog and resets the counter state. `WCN` is readable as the current
counter; any write is treated as a service/restart operation.

## Counting

The watchdog timer increments `WCN` once per 8192 master-clock cycles.
The counter starts at zero and compares against `WRR` with counter bit
0 ignored — the least significant counter bit is not used in the
comparison, giving an effective 512-count prescaler after the main
clock is divided by 16.

When the comparison matches:

- `WCN` and the internal cycle accumulator are reset.
- the Timer 3 interrupt source is marked pending in `IPR`;
- active-low `WDOG` is asserted for 16 master-clock cycles;
- counting continues when the watchdog remains enabled.

## Interrupt Path

`watchdog timeout -> IPR bit 0x0010 -> IMR/GIMR eligibility -> level 4 -> vector source Timer3`.

Software clears the pending condition through the normal IPR
write-one-to-clear path. Servicing WCN also clears the watchdog
pending state.

## Pins

After total reset, PB7 is selected for the dedicated `WDOG` function
(confirmed by the ASR-10 ROM's own `PBCNT<-0x0080` write, see
`docs/mc68302/pin-function-map.md`). The generic pin model exposes that
active-low output. The ASR-10 board destination for `WDOG` is not
modeled — external board wiring, not an MC68302 chip-semantic gap.

## Limits

This document covers the documented software watchdog countdown,
service, interrupt and `WDOG` output behavior only. It makes no claim
about any ASR-10-specific reset-tree destination for the `WDOG` pin.

## Status in this tree

Not implemented in fas 3 step 1 (explicitly out of scope — no
watchdog, no timer, no interrupt controller in that step). `PLAN.md`
notes the watchdog has no corresponding `ASR10_EXPERIMENT_*` flag at
all (see `docs/asr10/experiment-flags.md`), so there is no empirical
evidence either way that ASR-10 depends on it.
