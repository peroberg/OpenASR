# MC68302 Interrupt Source Map

Flyttad från `~/develop/mc68302/docs/mc68302/interrupt-source-map.md`
2026-07-30. Triage: alla register-/vektor-/prioritetsfakta oförändrade.
"Current Emulator Coverage" (sidoprojektets egen implementationsstatus)
borttagen — gäller inte den här MAME-enheten. Inte använt i fas 3 steg 1
(ingen interruptcontroller i det steget); sparat här som facit för fas
3 steg 2. Se `docs/mc68302/README.md`.

This document maps the documented MC68302 interrupt sources to the
interrupt-controller state an emulator needs. It describes chip
semantics only. Board wiring — which ASR-10 device drives an external
IRQ pin — is not inferred here.

Primary source: MC68302 User's Manual, Sections 3.2.1 through 3.2.6,
Tables 3-3, 3-4 and 3-5, and the interrupt-controller register diagrams
in Sections 3.2.5.1 through 3.2.6.

Confidence:

- `confirmed`: directly stated by the manual text/table.
- `strongly supported`: reconstructed from damaged OCR table plus nearby text and the vector table.
- `tentative`: needs visual confirmation from original page art/PDF.
- `unknown`: not established by the current audit.

## Controller Rules

- Internal requests are called INRQ.
- All INRQ sources are presented to the CPU as interrupt level 4.
- External requests are called EXRQ.
- In normal mode, external interrupt priority is encoded on IPL2-IPL0;
  level 4 is reserved for INRQ and must not be generated externally.
- In dedicated mode, external request pins are IRQ7, IRQ6 and IRQ1.
- `IMR` polarity is confirmed: `1` enables an INRQ source, `0` masks it.
- `IPR` is write-one-to-clear for software, but event-register-backed
  sources are normally cleared by clearing the local event register.
- `ISR` is write-one-to-clear. It is set by the interrupt controller
  when an INRQ vector is supplied during IACK.
- An INRQ source is eligible when pending, enabled by `IMR`, and not
  blocked by an in-service source of equal or higher INRQ priority.
- On IACK for an INRQ source, the controller returns the source vector
  and sets the corresponding `ISR` bit. The manual states that the IPR
  bit is cleared on vectoring unless an event register exists for that
  INRQ source.

## INRQ Priority and Source Descriptors

All entries below are CPU level 4. Bits are the corresponding `IPR`,
`IMR`, and `ISR` bit positions unless otherwise noted.

| Priority | Source ID | Functional block | Trigger/event source | Event register | Bit | Vector low bits | Vector at `GIMR=0x8040` | Multiple events |
|---:|---|---|---|---|---:|---:|---:|---|
| 1 | `PB11` | Parallel I/O | high-to-low on PB11 when configured as input interrupt | none local | 15 | `0x0F` | `0x4F` | no |
| 2 | `PB10` | Parallel I/O | high-to-low on PB10 when configured as input interrupt | none local | 14 | `0x0E` | `0x4E` | no |
| 3 | `SCC1` | Communications processor | SCC1 protocol/channel events | `SCCE1` | 13 | `0x0D` | `0x4D` | yes |
| 4 | `SDMA_BusError` | Communications processor / SDMA | SDMA channel bus error | CP error reporting | 12 | `0x0C` | `0x4C` | yes |
| 5 | `IDMA` | IDMA | normal termination or error if enabled by CMR event mask bits | `CSR` | 11 | `0x0B` | `0x4B` | yes |
| 6 | `SCC2` | Communications processor | SCC2 protocol/channel events | `SCCE2` | 10 | `0x0A` | `0x4A` | yes |
| 7 | `Timer1` | Timer 1 | reference or capture event if enabled by TMR1 | `TER1` | 9 | `0x09` | `0x49` | yes |
| 8 | `SCC3` | Communications processor | SCC3 protocol/channel events | `SCCE3` | 8 | `0x08` | `0x48` | yes |
| 9 | `PB9` | Parallel I/O | high-to-low on PB9 when configured as input interrupt | none local | 7 | `0x07` | `0x47` | no |
| 10 | `Timer2` | Timer 2 | reference or capture event if enabled by TMR2 | `TER2` | 6 | `0x06` | `0x46` | yes |
| 11 | `SCP` | SCP | SCP transmit/receive/control event | SCP event/status | 5 | `0x05` | `0x45` | no in Table 3-4 |
| 12 | `Timer3` | Watchdog timer | software watchdog timer event | watchdog/system control | 4 | `0x04` | `0x44` | no |
| 13 | `SMC1` | Communications processor | SMC1 event | SMC event/status | 3 | `0x03` | `0x43` | no in Table 3-4 |
| 14 | `SMC2` | Communications processor | SMC2 event | SMC event/status | 2 | `0x02` | `0x42` | no in Table 3-4 |
| 15 | `PB8` | Parallel I/O | high-to-low on PB8 when configured as input interrupt | none local | 1 | `0x01` | `0x41` | no |
| error | `Level4Error` | Interrupt controller | level-4 IACK with no eligible INRQ | none | 0 | `0x00` | `0x40` | no |

Notes:

- `IMR` bit 0 is undefined and cannot mask the error source.
- `ISR` bit 0 is always zero.
- Confidence is `confirmed` for the vector low bits and `strongly
  supported` for the exact rank list (Table 3-4 OCR is damaged; Table
  3-5 and surrounding text agree on the priority order above).
- Timer 1 and Timer 2 model reference events. Capture event input and
  external `TIN1`/`TIN2` pin behavior are separate, smaller checkpoints.

## External Request Sources

| Source | Mode | CPU level | Input | Vector supplied by MC68302 when enabled | Vector at `GIMR=0x8040` | Clear path |
|---|---|---:|---|---|---:|---|
| `ExternalIrq7` | normal or dedicated | 7 | IPL encoding or IRQ7 | yes when `IV7=0` | `0x57` | external source must clear |
| `ExternalIrq6` | normal or dedicated | 6 | IPL encoding or IRQ6 | yes when `IV6=0` | `0x56` | external source must clear |
| `ExternalIrq1` | normal or dedicated | 1 | IPL encoding or IRQ1 | yes when `IV1=0` | `0x51` | external source must clear |

External priority levels 2, 3, and 5 in normal mode do not have
MC68302-generated vector encodings in Table 3-5. They require an
external vector or autovector.

`ExternalIrq6` is the ASR-10's DUART (SCN2681) IRQ path per
`docs/asr10/duart.md` — an external adapter concern, not an MC68302
internal-window register. It stays out of this device in fas 3 step 1
and is unaffected by anything built there.

## Clear and End-of-Service Summary

| Source class | Pending clear | In-service clear | Local event clear |
|---|---|---|---|
| Simple INRQ without local event register | IPR W1C or IACK transition | ISR W1C | not applicable |
| INRQ with local event register | clear unmasked local event bits; manual states this clears the IPR bit | ISR W1C | event register W1C |
| EXRQ | external device/board logic | not represented in INRQ ISR | external device-specific |
| Level-4 error | no ISR bit set | not applicable | clear cause by avoiding level-4 external request without INRQ |

## Status in this tree

Not implemented in fas 3 step 1 by design (plumbing only). This
document is the register/vector/priority reference for fas 3's second
step (interrupt controller). `docs/asr10/baseline-media.md` already
shows live ASR-10 register traffic at `fc6814`/`fc6816` (IPR/IMR at
this map's offsets `0x0814`/`0x0816`) that never changes while the boot
is stalled — the concrete, empirically observed symptom this map's
eventual implementation needs to resolve.
