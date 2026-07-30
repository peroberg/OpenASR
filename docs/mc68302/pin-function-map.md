# MC68302 Pin Function Map

Flyttad från `~/develop/mc68302/docs/mc68302/pin-function-map.md`
2026-07-30, hårdvarufakta oförändrade. "Current Implementation Status"
(sidoprojektets kod) borttagen och ersatt med status i det här trädet.
**Använd direkt av fas 3 steg 1** (Port B PIO). Se
`docs/mc68302/README.md`.

Primary source: MC68302 User's Manual sections 3.3 and 5.14-5.19.

This document describes the standalone MC68302 chip pin model. It does
not infer Ensoniq ASR-10 board wiring beyond what is separately proven
in `docs/asr10/`.

## Port A

Port A has 16 pins. `PACNT[n] = 0` selects GPIO, and `PACNT[n] = 1`
selects the documented dedicated peripheral function. `PADDR[n] = 0`
configures GPIO input; `PADDR[n] = 1` configures GPIO output. Reset
clears `PACNT` and `PADDR`, so all Port A pins reset to GPIO inputs.
The pins have no internal pullups.

| Bit | GPIO | Dedicated function | Peripheral input when GPIO |
|---:|---|---|---|
| 0 | PA0 | RXD2 | GND |
| 1 | PA1 | TXD2 | not applicable |
| 2 | PA2 | RCLK2 | GND |
| 3 | PA3 | TCLK2 | RCLK2 |
| 4 | PA4 | CTS2 | GND |
| 5 | PA5 | RTS2 | not applicable |
| 6 | PA6 | CD2 | GND |
| 7 | PA7 | SDS2/BRG2 | mode-dependent / GND |
| 8 | PA8 | RXD3 | not yet modeled |
| 9 | PA9 | TXD3 | not applicable |
| 10 | PA10 | RCLK3 | GND |
| 11 | PA11 | TCLK3 | RCLK3 |
| 12 | PA12 | BRG3 | not applicable |
| 13 | PA13 | DREQ input | GND when GPIO, which implies asserted DREQ for external IDMA modes |
| 14 | PA14 | DACK output | not applicable |
| 15 | PA15 | DONE bidirectional/open-drain signal | VDD when GPIO |

Not modeled in fas 3 step 1 — Port A is out of scope (only Port B is
required to replace `LRCLK_CLOCK_BIT3`; see `docs/asr10/experiment-flags.md`).
`docs/asr10/PLAN.md` already established (`disk-read-path.md`) that
PA13-15 (`DREQ`/`DACK`/`DONE`) are never touched by the ASR-10 ROM —
programmed I/O, not IDMA — so Port A's dedicated IDMA pin functions
have no known ASR-10 dependency at all.

## Port B

Port B has 12 pins. `PB7-PB0` can be GPIO or dedicated peripheral
functions through `PBCNT[7:0]`. `PB11-PB8` are always GPIO pins with
interrupt capability and are not controlled by `PBCNT`. `PBDDR[11:0]`
selects GPIO direction. Reset clears `PBDDR`; `PBCNT` resets to
`0x0080`, selecting the dedicated `WDOG` function on PB7.

| Bit | GPIO | Dedicated function |
|---:|---|---|
| 0 | PB0 | IACK7 |
| 1 | PB1 | IACK6 |
| 2 | PB2 | IACK1 |
| 3 | PB3 | TIN1 |
| 4 | PB4 | TOUT1 |
| 5 | PB5 | TIN2 |
| 6 | PB6 | TOUT2 |
| 7 | PB7 | WDOG |
| 8 | PB8 | GPIO with interrupt capability; optional refresh request via SCR |
| 9 | PB9 | GPIO with interrupt capability |
| 10 | PB10 | GPIO with interrupt capability |
| 11 | PB11 | GPIO with interrupt capability |

`WDOG` is a dedicated active-low output that resets deasserted high.
Its timeout assertion belongs to the watchdog model, out of scope here.

## ASR-10 observed bootstrap PIO sequence

From `docs/mc68302/observed-access-coverage.md` (moved separately),
cross-checked against this tree's own `docs/asr10/` traces:

```text
PACNT  <- 0xE000   (PA13/14/15 select DREQ/DACK/DONE; unused by ASR-10, see disk-read-path.md)
PADDR  <- 0xFFFF
PADAT  <- 0x18FC, 0x18F8, 0x18E8 ... final readback 0x58E8
PBCNT  <- 0x0080   (PB7 = WDOG dedicated; PB0-6, PB8-11 stay GPIO)
PBDDR  <- 0xF097   (bits 0,1,2,4,7 = output; bit 3 (PB3) = INPUT)
PBDAT  <- 0x0007
```

`PBDDR` bit 3 clear means **PB3 is configured as a GPIO input**, not
the dedicated `TIN1` function (`PBCNT` bit 3 is 0, i.e. GPIO, since
only bit 7 is set in `PBCNT=0x0080`). This is the pin
`ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3` reads (`asr10_boot.cpp`'s
`m68302_internal_r` at `0xfc6828`, mem_mask `0x00ff`) — i.e. the
hand-modeled "LRCLK" toggle is a stand-in for whatever the ASR-10 board
actually wires into PB3 as a plain GPIO input. Nothing in this tree
drives that pin; a real PIO Port B model reads back whatever was last
latched (0 after reset, since nothing writes to an input pin's latch),
**not** a toggling signal. Replacing the hack with real semantics is
expected to make this bit read as constant 0 rather than an alternating
pattern — see `docs/asr10/PLAN.md` fas 3 step 1 verification notes for
the consequence.

## Status in this tree

Implemented in fas 3 step 1 (`mc68302sim.cpp`):

- `PBCNT`/`PBDDR`/`PBDAT` register storage with the documented
  GPIO/dedicated selection and direction/data-latch semantics above.
- `PBDAT` read reflects the data latch for output-configured bits and
  an unconnected (constant, no callback) value for input-configured
  bits, honestly — no synthetic toggling.

Not implemented (unchanged from the manual's own scope, and out of
scope for this step regardless):

- Port A (any pin, any function).
- PB11-PB8 high-to-low interrupt request generation (needs the
  interrupt controller, fas 3 step 2).
- PB8 DRAM refresh request mode from `SCR`.
- Dedicated peripheral signal producers/consumers for SCC, IDMA,
  timers, watchdog timeout, and interrupt acknowledge (IACK7/6/1 on
  PB0-2, TIN/TOUT on PB3-6, WDOG on PB7).
- Any real external signal wired into PB3 (or any other Port B pin) —
  no ASR-10 board-level source is modeled or claimed.
