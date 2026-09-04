# ES5506 Host-Access Mathematical Chain — Verification

**Purpose:** verify, from MAME source only, whether the observed CPU access
`FC2001+0x68` (four MOVEP bytes at FC2069/6B/6D/6F) can be shown to arrive at
`es5506_device`'s `PAR` register (`case 0x68/8` in `reg_read_test`/
`reg_read_low`/`reg_read_high`), or whether the chain fails somewhere.

This is the explicit gate for any future ES5506 experiment: if it closes,
instantiating the device and wiring a `read_port_cb()` is a *reuse* of an
existing, precedented MAME convention; if it doesn't close, any such wiring
would be invention, not verification.

---

## 1. The claim being tested

> CPU odd-byte address `FC2001+disp` → (hypothetical) `address_map` entry →
> odd-lane convention → `offset` units arriving at `es5506_device::read()` →
> the switch granularity behind `case 0x68/8`.

## 2. Step 1 — `es5506_device::read()`'s own offset semantics

`src/devices/sound/es5506.cpp:1537-1563`:
```cpp
u8 es5506_device::read(offs_t offset)
{
    es550x_voice *voice = &m_voice[m_current_page & 0x1f];
    int shift = 8 * (offset & 3);
    if (shift != 0)
        return m_read_latch >> (24 - shift);
    ...
    if (m_current_page < 0x20)      m_read_latch = reg_read_low(voice, offset / 4);
    else if (m_current_page < 0x40) m_read_latch = reg_read_high(voice, offset / 4);
    else                             m_read_latch = reg_read_test(voice, offset / 4);
    ...
    return m_read_latch >> 24;
}
```
`offset` here is a **raw byte counter** as seen by an 8-bit host bus (each
unit = one `es5506_device::read()` call = one host byte-lane cycle). Every
4th call (`offset&3==0`) triggers the actual 32-bit register fetch via
`offset/4`; the other 3 calls drain `m_read_latch` byte-by-byte. This
matches a genuine 8-bit accumulate-then-commit host protocol.

## 3. Step 2 — the switch granularity inside `reg_read_test`/`reg_read_low`/`reg_read_high`

`es5506.cpp:1515-1535` (`reg_read_test`), `1363-1437` (`reg_read_low`),
`1440-1514` (`reg_read_high`):
```cpp
case 0x68/8:    // PAR
    if (!m_read_port_cb.isunset())
        result = m_read_port_cb(0) & 0x3ff;
    break;
case 0x70/8:    // IRQV
    ...
case 0x78/8:    // PAGE
    ...
```
The `switch` argument is `offset/4` (passed in by `read()`). `0x68/8` = 13
(integer division), so this case fires when `offset/4 == 13`, i.e.
`offset` ∈ [52, 55] (0x34-0x37). **The literal `0x68` in the case label is
the register's position in the chip's own *datasheet* byte-numbering
(8 bytes per 32-bit register), which is exactly double MAME's internal
4-bytes-per-register `offset` counter.** This is a naming/documentation
choice in the source, not a claim that `offset==0x68` arrives at the
switch — `offset==0x34` (52 decimal) is what actually matches.

**This is the crux of the chain:** the CPU-visible displacement (+0x68) and
MAME's internal `offset` value (0x34) differ by exactly a factor of 2. The
chain only closes if the real (or hypothetical ASR-10) wiring divides the
CPU displacement by 2 before it reaches `es5506_device::read()` — i.e. an
odd-byte-lane-only connection to a 16-bit bus.

## 4. Step 3 — does that halving convention have real precedent?

Searched every MAME driver that instantiates `es5506_device` for its
address_map wiring (`grep -rl es5506_device src/mame`):

```
src/mame/ensoniq/esqkt.cpp:184:
    map(0x300000, 0x30007f).rw("ensoniq", FUNC(es5506_device::read),
                                FUNC(es5506_device::write)).umask16(0x00ff);

src/mame/nmk/macrossp.cpp:847:
    map(0x400000, 0x40007f).rw("ensoniq", FUNC(es5506_device::read),
                                FUNC(es5506_device::write)).umask16(0x00ff);

src/mame/seta/ssv.cpp:392:
    map(0x300000, 0x30007f).rw(m_ensoniq, FUNC(es5506_device::read),
                                FUNC(es5506_device::write)).umask16(0x00ff);
```

**Three independent, shipped, working MAME drivers use the identical
pattern**: a CPU range exactly `0x80` bytes wide (`base` to `base+0x7f`),
`.umask16(0x00ff)` (odd byte lane only on a 16-bit bus). `es5506_device`'s
own register file spans MAME-internal `offset` 0x00-0x3F (64 bytes — the
highest case, `0x78/8=15`, times 4, plus 3 = 0x3F). An 0x80-byte CPU
window with only the odd lane live delivers exactly `0x80/2 = 0x40 = 64`
distinct byte-offsets to the device — **an exact match to the register
file size**, not an approximation. This proves the convention
`CPU_displacement = 2 × device_offset` (equivalently
`device_offset = CPU_displacement / 2`) by construction, independent of
ASR-10 — it's how ES5506 is wired on a 16/32-bit bus everywhere it's used
in this codebase's shipped drivers.

`esqasr.cpp` (the existing ASR-10 skeleton) does **not** wire ES5506 into
its address map at all (`asr_map()` is ROM+RAM only) — so there is no
ASR-10-specific citation for the base address or window size, only for the
*device's own* wiring convention wherever it IS mapped.

## 5. Step 4 — closing the chain for our observed displacement

Given the proven convention (`device_offset = CPU_disp / 2`) and our
observed displacement `+0x68`:
```
device_offset = 0x68 / 2 = 0x34 = 52 decimal
case value    = 52 / 4  = 13
0x68/8        = 13                          <- exact algebraic identity
```
`52/4 == 0x68/8` is not a coincidence or a forced scaling — it's the same
division applied twice (`/2` then `/4`, vs. `/8` directly), which are
algebraically identical for any displacement that is itself a multiple of
8 (0x68 = 8×13 ✓, 0x70 = 8×14 ✓, 0x78 = 8×15 ✓ — all three of our observed
offsets satisfy this exactly, with zero remainder, meaning none of them
required rounding or approximation to land on a case boundary).

## 6. Verdict

**The chain closes: `FC2069/6B/6D/6F → PAR bytes 0-3` is proven consistent
math**, contingent on exactly one unproven link: that ASR-10's actual (or a
hypothetical) FC2001 wiring uses the same odd-lane-on-16-bit-bus convention
that `esqkt.cpp`/`macrossp.cpp`/`ssv.cpp` all use for this exact device.
That link is a **precedented, standard, but ASR-10-unconfirmed** choice —
no ASR-10 schematic, MAME driver, or prior doc in this tree states the
board wires ES5506 this way at this address. Every other link in the chain
(the offset/4, the case-label arithmetic, the register identity, the
0x80-byte window matching the 0x40-byte register file exactly) is proven
directly from source, not inferred.

**Gate status for next session: OPEN, one link short of fully closed.**
The missing evidence is *board-level*, not *chip-level* — confirming (or
refuting) that FC2001 is genuinely wired odd-lane/16-bit to whatever chip
lives there would close it completely.
