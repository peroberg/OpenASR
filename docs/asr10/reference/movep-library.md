# DPRAM MOVEP Access-Thunk Library — FC6028-FC6136

Source: live shadow-RAM dump of the DPRAM chunk (`FC6000-FC67FF`, 2048
bytes, captured via `ASR10_FC2001_TRACE event=dpram_dump` and disassembled
offline with `unidasm -arch m68000` — no `-xchbytes` needed for a live RAM
dump, since it's already in native CPU byte order, unlike the ROM file).
Every entry below was independently located by scanning for `4e75` (RTS)
occurrences and disassembling backward from each hit; all entries listed
decoded as genuine `movep` + `rts` pairs (a handful of coincidental `4e75`
matches that decoded as unrelated/garbage instructions are omitted).

All thunks use address register **A0** as the base (displacement-relative
addressing, `(disp,A0)`); none load their own base — the base (e.g.
`FC2001` for our proven caller) is set by whichever routine calls the
thunk, before the call.

| Addr | Instruction | Direction | Width | Register | Displacement | Terminator |
|---|---|---|---|---|---|---|
| FC6028 | `movep.l ($0,A0),D0` | read | long | D0 | +0x00 | rts (FC602C) |
| FC602E | `movep.l D0,($8,A0)` | write | long | D0 | +0x08 | rts (FC6032) |
| FC6034 | `movep.l ($8,A0),D0` | read | long | D0 | +0x08 | rts (FC6038) |
| FC603A | `movep.w D0,($34,A0)` | write | word | D0 | +0x34 | rts (FC603E) |
| FC6040 | `movep.w D3,($34,A0)` | write | word | D3 | +0x34 | rts (FC6044) |
| FC6046 | `movep.l ($18,A0),D3` | read | long | D3 | +0x18 | rts (FC604A/uncertain, see note) |
| FC6056 | `movep.l ($8,A0),D0` | read | long | D0 | +0x08 | rts (FC605A) |
| FC605C | `movep.l D0,($8,A0)` | write | long | D0 | +0x08 | rts (FC6060) |
| FC6062 | `movep.l D1,($8,A0)` | write | long | D1 | +0x08 | rts (FC6066) |
| FC6068 | `movep.l D0,($10,A0)` | write | long | D0 | +0x10 | rts (FC606C) |
| FC606E | `movep.l D2,($10,A0)` | write | long | D2 | +0x10 | rts (FC6072) |
| FC607A | `movep.w D0,($24,A0)` | write | word | D0 | +0x24 | rts (FC607E) |
| FC6086 | `movep.w D0,($2c,A0)` | write | word | D0 | +0x2c | rts (FC608A) |
| FC6092 | `movep.w D0,($3c,A0)` | write | word | D0 | +0x3c | rts (FC6096) |
| FC609E | `movep.w D0,($44,A0)` | write | word | D0 | +0x44 | rts (FC60A2) |
| FC60AA | `movep.w D3,($24,A0)` | write | word | D3 | +0x24 | rts (FC60AE) |
| **FC60B0** | **`movep.l ($68,A0),D2`** | **read** | **long** | **D2** | **+0x68** | **rts (FC60B4)** |
| FC60B6 | `movep.l ($78,A0),D0` | read | long | D0 | +0x78 | rts (FC60BA) |
| FC60BC | `movep.l ($70,A0),D0` | read | long | D0 | +0x70 | rts (FC60C0) |
| FC6136 | `movep.w D0,($14,A0)` | write | word | D0 | +0x14 | rts (FC613A) |

**Note (FC6046):** the byte immediately preceding several `rts` hits in the
low-address portion of the dump (`fc604e`) decoded as data/ILLEGAL rather
than a clean second thunk boundary — flagged so this entry isn't
over-stated as byte-perfect; the `movep.l ($18,A0),D3` instruction itself
is solid (matches the same opcode class as every other entry), only its
exact terminator offset carries slightly less confidence.

## Coverage vs. the offsets given in the initiating task context

The task's pre-supplied partial offset list was `+10,+14,+1c,+20,+24,+2c,
+34,+68,+70,+78`. This independent scan found `+00,+08,+10,+14,+18,+24,
+2c,+34,+3c,+44,+68,+70,+78` — **substantial overlap, not identical**.
`+1c` and `+20` from the given list were not located in this scan;
`+00,+08,+18,+3c,+44` were found but weren't in the given list. Reported
as found, not reconciled by assumption.

## Direction/register summary

- **Read-only offsets observed:** +00 (D0), +08 (D0 — also has write
  variants, see below), +18 (D3), +68 (D2, our proven caller), +70 (D0),
  +78 (D0).
- **Write-only offsets observed:** +10 (D0, D2), +14 (D0), +24 (D0, D3),
  +2c (D0), +34 (D0, D3), +3c (D0), +44 (D0).
- **Both directions at the same offset:** +08 — read into D0 (FC6034,
  FC6056) and write from D0 (FC602E, FC605C) and from D1 (FC6062). This
  read/write pairing at identical offsets, with register variety on the
  write side, is the strongest structural argument for a genuine hardware
  register file (a plain shared-RAM buffer wouldn't typically need
  dedicated per-register accessor thunks with this much variety).

## Static callers

**Corrected 2026-07-16 — retracts the "zero literal references" claim
below.** A direct word-level scan of the live ROM image (read through the
running CPU's own address space, `0xf80000-0xfbfffa`, matching every
`4eb9 fffc 60b0` triplet, i.e. `jsr $fffc60b0`) found **four** static ROM
callers, all clustered in one block:

```text
f8db04   jsr $fffc60b0
f8db24   jsr $fffc60b0
f8db36   jsr $fffc60b0
f8db52   jsr $fffc60b0
```

Full live disassembly of `f8db00-f8db60` shows these are four independent
callback bodies, not one routine with four call sites:

- **`f8db00-f8db10`**: `movea.l #$fc2001,A0` / `jsr $fffc60b0` / `asl.w #6,D2`
  / `ori #1,ccr` / `rts` — a bare utility that returns the scaled PAR
  sample in D2 (no local storage).
- **`f8db1e-f8db2e`**: same setup/call/scale, then `bra.w` to a shared
  tail at `f8db6e` (outside the captured range).
- **`f8db30-f8db4a`**: same setup/call/scale, then
  `add.w (6,A2),D2` / `lsr.w #1,D2` / `move.w D2,(6,A2)` / `bra.w f8db6e`
  — an exponential-smoothing filter (`new = (old + (raw<<6)) >> 1`)
  against a per-instance state cell at `(A2+6)`.
- **`f8db4c-f8db60+`**: same setup/call/scale, then reads `(6,A2)` into D0
  and continues (truncated in the captured dump) — a second, related
  filter variant.

None of these four blocks write PBDAT, PBCNT, PACNT, or the ES5506 PAGE
register immediately around the `jsr`/scale sequence — whatever selects
"which channel" PAR reads (if anything) is not adjacent to these call
sites. In every capture taken so far (45s and longer), these four blocks
are **never observed executing** — every captured `FC60B0` PAR read
still traces to exactly 32 events (8 samples x 4 MOVEP bytes) matching
only `00686e`'s own loop. This is consistent with these four being
**event-driven callbacks** (e.g. armed only when a physical control is
touched) that never fire during an unattended headless boot — see
`architecture.md`/evidence-tree.md Task 4 discussion.

The earlier claim of "zero literal `$fc60xx` references... ~97,000
instructions" was based on a differently-shaped search (looking for a
literal matching a different encoding form) and missed the
`4eb9 fffc 60b0` (absolute-long `JSR`) encoding actually used here. It is
retracted as a search-methodology error, not as new hardware evidence.

**Proven for a fifth, RAM-resident call site:** `FC60B0` is also called
from `00686e` (`jsr $fffc60b0` at `00688a`, live-disassembly-confirmed
this session; the 8-iteration `moveq #7,D7` measurement loop — see
`subsystems.md`). This remains the only call site actually observed
firing at runtime.

**Unproven for the other ~19 thunks.** No live RAM dump wide enough to
locate their callers has been taken; this is an open gap, not a claim
that no callers exist. See `subsystems.md` §"Open" and
`evidence-tree.md` §5.
