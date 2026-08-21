# SCC's Byte Source: What The Tree Can Answer, And What Only Per Can

The firmware side is mapped: real buffer-descriptor rings, `scc_rx_common`
disassembled, both its branches traced (a ring-full error, a silent
channel shutdown). What remains is board-level: what physically drives
SCC1/SCC2's receive pins. That question cannot be answered from
firmware alone, and this document does not try to guess it from
familiarity. It inventories what documentation actually exists, does
the analysis that inventory supports, and — unplanned, but the
strongest finding of this round — pins down exactly where "`TUNING
KEYBOARD - HANDS OFF`" actually comes from, since that turned out to be
directly checkable this time.

## Del 0 — Hard Gate: What Hardware Documentation Exists?

**Status: färdigt.**

**Inventoried, not assumed.** `docs/asr10/sources/` contains
`ASR10_manual.pdf` (390 pages, "ASR-10 Musician's Manual", front-panel
operation and user procedures — not a service manual), `es5701.vhd`
(a third-party VHDL reconstruction of the ES5701 glue chip's *intended*
behavior, explicitly marked by its own README as "never simulated,
only syntax-checked... documentation of intent, not a golden model" —
chip-level, not board-level), and a `README.md` that already states
the same discipline this task asks for. `docs/ensoniq/` contains only
chip datasheets (ES5506, ES5510, ES5511, ES5701, EPS/ES5700 comparative
material) — its own `README.md` already says explicitly: *"Physical
ASR-10 board routing remains `[OPEN]` unless a board-level source
identifies the wiring."* A repo-wide search for schematic, service-
manual, netlist, PCB, or board-photo material found nothing beyond
MAME's own unrelated generic netlist engine.

**Provenance note, since it matters for what can and can't be trusted
here**: the crystal list this project has used elsewhere (`Y1`/`Y2`/`Y3`
— Raltron 93L06, Ecliptek ECX-1278, ECX-964, with specific frequencies)
came from Per **verbally**, not from any file in this tree. It is
owner-supplied information, the same evidence category as the
keyboard-calibration message below (Del 4) — real and usable, but not
independently re-derivable from anything in the repository if it
needed to be checked again. Recorded here explicitly so it is never
mistaken for a documented, in-tree source in a future round.

**Result: no schematic, service manual, board photo, parts list, or
netlist exists anywhere in this tree.** Per this task's own gate: no
electrical mapping is attempted (Del 1, skipped below). Guessing what
sits against SCC1's pins from general familiarity with similar
hardware would be exactly the failure mode this project's rules exist
to prevent — an absence of documentation is an answer, not an obstacle
to route around.

**One thing the gate does not block**: `ASR10_manual.pdf` *is* present
in the tree, and it is a procedural/textual source, not a circuit
source — reading what it says about a documented user-facing behavior
is not "deriving board wiring from familiarity." Doing so this round
produced the strongest single finding in this whole investigation
(Del 4 below).

## Del 1 — Electrical Mapping

**Status: färdigt (correctly not attempted).**

**Skipped, per Del 0's own gate.** No circuit-level source exists to
answer which components sit against SCC1/SCC2's pins, what PB9/10/11
connect to, or which on-board circuits are unmodeled. Recorded as
`[OPEN] — no source`, not guessed.

## Del 2 — Matching Against The Channel Configuration

**Status: färdigt.**

This does not require board wiring — only the already-measured channel
configuration and a generic requirements estimate, so it proceeds
despite Del 1 being blocked.

**Configuration, already measured** (`scc-hardware-gap.md`): `SCM1 =
SCM2 = $703B` when armed (`ENR=1`, `MODE=$3`), `$7033` when disarmed
(`ENR=0`, ROM default) — both channels are cycled between these two
values repeatedly during boot (see Del 4). `MRBLR = $320` (800 bytes)
per buffer, 8 buffers per ring = 6400 bytes total capacity per channel.
Baud rate: still not computed — no local manual table decodes the
MC68302 clock-source-select bits, and this round does not change that.

**Back-of-envelope keybed data-rate estimate** (generic reasoning, not
board-specific): the ASR-10 has a 61-key, velocity- and pressure-
sensing ("Poly-Key™", per the manual) keyboard. Two distinct loads are
worth separating:

- **A one-time calibration pass** (what the manual describes: scan
  every key, "optimize velocity and pressure response," ~3 seconds):
  if the keybed reports one raw baseline reading per key — say a
  16-bit value — a full pass is `61 × 2 = 122 bytes`. Comfortably
  smaller than a *single* 800-byte `MRBLR` buffer, let alone the full
  6400-byte ring. A calibration-only load would not need 8 buffers;
  one would do.
- **A continuous polyphonic pressure/velocity stream** during normal
  play: if every currently-held key reports a fresh pressure byte at,
  say, 100 Hz (a plausible aftertouch-scan rate) and all 61 keys were
  (worst case) held at once, that is `61 bytes × 100/s ≈ 6100 bytes/
  second`. The full 6400-byte, 8-buffer ring is a close match to
  roughly **one second of buffering** at that estimated worst-case
  rate — consistent with a ring sized for a continuous stream, not
  sized for the much smaller one-time calibration pass alone.

**This is an estimate, not a measurement** — no per-byte protocol
detail is known, and the true figure could differ by a large factor
depending on the real encoding. It is offered because it is cheap and
it directly supports the qualitative point already made from the ring
structure alone: **a ring this size is over-built for a single
calibration burst and is a much better fit for a continuous, possibly
polyphonic, ongoing stream** — matching a keybed link, not a status
query.

## Del 3 — Questions For Per

**Status: färdigt. Produced regardless of Del 0's outcome, as
instructed — this is what unlocks the next round, and needs no
documentation this project may lack.**

Exact, bounded, one-answer-each. Not "describe the board."

1. **Beyond `MC68302`, `SCN2681`, `µPD72069`, `WD33C93`, `ES5506`, and
   `ES5510` — what other ICs are on the main board?** Reference
   designator and part number for each, if visible (e.g. "U12,
   74LS138").
2. **Is there a separate PCB or module for the keybed** (a dedicated
   keyboard-scanner board), or does all key-scanning logic live on the
   main board?
3. **Is there a cable between the keybed assembly and the main board?**
   If so, how many conductors does its connector have?
4. **Are there any crystals or oscillators on the board besides Y1,
   Y2, and Y3?**
5. **Are there any connectors on the board not used in the stock
   configuration** — expansion headers, digital I/O, test points, or
   anything unpopulated/unlabeled in the manual?

A short answer to each — a designator, a count, a yes/no — is enough;
no board description is needed beyond that.

## Del 4 — Journal

**Status: färdigt.**

### Del 0's result, stated plainly

No board-level hardware documentation exists in this tree. This
document does not analyze circuit wiring anywhere. Del 1 is `[OPEN] —
no source`, correctly, not filled in with inference.

### The source of "TUNING KEYBOARD - HANDS OFF": owner testimony, now cross-checked

**The phrase is owner testimony from Per, not a project artifact** —
correctly categorized this round as a real, distinct evidence class,
stronger than anything recoverable from the analyzed disk images
alone. Checked against what *is* in the tree: `ASR10_manual.pdf` (the
official ASR-10 Musician's Manual) documents this exact behavior in
detail, using the abbreviation the earlier searches never tried:

> "Right after the ASR-10 is finished loading the Operating System...
> it will calibrate its keyboard... The display will briefly read
> **TUNING KBD HANDS OFF**... After you turn on the ASR-10 and insert
> the Operating System in the drive, the display will show LOADING
> SYSTEM, then **TUNING KBD - HANDS OFF**... calibration process only
> takes about three seconds... Playing keys during calibration will
> cause the display to show **KBD FAILED - RETRY?**"

Two earlier rounds searched for the wrong wording (`TUNING KEYBOARD`,
`KEYBOARD TUNED` as full words) — the manual uses **`KBD`**, not
`KEYBOARD`, for the *tuning* message.

**Version note, explicit**: this project runs **OS V3.50** (the disk
image analyzed throughout, `floppies/asr10booth/V350.img`) and ROM
`asr10.bin` (reconstructed from `asr-648c-lo-1.5b.bin`/
`asr-65e0-hi-1.5b.bin`, hash-verified). The manual's own wording does
not identify which specific OS revision it was written against, and
the message could in principle have been added, changed, or removed
between OS versions — the manual's framing (calibration as something
that happens "each time you switch it on," not flagged as version-
specific) makes that less likely, but the manual by itself does not
rule it out. This does not resolve which OS version the manual
describes versus which this project runs — but it gave the right
string to actually test.

**Redone with the correct wording, statically: still not found as
literal ASCII.** `TUNING` appears exactly once in the analyzed ROM,
alone in an unrelated `PARAMS`/`FORMAT`/`CURRENT` string pool.
`HANDS` appears once, in the already-known factory-diagnostic pool
(`FAIL COUNT`/`ANALOG INPUTS`/`SOCKETS`). `FAILED` appears three times,
in a `DISK`/`SIMM` diagnostic pool. `RETRY` appears once in the V350 OS
image, next to `REWIND`/`CHECKSUM`/`VERIFY`/`DISCONNECT PHONES+OUTS` —
an unrelated tape/SCSI dialog. `KBD` itself appears 12-28 times across
all three images, every single occurrence part of the modulation-source
name table (`PBEND PRESS PEDAL XCTRL KBD VEL KEYDN SUSTN...`) used
throughout patch data — "KBD" there means "keyboard tracking" as a mod
source, unrelated to the boot message. None of these fragments sit
adjacent to each other anywhere; each belongs to a different,
identifiable, unrelated string table.

**Redone dynamically — and this is where the round's real finding is.**
A frequent (20ms) display poll across this project's own boot run,
never done before at this resolution (every prior probe jumped straight
to waiting for `FILE 1`, skipping past whatever came before it), shows:

```
t=15.060000  "TUNING KBD - H        "
t=15.080000  "TUNING KBD - HAND5 0FF"
t=15.200000  "    KEYB0ARD TUNED    "
t=16.220000  "FILE 1  TUT0RIAL BNK  "
```

**This project's own MAME model already displays the exact message —
both halves, "TUNING KBD - HANDS OFF" and "KEYBOARD TUNED" — every
time it boots.** It was never a missing feature; it was never looked
for at the right moment. The literal-ASCII search failing is now fully
explained: this text is not stored as a contiguous string anywhere in
ROM or the OS image (confirmed above) — it is assembled through the
display protocol by a mechanism this task did not decode, exactly
candidate 2 from the task's own list, now the confirmed one rather
than a guess.

**One instruction-level correction, made because it mattered**: the
task's own wording cited undecoded display-protocol codes "`$74`/`$76`."
The actual, already-documented annunciator register range
(`panel-button-sweep-v350.md`) is **`$77-$7B`**, five registers, not
two — verified directly against that document rather than repeated
from the task text uncritically. Filed as the same category of
correction this project has now made twice in two rounds (the 1404
hash, and this): a specific number, once wrong, propagates until
someone checks it against the actual source.

**The correlation, measured precisely**: SCM1/SCM2/IMR writes tapped
(after BAR settle, `#8.5`) alongside the same display poll, in one run:

```
t=15.047606  IMR=E480, SCM1=SCM2=$703B (ENR=1 -- SCC armed)
t=15.060000  DISPLAY -> "TUNING KBD - H..."          (13ms later)
t=15.062758  SCM1=SCM2=$7033 (ENR=0 -- SCC disarmed)
  ... repeated arm/disarm cycles ...
t=15.188696  IMR=E480, SCM1=SCM2=$703B (ENR=1 -- armed again)
t=15.200000  DISPLAY -> "KEYBOARD TUNED"              (0.3ms later)
t=16.210318  IMR=E480, SCM1=SCM2=$703B (ENR=1 -- armed once more)
t=16.220000  DISPLAY -> "FILE 1  ..."                 (10ms later)

CORR_LEVEL4_TOTAL = 0  -- zero level-4 IACKs throughout
```

**Every display-phase transition in this sequence is preceded, within
single-digit-to-low-double-digit milliseconds, by an SCC1/SCC2 arm/
disarm cycle** — three independent transitions, same tight pattern
each time, in one run. This is a direct, dynamic, structural
confirmation: the keyboard-calibration UI sequence is paced by SCC1/
SCC2 being repeatedly armed and disarmed. **Zero level-4 IACKs occur
during any of it** — the sequence completes entirely by timing out on
each arm cycle, never by a real interrupt, which is exactly consistent
with everything already established (`scc-hardware-gap.md`: buffers
never fill, no real reception ever completes) and exactly the "silent
timeout" mechanism `$F8C0E6` already showed exists in the firmware.

### Keyboard hypothesis: confirmed on structure and timing, still open on byte source

**Upgraded again, precisely.** Prior status was "plausible, with real
supporting structure, no textual or byte-source confirmation." This
round adds: (a) the exact real-hardware message is confirmed, from
owner testimony, cross-checked against the official manual's own
wording; (b) this project's *own* model already reproduces both halves
of that message, every boot, previously unnoticed; (c) the message's
timing is now shown to be **directly, repeatedly gated by real SCC1/
SCC2 arm/disarm cycles**, not a coincidence of unrelated boot timing.
**Still not fully resolved**: which physical component the SCC pins
connect to remains `[OPEN] — no source` (Del 0/1), and the literal
message text is still not stored as ASCII anywhere analyzed — it is
assembled through an undecoded display mechanism. The hypothesis is
now about as strong as it can get without either the board-level fact
Del 3 asks for, or a decode of the display's own fragment-assembly
protocol — both named precisely as the next two concrete steps, not
implemented here.

## Updates

- `subroutine-index.md`: not touched this round — no new named ROM/OS
  routine was identified (the SCM/IMR write PCs were not individually
  disassembled this pass; the finding is about *timing*, not a new
  routine).
- `interrupt-topology-gaps.md`: cross-referenced with this round's
  timing correlation.
- `current-status.md`: keyboard-hypothesis status updated to reflect
  the confirmed display sequence and its SCC-timing correlation, and
  the source of the phrase recorded as owner testimony.

## Verification

- No implementation. No SCC code, no interrupt controller, no
  `mem_map` change.
- No clock change, no bank-1 change, no ES5510 activation. Factor two
  untouched.
- No `-log`. All observation via Lua `print()`, `pdftotext`, and raw
  byte search.
- No board component list, schematic, or wiring diagram was invented;
  Del 1 stands as `[OPEN] — no source`.
- No fork with an open mandate.
- `docs/asr10/regression-test.sh`: 8 tests, 9 PASS lines, run before
  and after this task's edits — unaffected, Lua/documentation-only.
- `git diff --check`: clean.
