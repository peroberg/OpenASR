# ASR-10 Instrument To OTTO Runtime Boundary

## Scope

This document records the currently verified firmware boundary between a loaded
instrument/runtime voice object and ES5506/OTTO host-register programming. It is
not a full ES5506 description and it does not describe the disk file format.
For object ownership and the full voice-record lifecycle, see
`runtime-object-model.md`.

Evidence levels:

- `[Verified silicon spec]` local Ensoniq chip specification.
- `[Verified firmware]` V3.50/ROM static firmware evidence.
- `[Verified runtime]` reproduced current MAME runtime result.
- `[Architectural precedent]` related Ensoniq architecture, not ASR-10 proof.
- `[Likely]` inference supported by multiple local facts, but not yet directly
  proven.
- `[OPEN]` plausible or important, but not established.
- `[DISPROVEN]` contradicted by current evidence.

Chip specifications describe silicon capabilities. Firmware proves how ASR-10
software uses registers and RAM structures. EPS/EPS-16+ material is not used as
ASR-10 evidence here.

## Method and positive controls

[Verified firmware] The ES5506 host window is the odd-lane CS2 address
`$FC2001`. The existing MOVEP library documents known ES5506-style accesses:

- `$FFFC60B0`: `movep.l ($68,A0),D2` - PAR read.
- `$FFFC60B6`: `movep.l ($78,A0),D0` - PAGE read.
- `$FFFC60BC`: `movep.l ($70,A0),D0` - IRQV read.

Those known accesses calibrate the search. The ROM voice-runtime code uses the
same `$FC2001` base, direct PAGE byte writes through `($7E,A0)`, and MOVEP
thunks for page-dependent voice registers.

## Silicon boundary

[Verified silicon spec] ES5506/OTTO has 32 independent voices, a host register
interface, `PAGE`, `IRQV`, `PAR`, per-voice control/start/end/accumulator/
frequency/volume/filter state, autonomous sample fetch from external sound
memory, and `IRQB` voice-event signalling.

[Verified silicon spec] The meaning of the lower host offsets depends on the
selected ES5506 page. Therefore firmware writes to offsets such as `$08`,
`$10`, `$18`, `$24`, `$2C`, `$34`, `$3C` and `$44` must be interpreted together
with the PAGE value currently selected by `($7E,A0)`.

## Runtime voice table

[Verified firmware] ROM routine `$F8CD22` initializes a 32-entry runtime voice
table at `$00008000`.

| property | evidence | status |
|---|---|---|
| voice table base | `$F8CD2A: lea $8000.l,A4` | [Verified firmware] |
| voice count | loop compares `D7` through `$1F` | [Verified firmware] |
| voice stride | `$F8CD44: lea ($D8,A4),A4` | [Verified firmware] |
| voice/page index | `$F8CD30: move.w D7,($14,A4)` then low byte cleared | [Verified firmware] |
| per-voice helper/output pointer | `$F8CD40: move.l D0,($2A,A4)`, starting at `$00FF7F00` | [Verified firmware] |
| next/previous ring links | `$F8CD96` and `$F8CD9A` write `+$2E` and `+$32` | [Verified firmware] |
| initial list root | `$F8CD60` inserts voices via root `$0D36` | [Verified firmware] |
| active/current ring root | `$F8CD8C` stores `$0D5A` and builds a ring | [Verified firmware] |

[Likely] `$0D36` is a free/available voice list and `$0D5A` is an active/current
voice ring. The exact list semantics for `$0D32`, `$0D3A`, `$0D42`, `$0D4A`,
`$0D52`, `$0D62` and related roots remain open.

## Runtime voice fields consumed by OTTO code

[Verified firmware] The following voice-record fields are consumed by ROM
helpers that also access ES5506 registers:

| voice field | observed use | status |
|---|---|---|
| `A4+$14/$15` | voice state and low-byte PAGE/voice number; copied to ES5506 PAGE, often plus `$20` | [Verified firmware] |
| `A4+$18` | runtime/control object pointer used by `$F8D3C6`, `$F8DE2C`, `$F8E0CA`, `$F8E160` | [Verified firmware] |
| `A4+$1E` | instrument/layer/sample object pointer; `$F8CA38` loads `A3=($1E,A4)`; direct producer not located statically | [Verified static] consumer, producer [OPEN] |
| `A4+$22` | sample-address base/offset added to object sample addresses before ES5506 writes; `$F8DFAA` is one verified fixed-control producer | [Verified static] consumer and one producer path |
| `A4+$26` | per-voice event callback; PB9/IRQV handler calls through this field | [Verified firmware] |
| `A4+$2A` | per-voice helper/output buffer pointer used by segment/event code | [Verified firmware] |
| `A4+$A4` | pointer used by `$F8D626` while deriving pitch/address state | [Verified firmware] |
| `A4+$C6` | current sample/accumulator-like address saved during segment handling | [Verified firmware] |
| `A4+$CA` | linked secondary voice or chained runtime object in `$F8CAB4` | [Verified firmware] |

[OPEN] The top-level loaded instrument root is not localized. Current evidence
starts at a runtime voice record (`A4`) that already points to instrument or
sample-related objects.

[OPEN] The direct or indirect producer that installs `A4+$1E` into a normal
voice record is not localized. Targeted static search did not find a direct
`($1e,A4)` voice-record write; that is negative evidence for the current search,
not proof that the field is never written.

## ES5506 firmware helper map

| routine/access | ES5506-facing operation | callers/evidence | status |
|---|---|---|---|
| direct `move.b ...,($7E,A0)` with `A0=$FC2001` | PAGE write low byte | `$F8CE50`, `$F8CA96`, `$F8D634`, `$F8E16E`, `$F8E34A+` | [Verified firmware] |
| `$FFFC60B6` | PAGE read | PB9 handler `$F8D084` | [Verified firmware] |
| `$FFFC60BC` | IRQV read | PB9 handler `$F8D08C` | [Verified firmware] |
| `$FFFC60B0` | PAR read | documented PAR path | [Verified firmware] |
| `$FFFC6028` | page-dependent offset `$00` read, control/status-like | `$F8CA9A`, `$F8D68C`, `$F8D8F8`, `$F8E1B4` | [Verified firmware] |
| `$FFFC602E`, `$FFFC605C`, `$FFFC6062` | page-dependent offset `$08` long writes | voice reset/segment routines | [Verified firmware] |
| `$FFFC6068`, `$FFFC606E` | page-dependent offset `$10` long writes | `$F8CA9A`, `$F8CE8C`, `$F8E186`, `$F8E350+` | [Verified firmware] |
| `$FFFC6046` | page-dependent offset `$18` read | `$F8D6C6`, `$F8D7D4`, `$F8D82C` | [Verified firmware] |
| `$FFFC603A`, `$FFFC6074`, `$FFFC608C`, `$FFFC6098`, `$FFFC60C2` | page-dependent word writes used during voice setup/control | `$F8CE4A`, `$F8E01E`, `$F8E0CA` | [Verified firmware] |

[OPEN] Exact ES5506 register names for every page-dependent offset above are
not assigned here. The verified claim is the firmware-side host access pattern,
not a complete semantic decode of every OTTO register write.

## First verified runtime object to OTTO chain

[Verified firmware] The first solid boundary is:

```text
runtime voice record A4
  -> instrument/sample object pointer A3 = (A4+$1E) or caller-provided A3
  -> sample-address base A4+$22
  -> ES5506 PAGE select through $FC2001+$7E
  -> ES5506 page-dependent start/end/control-style writes
```

Key routines:

- `$F8CA38` loads `A3=($1E,A4)`, copies several object substructures into the
  voice record, and when `($EE,A3)==4` computes `movep.l ($F8,A3) + (A4+$22)`,
  shifts it left by two, selects `PAGE=(A4+$15)+$20`, and writes the result
  through `$FFFC606E`.
- `$F8CAB4` prepares voice-record state at `+$6A`, sets `($42,A4)=$0214`, sets
  `($14,A4)=8`, stores `A4+$B4` into `+$10`, and recurses through `+$CA` when
  linked.
- `$F8D626` selects `PAGE=(A4+$15)+$20`, installs per-voice callback
  `$F8D8F8` at `A4+$26`, reads sample/pitch/address-like fields from `A3+$F8`,
  `A3+$100`, `A3+$108`, `A3+$118`, `A3+$11A`, and combines them with `A4+$22`.
  The following segment-handling code writes ES5506 host offsets `$08`, `$10`
  and `$18`.
- `$F8E160` uses `A4+$18` to reach a runtime object, indexes entries with
  `0x14` stride, converts two addresses with `<< 10`, `<< 2`, adds `A4+$22`,
  and writes them to ES5506 host offsets `$10` and `$08`.

[Verified firmware] V3.50 OS static call graph has direct calls into these ROM
helpers:

- `$007514`, `$007526`, `$00764A`, `$007686`, `$0076C8`, `$00777A`,
  `$007CE6` -> `$F8CAB4`.
- `$007EC0` -> `$F8D626`.
- `$F13F5E` -> `$F8CA38`.

[OPEN] The upstream chain from key/MIDI event to those OS callers is not
localized in this pass.

## Load time vs note-on time

Current evidence supports this model:

| model | status | rationale |
|---|---|---|
| Instrument-load directly programs ES5506 voices | [OPEN] | No verified load-time path reaches OTTO voice programming; current observed instrument load stalls after RECALIBRATE start and before RECALIBRATE completion, SEEK, async READ DATA and IDMA start. |
| Instrument-load creates metadata/sample state, ES5506 is programmed by runtime/note voice path | [Likely] | ROM voice helpers consume runtime voice records and instrument/sample pointers, then program OTTO. Exact loaded instrument root and note event chain are still open. |
| Mixed model: load/global setup plus note-on voice-specific programming | [Likely] | Static firmware clearly has voice-specific OTTO programming; load-side sample-memory and metadata production remain open. |

[Verified firmware] Voice-specific OTTO programming exists downstream of runtime
voice records. [OPEN] Whether any non-voice ES5506 global state is programmed
during instrument load has not been verified.

## Voice allocation and note-on boundary

[Verified firmware] `$F8CD22` creates the 32-entry voice table and list/ring
roots. `$F8CAFA` obtains a voice from list roots and calls binding slot
`$8E6E` (`$F8CE4A`) when a record must be reset.

[Verified firmware] `$F8CE4A` is a voice reset/setup routine. It selects the
voice PAGE, clears or initializes page-dependent ES5506 registers, and programs
address/control-like values derived from global sample-memory constants
`$0D72/$0D76`.

[Likely] The OS callers into `$F8CAB4` and `$F8D626` are part of the note/voice
allocation path. This is supported by their placement in mapped OS RAM and by
the ROM callees' use of voice records, sample pointers, PAGE selection and
voice callbacks.

[OPEN] A complete chain from panel/MIDI/key event -> instrument lookup ->
layer/key/velocity selection -> voice allocation -> OTTO programming is not yet
verified.

## Sample-memory boundary

[Verified firmware] The runtime OTTO code converts sample-related firmware
addresses before writing them to ES5506:

- `$F8D020-$F8D054` derives `$0D6A/$0D6E/$0D72/$0D76` from `$0C52`, adds
  `$0200` and `$F000`, masks/rotates, and shifts by two before later ES5506
  programming.
- `$F8CA38` adds `A4+$22` to `movep.l ($F8,A3)`, shifts left by two, and writes
  via `$FFFC606E`.
- `$F8D626` reads `A3+$F8`, `A3+$100`, `A3+$108`, combines those values with
  `A4+$22`, and the following code writes ES5506 offsets `$08/$10/$18`.
- `$F8E160` reads address entries from a runtime object, shifts by eight and
  two, adds `A4+$22`, then writes ES5506 offsets `$10/$08`.

[Verified silicon spec] ES5506 autonomously fetches samples from external sound
memory, and ES5701-class Super-GLU silicon provides CPU/OTIS/static
sound-memory bus glue in its documented role.

[OPEN] The exact ASR-10 sample RAM destination for loaded instrument bytes is
not identified. It is not yet verified whether the CPU writes sample memory
directly through Super-GLU/OTTO memory access, through another host path, or via
a firmware-mediated copy not localized here.

## PB9 / IRQV event service

[Verified silicon spec] ES5506 provides `IRQB` and `IRQV` for voice interrupt
events.

[Verified firmware] PB9/vector `$47` handler `$F8D072` is an ES5506-like voice
event service:

```text
$F8D076  $FC6818 <- $0080       ; MC68302-side PB9 ISR clear/EOI bit
$F8D07E  A0 <- $FC2001
$F8D084  read PAGE via $FFFC60B6
$F8D08C  read IRQV via $FFFC60BC
$F8D0A4  voice = IRQV & $1F
$F8D0A8  voice offset = voice * $D8
$F8D0AE  A4 = $8000 + offset
$F8D0B4  A3 = (A4+$26)
$F8D0B8  jsr (A3)
$F8D0BA  restore PAGE
$F8D0C2  rte
```

[Verified firmware] The handler obtains a voice number/index from `IRQV`, maps
it to the `$8000 + voice * $D8` runtime record, and dispatches the per-voice
callback stored at `+$26`.

[OPEN] Physical ES5506 `IRQB` -> MC68302 PB9 wiring is not board-verified.

[OPEN] Runtime evidence that this handler fires during normal voice playback is
not present in this pass.

## ES5510 participation

[Verified firmware] The ROM voice-runtime routines identified here use the
ES5506 host window at `$FC2001`. They do not access the ES5510 host window
`$FC3000-$FC31FF` in the identified runtime voice-to-OTTO chain.

[OPEN] Broader instrument-load effect assignment, ESP microcode selection and
runtime effects routing are separate questions. The identified voice path does
not prove that ES5510 is irrelevant to all instrument runtime behavior.

## End-to-end boundary map

| boundary | current status |
|---|---|
| storage -> instrument/sample bytes | [OPEN] current observed instrument load stalls after RECALIBRATE start and before RECALIBRATE completion, SEEK, async READ DATA and IDMA start. |
| sample bytes -> sample memory | [OPEN] exact destination and write path not localized. |
| sample memory -> runtime metadata references | [OPEN] producer not localized. |
| runtime voice record -> instrument/sample object pointer | [Verified static] consumers use `A4+$1E`, `A4+$18`, `A4+$22`; direct/indirect `A4+$1E` producer remains [OPEN]. |
| runtime voice record -> ES5506 PAGE/register writes | [Verified firmware] ROM helpers select PAGE and write page-dependent OTTO registers. |
| ES5506 programmed voice -> autonomous playback | [Verified silicon spec]; firmware writes are verified, audible/runtime playback is not verified here. |
| ES5506 event -> PB9/IRQV voice callback | [Verified firmware] handler reads IRQV and dispatches per-voice callback; physical wiring remains [OPEN]. |

## Remaining open questions

- [OPEN] Loaded instrument root structure and slot/index lookup.
- [OPEN] Exact metadata producer after storage completion.
- [OPEN] Sample RAM destination and CPU/OTTO/Super-GLU write path.
- [OPEN] Complete note/key event to voice allocation chain.
- [OPEN] Exact semantic names for all page-dependent ES5506 register writes.
- [OPEN] Whether ES5506 global state is programmed during load.
- [OPEN] Physical ES5506 `IRQB` to PB9 routing.
- [OPEN] Runtime proof of PB9/IRQV service during normal playback.
- [OPEN] ES5510 effect assignment/execution relationship to loaded
  instruments.
