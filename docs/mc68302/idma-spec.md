# MC68302 IDMA Specification Map

Flyttad från `~/develop/mc68302/docs/mc68302/idma-spec.md` 2026-07-30.
Triage: registerkarta och driftsbeskrivning är hårdvarufakta,
oförändrade. "Implementation Status"-tabellen (sidoprojektets kod)
borttagen — ersatt med status i det här trädet, som redan är starkare:
`docs/asr10/disk-read-path.md`/`evidence-tree.md` visar via
disassemblering att IDMA aldrig nås alls av ASR-10-ROM:et, inte bara
"ej observerat i ett begränsat spår". Se `docs/mc68302/README.md`.

This document maps the MC68302 independent DMA controller from the
user manual. It is a specification and implementation-boundary
document, not a transfer-engine implementation.

Primary source: MC68302 User's Manual Section 3.1, especially 3.1.2
through 3.1.7, Table 2-9 and Table D-1.

## Register Map

Offsets are from the MC68302 internal base.

| Offset | Register | Width | Reset | Purpose |
|---:|---|---:|---|---|
| `0x0800` | reserved | 16 | reserved | not implemented |
| `0x0802` | `CMR` | 16 | `0x0000` | channel mode, request generation, transfer size, start/reset and interrupt event enables |
| `0x0804` | `SAPR` | 32 | undefined | source address pointer |
| `0x0808` | `DAPR` | 32 | undefined | destination address pointer |
| `0x080C` | `BCR` | 16 | undefined | byte count; zero means 64 KiB when started |
| `0x080E` | `CSR` | 8 | `0x00` | channel status/event register |
| `0x080F` | reserved | 8 | reserved | not implemented |
| `0x0810` | `FCR` | 8 | undefined | function code register for IDMA bus cycles |
| `0x0811` | reserved | 8 | reserved | not implemented |

### CMR Bit Layout

Added 2026-08-20 (ASR-10 tree), sourced from the manual's own OCR full
text (archive.org, `bitsavers_motorola68dMultiProtocolProcessorUsersManualRev2Ju_21005972`,
section 3.1.2.1) via a web fetch, not from a direct read of the original
scanned table — extraction provenance, not a scan/transcription of the
table itself. Treat as [Likely], not [Verified], until visually
cross-checked against the actual scanned page.

| Bit(s) | Field | Meaning |
|---:|---|---|
| 15 | reserved | — |
| 14 | `ECO` | External control option: whether external control signals apply to source or destination transfers |
| 13 | `INTN` | Interrupt-on-normal-completion enable |
| 12 | `INTE` | Interrupt-on-error enable |
| 11-10 | `REQG` | Request generation: `00`=limited rate, `01`=maximum rate, `10`=burst, `11`=cycle steal |
| 9 | `SAPI` | Source address pointer increment enable |
| 8 | `DAPI` | Destination address pointer increment enable |
| 7-6 | `SSIZE` | Source size: `01`=byte, `10`=word |
| 5-4 | `DSIZE` | Destination size: `01`=byte, `10`=word |
| 3-2 | `BT` | Burst transfer bus-bandwidth cap: `00`=75%, `01`=50%, `10`=25%, `11`=12.5% |
| 1 | `RST` | Software reset |
| 0 | `STR` | Start |

**The one value this driver has ever observed, `$0D51`, decoded against
this table:** `STR=1`, `RST=0`, `BT=00`, `DSIZE=01`(byte), `SSIZE=01`(byte),
`DAPI=1`, `SAPI=0`, `REQG=11`(cycle steal), `INTE=0`, `INTN=0`, `ECO=0`.

**Cross-validation, not derivation** — two independent facts already
established by measurement, not by this table, both match it exactly:
`SAPI=0`/`DAPI=1` (source fixed, destination increments) is precisely
what `asr10_boot.cpp`'s `idma_transfer_in()` already hardcodes and what
dozens of successful transfers this session have exercised;
`INTN=INTE=0` explains *why* vector `$4B` never fires as a second,
independent mechanism alongside the already-measured `IMR` bit-11 mask
(`idma-implementation-plan.md`) — the channel does not even request that
interrupt, not just get blocked from delivering it. This strengthens
confidence in the table without proving it generalizes:
`sample-topology-closure.md`'s own caution applies here too — **one
observed value does not validate a field decode** for `REQG`/`SSIZE`/
`DSIZE`/`BT`/`ECO`, which have no independent behavioral check.

`CSR` is an event register. Event bits are write-one-to-clear according
to the manual's general event-register rule.

## Operation Summary

The IDMA channel can transfer data between memory and I/O using
standard M68000-style bus cycles. It supports:

- byte or word transfers,
- odd or even source/destination addresses,
- source and destination increment/fixed behavior,
- internal request generation,
- external burst mode,
- external cycle-steal mode,
- block termination by count exhausted, DONE input or bus error,
- suspension by clearing `STR`,
- reset by external reset or `RST` in `CMR`.

## Start and Stop

Software initializes `SAPR`, `DAPR`, `FCR`, `BCR` and `CMR`. The
channel starts when `STR` is set in `CMR`. After `STR` is set,
registers describing the current operation (`SAPR`, `DAPR`, `FCR`,
`BCR`) may be read but must not be modified. `STR` is cleared
automatically on normal count exhaustion, external termination, or
bus-error termination.

## Events and Interrupts

```text
source      = IDMA
IPR/IMR/ISR = bit 11
priority    = INRQ rank 5
level       = 4
vector low  = 0x0B
vector at GIMR=0x8040 = 0x4B
```

Two `CMR` bits enable interrupt reporting: normal termination interrupt
enable (`INTN`) and error termination interrupt enable (`INTE`). When
the corresponding enabled event occurs, the IDMA sets status in `CSR`
and sets the IDMA bit in `IPR`. If the corresponding event-enable bit
is clear, `CSR` status may be set without setting `IPR`.

## Transfer Engine Boundary

A correct transfer engine requires bus-master behavior and timing:

- IDMA bus arbitration,
- priority against SDMA and external bus masters,
- BCLR/IBCLR interaction,
- multi-cycle odd/even packing behavior,
- byte/word bus cycles through the same address decoder used by CPU
  and external masters,
- DONE/DREQ/DACK external pin state,
- BERR/HALT/retry behavior,
- interaction with CPU interrupt latency and `SCR.IPA/BCLM`.

These are documented by the manual. Implementing only raw register
shadowing would be misleading: the first future IDMA access should be
identifiable, but an IDMA transfer must not silently appear to work
without arbitration and bus cycles.

## ASR-10 Status

`[Verified]`, stronger than an absence-of-observation note:
`docs/asr10/evidence-tree.md` disassembled the ROM's own FDC transfer
loop (`fb8aa2`-`fb8db2`) instruction by instruction and found it
touches only `$FFFC4001`/`$FFFC4003` (the FDC's own MSR/FIFO ports) —
no MC68302 IDMA register of any kind, anywhere near FDC command
dispatch. `docs/asr10/disk-read-path.md` independently confirms this
quantitatively: FIFO read count matches transferred byte count exactly,
one CPU access per byte — programmed I/O, not DMA. **IDMA is not on
fas 3's requirements list** (`docs/asr10/PLAN.md` fas 3) for this
reason; Port A's dedicated `DREQ`/`DACK`/`DONE` pin *functions* are
still worth having (`docs/mc68302/pin-function-map.md`) so an access
is classified correctly if one is ever found, but the transfer engine
itself is not required.

## Implementation Status

Not implemented in fas 3 step 1 or planned for any near-term step:
register map is documented above; `CSR` W1C semantics, `CMR` decode,
transfer engine, interrupt event propagation, bus arbitration, and the
external `DREQ`/`DACK`/`DONE` pins remain unimplemented by design, not
by gap.
