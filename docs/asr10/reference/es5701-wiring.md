# ES5701 SuperGLU Wiring Study

Source: `docs/asr10/superglu-investigation-2026-06-29.md` (the only doc in
this tree citing the Buchty VHDL reconstruction) and `architecture.md` §4.
No additional ES5701/SuperGLU sources exist in this tree; this document
does not introduce new hardware evidence, only organizes and cross-checks
what's already documented against this session's FC2xxx/FC2Dxx/FC30xx
findings.

## 1. What the Buchty VHDL reconstruction documents [STAT, per existing doc]

Two separate chips, with **non-overlapping responsibilities**:

**ES5570 GLU** — the address decoder / bus controller:
- system address decode, bus control, DTACK generation, interrupt
  acknowledge, chip-selects, RAM/ROM decode
- FDC chip-select, DUART chip-select, DOC/OTIS chip-select, DMAC
  chip-select
- documented candidate map (VFX-SD-family): `20xxxx`=DOC, `24xxxx`=DMAC,
  `28xxxx`=DUART, `2Cxxxx`=FDC, `30xxxx`=External, `40xxxx`=Sample RAM,
  `C0xxxx`=ROM
- **implements no functionality of its own** — purely "which hardware does
  the CPU talk to", not "what that hardware does"

**ES5701 SuperGLU** — audio-bus glue, working almost exclusively with the
sound subsystem:
- bus translation between CPU and ESP
- bus translation between CPU and OTIS/OTTO
- **latching of OTIS sample addresses**
- data-bus masking for 8/12/13/16-bit sample formats
- clock division
- DTACK to ESP
- **has no memory decoder, generates no chip-selects, does not know the
  floppy controller exists**

Explicit existing conclusion (`superglu-investigation-2026-06-29.md`):
"SuperGLU is not a floppy controller and should not be treated as the
primary track for the disk-boot blocker" and "Neither of these chips
implements floppy logic. ES5570 only selects the FDC. ES5701 doesn't even
know the FDC exists."

## 2. Cross-check against this session's FC-prefixed evidence

**Critical distinction:** the ES5570 candidate map above (`20xxxx`-`40xxxx`,
`C0xxxx`) is entirely in the **low** half of the 24-bit address space. Our
own `asr10_boot.cpp` already carries "candidate" stub mappings at exactly
these addresses (`es550x_vfx_candidate_r/w`@0x200000,
`es5510_vfx_candidate_r/w`@0x260000, `duart_vfx_candidate_r/w`@0x280000,
`fdc_vfx_candidate_r/w`@0x2c0000, `es5506_ts_candidate_r/w`@0x300000,
`es5510_ts_candidate_r/w`@0x380000) — modeling the VFX-SD/TS-10-style
ES5570 decode directly. **None of these addresses are ever hit at
runtime** in any session's capture. Every real observed access this
session (`FC2001`-relative MOVEP library, `FC2D40-FC2D7F`, `FC3000-FC31FF`)
is **high**-half, `FC`-prefixed — architecturally on the opposite side of
the address space from the ES5570 candidate map.

This is consistent with `architecture.md` §4's board part list: ASR-10 has
its **own custom decode PAL** — `U5 = custom PAL "ASR-10 V1.1" +
74HC138/139 decoders` — distinct from the ES5570 GLU used in the VFX-SD/
TS family. **ASR-10 almost certainly does not use ES5570's decode map at
all**; the FC-prefixed windows we've observed are consistent with a
completely different, custom chip-select scheme built from the PAL +
74HC138/139, not yet reverse-engineered.

## 3. Does ES5701 plausibly sit on the FC2xxx/FC2Dxx/FC30xx path?

**This requires care, because ES5701's documented scope is narrower than
"anything audio-related."** Per the source, ES5701's jobs are: CPU↔ESP bus
translation, CPU↔OTIS bus translation, **sample address latching**, and
**sample-data bit-depth masking**. These are all downstream-of-chip-select
functions concerned with *sample memory access* (reading/writing waveform
data), not with a chip's *host control register* interface.

The access chain we've proven this session (`FC2001+0x68` → MOVEP →
`es5506_device`'s `PAR`/`IRQV`/`PAGE` registers) is **host-register**
traffic — the CPU reading/writing the chip's control/status registers,
not fetching sample data. ES5506/ES5505's own host-register protocol
(`reg_read_low/high/test`, `m_write_latch`/`m_read_latch` accumulation) is
handled entirely inside the chip's own host interface logic in every MAME
driver that wires it — none of those drivers route the host register
interface through anything resembling ES5701; ES5701's analog in those
drivers (`esqpump.h`'s `esq_5505_5510_pump_device`) only connects **serial
audio sample streams** between the ES5505/6 and ES5510, and is bound as a
`device_sound_interface`, never inserted into the CPU's `address_map`.

**Conclusion: ES5701, per its documented scope, is unlikely to be
directly involved in the specific MOVEP/PAR host-register chain traced
this session.** If FC2001 does turn out to be an ES5506-class chip, the
chip-select routing it to that address would be the job of the custom PAL
(U5), and ES5701 (if present at all in that path) would only become
relevant for *sample-memory* traffic from that same chip — a separate,
unobserved address window, not FC2001/FC2069 itself.

## 4. Wiring hypothesis table

| Window | Hypothesized path | Evidence level |
|---|---|---|
| FC2001 (host regs) | CPU → custom PAL (U5) chip-select → ES5506-class chip's own host-register logic | [HYP] — register-offset math is [PROVEN] (see `es5506-chain-verification.md`); the *board* routing through U5 is inferred, not cited |
| FC2001 (sample-side, unobserved) | CPU → custom PAL → ES5701 → OTIS sample RAM | [HYP], and *only* relevant if a separate, not-yet-observed window turns out to carry sample-address traffic — no evidence gathered this session distinguishes this from the host-register window |
| FC2D40-FC2D7F | unknown — not reached at runtime this session | [OPEN] — no hypothesis supported by evidence |
| FC3000-FC31FF | CPU → custom PAL → some device via the same MOVEP/A0 convention as FC2001 (proven shared idiom, not shared chip) | [HYP] for device identity; [PROVEN] only that the *access convention* matches |
| ES5570 candidate map (0x200000-0x380000, this driver's `*_vfx_candidate`/`*_ts_candidate` stubs) | Not applicable to ASR-10 — modeled on VFX-SD/TS-10's ES5570 decode, never hit at runtime | [DISPROVEN as ASR-10's actual map] — retained in the driver as inert reference stubs only |

## 5. Bottom line

The Buchty/ES5701 material is well-documented for **what ES5701 does**,
but provides **zero direct evidence for ASR-10's actual FC-prefixed
address map**, because ES5701 (per its own documented scope) isn't the
address decoder — U5's custom PAL is, and that PAL has not been reverse
engineered in any doc in this tree. Any claim that a specific FC-window
"routes through ES5701" would currently be invention, not verification.
