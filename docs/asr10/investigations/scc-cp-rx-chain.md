# SCC/CP RX chain boundary

Date: 2026-08-23
Scope: current MAME tree, `mc68302_device`, SCC1 sampling receive path
Outcome: C - the current MC68302 model has no SCC receive behavior

## Question

Can a deterministic byte stream be injected before descriptor completion so
that the current MC68302 SCC/CP/SDMA model itself fills the receive buffer,
updates the descriptor and raises the SCC interrupt?

This is a source-boundary audit. No ADC, analog routing, Port B, ES5506,
ES5510, memory map, clock or physical-source claim is involved.

## Phase 1 reference

Phase 1 is documented in `virtual-scc-rx-chain.md`. Its temporary bridge
started after SCC reception and explicitly produced the effects that real
MC68302 CP/SDMA would normally own.

| Phase 1 bridge action | hardware/model owner being bypassed |
|---|---|
| wrote 800 deterministic bytes to `$F76600` | SCC receiver plus SDMA buffer writes |
| wrote descriptor length `$0320` at `$FC6402` | receive CP |
| changed descriptor status `$D000 -> $5000` | CP ownership/E-bit completion |
| wrote SCCE1 receive event at `$FC6888` | SCC event logic |
| asserted level 4 and supplied vector `$4D` | MC68302 interrupt controller |
| later read firmware SAPR and supplied bytes through `idma_transfer_in()` | IDMA source dereference/bus mastering |
| supplied completion vector `$4B` | IDMA interrupt-controller completion |

Firmware, not the bridge, then executed `$00643C`, returned the descriptor to
SCC ownership, applied threshold/pretrigger logic, reached `RECORDING`,
programmed IDMA and ran `$00AA48`.

The Phase 1 experiment therefore proved the path *after* CP-shaped descriptor
completion. It did not exercise SCC receive framing, CP descriptor ownership,
receive SDMA, SCC event generation or internal interrupt arbitration.

## Current model inventory

### Public boundary

`mc68302_device` inherits only `m68000_device`. It does not inherit
`device_serial_interface` and exposes no `rxd_w`, `rx_w`, channel receive
callback, receive FIFO or test-byte method.

Its relevant public board-facing methods are limited to:

```text
set_pb_input(bit, level)
irq1_ack_vector()
irq6_ack_vector()
idma_channel_active()
idma_transfer_in(data)
```

There is no SCC1/SCC2 receive entry point between a virtual source and CP.

### Parameter RAM and SCC registers

`mc68302_device::classify_offset()` classifies both communications areas as
`known_unimplemented`:

```text
$0400-$07FF  SCC/SMC parameter RAM
$0880-$08B5  SCC1-3/SMC/SCP registers
```

`internal_r()` returns `m_shadow[...]`. `internal_w()` applies only
`COMBINE_DATA` to the same shadow word. The only writers of `m_shadow` in the
entire device are reset (`fill(0)`) and those CPU-visible memory-write paths.
There is no background CP writer.

Consequences:

- E/ownership is not inspected by a model component;
- descriptor buffer pointers and MRBLR are not consumed;
- no RX byte is written through a descriptor buffer pointer;
- received length is never generated;
- wrap and current-buffer-pointer state are not advanced;
- SCCE has no event or write-one-to-clear semantics;
- SCCM does not gate an event;
- no SCC CP state exists outside shadow memory.

### Scheduling and interrupts

The device allocates no CP/SCC timer and has no `device_timer()` or receive
callback. The ASR-10 machine configuration connects no SCC RX pin.

The driver has no level-4 producer. `maincpu_iack_r(4)` returns the 68000
autovector unless temporary experiment code replaces it. The modeled external
vector helpers cover only level 1 and level 6. GIMR/IPR/IMR/ISR remain shadow
storage, so no SCCE-to-INRQ arbitration exists.

### Other MAME serial devices

The tree contains working receive front ends in unrelated devices:

- `z80scc_device` has `rxa_w`/`rxb_w` and serial receive logic;
- `scc2698b_device` has per-channel RX pins and FIFOs;
- `tmp68301_device` has `rx0_w`/`rx1_w`/`rx2_w`.

These are positive controls for the source search: receive APIs and
`device_serial_interface` are found when present. None implements the MC68302
parameter-RAM layout, CP buffer descriptors, receive SDMA or MC68302 interrupt
source map. Substituting one would introduce a different peripheral plus a new
adapter and still leave the CP descriptor engine absent. It is not an existing
MC68302 receive path that can be reused for this experiment.

The only MC68302 device source in the tree is
`src/devices/machine/mc68302.{h,cpp}` plus its SIM helper. There is no second,
more complete MC68302 implementation hidden elsewhere in MAME.

## Minimal injection-point decision

No valid minimal runtime injection point currently exists.

Writing a byte to parameter RAM would be a CPU memory write, not SCC receive.
Writing buffer contents, descriptor length, E-bit or SCCE would recreate the
Phase 1 bridge. Asserting level 4 would additionally bypass the missing
interrupt controller. Such a run could only prove the already-verified
firmware path and cannot test whether CP produced the completion.

For that reason no deterministic runtime stream was injected in this phase.
This is not a zero-traffic inference: the source model contains no callable or
scheduled receive path on which traffic could be placed.

## Smallest missing receive-only slice

A future discriminating experiment needs a deliberately bounded receive-only
MC68302 slice. It does not require complete SCC protocol support, but it must
put the experimental boundary before CP-owned side effects:

1. A channel-specific RX-byte entry point after serial bit/framing decode, so
   byte transport can be tested independently of unknown physical clocks and
   pins.
2. SCC1 receive-enable and mode gating sufficient to reject bytes when ENR is
   clear. Exact serial framing remains a separate layer.
3. CP lookup of the current RX descriptor from parameter RAM.
4. E/ownership validation and SDMA writes through the descriptor's buffer
   address, bounded by MRBLR.
5. Received-length update, E-bit clear, wrap handling and receive-descriptor
   pointer advance.
6. SCCE receive-event generation with SCCM gating and write-one-to-clear
   behavior.
7. The minimum internal interrupt path needed to turn the enabled SCC1 event
   into level 4/vector `$4D` and support firmware EOI.

The test source must call only item 1. If it writes any item 3-7 directly, the
experiment has not moved behind the Phase 1 boundary.

This list is a behavioral contract derived from the observed firmware and
documented descriptor/register layout, not an implementation proposal for
physical ADC routing.

## Phase comparison

```text
Phase 1 (measured):
virtual bytes
  -> temporary bridge writes buffer/descriptor/SCCE and asserts $4D
  -> firmware $00643C
  -> threshold/pretrigger
  -> IDMA
  -> recording destination

Requested Phase 2:
virtual RX byte
  -> SCC receive front end       MISSING
  -> CP ownership/SDMA           MISSING
  -> descriptor completion       MISSING
  -> SCC event/level-4 IRQ       MISSING
  -> firmware $00643C            already verified after this boundary
```

## Status

**`[Verified source/model]`:** the current `mc68302_device` has no SCC RX
front end, CP descriptor engine, receive SDMA, SCC event semantics or SCC
internal-interrupt delivery. SCC parameter RAM and registers are shadow
storage only.

**`[OPEN capability]`:** MC68302 CP/SDMA descriptor completion has not been
implemented or dynamically tested. It cannot be promoted to `[Verified]` by
the current model.

**Outcome C:** the SCC model lacks RX behavior. Outcome B cannot yet be
distinguished dynamically because there is no receiver that can accept the
first byte; execution stops before the B boundary.

The simplest explanation is complete at the model layer: descriptor completion
does not occur because no code exists that could produce it. This says nothing
about physical MC68302 silicon or the ASR-10 analog board.

Falsifier: a source path in the current model that accepts an RX byte and can
be shown to invoke CP-owned descriptor updates without direct test writes would
contradict this audit. Full-tree symbol, inheritance, callback, timer and shadow
writer searches found no such path, while finding the positive-control receive
paths in unrelated serial devices.

## Verification accounting

- Executable or probe files changed: 0.
- Runtime injections: 0; no valid pre-CP injection API exists.
- Regression tests run: 0; executable behavior was unchanged.
- Regression PASS lines: 0.
- `static/*.csv` files changed: 0.
- `-log` runs: 0.
- `git diff --check` before documentation: PASS.
- `git diff --check` after documentation: PASS.
