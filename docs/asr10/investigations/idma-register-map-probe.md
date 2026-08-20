# IDMA Register Map Probe

**Status update:** `idma-implementation-plan.md` resolved the open items
below (BCR's `513→512` interpretation, CMR bit 0 as `STR`, whether vector
`$4B` is needed) and implemented a minimal IDMA slice on that basis. It
also **corrects an IMR bit-arithmetic error** made when this document was
first read against `docs/mc68302/interrupt-source-map.md`: bit 11
(`$0800`), not bit 10, is IDMA's INRQ source, and `$E480 AND $0800 = $0000`
— IDMA was (and remains, per measurement) masked, not unmasked. See that
document for the corrected reasoning and the measured result.

## Scope

`ready-line-artifact-probe.md` got the full completion chain running
correctly through RECALIBRATE, SEEK, and READ DATA, stopping at a
legitimate uPD765 overrun (`DISK ERROR - LOST DATA`) because MC68302 IDMA
is not implemented. This measures what firmware actually writes to the
IDMA register window before issuing READ DATA, to derive the register map
from real writes rather than a datasheet guess — needed before IDMA can be
implemented at all.

Method only, no code change. Board policy (`set_ready_line_connected(false)`
+ IRQ1 wiring) is now permanently landed, so this runs directly against the
current build with no C++ toggling.

## Instrument Correction

First pass (`docs/asr10/lua/archive/idma_register_probe.lua`) reused this
investigation's established `byte_address()`/`byte_value()` helpers, which
assume a single active byte lane — true for every other register measured
in this whole investigation (FDC FIFO, DUART, BAR), all genuinely
byte-granularity accesses. It is **not** true here: SAPR/DAPR are 32-bit
registers, and the writes turned out to be full word-wide (`mask=$FFFF`).
The heuristic silently picked one byte and discarded the other, corrupting
half the data (e.g. reporting a byte at `$FC6805` instead of the full word
at `$FC6804`). Caught by cross-checking against
`docs/mc68302/idma-spec.md`'s register widths before trusting the first
pass's numbers — a register map derived from corrupted reads would have
been worse than no map. Redone
(`docs/asr10/lua/archive/idma_register_probe2.lua`) logging raw
`(offset, data, mask)` with no reconstruction. All values below are from
the corrected pass.

## Measured: Two Distinct Sequences

### Shared prelude — every vector `$51` IACK, all four times

```text
FB7F12  R  $FC680E = $0000  (mask=$FF00, CSR high byte)
FB7F3C  W  $FC6802 = $0002  (CMR)
FB7F42  W  $FC6814 = $000B  (IPR — write-one-to-clear, per interrupt-source-map.md)
FB7F46  R  $FC6816 = $E480  (IMR)
FB7F4A  W  $FC6816 = $E480  (IMR rewritten unchanged)
```

This is `storage-completion-dispatch.md`'s already-documented dispatcher B
shared prelude (`F114BA`/`F114C0 jsr $FFFB7F0A`), which runs regardless of
FDC-vs-SCSI branch. `$FC6816=$E480` matches the already-independently-known
IMR value (`vector-origin-map.md`: *"baseline-media.md observes IMR
(fc6816) later in the boot at 0xe480"*) — cross-validates the offset
identification, not a new finding. `CSR` reads `$00` every time: no IDMA
event has ever actually occurred, consistent with IDMA never having run a
real transfer in this build. This sequence writes `CMR=$0002` — **not** a
channel start (see below); it is boilerplate housekeeping unrelated to any
specific transfer.

### The real IDMA programming — once before READ DATA, then four retries after the overrun

```text
t=18.311359  FB85DC  W  $FC680C = $0201        <- BCR
t=18.311360  FB85E2  W  $FC6802 = $0002        <- CMR (same housekeeping value as above)
t=18.311361  FB85E8  W  $FC6810 = $99  (mask=$FF00, byte at $FC6810)   <- FCR
t=18.311364  FB85FC  W  $FC6804 = $FFFC        <- SAPR high word
t=18.311364  FB85FC  W  $FC6806 = $5803        <- SAPR low word   => SAPR = $FFFC5803
t=18.311366  FB8604  W  $FC6808 = $0000        <- DAPR high word
t=18.311366  FB8604  W  $FC680A = $0944        <- DAPR low word   => DAPR = $00000944
t=18.311367  FB860A  W  $FC6802 = $0D51        <- CMR, second write, different value
```

Then READ DATA `46 00 00 00 08 02 08 1B FF` (`C=0,H=0,R=8,N=2`) is issued
at `t=18.311412`, ~50µs later. This exact eight-write sequence then repeats
**four more times**, identically, at `t=18.437165`, `18.437215`,
`18.437264`, `18.437314` — all *after* the overrun result phase is read
(`t=18.436975-437118`) and *without* READ DATA ever being reissued
(confirmed: no second `$46` command byte appears anywhere in the FDC FIFO
log). `[Likely]` this is firmware's own retry path for a failed transfer,
re-arming IDMA registers and re-triggering `CMR` without re-sending the FDC
command — but this is retry behavior observed in the *absence* of real IDMA
hardware ever completing anything, so it should not be read as a confirmed,
general retry protocol without corroborating evidence from a working IDMA
path.

## Derived Register Map

| Offset | Manual name (`docs/mc68302/idma-spec.md`) | Measured value | Confidence |
|---:|---|---|---|
| `$FC6802` | CMR | `$0002` (prelude/every IACK), then `$0D51` (the actual start, immediately before READ DATA) | `[Verified dynamic]` offsets and write-twice pattern; `[OPEN]` exact bit semantics of `$0D51` — CMR governs mode/request-generation/size/start/interrupt-enable per the manual but this measurement doesn't decode which bits are which |
| `$FC6804-$FC6807` | SAPR (source address, 32-bit) | `$FFFC5803` | `[Verified dynamic]`, and matches the pre-existing `[Verified static]` claim in `architecture-handoff.md` exactly |
| `$FC6808-$FC680B` | DAPR (destination address, 32-bit) | `$00000944` | `[Verified dynamic]` — **corrects** `architecture-handoff.md`'s prior `[Verified static]` claim of `$040E`; that was a static guess, this is a measurement, measurement wins per instruction. See "Disposition" below. |
| `$FC680C-$FC680D` | BCR (byte count, 16-bit) | `$0201` (513 decimal) | `[Verified dynamic]` for the raw value; `[OPEN]` for interpretation — see below |
| `$FC680E` | CSR (channel status, 8-bit) | `$00` (every read) | `[Verified dynamic]` — never anything but zero; IDMA has never actually run |
| `$FC6810` | FCR (function code, 8-bit) | `$99` | `[Verified dynamic]` for the raw byte; `[OPEN]` for bit-field decode |

### BCR interpretation — flagged, not resolved

`$0201` does not cleanly match "one 512-byte sector" (`$0200`) or "N=2
sector-size code alone" (`$02`). Two candidate readings, neither confirmed:

1. Plain 16-bit byte count = 513. One byte more than a full 512-byte
   sector; could be a count-inclusive/count-exclusive fencepost
   convention this measurement doesn't resolve.
2. Split 8+8 fields: high byte `$02` (matches READ DATA's `N=2`
   sector-size code, or a "2 units" multiplier) and low byte `$01` a
   separate mode/enable flag packed into the same word, not part of a
   numeric count at all.

Not adjudicated here. Implementation must not silently pick one without
re-deriving from a second, independent measurement (a different sector
count/`N` value would disambiguate immediately: reading (1) predicts BCR
scales with byte count, reading (2) predicts the high byte tracks `N`
directly with a fixed low byte).

### SAPR: source is not the FDC's CPU-side FIFO port

`SAPR=$FFFC5803` (24-bit masked: `$FC5803`) is **not** `$FC4003` (the FDC
FIFO register the CPU itself uses for polled transfers throughout this
whole investigation). `$FC5803` currently falls inside this driver's
generic `.ram()` fallback (`map(0xfc5020, 0xffffff).ram();`) — real IDMA
hardware must route reads from that specific address to the FDC's data
register through some path other than the ordinary CPU-visible FIFO port.
`[OPEN]`: what that path is. Confirmed only that firmware programs this
specific address as source; not confirmed what real hardware wires there.

## What This Doesn't Answer

**DRQ edge count and time-to-first-DRQ: not measurable with available
instruments.** `upd765_family_device::drq_wr_callback()` is a devcb signal
with no memory-mapped representation and no exported Lua device-state
entry (`m68000_device`'s pattern of registering CPU registers via
`state_add()` has no equivalent in `upd765_family_device`); `get_drq()` is
a public C++ method but not one MAME's Lua bindings expose generically.
`manager.machine.debugger` (which would allow `bpset`/`wpset`) requires
`-debug`, out of scope for headless investigation per project convention.
Reported as a blocker, not estimated — no DRQ-derived number in this
document should be trusted if one appears to exist; none does.

## Disposition

`architecture-handoff.md`'s pre-existing `[Verified static]` claim (SAPR
`$FFFC5803`, DAPR `$040E`) is corrected: SAPR matches this measurement
exactly; DAPR does not (`$0944` measured). Per instruction, measurement
wins the conflict — the DAPR value in `architecture-handoff.md` is updated
to point here.

## Verification

- No C++ change. No `mem_map` change. No `-log`.
- `docs/asr10/regression-test.sh`: 5/5 (unaffected — Lua-only).
- `git diff --check`: clean.

## Line Count

- `docs/asr10/lua/archive/idma_register_probe.lua`,
  `idma_register_probe2.lua`: written, run, archived. The first pass's
  instrument bug is documented rather than deleted quietly, per the
  standing rule that a wrong measurement is itself a finding.
