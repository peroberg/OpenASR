# VFX / VFX-SD / SD-1 as a firmware reference for the ASR-10 FC3000 handshake

Scope: read-only investigation. No emulation behavior was changed, no
source files were modified, nothing was committed. All MAME runs used
an external `-autoboot_script` Lua tracer (scratch file, not part of
the repo) that only observes (`install_read_tap`/`install_write_tap`,
returning `nil`); `asr10_boot.cpp` was not touched and no ES5510 device
was instantiated anywhere.

## 1. Driver / ROM / mapping identification [confirmed]

VFX, VFX-SD, SD-1 (21-voice) and SD-1 (32-voice) are all one driver,
`src/mame/ensoniq/esq5505.cpp` (`esq5505_state`), differentiated only by
`machine_config` variant and ROM set. ROMs verified locally:
`./mame -verifyroms vfxsd` / `-verifyroms sd1` both report "is good".

ES5510 wiring, current mainline MAME (`src/mame/ensoniq/esq5505.cpp`):
```
577:  map(0x260000, 0x2601ff).rw(m_esp, FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);   // vfx_map
588:  map(0x260000, 0x2601ff).rw(m_esp, FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);   // vfxsd_map
612:  map(0x260000, 0x2601ff).rw(m_esp, FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);   // sq1_map
766, 893: ES5510(config, m_esp, 10_MHz_XTAL); m_esp->set_disable();
```
`.umask16(0x00ff)` is the same odd-byte-lane convention already
established for ASR-10's FC2001/FC3000 clusters. `set_disable()` is
the generic `device_execute_interface` flag that stops the ES5510's
*internal DSP program* from executing; it does not gate `host_r`/
`host_w`, which are separate MMIO callbacks — so even in these
"disabled" configs, the host-visible GPR/INSTR/DIL/DOL/control
register file is fully live. `src/mame/ensoniq/esqkt.cpp` (already
cited in the ASR-10 investigation, lines 173/185) wires the same
device in the same way. `src/mame/ensoniq/esqasr.cpp` (the ASR-10
*skeleton* driver, distinct from the actively-investigated
`asr10_boot.cpp`) instantiates `ES5510` but never maps it into any
address space (`optional_device`, no `.rw()` call anywhere in the
file) — confirming the prior investigation's finding that no MAME
driver currently exercises a real ES5510 host interface for any ASR-10
firmware.

## 2. Method [confirmed]

Static grep across `esq5505.cpp` located the mapping and BIOS ROM
names. The actual download routine was found by combining:
- a live, external, read-only Lua tracer installed via
  `-autoboot_script`, tapping `0x260000-0x2601ff` and logging
  `pc`/`offset`/`data`/`mem_mask` (and, in one variant, `D0-D3`/`A0-A1`)
  to a text file — run as:
  ```
  ES5510_TRACE_OUT=/tmp/es5510_trace.log SDL_VIDEODRIVER=dummy \
    ./mame vfxsd -rompath roms -autoboot_script <tracer.lua> \
    -bench 20 -skip_gameinfo -log
  ```
- `unidasm -arch m68000 -basepc 0xc00000 <interleaved-osrom.bin>` on
  the VFX-SD v2.00 BIOS (`vfxsd_200_upper.bin`/`vfxsd_200_lower.bin`,
  interleaved even/odd per `ROM_REGION16_BE`+`ROM_SKIP(1)`) and on the
  SD-1 21-voice BIOS (`sd1_21_300b_upper.bin`/`_lower.bin`), to confirm
  the same routine (down to the `$260001` literal) exists in both ROMs.
- cross-reference against `src/devices/cpu/es5510/es5510.cpp`
  `host_r`/`host_w` (~lines 350-520) for the real register map: offsets
  0-2 GPR latch, 3-8 INSTR latch (6 bytes), 9-11 DIL latch
  (**read-only**, byte 0 hardcoded 0), 0x12 host_control (read always
  0), 0x16 hardcoded 0x27, 0x80 read-select GPR+INSTR, 0xa0 write-select
  GPR, 0xc0 write-select INSTR, 0xe0 write-select GPR+INSTR.

Caveat: the tracer script reliably triggers a SIGSEGV in `vfxsd` after
roughly 150-2250 tapped accesses (count varies run to run); a plain run
with no script boots and runs cleanly for 20+ real seconds. This looks
like Lua-tap overhead exposing an unrelated timing sensitivity, not
something this investigation changed or needs to fix — the script was
never part of any driver source and was discarded. The data quoted
below comes from the captured portion before each crash.

## 3. Structural comparison: VFX-SD/SD-1 vs ASR-10 [confirmed]

VFX-SD v2.00, `c0a09e-c0a118` (record-type dispatch + bulk write loop)
is structurally identical — same register roles, same branch shape,
same literal constants, same address-derivation quirk — to ASR-10
`f97412-f9748e`, differing only in the device base address. SD-1
21-voice ROM contains the same code at `c0aa60-c0aa9c` (confirmed via
`unidasm` on the interleaved SD-1 BIOS), with the identical `$260001`
literal — this is shared OS code across the whole VFX/VFX-SD/SD-1
family, not something specific to one model.

### Compact routine map

| Structural role | ASR-10 address | VFX-SD address | SD-1 address | ASR host base | VFX/SD host base | MAME handler |
|---|---|---|---|---|---|---|
| Record-type dispatch (A4/A5/D4) | `f97450-f9748e` | `c0a0dc-c0a118` | `c0aa60-c0aa9c` | `$fffc3001` | `$260001` | ASR: plain `.ram()` (`asr10_boot.cpp`); VFX/SD: `es5510_device::host_r/host_w` (`esq5505.cpp:588`) |
| Per-byte GPR/INSTR write loop | `f97432-f97438` | `c0a0be-c0a0c4` | (same, inline in dispatch range above) | `$fffc3001` | `$260001` | same as above |
| Commit-write-with-retry (write/read-select strobe) | `f97776-f977ae` | `c0a306-c0a33a` | `c0ac8a-c0acbe` | `$fffc3001` | `$260001` | same as above |
| Bulk GPR readback via MOVEP | `f97718-f97728` | `c0a2aa-c0a2ba` | `c0ac2e-c0ac40` | `$fffc3001` | `$260001` | same as above |
| Bulk INSTR readback via MOVEP | `f9772a-f97742` | `c0a2c0-c0a2da` | `c0ac44-c0ac5e` | `$fffc3001` | `$260001` | same as above |
| Exhaustive per-byte re-verify pass (whole table, `cmp.b (A6),D2`) | `f97496-f97596` | **not found** (§4) | not searched | `$fffc3001` | — | ASR: plain `.ram()`; VFX/SD: n/a |

The last row is the one asymmetry: everything else in this table is a
one-to-one structural (and largely byte-identical) match across all
three ROMs; the exhaustive bulk-table re-walk-and-compare has no
located VFX-SD/SD-1 twin (see §4).

### Representative excerpt (record-type dispatch entry, raw words)

```
ASR-10   f9745c: 287c fffc 3001   movea.l #$fffc3001,A4
         f97462: 4bec 0010        lea     ($10,A4),A5
         f97466: 383c 01c0        move.w  #$1c0,D4
         f9746a: b63c 0002        cmp.b   #$2,D3
         f9746e: 6608             bne     $f97478

VFX-SD   c0a0e8: 287c 0026 0001   movea.l #$260001,A4
         c0a0ee: 4bec 0010        lea     ($10,A4),A5
         c0a0f2: 383c 01c0        move.w  #$1c0,D4
         c0a0f6: b63c 0002        cmp.b   #$2,D3
         c0a0fa: 6608             bne     $c0a104

SD-1     c0aa6c: 287c 0026 0001   movea.l #$260001,A4
         c0aa72: 4bec 0010        lea     ($10,A4),A5
         c0aa76: 383c 01c0        move.w  #$1c0,D4
         c0aa7a: b63c 0002        cmp.b   #$2,D3
         c0aa7e: 6608             bne     $c0aa88
```
Every word is identical across all three ROMs except the four
immediate bytes of the base-address literal (`fffc3001` vs `260001`).

### Side-by-side: record-type dispatch (A4/A5/D4 derivation)

| ASR-10 (`f97450-f9748e`) | VFX-SD (`c0a0dc-c0a118`) |
|---|---|
| `cmp.b #$1,D3` / `bcs [fail]` | `cmp.b #$1,D3` / `bcs [fail]` |
| `cmp.b #$4,D3` / `bhi [fail]` | `cmp.b #$4,D3` / `bhi [fail]` |
| `movea.l #$fffc3001,A4` | `movea.l #$260001,A4` |
| `lea ($10,A4),A5` | `lea ($10,A4),A5` |
| `move.w #$1c0,D4` | `move.w #$1c0,D4` |
| `cmp.b #$2,D3` / `bne [skip]` | `cmp.b #$2,D3` / `bne [skip]` |
| `lea ($6,A4),A4` | `lea ($6,A4),A4` |
| `move.w #$180,D4` | `move.w #$180,D4` |
| `cmp.b #$3,D3` / `beq [go]` | `cmp.b #$3,D3` / `beq [go]` |
| `cmp.b #$4,D3` / `bne [skip]` | `cmp.b #$4,D3` / `bne [skip]` |
| `lea ($4,A4),A5` | `lea ($4,A4),A5` |
| `move.w #$140,D4` | `move.w #$140,D4` |

Note `A5` is computed **before** the type-2 shift to `A4` in both ROMs
(`lea ($10,A4),A5` happens while `A4` is still the unshifted base) —
the same quirk this investigation used to retract the ASR-10 "DIL
overrun" hypothesis is present, byte-for-byte, in VFX-SD/SD-1 too.

### Side-by-side: per-byte write loop

| ASR-10 `f97432-f97438` | VFX-SD `c0a0be-c0a0c4` |
|---|---|
| `move.b (A3)+,(A6)` | `move.b (A3)+,(A6)` |
| `addq.l #2,A6` | `addq.l #2,A6` |
| `cmpa.l A5,A6` | `cmpa.l A5,A6` |
| `ble [loop]` | `ble [loop]` |

### Side-by-side: commit-write-with-retry subroutine

| ASR-10 `f97776-f977ae` | VFX-SD `c0a306-c0a33a` |
|---|---|
| `movea.l #$fffc3001,A0` | `bsr $c0a2fe` → `movea.l #$260001,A0` |
| `moveq #1,D0` / `swap D0` | `moveq #1,D0` / `swap D0` |
| `move.w #$32,D0` (retry count) | `move.w #$14,D0` (retry count) |
| `tst.b $0e8b.w` (busy flag) | `tst.b $9625.w` (busy flag) |
| `cmpi.b #$28,($2c,A0)` / `bcc [loop]` | `cmpi.b #$28,($2c,A0)` / `bcc [loop]` |
| `move.b D1,(A0,D4.w)` (commit) | `move.b D1,(A0,D4.w)` (commit) |
| `btst #2,($24,A0)` / `dbeq D0,[loop]` | `btst #2,($24,A0)` / `dbeq D0,[loop]` |
| `move.b #$21,D0` / `trap #0` (exhausted) | `move.b #$21,D0` / `trap #0` (exhausted) |

Only the retry-count constant (`0x32`=50 vs `0x14`=20) and the
per-platform busy-flag lowmem address differ; the error code on
exhaustion (`0x21`) is identical.

### Side-by-side: bulk GPR/INSTR readback via MOVEP

| ASR-10 `f97718-f97742` | VFX-SD `c0a2aa-c0a2da` |
|---|---|
| disable IRQ (`bsr $f976ec`) | disable IRQ (`bsr $c0a294`) |
| `move.w #$100,D4` (read-select, offset 0x80) | `move.w #$100,D4` |
| `bsr [commit-with-retry]` | `bsr $c0a306` |
| `movep.l ($0,A0),D2` (GPR) / `movep.l ($6,A0),D2`+`movep.w ($e,A0),D2` (INSTR) | same offsets, same instructions |
| restore IRQ | restore IRQ |

ASR-10's own verify pass (`f974fc` onward) also issues this same
read-select trigger (`D4=0x100`, `bsr $f97776`) at `f974ea-f974ee`
before its per-byte compare loop — this was not previously called out
explicitly in `evidence-tree.md`'s Task 2 writeup; it does not change
that entry's "case A" conclusion, but the full handshake is one step
more elaborate than "read the same address back": write → write-select
commit → **read-select** → read/compare.

## 4. The one clear structural difference found [strong inference, not fully closed]

ASR-10's record interpreter inlines an **exhaustive second pass**
(`f97496-f97596`) that re-walks every record of the *same* table and
does a per-byte `cmp.b (A6),D2` directly against the device, retrying
up to 10 times per record and propagating failure to the outer
`ffc884` retry loop (→ ERROR 032).

In the VFX-SD v2.00 disassembly, the bulk table-driven loader
(`c0a09e-c0a118`, the twin of ASR-10's write pass) is **write-only** —
no second re-walk-and-compare pass over the just-downloaded table was
found following it. VFX-SD does implement the same conceptual
"write → commit → read-select → readback → compare → retry ≤10 →
report error `0x21`" pattern (`c0a22c-c0a26c`), but as a **generic,
reusable single-GPR-write utility**, not as an inline re-verification
of the bulk table. That utility is called 129 times in a priming sweep
(`c0a082-c0a08c`, `D1=0..0x80`, `D2=0`) that runs *before* the bulk
table loader on every "load effect" invocation (the only caller found
of the bulk loader's entry, `c0a066`, is `c0a408`), and is also called
from several unrelated addresses elsewhere in the ROM
(`c0a948`, `c0aa2a`, `c0aad0`, `c0ab4a`, `c0ae50`, `c0ceb0`).

This is reported as strong inference, not proven, because callers of
the bulk loader and of the verify utility were not exhaustively traced
across the whole ROM — a second, separately-invoked re-walk of the
downloaded table (elsewhere in the ~256KB image) cannot be ruled out
with the time spent here.

## 5. Live confirmation against the real `es5510_device` [confirmed, bounded]

The Lua tracer captured several hundred to ~2250 real write/read-select/
readback events during VFX-SD's actual boot, against the ES5510 wiring
already in mainline MAME (`esq5505.cpp:588`). Within the captured
window, the exact addresses hit (`260000/2/4/6` = GPR+INSTR[5], `260024`
= host_control always-0, `260100`/`260140`/`2601c0` = read-select/
write-select-GPR/write-select-GPR+INSTR) match the real device's
register map precisely, and no retries or the `0x21` error trap were
observed — i.e., **today, in mainline MAME, this write/commit/
read-select/readback protocol round-trips correctly against a real
`es5510_device`.** This doesn't prove ASR-10's specific type-2
INSTR-latch handshake would pass unmodified (that table/type wasn't
exercised in the captured VFX-SD window), but it is direct, live
evidence that the underlying host-latch primitives ASR-10's identical
code depends on are faithfully modeled by the existing device, not
just plausible from source-reading.

## 6. Unresolved questions

- Whether VFX-SD's bulk table loader is followed by a separate,
  not-yet-located re-verify pass elsewhere in the ROM (§4).
- Why VFX-SD's dispatch subroutine returns via `bra $c04b32`/`$c04b2c`
  at its two exit points (`c0a118`/`c0a11c`) instead of ASR-10's
  `cmp.w D0,D0`/`rts` — not chased further; likely an equivalent
  carry-clearing tail-call, not confirmed.
- Root cause of the Lua-tracer-induced crash (§2) — not diagnosed, and
  not needed for this comparison since it doesn't depend on runs past
  the crash point.
- Whether ASR-10's own type-2 handshake, if run against a correctly
  wired `es5510_device` (offset mapping `fc3001 + 2×device_offset`),
  would actually pass — not tested; doing so would require
  instantiating ES5510 in `asr10_boot.cpp`, explicitly out of scope for
  this task.

## Conclusion

VFX/VFX-SD/SD-1 firmware contains a routine that is structurally
identical — same register roles, same branch shape, same literal
constants, same address-derivation quirks, shared across all three
models' ROMs — to ASR-10's `f973f0-f9748e` write pass and its
`f97776`/readback support routines, differing only in base address and
two platform-specific constants. It is mapped, in current mainline
MAME, onto a real `es5510_device`, and that mapping is live-confirmed
(not just source-inferred) to correctly round-trip GPR/INSTR writes
through commit and read-select today.

**Strengthened conclusion**: the ASR-10 FC3000 routine is now
confirmed, at the firmware level, as a reused VFX/SD-family ES5510
host-download implementation — not merely an analogous or plausibly
similar one. The dispatch logic, byte-write loop, commit/retry
strobe, and MOVEP-based bulk readback are the same code, evidenced
independently across VFX-SD and SD-1 ROMs, differing only in the host
base address and two per-platform constants (§3's compact routine
map). This is firmware-level confirmation of the routine's *origin
and intended target*, not confirmation that ASR-10's *physical* host
interface is wired identically to VFX-SD/SD-1's direct
`es5510_device` mapping — the exact ASR-10 physical interface may
still involve ES5701 (or another glue/adapter) semantics between the
CPU bus and the ES5510 itself, as already flagged as an open,
unresolved classification in `evidence-tree.md`'s "ES5701 wiring
study" reference. This report narrows *which* firmware routine and
*which* chip's host protocol are involved; it does not settle *how*
ASR-10's own bus reaches that chip.

It is a weaker reference for the *specific* write-then-verify retry
structure at ASR-10 `f97496-f97596`, since the closest VFX-SD analogue
found is a generic utility rather than an inlined bulk-table re-walk —
so it does not, by itself, resolve why ASR-10's plain-RAM model fails
that specific compare; it only strengthens the case that a real ES5510
(possibly behind an ASR-specific adapter) is the right thing being
modeled, not that the fix is a trivial device swap.
