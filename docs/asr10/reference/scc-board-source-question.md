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

**Update, board-source recalculation round**: the gate's own result —
"no service manual" — is now superseded, not by anything guessed, but
by a real external source found and downloaded at a URL supplied
directly: the ENSONIQ ASR Service Manual
(`docs/asr10/sources/ASR10_service_manual.pdf`), plus two keyboard
coil-board schematics (`ASR10_upper_coil_board_schematic.pdf`,
`ASR10_lower_coil_board_schematic.pdf`). **This still does not cover
the digital board itself** — no schematic for the board carrying the
MC68302 was found anywhere searched. The gate therefore still holds
for Del 1's original question (what sits against the MC68302's own
pins on the digital board) — see "Recalculation" below for what the
newly-found sources *do* answer, and Per's shortened question list for
what remains genuinely undocumented.

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

**Shortened this round** — questions 2 and 3 from the original list are
now answered by the service manual and coil-board schematics
(separate 80C52-based keybed scanner board, 20-pin ribbon cable at
connector J7) and are dropped. Three remain, all about the *digital
board* specifically, which no located source documents:

1. **Beyond `MC68302`, `SCN2681`, `µPD72069`, `WD33C93`, `ES5506`, and
   `ES5510` — what other ICs are on the digital board?** Reference
   designator and part number for each, if visible (e.g. "U12,
   74LS138").
2. **Are there any crystals or oscillators on the digital board
   besides Y1, Y2, and Y3?**
3. **Are there any connectors on the digital board not used in the
   stock configuration** — expansion headers, digital I/O, test
   points, or anything unpopulated/unlabeled in the manual?

A short answer to each — a designator, a count, a yes/no — is enough;
no board description is needed beyond that. (Dropped, answered by
`ASR10_service_manual.pdf` this round: is there a separate keybed
board — yes, the coil boards, with their own 80C52 scanner MCU; is
there a cable — yes, the 20-pin keyboard ribbon cable, connector J7 on
the digital board.)

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

## Recalculation: The Byte Source Is Found

**`[External]` evidence — stronger than family precedent, weaker than
this project's own measurement, and never a substitute for one.** The
real ENSONIQ ASR service manual was located and downloaded this round
(`docs/asr10/sources/ASR10_service_manual.pdf`, from the exact URL
supplied), verified by direct `pdftotext` extraction, not assumed from
a paraphrase. Its own words, quoted exactly, page 9 of the printed
manual:

> "The digital board communicates with the keyboard over a two-line
> asynchronous interface carried by the 20-pin keyboard ribbon cable.
> The keyboard communicates with the keypad/display board over a
> three-line synchronous interface that is carried over to the digital
> board via the 20-pin ribbon cable, then up to the keypad/display
> board via the 24-pin ribbon cable."

**This describes two separate links, not one** — a fact this task's
own framing didn't have yet: a 2-line **asynchronous** digital-board↔
keyboard link, and a 3-line **synchronous** keyboard↔keypad/display
link (routed as a pass-through through the keyboard assembly, then
onward via a second, 24-pin cable). Both are carried, at the digital-
board end, over the same 20-pin ribbon cable at connector **J7**.

**Independent schematic confirmation, also downloaded this round**
(`docs/asr10/sources/ASR10_upper_coil_board_schematic.pdf`, R. Grieb/
Tauntek reconstruction, explicitly marked by its own author "may
contain errors"): the keyboard's **upper coil board** carries its own
microcontroller, **U3 = an 80C52** (an Intel/compatible 8051-family
MCU with a real, standard on-chip UART), with its `SERIN`/`SEROUT`
pins — the 8052's own hardware UART lines — buffered through `7407`
open-collector drivers and wired directly to the 20-pin flex-cable
connector (`J1` on that board). **This is the physical device on the
other end of the wire the service manual describes**: a real MCU with
a real asynchronous serial port, not a guess from familiarity. The
lower coil board carries no MCU — only the drive/sense oscillator
network for the key-position induction sensors, confirming the scanning
intelligence lives entirely on the upper board's 80C52.

### Del 1 — Recalculated: Asynchronous, Not Transparent

**Bit positions: cross-validated externally, unchanged.** A targeted
search for the actual MC68302 User's Manual (NXP-hosted PDF 404s;
`manualslib.com`'s indexed copy of the same manual was reachable
instead) confirms this project's own pre-existing SCM bit map exactly:
`ENR`=bit 3, `ENT`=bit 2, `DIAG1`/`DIAG0`=bits 5-4, `MODE1`/`MODE0`=
bits 1-0 — the same positions `runtime-service-model.md` already used,
now independently confirmed against the manual itself (UM §4.5.3,
matching the section number that document already cited) rather than
carried forward untested.

**New, and this changes the picture**: the same source states plainly
that **Transparent mode applies only to SCC2 and SCC3, not SCC1** — and
separately, that `MODE1-MODE0 = 10` (binary) selects the **synchronous**
DDCMP protocol. Both facts were previously unknown to this project.
Since SCC1 (this project's own measurement: `SCM1 = $703B` when armed)
cannot legally run in Transparent mode, **the prior "MODE=3 = BISYNC/
Transparent" label was wrong for SCC1 at minimum** — `runtime-service-
model.md`'s own hedge between the two names is resolved by elimination
in the wrong direction: neither name it offered is the necessarily
correct one for this channel.

**Still not resolved**: which exact 2-bit value corresponds to UART
specifically. Multiple targeted fetches (searching for the general
MODE1-MODE0 protocol-select table, the section that would list all
four values against HDLC/UART/BISYNC or DDCMP/Transparent together)
did not surface the complete table — every fetch found an adjacent
section (the UART-specific register fields, the DDCMP timing figure,
the diagnostic-mode bits) but not the one table naming all four values
at once. **Blocked, not guessed around**: this project does not assert
which number means UART without seeing that table.

**What the external evidence does support, without needing that
number**: the physical device on the other end (an 8052 with a
standard hardware UART) can only speak asynchronous, character-framed
serial — it has no synchronous/bit-clock mode to offer. Whatever MC68302
mode value SCC1 (or SCC2) is actually configured with, **it must be
whichever one is UART-compatible**, because the counterpart hardware
requires it. This is inference from a real, external, physical
constraint — not a guess from familiarity — but it is still inference,
not a confirmed register decode, and is reported as such.

**Character format**: not resolved beyond what the raw bits already
show, decoded this round against the UART-mode-specific field layout
(`manualslib.com`'s indexed copy, same section): `TPM1`/`TPM0` (bits
15-14) = transmit parity mode, `RPM` (bit 13) = receive parity mode,
`PEN` (bit 12) = parity enable, `UM1`/`UM0` (bits 11-10) = UART
addressing mode (multidrop vs. normal), `FRZ`/`CL`/`RSTM`/`SL` (bits
9-6) = freeze/character-length/reset-mode/stop-length, each
individually plausible-looking but not independently confirmed against
prose descriptions this task could retrieve (the fetched excerpts
labeled the bit positions without the accompanying value tables for
several of them). Applying this layout to the measured `$703B`: `PEN=1`
(parity **enabled**), `RPM=1`/`TPM=01` (asymmetric parity selection —
unusual, not resolved), `UM=00` (non-multidrop, the simplest point-to-
point addressing — consistent with a single dedicated keybed link),
`DIAG=$3` (loopback/echo — same value already known, meaning unchanged
by this round). **Reported as a partial, hedged decode, not a settled
character format.**

**Baud rate: still not computed.** No source found this round decodes
the MC68302's own clock-source-select bits (needed to know what divides
into the baud generator) — the same gap flagged last round, unchanged.
The coil-board schematic adds context but not the missing number: the
80C52 runs its own local 16.0 MHz crystal (independent of the ASR-10
main board's Y1/Y2/Y3) and has a standard 8052 UART capable of a wide
range of baud rates depending on its own firmware's timer-1 reload
value — which this project has no access to (the 80C52's own firmware
is not part of any artifact analyzed here). **The channel-B method that
solved 62,500 baud for the panel link does not transfer directly**:
that derivation used the SCN2681 DUART's own documented IPR-to-baud
divisor table, a resource this task does not have an MC68302 SCC
equivalent for. Left `[OPEN]`, not estimated from an unfounded formula.

**Keybed data-rate comparison, restated against the corrected
picture**: Del 2 of the prior round already estimated 122 bytes for a
one-time 61-key calibration pass versus ~6,100 bytes/second for a
worst-case continuous polyphonic pressure stream, against the ring's
6,400-byte total capacity. The schematic adds a concrete key count: 64
physical coil-sense channels are wired (61 real keys plus at least one
labeled "Dummy/Ref Coil #62" for calibration baseline), matching the
same order of magnitude used in that estimate. Unchanged conclusion:
the ring is sized for a continuous stream, not a single burst.

### Del 2 — Which SCC Is The Keyboard, And What Is The Other?

**Measured, directly: SCC1 and SCC2 are configured identically in
every respect this project has checked** — same `SCM` value when armed
(`$703B`) and disarmed (`$7033`) at every checkpoint, same `MRBLR`
(`$320`), same ring size (8 descriptors), same wrap-bit pattern (last
descriptor `$F000`). The only differences are addressing: different
parameter-RAM base (`$FC6400` vs `$FC6500`), different register base
(`$FC6880` vs `$FC6890`), different buffer pool (`$00F766xx` vs
`$00F74Bxx`). **New this round**: the two channels are armed and
disarmed **in lockstep**, within microseconds of each other, at every
observed transition (`scc-board-source-question.md`'s earlier
correlation trace) — consistent with one shared piece of init code
arming both together, not with two independently-managed links each
reacting to its own traffic.

**This is an honest non-distinction, not a resolved one.** Nothing in
this project's own dynamic measurement tells SCC1 and SCC2 apart by
configuration. The service manual's two-link description (async
keyboard, sync keypad/display) gives a strong *motivated* candidate for
what the second channel is — **not an option card, not SP-3, not
DI-10, but the keypad/display board's own synchronous link**, since
that is the only *other* serial path the manual documents running
through the same 20-pin cable and the same MC68302. This fits the
physical picture far better than the option-card candidates named in
the original question list — SP-3 (SCSI) and DI-10 (digital audio I/O)
both have their own well-documented, already-modeled interfaces
elsewhere (`WD33C93`, and the audio path itself) and no service-manual
mention of running through an SCC. **Still not proven**: the dynamic
lockstep-arming behavior does not, by itself, distinguish "two
different links, armed together by shared code" from "one link,
managed redundantly by two channels" — both remain consistent with
what was actually measured. `[Likely]`, not `[Verified]`: SCC1/SCC2 are
the keyboard-async and keypad/display-sync links respectively (or in
some order), on the strength of the manual's own two-link description
matching two configured channels — but this project's own
configuration snapshot cannot currently tell the two apart, since it
never observed them differing.

### Del 3 — Error Codes, Corrected

**A real error in the prior round's own analysis, caught by checking
against the actual manual rather than repeating the earlier reading.**
The prior write-up treated `scc_rx_common`'s `ori.b #$28,D0` as
constructing the error code passed to `trap #0` (giving `$2D`/`$2E`,
45/46 decimal). Re-reading the disassembly transcript already on
record: **`ori.b #$28,D0` executes *after* `trap #0`**, not before —
`D0` is `$5`/`$6` (5/6 decimal) at the moment of the trap itself,
matching this project's own already-documented `trap #0` convention
(`raise_error_145`: `moveq #$91,D0 / trap #0`, no OR step). The earlier
reading was simply wrong about which instruction ran first.

**Checked against the real service manual's own error-code list**
(quoted directly, `pdftotext`-extracted, not paraphrased): codes **045**
and **046** do not appear anywhere in it — not in the software-error
list, not in the digital-board list, not in the disk-operation list.
Codes **005** and **006** do, both reading, verbatim: **"could not
synchronize audio input."** This is the real match. `scc_rx_common`'s
ring-full path raises exactly these two codes.

**What this does and doesn't confirm**: "could not synchronize audio
input" is a plausible, if imperfect, description for a serial-
reception ring that never gets drained — the same mechanism this
project already established handles character/frame-level reception
generically, not audio-specifically. Whether codes 5 and 6 distinguish
SCC1 from SCC2 (one code per channel) or two different sub-conditions
within a single channel's handling is **not resolved by the manual** —
both entries carry the identical description, so the manual cannot
settle which reading is right. Left `[OPEN]`.

**`ERROR 032` cross-check, as asked**: the manual's own list reads
**"032 bad download"** under "Digital Board Problems" — consistent
with this project's own already-documented `ERROR 032` = "EFFECT
DOWNLOAD FAILED" (an ESP/effect download failure). **Bonus
cross-validation, unprompted**: the same list entry **"145 unknown
DUART interrupt error"** matches this project's own long-standing
`raise_error_145` (`$F884F8`, `subroutine-index.md`) exactly, by name
as well as by code — independent confirmation of a fact this project
already had, found while checking a different one.

### Del 4 — Scope, Now That The Byte Source Is Named

The prior round's flagged risk — "no byte source is identified, modeling
would mean guessing one to exercise the logic synthetically" — is
resolved in principle, not in the tree: the byte source is the keybed's
own 80C52 UART, over the 20-pin ribbon cable, into whichever SCC channel
turns out to be the keyboard link (`[Likely]`, per Del 2, not fully
distinguished from its sibling channel).

- **What a minimal model needs to do, unchanged in kind, sharper in
  target**: register storage for `SCON`/`SCM`/`DSR`/`SCCE`/`SCCM`, and
  buffer-descriptor traversal honoring the ready/wrap bits exactly as
  `scc_rx_common` already both tests and sets them (`scc-hardware-
  gap.md` Del 3) — nothing about this changes now that the source is
  named; the logic to drive was already fully mapped.
- **What it would need to send**: still the next unknown. The 80C52's
  own firmware (what bytes it actually transmits, in what framing, at
  what rate, on what trigger) is not part of any artifact this project
  has — the coil-board schematic shows the wiring, not the program.
  This is a new, precisely-named gap, not the same one restated: it is
  a *firmware* question about a *different* processor, not a board-
  wiring question this project can answer from schematics alone.
- **A silent-but-present link**: already answered by the existing
  measurement (`scc-hardware-gap.md`), not something this round needed
  to re-derive. `$F8C0E6` disables both channels with no error and no
  display text on one specific event condition; the ring-full path
  (Del 3 above) raises a real, recognizable error ("could not
  synchronize audio input") on a different condition. A model that
  simply never asserts a ready descriptor would exercise neither path
  by default — matching this project's own repeated, measured
  observation that the calibration sequence currently completes
  silently, by timing out, every boot.
- **A good first milestone, named precisely**: a model that responds
  to being armed (`ENR=1`) by doing nothing at all — never asserting a
  filled descriptor, never raising `SCCE` — would reproduce *exactly*
  the currently-observed, already-measured behavior (arm/disarm cycles,
  zero level-4 IACKs, "TUNING KBD - HANDS OFF" → "KEYBOARD TUNED" →
  `FILE 1`, unchanged) with an actual register/descriptor
  implementation underneath instead of the current total absence of
  one. This would not add new observable behavior — it would replace
  "no SCC exists" with "an SCC exists and correctly does nothing,"
  which is a real, checkable step (a regression guard could assert
  the descriptor ring is honored correctly) before attempting the much
  harder step of actually feeding it real keybed bytes, which requires
  the 80C52's protocol, not yet known.

## Del 5 Addendum — Journal And Sources, This Round

**Sources, verbatim, all fetched from URLs supplied directly, none
guessed:**

- [ENSONIQ ASR-10 Service manual — Manualzz](https://manualzz.com/doc/1912402/ensoniq-asr-10-service-manual)
- [ASR-10 Service Manual PDF — SynthXL](https://www.synthxl.com/wp-content/uploads/2018/03/Ensoniq-ASR-Service-Manual.pdf) (downloaded from this URL; saved as `docs/asr10/sources/ASR10_service_manual.pdf`)
- [Ensoniq Technical Documents and Schematics — R-Massive](https://zine.r-massive.com/ensoniq-technical-documents-and-schematics/) (led to the Tauntek coil-board schematics)
- [Ensoniq ASR-10 Keyboard Calibration Error — Syntaur Forums](https://forums.syntaur.com/t/ensoniq-asr-10-keyboard-calibration-error/5559) (independent confirmation that keyboard-calibration failure is a real, discussed ASR-10 phenomenon — corroborates the owner testimony without this task re-deriving the exact forum text)
- Coil-board schematics: `http://www.tauntek.com/ASR10highcoilsch.pdf` and `.../ASR10lowcoilsch.pdf`, R. Grieb/Tauntek, saved as `docs/asr10/sources/ASR10_upper_coil_board_schematic.pdf` / `..._lower_coil_board_schematic.pdf`. Both carry the author's own disclaimer ("This schematic drawn from through-hole version of pc bd. It may contain errors.") — treated as `[External]`, not `[Verified]`, per that disclaimer.
- MC68302 SCM bit-field cross-check: `manualslib.com`'s indexed copy of the Motorola MC68302 User's Manual (the NXP-hosted original PDF 404s as of this check).

**Keyboard hypothesis: what remains for `[Verified]`.** Status is now
**confirmed on structure, timing, and external documentation** — the
message is real (owner testimony, cross-checked against the owner's
manual's own wording), this project's own model reproduces it with
precise SCC-timing correlation (prior round), and the physical byte
source is now named with a schematic behind it (this round). What is
still missing before this can be called `[Verified]` rather than a
very strong `[Hypothesis]`/`[Likely]`: (a) distinguishing SCC1 from
SCC2 by an actual measured difference, not just a motivated
assignment (Del 2); (b) the 80C52's own transmitted byte format,
unknown and out of reach without its firmware; (c) direct evidence
(not just physical plausibility) that *this specific* MC68302 SCC
channel's `MODE` value is the one that means UART.

**Main-board schematic**: still not found publicly scanned anywhere
searched this round (only the two coil-board schematics were located,
both for the keyboard assembly, not the digital board itself). Del 0's
gate stands for the main board specifically — no electrical mapping of
what sits against the MC68302's own pins on the digital board was
attempted, and none should be, absent that source.

**Per's question list, shortened**: questions 2 ("is there a separate
keybed circuit/board") and 3 ("is there a cable, how many conductors")
are now answered by the service manual and schematics — yes, the
keybed has its own 80C52-based scanner board, and the connection is
the 20-pin keyboard ribbon cable at connector J7. Revised list below.

## Second Recalculation: Testing "SCC1/SCC2 = Stereo Audio Input"

The prior round's own error-code correction (`trap #0` codes are 5/6,
matching the manual's "could not synchronize audio input" verbatim,
not 45/46) reopened the question this section tests directly: is the
SCC pair the audio input path rather than the keyboard link? Used the
machine's own sampling function as stimulus, per instruction — the
same kind of missing-stimulus fix that made note playback work
earlier in this investigation.

### Del 1 — Entering Sample Mode, Measured

**Procedure, from `ASR10_manual.pdf`'s own "Easy Sampling" section**:
press `Sample-Source Select` (display → `REC SRC=INPUTDRY LEFT`), then
press an unloaded `Instrument-Sequence Track` button (the manual's own
recommended path, versus `Enter-Yes` + a picker screen). Button codes:
`Sample-Source Select` = `BTN_20` (already established,
`panel-button-sweep-v350.md`: this exact button produces
`REC SRC=INPUTDRY LEFT`); `Instrument 1` = `BTN_02`
(`keyboard-and-sample-bridge-5.md`), left deliberately unloaded this
run by skipping the boot-time file-load sequence entirely, so it
qualifies as "unloaded" per the manual's own instruction.

**Witnessed, `#8.5`-safe**: SCM1/SCM2/IMR/BD taps installed only after
`FILE 1` (well past BAR settle at ~5.4s); level-4 IACK tap installed
from script start (`cpu_space`, not the SIB window, no `#8.5` concern).

**Result: the channels do arm — and this is specific to this button,
not a generic side effect.** Pressing `Sample-Source Select` triggers
the exact same `SCM1`/`SCM2` (`$7033`↔`$703B`) / `IMR` (`$C080`↔
`$E480`) arm/disarm cycle, and a full rewrite of both descriptor rings,
within ~100ms of the button press. **Control test, run separately**:
pressing an unrelated button (`Up Arrow`, `BTN_0B`) after `FILE 1`
produces **zero** `SCM`/`IMR` writes at all, despite the display
changing (`FILE 14 BLUES ORGAN`) — proving this is not "any button
press" or "any display update" triggering the pattern. Sample-mode
entry specifically re-arms SCC1/SCC2; ordinary file-browser navigation
does not.

**But the content of what arms is the decisive fact, and it cuts
against the strong reading of the new hypothesis**: every descriptor
rewritten during sample-mode entry points to **the identical buffer
addresses already used during boot-time keyboard calibration**
(`$00F76600`... for SCC1, `$00F74B00`... for SCC2 — byte-for-byte the
same as `scc-hardware-gap.md`'s original measurement). **Not** the
sample-RAM pool (`map(0x100000,0x1fffff).ram().share(":asr10_sample_
ram")`, this driver's own actual audio-sample destination) — not a
larger allocation, not a different `MRBLR`, not a different ring size.
The same 6,400-byte scratch ring gets torn down and rebuilt identically
regardless of which of the two triggers (calibration or sample-mode
entry) fires it. **Zero level-4 IACKs, throughout** — matching every
prior measurement.

**Where the sequence gets to, and where it stops**: the display reaches
"?" after the instrument-slot press rather than a readable Level-Detect
VU meter — almost certainly this project's ASCII display decoder
failing to render the VU meter's bar-graph glyphs (a `?` fallback is
this decoder's own established behavior for undecodable characters,
not evidence the mode failed to start) rather than a real failure to
enter the mode; not confirmed further this round.

### Del 2 — What This Does And Doesn't Tell Us

**The test criterion as literally stated ("do the channels arm") is
met.** But Del 1's own content finding — identical tiny buffers, no
scale-up, no real transfer, zero IACKs — does not look like what a
bulk digitized-audio DMA path would look like. A real stereo audio
input, even at the lower-fidelity 29.76 kHz mode, needs continuous
throughput orders of magnitude larger than an 800-byte-per-descriptor,
8-descriptor ring can usefully carry for more than a few milliseconds;
it would need to target the sample-RAM pool, not this fixed 6,400-byte
scratch area reused unchanged from calibration. **This experiment does
not confirm the strong form of the audio-input hypothesis** — SCC1/
SCC2, as configured and exercised here, are not shown feeding bulk
sample data anywhere. Format/rate/relationship to Y3 = 33.8688 MHz:
moot given the above — there is no observed block matching an audio
transfer to characterize.

**What "could not synchronize audio input" might mean instead,
consistent with everything measured**: a small, generic status/
handshake exchange — common to several UI transitions, not unique to
sampling — that could plausibly *check whether an external device
(the keyboard's own scanner, or an audio-input-adjacent status line)
is present and answering*, using the same tiny ring both times because
the message itself is small, not because the underlying payload is
audio. This reading is consistent with the manual's own error text
without requiring SCC1/SCC2 to carry bulk PCM data.

### Del 3 — The Keyboard Link Is Real; Which Wire On The Digital Board Is Now Its Own Open Question

**Not disproven, not confirmed — downgraded to `[OPEN]`, precisely
because Del 1/2's result is genuinely mixed.** The new audio-input
hypothesis is not confirmed either (same section). Both hypotheses
now have real evidence and real friction:

- *For* SCC1/SCC2 = keyboard: real buffer-descriptor rings, `scc_rx_
  common`'s genuine event-driven character reception logic, the silent-
  shutdown path, and now a specific (non-generic) re-arm triggered by
  entering a mode (`Sample-Source Select` → Level-Detect) that the
  manual itself says explicitly supports playing the keyboard during.
- *Against*: the error text says "audio," and the same ring never
  scales to anything audio-sized, in either the calibration or the
  sample-mode context.
- *Against the audio reading specifically*: no observed transfer looks
  like bulk audio DMA; the buffers stay small, fixed, and empty.

**SCC3 as a keyboard-link candidate**: weak. Zero traffic, IMR bit
clear, in every measured run including this one — inconsistent with
"the keyboard is calibrated every single boot" (the manual's own
claim), which would need *some* channel firing every time. Not
promoted.

**A fourth serial path — checked against the manual, not measured
directly**: the service manual's own communications-path description
(quoted above) names a **three-line synchronous** link between the
keyboard and the keypad/display board, distinct from the two-line
async keyboard link. This project already has an established, verified
fact that sits in tension with treating that sync link as one of
SCC1/SCC2: **DUART channel B is the panel/keypad-display link,
`[Verified]`** (`subroutine-index.md`: *"kanal B panel [Verified]"*).
The SCN2681/MC68681 DUART family is **asynchronous only** — it has no
native synchronous serial mode. Either the manual's "synchronous"
description does not survive to the digital board in that form (some
bridging happens inside the keyboard assembly before the signal
reaches DUART channel B), or the manual's word choice is looser than a
strict USART distinction, or the two facts describe genuinely
different signals. **Not resolved this round** — flagged as a specific,
well-defined open question rather than left implicit: *what carries
the keypad/display board's own synchronous protocol, given that DUART
channel B is both established as the panel link and incapable of
synchronous framing?* SMC1/SMC2 (`docs/mc68302/communications-block-
map.md`: parameter RAM `$0660-067F`, their own register block) were
not measured for traffic this round — a real, named gap, not
overlooked in the write-up even though it was in the practical
measurement.

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
  byte search. `-wavwrite` was available this round per instruction but
  not used — the sample-mode measurement stopped at register/descriptor
  level, which already gave a decisive (negative, for the strong
  audio-DMA reading) answer before any audio-capture step was needed.
- Sample-mode SCM/IMR/BD taps installed only after `FILE 1` (`#8.5`);
  a clean negative control (unrelated button press, zero SCC writes)
  satisfies `#8.7`'s live-witness requirement for the "not generic"
  claim.
- No board component list, schematic, or wiring diagram was invented.
  The digital board's own component layout remains `[OPEN] — no
  source`; the keyboard assembly's is now documented, from real,
  downloaded, cited sources (service manual + two coil-board
  schematics), not guessed.
- All external documents fetched from URLs supplied directly (the
  service manual, the R-Massive index page, the Tauntek schematics,
  the Syntaur forum thread) or reached via a web search for the actual
  MC68302 manual's own bit tables — no URL was invented.
- No fork with an open mandate.
- `docs/asr10/regression-test.sh`: 8 tests, 9 PASS lines, run before
  and after this task's edits — unaffected, Lua/documentation-only.
- `git diff --check`: clean.
