# Observed MC68302 Access Coverage (ASR-10 bootstrap sequence)

Flyttad från `~/develop/mc68302/docs/mc68302/observed-access-coverage.md`
2026-07-30, **omskriven** från "diagnostic guard"-rapportform (som
beskriver sidoprojektets egna testverktyg) till en ren lista över
observerade ROM-register skrivningar i ordning — bestående fakta om vad
ASR-10-ROM:et faktiskt gör, oberoende av vilket verktyg som observerade
det. Använd direkt av fas 3 steg 1 (Port A/B-sekvensen). Se
`docs/mc68302/README.md`.

This is the observed MC68302-internal register write sequence during
ASR-10 ROM bootstrap, from the side project's bounded ROM trace
(instructions 1-120000, ending at the known DBRA delay loop at PC
`0xFFF89C52`). It is not a full boot trace — only the SIB-relevant
writes up to that point.

## Bootstrap Register Write Sequence

| Order | PC | Address | Offset | Register | Access | Value | Notes |
|---:|---:|---:|---:|---|---|---:|---|
| 1 | `0x00000016` | `0x000000F4` | bootstrap alias | SCR high word | write word | `0x0F20` | see `docs/mc68302/scr-spec.md`; final readback `0x00200000` after W1C |
| 2 | `0x00000016` | `0x000000F6` | bootstrap alias | SCR low word | write word | `0x0000` | disables hardware watchdog enable bits selected by reset SCR value |
| 3 | `0x0000006C` | `0x00FC6200` | `0x0200` | dual-port RAM | write word | `0x33FC` | first internal-RAM write after BAR, offset `0x0000..0x07FF` range |
| 4 | `0xFFFB8E06` | `0x00FC681E` | `0x081E` | PACNT | write word | `0xE000` | PA13/PA14/PA15 select DREQ/DACK/DONE (unused by ASR-10 — programmed I/O, see `docs/asr10/disk-read-path.md`) |
| 5 | `0xFFFB8E0E` | `0x00FC6820` | `0x0820` | PADDR | write word | `0xFFFF` | all Port A GPIO bits configured output (moot given PACNT above selects dedicated function for 13-15) |
| 6 | after PADDR | `0x00FC6822` | `0x0822` | PADAT | write word | latch sequence `0x18FC`, `0x18F8`, `0x18E8`; final readback `0x58E8` | Port A latch/readback |
| 7 | `0xFFFB8E16` | `0x00FC6824` | `0x0824` | PBCNT | write word | `0x0080` | PB7 selects WDOG dedicated output; PB0-6 and PB8-11 stay GPIO |
| 8 | `0xFFFB8E1E` | `0x00FC6826` | `0x0826` | PBDDR | write word | `0xF097` | bits 0,1,2,4,7 = output; **bit 3 (PB3) = input** — see `docs/mc68302/pin-function-map.md` |
| 9 | after PBDDR | `0x00FC6828` | `0x0828` | PBDAT | write word | `0x0007` | Port B latch/readback |
| 10 | `0xFFFB8E36` | `0x00FC6821` | `0x0821` | PADDR low byte | read/write byte | RMW clears bit 4 | byte-lane read-modify-write |

Bounded trace result: no unhandled MC68302-internal register access
observed within instructions 1-120000 beyond this sequence. This is a
bound, not a claim that the ROM never touches other SIB registers
later — `docs/asr10/baseline-media.md`'s own, independently captured
traces already show later, different `fc6814`/`fc6816` traffic well
past this window.

## Cross-check against this tree

`docs/asr10/experiment-flags.md`'s `ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3`
and `ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE` both operate
on registers this sequence touches (`PBDAT` at `0xfc6828`, and the
undocumented `0xfc6860` respectively) — this sequence is the
authoritative "what does the real bootstrap actually write" reference
for building `mc68302sim.cpp`'s PIO Port B model.
