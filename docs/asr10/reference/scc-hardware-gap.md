# SCC: What Is Firmware Actually Wired To?

The interrupt topology ranked SCC1/SCC2 as the strongest-evidenced gap
in the MC68302 model: firmware unmasks them in `IMR=$E480`, real
handlers exist at their computed vectors, and this project models no
SCCs at all. This document measures what firmware actually does with
them — not whether the topology finding was right (it was), but what
it would take to close the gap. No implementation this round.

## Del 1 — Two Closures From The Prior Task

### 1.1 Where do the 1404 edges actually live?

**They live exactly where the prior document said: `static/call-graph-
edges.csv`, column `mapping_basis`, value `mirror-hypothesis`.**
Verified directly against the current file, not against any prior
summary:

```
awk -F',' 'NR>1{print $10}' static/call-graph-edges.csv | sort | uniq -c
   3839 direct
   1404 mirror-hypothesis
```

3839 + 1404 = 5243, matching the documented total exactly. The prior
task's "no rows for the CSV" claim was **correct but incomplete in a
way that produced a wrong impression**: it was checking whether the
CSV had rows for the *five E2 MMIO samples* (it does not — true, and
still true), not whether the 1404-count existed in the file at all (it
does, trivially, and always did). Restated precisely so it cannot be
misread a second time: **the CSV has zero rows for MMIO data
references and exactly 1404 rows for the short-address mirror-
hypothesis call targets — two different, non-overlapping subsets of
the same 5243-row file, not two different files.**

Breakdown of the 1404, by mechanism (measured, not assumed uniform):

```
1348  binding-slot-call     (ROM -> $FF8000-$FF9FFF short-address jsr/jmp)
  51  binding-slot-target   (per-version binding-table slot definitions)
   4  vector                (TRAP #10/#11's own vector-table entries, both OS versions)
   1  direct-abs.l          (the $00BF0E historical V1.61 SCC test case, below)
```

**The real bookkeeping defect, found while closing this loop**: the
CSV's own SHA-256 is cited inconsistently across the project's own
canonical documents. `static/README.md`'s own table has the *correct*,
currently-matching hash:

```
sha256sum static/call-graph-edges.csv
472373eea0fac895f07d115c8168cc4551957a9a2b1c6f3a1e6909541c5715de
```

Three other documents (`DOCUMENTATION-MANIFEST.md`, `call-graph.md`,
`static/runtime-observations-2026-08-10.md`) cite a *different* hash,
`b8bfb32053274a66...`, for the same file — and one of them
(`runtime-observations-2026-08-10.md`) explicitly claims it "matches
the consolidation manifest and `static/README.md`," which is false as
of this check: `static/README.md` has never had that hash in its own
row for this file. `call-graph.md`'s own text ("kantantalet gick från
5240 till 5243 mellan två generationer av filen") already documents
that at least two on-disk generations of this file existed — the
stale hash almost certainly belongs to the 5240-edge generation, and
three citations were never updated when the file was regenerated to
5243 edges, while `static/README.md`'s was. **The 1404 count itself is
unaffected** — verified directly against the current 5243-row file,
independent of which hash is cited where — but a hash citation that
three of four documents got wrong, unnoticed until this check, is
exactly the kind of repeated-without-reverification fact this task
exists to catch. Corrected in `call-graph.md` and
`DOCUMENTATION-MANIFEST.md` below; the worklog is left as historical
record, per this project's own convention that worklogs are
provenance, not live truth, and get corrections added, not values
silently rewritten.

**What actually depends on the mirror hypothesis**: all 1404 rows have
`effective_address` in `$FF801E-$FF9FF6` (`to_space=HIGH-RAM-ALIAS-
CANDIDATE`), confirming `e2-address-model.md`'s own claim that a
`$FC5803`-only decode change cannot touch any of them — they simply
don't share an address with it. One row is worth naming precisely
because it intersects this task directly: `dal:V161:00BF0E:FF8ECA`,
`executed=observed`, comment *"återflöde till $00BF14 observerat;
träffar slot $008ECA endast om spegling gäller"* — this is the
historical V1.61 test case from `$00BF0E` (`4EB9 FFFF8ECA`), sitting
**inside the SCC1/SCC2 receiver-enable sequence itself**
(`runtime-service-model.md` §3: `$00BEF2-$00BF1A` turns on SCC2's then
SCC1's receiver, calls this slot, then unmasks both interrupts).
Binding-table slot `$008ECA` resolves (under the still-open mirror
hypothesis) to ROM `$F8E054` in V1.61 and to **OS code `$00ECB0`** in
V3.50 — an already-documented example of the ROM→OS patch mechanism,
called between enabling the SCC receivers and unmasking their
interrupts. Not traced further this round (out of this task's
disassembly budget), but named precisely so the next SCC pass does not
have to rediscover that this one mirror-hypothesis edge sits directly
in the SCC init path.

### 1.2 The `$FF7F00-$FF7FF6` stride-8 structure

**Confirmed structurally, not confirmed as a voice table.** A flat
byte dump of `$FF7F00-$FF7FFF` (256 bytes, no assumed slot phase) shows
an exact, regular grid: byte offset ≡ `$07` (mod 8) reads `$0F`, every
other byte reads `$00`, for **exactly 31 marker bytes**
(`$FF7F07, $FF7F0F, ..., $FF7FF7`) — the 32nd possible slot
(`$FF7FFF`) breaks the pattern at `$00`. 31 is exactly `ACT=$1F`, as
hypothesized, and the count was verified directly (31, not assumed
from the earlier stride observation).

**Before/after a MIDI note-on**, only three bytes change, all inside
the *first* record (`$FF7F00-$FF7F07`):

```
$FF7F03: $00 -> $01
$FF7F05: $00 -> $02
$FF7F07: $0F -> $03
```

This is real, structured, note-triggered activity in a 31-entry
array sized exactly to the ES5506's configured voice count — but a
single before/after diff, with only one record ever touched (always
the same one, record 0), does not by itself distinguish "per-voice
allocation table, and this build's test always assigns voice 0" from
"an unrelated 31-entry structure that happens to react to any MIDI
event." **`[Likely]` structurally a per-voice or per-event table sized
to ACT; `[OPEN]` whether it specifically holds voice-assignment data**
— would need a second note (ideally while voice 0 is still active) to
see whether a *different* record changes for a *different* voice,
which this task's scope did not include measuring.

## Del 2 — SCC Register And Parameter-RAM Traffic

**Method**: read/write taps across SCC1/SCC2/SCC3's parameter-RAM and
register ranges (`memory-map.md` §2.1b/§2.2,
`docs/mc68302/communications-block-map.md`), installed after BAR
settle (`t=7s`, matching the already-established `mc68302_guards.lua`
convention — an earlier install on `$FC6000-$FC6FFF` is silently
dropped by `install_internal_window()`'s own reinstall, per §8.5).
Witness: three checkpoints (`file_loaded`, `selected`, `note_played`)
all printed successfully, confirming the tap was live through the
whole post-install window; the boot phase before `t=7s` is a known,
documented gap (BAR itself is written at `t≈0.000004s`/`5.395s`, well
before any SCC configuration observed below, so this gap does not
affect what follows).

**Result: SCC1 and SCC2 are both fully configured with real,
standards-shaped buffer-descriptor rings. SCC3 receives nothing at
all.**

### SCC1 (`$FC6400-$FC643F`, parameter RAM; `$FC6880-$FC688B`, registers)

Eight buffer descriptors, each exactly 8 bytes
(`STATUS:16, RESERVED:16, BUFFER_ADDRESS:32`), matching the MC68302's
standard communications-processor BD format exactly:

```
$FC6400  STATUS=$D000 RESERVED=$0000 ADDR=$00F76600
$FC6408  STATUS=$D000 RESERVED=$0000 ADDR=$00F76920
$FC6410  STATUS=$D000 RESERVED=$0000 ADDR=$00F76C40
$FC6418  STATUS=$D000 RESERVED=$0000 ADDR=$00F76F60
$FC6420  STATUS=$D000 RESERVED=$0000 ADDR=$00F77280
$FC6428  STATUS=$D000 RESERVED=$0000 ADDR=$00F775A0
$FC6430  STATUS=$D000 RESERVED=$0000 ADDR=$00F778C0
$FC6438  STATUS=$F000 RESERVED=$0000 ADDR=$00F77BE0   <- last BD, wrap bit set
```

Every buffer address steps by exactly `$0320` (800 bytes). The eighth
descriptor's status word is `$F000`, not `$D000` — exactly `$D000 |
$2000`, the extra bit matching the standard SCC "W" (wrap: last
descriptor in the ring, loop back to the first) convention. `$FC6482 =
$0320` — the same 800-byte stride, in the "extra" parameter block just
past the eight descriptors — is `MRBLR` (Maximum Receive Buffer Length
Register) in the standard communications-processor parameter-RAM
layout: firmware sets the per-buffer capacity once, and every
descriptor's stride matches it exactly. `$FC6486` is the only address
in the whole probe that is both read and written (twice each) — the
natural signature of a live "current buffer pointer" field, unlike the
one-time `MRBLR` write next to it. Registers: `SCON1=$7000`,
`SCM1=$703B` (already known: `ENR=1`), `$FC6888=$FFFF`, `$FC688A=
$0505` (SCCE1/SCCM1 by offset, not decoded bit-by-bit here).

### SCC2 (`$FC6500-$FC653F`; `$FC6890-$FC689B`)

Identical structure, eight descriptors, `$0320`-byte stride, wrap bit
on the eighth (`$F000`), buffers at `$00F74B00` through `$00F760E0`,
`MRBLR=$0320` at `$FC6582`, live pointer at `$FC6586`. `SCON2=$7000`,
`SCM2=$703B` (`ENR=1`, matching the already-known boot sequence).

### SCC3 (`$FC6600-$FC660F`, `$FC6700-$FC6714`, `$FC68A0-$FC68A5`)

**Zero reads, zero writes, anywhere, across the whole run.** No buffer
descriptors, no register writes. Consistent with (and independently
confirming) the interrupt topology's own finding: SCC3's IMR bit (bit
8) is not set in the observed `$E480`, so firmware never enables it —
and now confirmed firmware never even *configures* it, not just leaves
its interrupt masked.

### SIB-coverage baseline, re-attributed

The consolidation round's calibrated `known_unimplemented` allowlist
(`asr10_guards.lua`) has **89 distinct offsets** (re-counted directly
from the source list this round; the code comment's own "88" is a
minor stale figure, off by one, from before a later addition — not
chased further, immaterial to the finding). Of those 89, **83 (93%)
belong to SCC1/SCC2**: 32 each for their buffer-descriptor ranges
(`$0400-043E`/`$0500-053E`), 5 each for the adjacent extra fields
(`MRBLR`/current-pointer/etc.), and 9 for the SCC1-3 register block
(`SCON`/`SCM`/`DSR`/etc., including `SIMODE`). The remaining 6 are
`GIMR`/`IPR`/`IMR`/`ISR`/`PACNT`/`PADDR` — nothing to do with SCC.
**All 89 are classified uniformly as `known_unimplemented`** — none as
`known` (fully modeled) or as an "intentionally inert" case like
IDMA's masked sources. This baseline already said, in its own byte
count, that SCC1/SCC2 dominate this project's uninstrumented MC68302
surface; this task makes that explicit instead of leaving it folded
into an allowlist total.

## Del 3 — What The Handlers Expect

`scc1_isr`/`scc2_isr` (`$FF8D56`/`$FF8D92`, live addresses;
`$008D56`/`$008D92` static/ROM-relative, per `e2-address-model.md`'s
own reconciliation) set up `A2` = a control block pointer
(`($12D8).w`/`($1320).w`), `A0` = the channel's parameter-RAM base
(`$FC6400`/`$FC6500` — the exact buffer-descriptor bases measured
above), `A1` = the channel's register base (`$FC6880`/`$FC6890`), call
a shared routine at `$00643C`, then EOI to ISR with the channel's own
bit. Disassembled this round (`unidasm -arch m68000`, V3.50 OS image,
segment-1 rule: RAM `$00643C` = disk offset `0x8A3C`):

```
00643c: move.b  ($8,A1), D1        ; read SCCE/SCCM byte at register-base+8
006440: btst    #$2, D1            ; test event bit 2
006444: beq     $6466              ; skip if clear
006446: move.b  #$4, ($8,A1)       ; write-1-to-clear bit 2 (SCCE convention)
00644c: jsr     $fff8c0e6.l        ; ROM callout
006452: clr.w   $d04.w
006456: trap    #$3                ; known OS primitive, trap3_enqueue
006458: move.w  #$8d4a, ($2,A5)
00645e: movea.w #$23f6, A1
006462: trap    #$9                ; known OS primitive, trap9_slot_install
006464: rts
006466: btst    #$0, D1            ; test event bit 0
00646a: beq     $64b8
00646c: move.b  #$1, ($8,A1)       ; write-1-to-clear bit 0
006472: move.w  ($c,A2), D2        ; D2 = a counter from the control block (+$C)
006476: lsl.w   #3, D2             ; D2 *= 8 -- an index into an 8-byte-stride table
006478: lea     (A0,D2.w), A0      ; A0 = base-of-BD-ring + index*8 = the ACTIVE descriptor
00647c: bset    #$7, (A0)          ; set the descriptor's own top status bit
006480: beq     $648a
006482: move.b  #$5, D0
006486: trap    #$0
006488: ori.b   #$28, D0
00648c: ori.b   #$1, D1
006490: beq     $649a
006492: move.b  #$6, D0
006496: trap    #$0
006498: ori.b   #$28, D0
00649c: ori.b   #$1, D0
0064a0: beq     $64a4
0064a2: bra     $6446              ; loop back to re-check event bits
0064a4: andi.w  #$ff00, (A0)       ; clear the descriptor's low status byte
0064a8: btst    #$5, (A0)
0064ac: beq     $64b4
0064ae: clr.w   ($c,A2)            ; reset the control-block counter
```

**This answers Del 3's question directly.** `scc_rx_common` reads a
byte-wide *event* register (offset `+8` from the SCC's own register
base — `SCCM`/`SCCE`'s byte lane, per `communications-block-map.md`),
tests **individual event bits one at a time** (bit 2, then bit 0),
acknowledges each with a write-1-to-clear matching the manual's own
`SCCE` semantics exactly, and — critically — computes an index
(`control-block counter × 8`) into the **same 8-byte-stride buffer-
descriptor ring measured dynamically in Del 2**, then manipulates that
descriptor's own status bits (`bset #$7`, clearing the low byte).
Static disassembly and dynamic register measurement independently
converge on the same structure: this is genuine, standard MC68302
communications-processor buffer-descriptor handling, dispatched by
individual serial *event* bits — **character/frame-level reception,
not a generic software event queue.** `$00644C`'s `jsr
$FFF8C0E6.l` (a ROM callout not traced this round) and the two `trap
#0` calls with distinct codes (`$5`, `$6`) are the two branches'
respective "something arrived" notifications; what consumes them
downstream is not traced further here, consistent with this task's
"measure and map, don't implement" scope.

## Del 4 — The Keyboard Hypothesis

**`[Hypothesis]`, tested against available string evidence — not
confirmed.** Both OS images (V161, V350) and the analyzed ROM revision
were searched, byte-for-byte, for `TUNING KEYBOARD`, `KEYBOARD TUNED`,
and `HANDS OFF`, plain and with this project's own established 14-
segment glyph substitutions (`O`↔`0`, `S`↔`5`). Positive control: the
ROM reconstruction was verified byte-perfect first (`sha256sum` of the
freshly-interleaved lo/hi ROM halves matches `DOCUMENTATION-
MANIFEST.md`'s own recorded `asr10.bin` hash exactly), so a null result
is not an instrument failure.

**Result**: `TUNING KEYBOARD` and `KEYBOARD TUNED` — zero hits, any
form, any image. `HANDS OFF` — exactly one hit, in ROM only, at
`$F81196` (`ROM+0x01196`), as the literal fragment `"- HANDS OFF\0"`,
immediately preceded by another fragment, `"S ON PAGE\0"`. The
surrounding 300-byte window (`$F8106A-$F8128A`) reads as a **factory
service/diagnostic-menu string pool** — neighboring fragments include
`FAIL COUNT`, `ANALOG INPUTS`, `A=SAVE`, `A=SEND`, `DISK OFFSET`,
`SOCKETS`, `MOD CONTROL` — not a boot-time keyboard-calibration
message. No 32-bit absolute reference to this string (or a ±0x400-byte
neighborhood around it) was found anywhere in the reconstructed ROM,
unlike `error_message_formatter`'s own confirmed direct references to
its message bases (`$FFF824AA`/`$FFF824B1`) — so even reachability of
this specific fragment is unconfirmed, not just its identity.

**The hypothesis does not fall, but string evidence does not support
it either.** `TUNING KEYBOARD - HANDS OFF` cannot be located as
firmware text in the artifacts this project has analyzed (V1.61,
V3.50, this ROM revision) — it may belong to a different ROM/OS
revision, be assembled by a mechanism this search cannot detect
(character-by-character, matching the project's own already-
established caveat for `NO INST OR BANK FILES`), or simply not be
this firmware's message. What Del 2/3 *do* establish independently —
real, standards-shaped, event-driven serial reception configured on
two channels this project doesn't model — is exactly the kind of
capability a real keyboard-scanner link would need, but that is
capability evidence, not identity evidence. Status: `[Hypothesis]`,
unchanged, now with a documented negative string-search result
attached rather than an untested guess.

## Del 5 — What It Would Take To Model An SCC

**Scope, not code.** A minimal SCC slice sufficient to let SCC1/SCC2
fire real interrupts and process real buffer descriptors would need:

1. **Register storage**: `SCON`/`SCM`/`DSR`/`SCCE`/`SCCM`/`SCCS` per
   channel (12 bytes × 2 channels), already `known_unimplemented`
   shadow space today — the same pattern the IDMA slice already used
   for its own register block.
2. **Buffer-descriptor traversal**: read the 8-byte BD at the current
   ring index from parameter RAM, honor the status word's ready/wrap
   bits exactly as `scc_rx_common` both sets and clears them (Del 3) —
   this is the one genuinely new piece of logic, not present in any
   existing device this project reuses.
3. **An actual byte source.** This is the open engineering question,
   not a small one: real hardware must feed SCC1/SCC2 real serial
   bytes from *somewhere* on the ASR-10 board. Nothing in this task
   identifies what that is — MIDI and panel serial are already
   independently covered by the DUART (`communications-block-map.md`'s
   own standing open question, `PLAN.md` fas 2, still open). Without a
   real source, an SCC implementation can only be exercised
   synthetically (inject bytes via Lua/test hook), which would prove
   the buffer-descriptor logic works but not that it is the right
   logic for whatever the real source turns out to be.
4. **Event-bit-driven INRQ delivery**: `SCCE`/`SCCM` gate bits into the
   IMR-masked interrupt path already reconstructed in
   `interrupt-topology-gaps.md` — this ties directly into the existing
   (currently absent) interrupt-controller work, not a separate concern.

**Risk**: implementing 1-2 without 3 produces exactly the kind of
"looks plausible, never dereferenced against real behavior" gap this
project's own rules warn about (the same caution `idma-implementation-
plan.md` applied to CMR's undecoded bits). **Recommendation**: this is
the last major uninstrumented MC68302 surface, but its correctness
depends on an board-level fact (the real byte source) this project has
not established and that a future task should target *before*
writing an SCC device, not after.

## Updates

- `subroutine-index.md`: `scc_rx_common` (`$00643C`) promoted from
  `[start-V]` to a disassembled, structurally-understood entry; cross-
  referenced against Del 2's buffer-descriptor measurement.
- `interrupt-topology-gaps.md`: SCC1/SCC2 gap entry updated with this
  document's confirmation — real buffer-descriptor rings, not just a
  masked-vector curiosity.
- `current-status.md`: SCC open question updated; keyboard hypothesis
  status and the 1404/hash bookkeeping closure recorded.

## Verification

- No implementation. No SCC code, no interrupt controller.
- No `mem_map` change — the replacement rule from `e2-address-model.md`
  is recommended, not adopted; Del 1.1 is closed (this document), so
  nothing blocks adopting it, but adoption itself is not this task.
- No clock change, no bank-1 change, no ES5510 activation. Factor two
  untouched.
- No `-log`. All observation via Lua `print()` and `unidasm`.
- `static/*.csv` not hand-edited.
- No fork with an open mandate.
- `docs/asr10/regression-test.sh`: 8 tests, 9 PASS lines, run before and
  after this task's edits — unaffected, Lua/documentation-only.
- `git diff --check`: clean.
