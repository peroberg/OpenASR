# ASR-10 Runtime Object Model

## Scope

This is the central reference for firmware-owned runtime objects that connect
storage, loaded instrument state, sample memory, voice allocation and ES5506
programming.

It is intentionally not a call graph, timeline, ES5506 manual or implementation
plan. It records ownership boundaries and object lifecycles that are currently
visible from ASR-10 ROM/V3.50 firmware evidence.

Evidence levels:

- `[Verified silicon spec]` local Ensoniq chip specification.
- `[Verified firmware]` V3.50/ROM static firmware evidence.
- `[Verified static]` targeted static disassembly/call-graph evidence from the
  current V3.50/ROM analysis.
- `[Verified runtime]` reproduced current MAME runtime result.
- `[Architectural precedent]` related Ensoniq architecture, not ASR-10 proof.
- `[Likely]` inference supported by multiple local facts, but not directly
  proven.
- `[OPEN]` plausible or important, but not established.
- `[DISPROVEN]` contradicted by current evidence.

## High-level ownership

| object | owner | create/allocate | modify/read | release | status |
|---|---|---|---|---|---|
| Storage async operation | storage/FDC/SCSI firmware service | `$0402` writers and FDC/SCSI start routines | vector `$4B` IDMA dispatcher, vector `$51` shared storage dispatcher | continuation callbacks | [Verified firmware] |
| Sample bytes from disk | storage loader | async FDC/SCSI READ path, not reached in current instrument-load run | [OPEN] | [OPEN] | [OPEN] |
| Sample memory | sample/address manager, exact owner not localized | [OPEN] | address conversion globals `$0C52`, `$0D6A`, `$0D6E`, `$0D72`, `$0D76` are consumed by voice code | [OPEN] | [Likely]/[OPEN] |
| Loaded instrument root | [OPEN] | [OPEN] | voice code consumes instrument/sample object pointers, but producer/root is not found | [OPEN] | [OPEN] |
| Runtime voice record | ROM voice manager | `$F8CCF6/$F8CD22` init, `$F8CAFA` allocation, list roots `$0D32-$0D52` | `$F8C2xx-$F8D9xx`, `$F8DD94-$F8E4xx`, PB9 handler `$F8D072` | `$F8C994/$F8C9AA/$F8C9CC` list return/reset paths | [Verified firmware] |
| ES5506/OTTO | ES5506 silicon, programmed by firmware voice manager | hardware has 32 voices | firmware selects PAGE and writes host registers through `$FC2001` | autonomous playback and IRQV events | [Verified silicon spec]/[Verified firmware] |
| ES5510/ESP | effects DSP subsystem | host window `$FC3000-$FC31FF` | not used by the identified voice-to-OTTO chain | [OPEN] | [Verified firmware]/[OPEN] |

## Storage request and payload boundary

[Verified static/runtime] The current instrument-load request is a generic
service-node request delivered to storage target `$14DA`, not a direct
instrument-object call.

Observed request chain:

```text
producer $FFA882-$FFA8AA
  -> trap #3 service-node allocation
  -> node +2/+3 = $03/$02
  -> node +4 = $0002B600
  -> A1 = $14DA
  -> trap #12 immediate path
  -> target callback (14DA) = $00B08C
  -> storage entry
```

[Verified runtime] The `$14DA` service queue at `$14E0/$14E2` stayed empty in
the observed `LOADING JM DIGI SYN` run. The path used trap `#12` immediate
dispatch, with `$14EA: 0000 -> 0001`.

[Verified static/runtime] Storage entry preserves payload identity:

```text
A2 = $02B600
  -> node +4
  -> $046A = $02B600

$0466 = request node $1504
```

Observed stall state:

```text
$0466 = $1504
$046A = $0002B600
$043E = $00000000
```

[Verified runtime] The payload accepted into `$046A` has not been promoted to
`$043E` in the observed stalled run.

### Request class and subtype

[Verified static/runtime] The request stores two bytes, not one fully decoded
storage opcode:

```text
node +2 = $03
node +3 = $02
```

Storage copies the word into the `$049A/$049B` byte pair. Later code uses the
bytes separately:

```text
$049A.b = $03  high-level request class / dispatch byte
$049B.b = $02  subtype/tag byte
```

[DISPROVEN] `$0302` should not be described as a single simple storage opcode.

### `$043E` model

Best current classification:

```text
$043E = current storage payload/runtime descriptor pointer
```

[DISPROVEN] `$043E` is not verified as an instrument object.

[Verified static] Known `$043E` writers in the current OS image:

```text
$00B33C  $043E <- $046A   class $0D path
$00B8FE  $043E <- $046A   class $06 path
```

The observed instrument-load request is class `$03`, so it does not directly
enter either promotion path. Promotion to `$043E` is a later operation
hierarchy boundary.

[Verified static] Current `$043E` object/descriptor users touch fields around:

```text
+0, +4, +8, +C, +E, +10, +14, +18, +1C, +20, +22
```

[Verified static] In the class `$0D` promotion path, `+$22` receives the
subtype/tag byte from `$049B`.

[Likely] `$043E` points at a variant-tagged storage/runtime descriptor-like
object.

[OPEN] Exact object type, common-header-vs-union layout, sample/instrument
ownership and downstream runtime owner.

### Storage operation hierarchy

[Verified static/runtime] The firmware model is better described as a hierarchy
than as one contiguous struct:

```text
generic service/request node
  +-- +2/+3 class/subtype
  +-- +4 payload
  v
storage manager
  +-- high-level request state
  +-- transfer state
  +-- device command state
  +-- async completion/continuation
  v
payload/current descriptor processing
  v
[OPEN] sample/instrument runtime
```

[DISPROVEN] `$0402/$049A/$04B0/$046A/$043E` should not be documented as one
contiguous `StorageOperation` structure. The current evidence supports a
coherent operation hierarchy represented by service nodes, global phase/state
fields, payload/descriptor objects and continuation pointers.

### Current class `$03` continuation boundary

[Verified static] The class `$03` request dispatches through the verified
static READ/IDMA path:

```text
$049A.b == $03
  -> dispatch table $BA76[$03]
  -> $B64C
  -> $FB7F9E
  -> $FBA5A2
  -> $FB9C5E
  -> $FB9FE2
  -> $FB84DA
  -> $FB85C0
  -> MC68302 IDMA setup
  -> $FB8672
  -> FDC READ DATA $46
```

[Verified static] The IDMA transfer uses source `$FFFC5803`, destination
`$040E`, count derived from transfer/sector state, and MC68302 IDMA registers
`$FC6802/$FC6804/$FC6808/$FC680C/$FC6810`.

[Verified static] After device/IDMA completion, storage common exit returns the
same saved request node from `$0466`. The observed node has positive
`node +2=$0302`, so the expected static return context is scheduler slot
`$23F6`, not `$2438`.

[OPEN] The full continuation from class `$03` common exit through `$23F6`
consumer, later request-class transitions into class `$06/$0D`, and `$043E`
activation.

### Load boundaries

| boundary | current status |
|---|---|
| A. request constructed | [Verified runtime/static] producer `$FFA882-$FFA8AA`, node `$1504` |
| B. request accepted by storage | [Verified runtime] `$0466=$1504`, `$046A=$02B600` |
| C. device initialization / RECALIBRATE starts | [Verified runtime] command `07 00` sent |
| D. RECALIBRATE completion | [OPEN] not delivered in current emulator path |
| E. first actual READ/data transfer | [OPEN] not reached in observed load |
| F. payload/runtime descriptor activation via `$043E` | [Verified static mechanism], not reached at runtime |
| G. sample/instrument attachment | [OPEN] |
| H. UI/runtime-visible loaded | [OPEN] |
| I. voice-consumable state | [OPEN] |

### Boundaries kept separate

The following worlds are not yet connected by verified pointer/dataflow for the
current instrument-load request:

| world | representative evidence | bridge status |
|---|---|---|
| storage descriptor | `$046A/$043E` | current request reaches `$046A`; `$043E` remains zero at stall |
| sample manager | `$F8E6BE-$F8EA56`, `A2+$F0/$F8/$100/$108/...` | [OPEN] no verified pointer bridge from `$046A/$043E` |
| instrument selector | `$14AC`, `$0D10/$0D18/$0D1A/$0D1C`, `$F8C492/$F8C412` | [OPEN] no verified pointer bridge from `$046A/$043E` |
| voice manager | `$8000` table, stride `$D8`, `A4+$1E`, `A4+$22`, `A4+$26` | [OPEN] no verified load-time attachment bridge |

## Voice manager subsystem

[Verified firmware] The runtime voice table is owned by a ROM voice manager
cluster centered around `$F8C2xx-$F8D0xx` and `$F8DD94-$F8E4xx`, with several
ROM-backed binding slots.

### Binding slots in the voice manager

| slot | V3.50 target | role in voice manager | status |
|---|---|---|---|
| `$8E38.w` | `$F8C994` | unlink current voice record, clear state, reset OTTO through `$8E6E` | [Verified firmware] |
| `$8E3E.w` | `$F8C9AA` | insert voice record into root `$0D32`, clear state, reset OTTO | [Verified firmware] |
| `$8E44.w` | `$F8C9CC` | unlink voice record, insert into `$0D32`, clear state, reset OTTO, preserving `A1` | [Verified firmware] |
| `$8E50.w` | `$F8CAFA` | allocate/select a voice record from manager lists, reset if needed, unlink from chosen list | [Verified firmware] |
| `$8E6E.w` | `$F8CE4A` | ES5506 voice reset/setup for the selected voice record | [Verified firmware] |
| `$8EB2.w` | `$F8DD94` | synchronize four fixed control objects with four voice records | [Verified firmware] |
| `$8ED6.w` | `$F8E338` | per-voice segment/event callback target | [Verified firmware] |
| `$8EDC.w` | `$F8E384` | per-voice segment/event callback target | [Verified firmware] |
| `$8EE2.w` | `$F8E3D2` | per-voice segment/event callback target | [Verified firmware] |
| `$8EEE.w` | `$F8E46C` | per-voice segment/event callback target | [Verified firmware] |
| `$8EF4.w` | `$F8E4BA` | per-voice segment/event callback target | [Verified firmware] |
| `$8E02.w` | V3.50 `$00E54E` | OS-owned voice/event routine called by ROM manager paths | [OPEN] |
| `$8E7A.w` | V3.50 `$00E7B4` | OS-owned routine called during manager init | [OPEN] |
| `$8E86.w` | V3.50 `$0069FC` | OS-owned routine called after audio/OPR update path | [OPEN] |
| `$8EE8.w` | V3.50 `$00F096` | OS-owned per-voice callback family member | [OPEN] |

### Core routines

| routine | role | callers/entry evidence | data operated on | status |
|---|---|---|---|---|
| `$F8CCF6` | initializes list roots `$0D32-$0D52` as self-linked sentinels | ROM code before `$F8CD22` | list sentinels | [Verified firmware] |
| `$F8CD22` | creates 32 voice records at `$8000`, stride `$D8` | static ROM | voice table | [Verified firmware] |
| `$F8CD60-$F8CD82` | inserts initially available voices through root `$0D36` | inside init | doubly-linked list fields `+0/+4` | [Verified firmware] |
| `$F8CD8C-$F8CDAE` | builds active/current ring using `+$2E/+32`, stores `$0D5A` | inside init | ring links | [Verified firmware] |
| `$F8CDCE` | calls `$8E6E` for each initialized voice | inside init | ES5506 voice reset/setup | [Verified firmware] |
| `$F8CAFA` | selects a voice from `$0D32`, otherwise `$0D4A`, `$0D3A`, `$0D42`; traps error if all empty | slot `$8E50`, direct ROM calls | manager list roots | [Verified firmware] |
| `$F8C994` | removes a record from its current list, clears state, resets OTTO | slot `$8E38` | list links, state byte, ES5506 | [Verified firmware] |
| `$F8C9AA` | inserts a record at `$0D32`, clears state, clears `+8`, resets OTTO | slot `$8E3E` | free/idle list | [Verified firmware] |
| `$F8C9CC` | same return-to-`$0D32` path, preserving `A1` | slot `$8E44` | free/idle list | [Verified firmware] |
| `$F8C2CC-$F8C338` | scans `$0D3A`, compares owner pointer `+$16`, moves matching records to `$0D4E` with state `4` | ROM note/control path | owner and list state | [Verified firmware] |
| `$F8C39C-$F8C410` | scans `$0D4A`/`$0D3A`, tests channel-like byte `+$0B` and state `4`, calls `$8E02` | ROM note/control path | channel/state filtering | [Verified firmware], semantics [Likely] |
| `$F8C8F8-$F8C968` | rotates/reorders the `$0D42` ring, updates `$0D5A`, sets state `2` | direct ROM use | ring/current ordering | [Verified firmware] |
| `$F8CA38` | copies instrument/sample substructures into a voice, writes one sample address to ES5506 when type byte `A3+$EE` is `4`, sets state `6` | V3.50 caller `$F13F5E` | voice + instrument/sample object | [Verified firmware] |
| `$F8CAB4` | prepares voice envelope/control state, sets state `8`, handles chained voice at `+$CA` | V3.50 callers `$007514` etc. | voice record | [Verified firmware] |
| `$F8CE4A` | resets/programs ES5506 page-dependent voice registers | slot `$8E6E` | ES5506 host window `$FC2001` | [Verified firmware] |
| `$F8D626` | selects ES5506 high page, installs callback `$F8D8F8`, reads sample/pitch/address fields from object `A3` and voice `A4` | V3.50 caller `$007EC0` | voice + sample object | [Verified firmware] |
| `$F8D8F8` | IRQV/event callback: reselects PAGE, writes saved current address `+$C6`, updates control bits | stored at `+$26` by `$F8D626` | per-voice callback | [Verified firmware] |
| `$F8DD94-$F8DDF2` | maps four fixed control objects `$1368/$13B0/$13F8/$1440` to four records `$8288/$8360/$8438/$8510` depending object state `+$1A` | slot `$8EB2` | control object `A2`, voice record `A4`, object backpointer `+$2C` | [Verified firmware] |
| `$F8DF3E-$F8DFF8` | maps two control objects `$12D8/$1320` to records `$80D8/$81B0`, installs callback `$F8E160`, sets sample base `+$22` | direct ROM path | control object + voice record | [Verified firmware] |
| `$F8E160` | callback/service routine that advances a runtime object table and writes ES5506 offsets `$10/$08` | stored at `+$26` | control object -> voice -> ES5506 | [Verified firmware] |

## Voice record layout

[Verified firmware] Each runtime voice record is `$D8` bytes. The table below
only names fields with concrete observed reads/writes. Semantics stay `[OPEN]`
when the code proves access but not meaning.

| offset | type | writers | readers | phase | classification |
|---:|---|---|---|---|---|
| `+0x00` | list next pointer | list insertion/removal routines `$F8C9xx`, `$F8CD60`, `$F8CB24` | list scans/allocation | allocation/release | [Verified firmware] doubly-linked list next |
| `+0x04` | list previous pointer | list insertion/removal routines | list removal | allocation/release | [Verified firmware] doubly-linked list previous |
| `+0x08` | word state/priority/count | `$F8C9C0`, `$F8C9F0`, `$F8CB52`, `$F8DE30`, `$F8DF8E` | `$F8CB52`, runtime callbacks | allocation/control | [Verified firmware] field, exact meaning [OPEN] |
| `+0x0B` | byte selector/channel-like value | [OPEN] | `$F8C39C` compares with event selector `D5` | event filtering | [Likely] channel/group selector |
| `+0x0C` | byte selector/owner group | [OPEN] | `$F8CA0E/$F8CA2A` compare with `D1` | group filtering | [Likely] group/program selector |
| `+0x0E` | byte runtime parameter | [OPEN] | `$F8C2F0` copies to `$0D08`; `$F8C7D4` uses in scaling | note/control setup | [OPEN] |
| `+0x0F` | byte smoothing/level parameter | `$F8D194` decrements | `$F8D184` adds to level-like value | runtime control | [OPEN] |
| `+0x10` | long pointer | `$F8CADA` sets to `A4+$B4`; other code uses object pointers | `$F8D142`, `$F8D3CE` | envelope/control | [Verified firmware] pointer, semantics [Likely] control/envelope source |
| `+0x14` | byte state | many routines set `0`, `2`, `4`, `6`, `8`, `$0C` | state tests in manager routines | lifecycle | [Verified firmware] voice manager state |
| `+0x15` | byte voice/PAGE number | `$F8CD30` initializes low byte of word at `+0x14` | all ES5506 PAGE select routines | ES5506 programming | [Verified firmware] voice index / PAGE low byte |
| `+0x16` | word owner pointer/reference | [OPEN] | `$F8C2D8` compares with `A5` | owner matching | [Likely] owner/control object reference |
| `+0x18` | word pointer to control object | `$F8DE2C`, `$F8DF88` | `$F8D3CA`, `$F8E0CA`, `$F8E160` | control/runtime object binding | [Verified firmware] |
| `+0x1E` | long instrument/sample object pointer | direct writer not located statically | `$F8CA38`, `$F8D3C6` | instrument/sample consumption | [Verified static] consumer, producer [OPEN] |
| `+0x22` | long sample-address base/offset | `$F8DFAA` in fixed-control path; normal instrument/sample producer [OPEN] | `$F8CA82`, `$F8D656`, `$F8D8xx`, `$F8E160` | sample address conversion | [Verified static] consumer and one producer path |
| `+0x26` | long callback pointer | `$F8D638`, `$F8D738`, `$F8DE36`, `$F8DF94`, `$F8E2D2+` | PB9/IRQV handler `$F8D0B4`, callback chaining | IRQ/event service | [Verified firmware] |
| `+0x2A` | long helper/output buffer pointer | `$F8CD40` initializes from `$00FF7F00` | `$F8CC1E`, `$F8DE5C`, `$F8E270`, `$F8E338+` | address/page helper data | [Verified firmware] |
| `+0x2E` | long ring next | `$F8CD96`, `$F8C92C-$F8C95C`, `$F8E2xx` helpers indirectly use buffer not record | ring operations | active/current ordering | [Verified firmware] |
| `+0x32` | long ring previous | `$F8CD9A`, `$F8C930-$F8C95C` | ring operations | active/current ordering | [Verified firmware] |
| `+0x36` | envelope/control substructure base | `$F8CA38` copies from object `A3+$26`; `$F8D1AC` updates | `$F8D1AC`, setup helpers | runtime modulation/control | [Verified firmware] substructure, exact meaning [OPEN] |
| `+0x3C` | envelope/control substructure base | `$F8CA38` copies from `A3+$52`; `$F8D1C6` updates | `$F8D1C6` | runtime modulation/control | [Verified firmware] substructure, exact meaning [OPEN] |
| `+0x42` | envelope/control substructure base/state word | `$F8CA38`, `$F8CACA`, `$F8D1E0` | `$F8C664+`, `$F8D1E0` | runtime modulation/control | [Verified firmware] substructure, exact meaning [OPEN] |
| `+0x46..+0x6A` | accumulators/tables | `$F8CA38`, `$F8D1A2-$F8D210` | `$F8D1A2-$F8D210` | envelope/control stepping | [Verified firmware] field range, exact names [OPEN] |
| `+0x6C/+0x6E/+0x7A` | words in control stepping | `$F8D202-$F8D20C`, `$F8D3E0` | runtime control routines | envelope/control | [OPEN] |
| `+0x80/+0x82` | words | `$F8D3EE`, `$F8D402` | runtime control routines | pitch/modulation-like state | [OPEN] |
| `+0x88/+0x8C/+0x90/+0x94/+0x96` | pointers/offsets | [OPEN] | `$F8D410-$F8D488` | table-driven control | [OPEN] |
| `+0xA4` | long pointer | [OPEN] | `$F8D640` | sample/pitch setup | [Verified firmware] pointer, exact owner [OPEN] |
| `+0xA8/+0xAA/+0xAC` | words | `$F8C854-$F8C87A` | `$F8D892`, runtime control | pitch/filter/modulation-like state | [OPEN] |
| `+0xB2/+0xB4` | bytes | `$F8CD38`, `$F8D142-$F8D174`, `$F8C3E8` | runtime smoothing/control | level/envelope-like state | [OPEN] |
| `+0xB6` | word | `$F8C61A`, `$F8D4E8+` consumers | `$F8EB2A` etc. | envelope/control | [OPEN] |
| `+0xBD` | byte page/bank helper | `$F8E26C`, `$F8E2AE`, `$F8E304` | segment callbacks | sample-memory page helper | [Likely] |
| `+0xBE` | long/word segment state | `$F8D846`, `$F8D870` | segment callbacks | sample segment current state | [OPEN] |
| `+0xC2` | long boundary/current segment address | `$F8E2BE`, `$F8E30E`, consumed by `$F8E3FE/$F8E498` | segment callbacks | sample segment boundary | [Likely] |
| `+0xC6` | long current ES5506 address/accumulator-like value | `$F8D6EE`, `$F8D724`, `$F8D7DE`, `$F8D8CC` | `$F8D8F8`, segment callbacks | playback event service | [Verified firmware] |
| `+0xCA` | long linked voice/object | `$F8CD3C` clears; `$F8CAB4` consumes and clears | `$F8CAB4` recursion | linked secondary voice/control | [Verified firmware] |

## Voice lifecycle model

Current state machine, using only verified transitions:

```text
init
  $F8CCF6/$F8CD22
  -> records at $8000, stride $D8
  -> list roots initialized
  -> ES5506 reset/setup through $8E6E

free/idle
  list root $0D32 or initial root $0D36
  state byte +$14 = 0

allocated/selected
  $F8CAFA chooses a record from $0D32/$0D4A/$0D3A/$0D42
  -> unlinks it
  -> may reset via $8E6E

owner-bound / event-filtered
  $F8C2CC and $F8C39C paths test owner/channel-like fields
  -> move records between list roots such as $0D3A/$0D4A/$0D4E
  -> state 4 appears in the filtered/transition state

instrument/sample prepared
  $F8CA38 consumes A4+$1E object
  -> copies object substructures
  -> may write an ES5506 address
  -> state +$14 = 6

voice prepared
  $F8CAB4 initializes control substructure and chain
  -> state +$14 = 8

programmed/running
  $F8D626 / $F8DE24 / $F8DF3E family
  -> select ES5506 PAGE
  -> set callback +$26
  -> program ES5506 page-dependent address/control registers

IRQ/event service
  PB9 handler $F8D072
  -> read IRQV
  -> A4 = $8000 + (IRQV & $1F) * $D8
  -> jsr (A4+$26)

release/reuse
  $F8C994 / $F8C9AA / $F8C9CC
  -> unlink
  -> insert into $0D32
  -> clear state
  -> reset ES5506 through $8E6E
```

[OPEN] Human-readable state names for `+$14 = 2/4/6/8/$0C` are not verified.
The numeric transitions are verified; the labels above are architectural
descriptions of the observed transition roles.

## Instrument ownership

[OPEN] No top-level loaded instrument root/table is localized in this pass.

Current positive evidence:

- [Verified static] `$F8CA38` consumes `A4+$1E` as `A3`, copies
  instrument/sample substructures into the voice record, and is called by V3.50
  `$F13F5E`.
- [Verified static] `$F8C492` indexes a table at `$14AC` by `D5`, follows
  pointers, derives
  key-like and layer-like runtime state in `$0D10/$0D18/$0D1A/$0D1C`, and
  uses object fields such as `+$24`, `+$36`, `+$40`, `+$42`.
- [Verified static] `$F8C412` tests bitmasks in `$0D18`, iterates
  layer-like candidates, and
  calls binding slots `$8DFE` and `$8EFA` depending on candidate fields.
- [Verified static] `$F8D626` consumes a caller-provided `A3`
  sample/instrument object and fields
  `A3+$F8`, `+$100`, `+$108`, `+$118`, `+$11A`.
- [Verified static] `$F8E862-$F8E990` reads/writes sample-object address
  fields at `A2+$F0`,
  `+$F8`, `+$100`, `+$108`, and maps them against `$0C4E/$0C52`.

Current boundary:

```text
instrument/layer/sample object exists
  -> voice record gets A4+$1E and/or caller provides A3
  -> voice manager consumes that object
  -> OTTO is programmed
```

The producer of `A4+$1E`, the loaded instrument slot table, and the top-level
runtime instrument list remain `[OPEN]`.

### Producer-side negative evidence for `A4+$1E`

[Verified static] Targeted static searching did not locate a direct
voice-record write to `($1e,A4)`. This is negative evidence for the current
static search, not proof that the field is never written.

The following nearby `+$1E` hits are not the voice-record producer:

| address | operation | why it is not the voice `A4+$1E` producer |
|---|---|---|
| `$F90232/$F90236` | copies and updates `($1e,A1)` through `A3/A5` | operates on another linked/scheduler-style structure, not `$8000 + voice*$D8` with base `A4` |
| `$F902D2` | `lea ($1e,A4),A2`, then word-list traversal | uses `+$1E` as a word-list head in a different object family |
| `$F96510` | `clr.l ($1e,A1)` | clears another object record rooted through `A1`, not a voice record based at `A4` |

### Analysis method / open search space

[OPEN] `A4+$1E` can still be produced by code patterns that a narrow
`move.l ...,($1e,A4)` search would miss. Future analysis should check for:

- whole-record copies
- block-copy loops
- MOVEM/copy loops
- temporary object construction followed by insertion
- register-indirect pointer assignment after address arithmetic
- voice records built under another base register before list insertion

These are search-method constraints, not verified firmware behavior.

## Sample ownership and sample RAM

[Verified firmware] Voice code consumes sample-address-like fields:

- `A3+$F8`, `A3+$100`, `A3+$108` via MOVEP long reads in `$F8D626` and helper
  routines.
- `A3+$F0`, `+$F8`, `+$100`, `+$108`, `+$10C` through the `$F8E894-$F8E990`
  address helper family.
- Voice base `A4+$22`, added before ES5506 writes.
- Global address constants `$0D6A/$0D6E/$0D72/$0D76`, derived from `$0C52`.

### Sample object field producers

[Verified static] The current binding table maps sample-object field writer
slots as follows:

| binding slot | target | verified write |
|---|---|---|
| `$8FE4` | `$F8E93C` | normalizes `D0`, then writes `A2+$F0` |
| `$8FF0` | `$F8E948` | normalizes `D0`, then writes `A2+$F8` |
| `$8FFC` | `$F8E954` | normalizes `D0`, then writes `A2+$100` |
| `$9008` | `$F8E960` | normalizes `D0`, then writes `A2+$108` |
| `$9014` | `$F8E96A` | packs bits into `A2+$108` |

[Verified static] `$F8E984/$F8E98C` are address-normalization helpers used by
that writer family:

```text
D0 = D0 - A2 - $120
clear bit 0
```

No stronger semantic names are assigned to `A2+$F0`, `+$F8`, `+$100` or
`+$108` here.

[Likely] `$F8E6BE-$F8EA56` is a sample/object address-manager family. It uses globals
`$0BF0/$0BF4/$0BF8/$0BFC/$0C04/$0C08/$0C0C/$0C10/$0C14/$0C18`, bounds
`$0C4E/$0C52`, and helpers `$8F72/$8FA8/$903E` to translate, scan or validate
object/sample addresses. This is not yet enough to call it a verified sample
RAM allocator.

[OPEN] The following remain unverified for `$F8E6BE-$F8EA56` and the adjacent
sample/object helper family:

- allocator semantics
- free-list semantics
- ownership semantics
- physical sample RAM writer
- sample byte destination

[OPEN] The exact owner that writes sample bytes into sample RAM is not
identified. The path:

```text
disk
  -> loader buffer
  -> sample destination
  -> sample RAM
  -> sample object address fields
  -> voice A4+$22 / A3+$F8+$100+$108
  -> ES5506 START/END/control-style writes
```

is only verified from the `sample object address fields` step downstream.
Everything upstream of those fields remains `[OPEN]` for the current observed
instrument-load path.

### Recording path is separate

The `[OPEN]` load-path statements above do not apply to the newly traced
recording writer. For sampling, `$12D8/$1320` point to objects
`$F76400/$F74900`; completed SCC descriptor ranges are stored relative to those
objects and submitted as type `$0E` to target `$14DA`. `$00B478` programs
MC68302 IDMA from `object+range_start` to the destination at object `+$20`, and
completion `$00AA48` advances `+$20` by the byte count.

This recording bus master is `[Verified firmware]`. The current LEFT runtime
prepares `+$20=$02C110`, but no RX completion occurred and the physical
sound-memory decode for that address remains `[OPEN]`. See
`../investigations/scc-rx-source-and-consumer.md`.

### `A4+$22` producer status

[Verified static] `$F8DF5C/$F8DFAA` contains an actual voice-record write:

```text
$F8DFAA  move.l D3,($22,A4)
```

This path maps the fixed control objects `$12D8/$1320` to fixed voice records
`$80D8/$81B0`, installs callback `$F8E160`, and derives `A4+$22` from the
control object pointer. It is a verified fixed-control producer, not proof that
all normal instrument/sample voices get `+$22` from the same routine.

[OPEN] The normal instrument/sample voice producer for `A4+$22` remains
unlocalized.

## Producer-side lifecycle

Current producer-side boundary model:

```text
sample/object arena/state
  | [Likely] $F8E6BE-$F8EA56 address-manager family
  v
sample object address fields produced
  | [Verified static] $8FE4/$8FF0/$8FFC/$9008/$9014 writer slots
  v
instrument/layer candidate selection
  | [Verified static] $F8C492 uses $14AC[D5]; $F8C412 consumes $0D18
  v
voice allocated
  | [Verified firmware] ROM voice manager at $8000
  v
instrument/sample object attached to voice +$1E
  | [OPEN] direct/indirect producer not located statically
  v
sample base/address state becomes available at +$22
  | [Verified static] fixed-control producer $F8DFAA
  | [OPEN] normal instrument/sample producer
  v
voice preparation/programming
  | [Verified firmware] $F8CA38/$F8CAB4/$F8D626/$F8E160
  v
ES5506 runtime
  | [Verified firmware] PAGE/MOVEP host writes through $FC2001
  v
IRQ/event service
  | [Verified firmware] PB9/IRQV handler dispatches A4+$26
  v
release/reuse
  | [Verified firmware] $F8C994/$F8C9AA/$F8C9CC
```

## ES5506 role

Current model:

```text
firmware builds voice records
firmware installs per-voice callbacks
firmware programs OTTO through $FC2001
ES5506 fetches samples autonomously
ES5506 raises voice events through IRQV/IRQB semantics
firmware services events through PB9/vector $47
```

Status:

- `[Verified silicon spec]` ES5506 is a 32-voice autonomous sample playback
  engine with PAGE, IRQV and IRQB.
- `[Verified firmware]` ASR-10 firmware builds and owns the `$8000` voice table.
- `[Verified firmware]` ASR-10 firmware programs ES5506 host registers from
  voice records and sample object fields.
- `[Verified firmware]` PB9 handler reads IRQV and dispatches a voice callback.
- `[OPEN]` Physical ES5506 `IRQB` to PB9 wiring is not board-verified.

[DISPROVEN] There is no evidence in the identified runtime chain that ES5506
owns the firmware voice records. ES5506 is the programmed playback engine; the
record lifecycle is firmware-owned.

## Architecture diagram

```text
Storage subsystem
  | [Verified firmware] $0402, vector $4B, vector $51
  v
Sample bytes / loader buffer
  | [OPEN] current observed instrument-load path stalls after RECALIBRATE start,
  |        before RECALIBRATE completion, SEEK, async READ DATA and IDMA start
  v
Sample byte destination / RAM writer
  | [OPEN] exact sample RAM writer/destination
  v
Sample/object address manager
  | [Likely] $F8E6BE-$F8EA56 address-manager family
  v
Sample object fields
  | [Verified static] A2+$F0/$F8/$100/$108 writer slots
  v
Instrument/layer selector near $14AC
  | [Likely/OPEN] selector/root/cache role not established
  v
Voice +$1E installation
  | [OPEN] direct/indirect producer not located statically
  v
Voice manager
  | [Verified firmware] ROM-owned $8000 table, 32 x $D8
  | [Verified firmware] allocation/release/list roots $0D32-$0D52
  v
OTTO driver layer
  | [Verified firmware] PAGE select and MOVEP writes through $FC2001
  v
ES5506 / OTTO
  | [Verified silicon spec] autonomous sample fetch/playback
  v
Voice event service
  | [Verified firmware] PB9 handler reads IRQV and dispatches A4+$26
  | [OPEN] physical IRQB -> PB9 routing
  v
Voice manager
```

## Stronger models after this pass

- [Verified firmware] The `$8000` voice table is owned by a ROM voice manager,
  not by ES5506 silicon.
- [Verified firmware] The voice manager has a real lifecycle: init, list
  allocation, preparation, ES5506 programming, IRQ/event callback and release.
- [Verified firmware] `+$26` is a per-voice callback pointer, not just an
  incidental function pointer.
- [Verified static] `+$22` is consumed as a sample-address base before ES5506
  address writes, and `$F8DFAA` is one verified fixed-control producer.
- [Verified static] Sample-object address fields at `A2+$F0/$F8/$100/$108`
  have binding-slot writer routines.
- [OPEN] The current instrument-load request has not yet been bridged to the
  sample manager, instrument selector or voice manager. Direct load-time voice
  programming remains unverified.

## Remaining open questions

- [OPEN] Top-level loaded instrument table/root and slot ownership.
- [OPEN] Direct or indirect producer of voice field `A4+$1E`.
- [OPEN] Ownership and meaning of `$14AC`.
- [OPEN] Normal instrument/sample producer of voice field `A4+$22`.
- [OPEN] Full key/MIDI/note event -> instrument -> layer -> voice allocation
  chain.
- [OPEN] Exact semantic names for many voice fields, especially control and
  envelope substructures.
- [OPEN] Storage bytes -> sample RAM path.
- [OPEN] Sample RAM destination/write path and physical sample RAM mapping.
- [OPEN] Sample allocator/free-list/reference-counting/ownership semantics, if
  any.
- [OPEN] Whether any ES5506 global state is programmed during load.
- [OPEN] Runtime proof that PB9/IRQV fires during normal audible playback.
- [OPEN] ES5510/effect participation in loaded instrument runtime behavior.
