# ASR-10 Architecture Handoff

## Scope

This document is a compact handoff for the current ASR-10 architecture phase.
It summarizes the strongest current model, the runtime boundary, and the next
implementation target. It is not a complete reverse-engineering log; detailed
evidence remains in the subsystem reference documents.

Evidence labels used here:

- [Verified runtime] observed in the running V3.50 system.
- [Verified static] derived from ROM/OS disassembly, binding tables, call graph,
  and writer/reader analysis.
- [Verified device] derived from the MAME device model.
- [Verified chip] derived from chip documentation.
- [Verified firmware] firmware semantics independent of physical board wiring.
- [Likely] supported interpretation, not yet a hard fact.
- [OPEN] not yet established.
- [DISPROVEN] an earlier hypothesis contradicted by current evidence.
- [Architectural interpretation] a model/analogy used to explain the verified
  mechanisms, not Ensoniq terminology or origin evidence.

## Where We Are

[Verified runtime/static] The observed `LOADING JM DIGI SYN` path reaches:

```text
high-address OS producer
 -> generic service node
 -> trap #12 immediate dispatch
 -> target $14DA
 -> storage entry $00B08C
 -> request class/subtype $03/$02
 -> payload $02B600 -> $046A
 -> FDC RECALIBRATE 07 00
 -> current stall
```

The observed state at the stall is:

```text
node        = $1504
node +2     = $0302
node +4     = $0002B600
$0466       = $1504
$046A       = $0002B600
$043E       = $00000000
```

[Verified runtime] Runtime has not reached RECALIBRATE completion, SEEK, READ
DATA, IDMA programming, common storage exit, scheduler resume, or `$043E`
activation.

Not reached at runtime:

- RECALIBRATE completion
- vector `$51`
- SEEK
- READ DATA `$46`
- IDMA setup
- vector `$4B`
- later vector `$51`
- common exit
- `$23F6` resume
- later request class
- `$043E`

No further post-RECALIBRATE runtime conclusions should be promoted from static
to runtime evidence until IRQ1/vector `$51` delivery is modeled.

[Verified static] The class `$03/$02` path does continue beyond this point if
the missing completion is delivered:

```text
RECALIBRATE
 -> vector $51
 -> SEEK
 -> vector $51
 -> class $03 dispatch
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
 -> vector $4B
 -> vector $51
 -> common exit
```

## Stable Architectural Conclusions

The following conclusions are considered stable and should not be reopened
without contradictory evidence:

- [Verified static] V3.50 uses a generic service-node mechanism.
- [Verified static] `trap #3` allocates service nodes and `trap #4` returns
  them to the free-list.
- [Verified static] `trap #9` posts to scheduler/service pending/deferred
  state; it is not the same queue that `$F8822C` drains.
- [Verified static/runtime] `trap #12` has immediate/service behavior, and the
  observed instrument-load request uses that immediate path.
- [Verified static] `trap #13` participates in queue-style service behavior,
  within the limits documented in `runtime-service-model.md`.
- [Verified static] Target/service records and scheduler slots are separate
  low-RAM runtime structures with known overlapping but distinct roles.
- [Verified static] The scheduler slot model includes saved PC, SR, USP, an A5
  node mailbox, and pending/deferred node state.
- [Verified runtime/static] The observed storage request lifecycle is:
  service-node envelope -> class/subtype -> payload -> storage entry ->
  device/transfer state.
- [Verified runtime/static] `$049A.b` and `$049B.b` are class/subtype bytes for
  the observed request; `$0302` is not one simple opcode.
- [Verified runtime/static] `$0466` is the current request node and `$046A` is
  the accepted current payload pointer for the observed request.
- [Verified static] Class `$03` has a path toward FDC READ DATA `$46` and MC68302
  IDMA setup.
- [Verified static] Vector `$4B` is the IDMA completion path.
- [Verified firmware] Vector `$51` is the shared storage-device completion
  ingress for the firmware-side FDC/SCSI status dispatcher.
- [DISPROVEN] `$043E` is not verified as an instrument object.
- [OPEN] Storage descriptor, sample manager, instrument selector/root and voice
  manager worlds must remain separate until pointer/dataflow evidence bridges
  them.

## Runtime Service Kernel

[Verified static] V3.50 uses generic service nodes and target/service records:

- `trap #3` at `$F8807C` allocates a service node from free-list `$0B6C`.
- `trap #4` at `$F880A2` returns a service node to the free-list.
- `trap #9` at `$F88138` posts a node to a target/service record using the
  target's pending/deferred fields.
- `trap #12` has an immediate path and a queue path; the observed instrument
  load uses the immediate path.
- `trap #13` participates in queue-style posting.
- `$F8822C` dequeues from the target service queue `+6/+8`, which is separate
  from the `trap #9` pending/deferred chain `+10/+12`.

[Verified runtime/static] The observed producer is `$FFA882-$FFA8AA`:

```text
trap #3
node +2/+3 <- $03/$02
node +4    <- $0002B600
A1         <- $14DA
trap #12
```

`$14DA` is the storage-related target/service record. In this concrete runtime
case `$14E0/$14E2` service-queue head/tail remain zero; the request reaches
storage through `trap #12` immediate dispatch, not ordinary `trap #9` enqueue.

## Scheduler Return

[Verified static] The primary scheduler slot table is `$23F6..$2464` with
stride `$16`.

The slot layout includes:

```text
+2/+3  scheduler state bytes
+6     saved PC
+A     saved SR
+C     node mailbox loaded into A5 on resume
+E     saved USP
+10/+12 pending/deferred node chain
+14    timer/state
```

[Verified static] Storage common exit returns the saved request node from
`$0466` and selects a scheduler return context from the sign of `node +2`:

```text
$B2E6  A5 <- $0466
$B304  tst.w (A5+$2)
$B30A  positive -> A1=$23F6 ; trap #9
$B312  negative -> A1=$2438 ; trap #9
```

[Verified runtime] The observed node has `node +2 = $0302`, so the expected
static return context for this request is `$23F6`, not `$2438`.

[Verified static] `$23F6` is scheduler slot 0. `$2438` is scheduler slot 3.
Both use the same scheduler-slot format.

[OPEN] `$23F6` is not a verified instrument owner. The concrete post-completion
consumer is still unknown because current runtime never reaches storage
completion.

## Storage Completion Boundary

[Verified device] The uPD72069 MAME device completes successful RECALIBRATE
`07 00` by setting interrupt state and driving its INTRQ callback. This is an
INTRQ completion path, not DRQ.

[Verified firmware] The firmware completion path for this RECALIBRATE state is:

```text
external IRQ1
 -> MC68302 vector $51
 -> $F114B6
 -> FDC branch $FB7E8E
 -> SENSE INTERRUPT STATUS 08
 -> $0402 continuation
 -> $BA5E
 -> SEEK 0F 00 01
```

[Verified chip] MC68302 external IRQ1 delivers CPU level 1 and, with
`GIMR=$8040` / `IV1=0`, IACK supplies vector `$51`.

[OPEN] The ASR-10 board routing that carries storage-device completion to
MC68302 external IRQ1 remains unverified:

```text
storage device completion
 -> [ASR-10 board policy / physical routing OPEN]
 -> MC68302 external IRQ1
 -> vector $51
```

Do not record "FDC INTRQ is physically wired directly to IRQ1" as verified
fact.

## Known Runtime Blind Spot

[Verified device] The uPD72069 device-side RECALIBRATE completion exists.

[Verified firmware] The firmware-side vector `$51` RECALIBRATE continuation
exists.

[Verified chip] MC68302 external IRQ1/vector `$51` semantics are known.

[OPEN] What is missing in the current machine model is the board/machine-level
delivery contract connecting a storage completion source to MC68302 external
IRQ1. Physical board wiring remains [OPEN]. This is the primary current
runtime blocker.

## IDMA And READ DATA

[Verified dynamic] The class `$03` READ path programs MC68302 IDMA before
issuing FDC READ DATA `$46` — now runtime-measured, not just statically
predicted; full register-by-register trace and derivation in
`../investigations/idma-register-map-probe.md`.

Known IDMA dataflow:

```text
CMR (`$FC6802`)  prelude value $0002 (every vector $51 IACK), then $0D51
                 immediately before READ DATA (the actual channel start)
SAPR (`$FC6804`) $FFFC5803 -- matches the earlier static prediction exactly
DAPR (`$FC6808`) $00000944 -- [Verified dynamic] corrects the earlier
                 static prediction of $040E; measurement wins per project
                 rule, see idma-register-map-probe.md
BCR (`$FC680C`)  $0201 (513) -- interpretation open, not resolved
FCR (`$FC6810`)  $99 -- bit-field decode open, not resolved
CSR (`$FC680E`)  reads $00 every time; no IDMA event has ever occurred
```

`$4B` is the MC68302 IDMA completion path. `$51` is the shared storage-device
completion/status entry. These are separate completion paths.

## Payload And Runtime Objects

[Verified runtime/static]

```text
A2=$02B600
 -> node +4
 -> storage entry
 -> $046A=$02B600
```

[Verified runtime] `$043E` remains zero at the current stall.

[Verified static] `$043E` activation exists in at least:

- class `$06` path at `$B8FE`
- class `$0D` path at `$B33C`

[OPEN] There is no verified edge from the current class `$03` completion to
class `$06` or class `$0D`.

Best current classification: `$043E` is a current storage payload/runtime
descriptor pointer. It is not verified as an instrument object.

## Downstream Boundaries

These worlds are separately verified but not yet bridged for the current
instrument-load path:

- storage descriptor world: `$046A`, `$043E`
- sample/object address-manager family: `$F8E6BE-$F8EA56`
- sample-object fields: `+$F0`, `+$F8`, `+$100`, `+$108`, `+$118`, `+$11A`
- instrument selector/root-adjacent world: `$14AC`, `$0D10`, `$0D18`, `$0D1A`,
  `$0D1C`, `$F8C492`, `$F8C412`
- voice manager world: `$8000` table, stride `$D8`, voice `+$1E`, voice `+$22`,
  callback `+$26`, `$F8CA38`

[OPEN] No verified pointer/dataflow bridge yet binds storage payload/`$043E` to
sample object, instrument selector/root, or voice attachment for the observed
load.

## Audio Separation

[Verified silicon spec / firmware mapping] The audio-side chips remain separate
from the storage IRQ boundary:

- ES5701 / Super-GLU: audio and sound-memory glue.
- ES5506 / OTTO: voice/sample playback engine.
- ES5510 / ESP: effects/DSP engine.
- FDC/SCSI/storage: separate storage subsystem.

[DISPROVEN] ES5701 should not be described as verified storage IRQ glue.

## Methodology

The current model was built from:

1. ROM/OS disassembly.
2. high/low/binding address alias handling.
3. binding-table resolution.
4. static call graph.
5. vector mapping.
6. writer/reader matrices for low-RAM state.
7. MAME device implementation as device-behavior reference.
8. chip manuals/specs.
9. targeted Lua runtime observations.
10. comparison of static and dynamic evidence.
11. negative evidence.
12. explicit separation between firmware semantics, chip semantics, device
    model, board wiring, and architectural interpretation.

Important corrected hypotheses:

- `$0402` first appeared FDC-specific; it is now verified as a general async
  device/storage continuation pointer.
- `$F114B6` first appeared FDC-specific; it is now verified as shared FDC/SCSI
  storage completion firmware.
- `$0302` first appeared to be one storage opcode; it is now documented as
  class/subtype `$03/$02`.
- IDMA first appeared not to participate in the observed load; class `$03` now
  has a verified static READ/IDMA path, although current runtime has not reached
  it.
- `$14DA` first looked like an ordinary `trap #9` target queue; the observed
  load uses `trap #12` immediate dispatch.
- `$2438` was temporarily treated as the observed return context; observed
  `node +2=$0302` corrects this to `$23F6`.
- `$043E` has been kept OPEN and should not be promoted to "instrument object".

## Reverse-Engineering Principles

- Observation before inference.
- Runtime evidence overrides static assumptions.
- Negative evidence is evidence.
- Preserve OPEN states instead of filling gaps.
- Emulate causes, not observed effects.
- Do not merge state machines without evidence.
- Do not invent object ownership.
- Implementation follows the verified architecture.
- Physical wiring, chip semantics and firmware semantics are separate evidence
  domains.

## Architectural Interpretation

[Architectural interpretation] The verified mechanisms are well described as
an event-driven message-passing firmware built around service nodes,
asynchronous state machines, cooperative scheduling, and subsystem ownership.

Useful analogies:

- event-driven service kernel
- message passing
- deferred execution
- Active Object-like service targets
- Actor-like subsystem communication
- cooperative scheduler
- Amiga Exec Message/MsgPort-style lifecycle
- modern work queues, continuations, futures, and event loops

These are analogies only. They are not Ensoniq terminology, historical-origin
claims, API equivalence, or evidence of modern actor guarantees such as one
thread per actor.

The design expresses modern architectural ideas with the low-level mechanisms
available and appropriate for an early-1990s embedded 68000 system: untyped
payload pointers, low-RAM globals, numeric classes/subtypes, manually managed
node pools, and explicit continuations.

## Boot And Runtime

[Architectural interpretation] Boot is best treated as bootstrapping of the
runtime/service environment, not as a fully separate architecture.

Boot initializes hardware, vectors, bindings, scheduler slots, service records,
and RAM/OS state. Runtime reuses the same traps, target records, scheduler
slots, bindings, interrupts, continuations, and device state machines.

## Context Switch Status

[Verified static] Scheduler slot resume stores/restores at least:

- saved PC
- saved SR
- saved USP
- A5 mailbox semantics

[Verified static] Context-restore trampoline paths exist.

[OPEN] Full autonomous `D0-D7/A0-A7` task-context semantics across all scheduler
slot paths remain unverified. Do not document a general full context switch yet.

## Implementation Readiness

Ready / sufficiently modeled:

- generic service-node primitives
- scheduler-slot layout and basic dispatch
- uPD72069 device completion behavior
- MC68302 external IRQ1 chip semantics
- vector `$51` firmware entry
- class `$03` static RECALIBRATE -> SEEK -> READ DATA -> IDMA path
- IDMA/vector `$4B` semantic split

Ready with explicit board-policy assumption:

- storage completion -> MC68302 external IRQ1: chip-level vector supply
  (`mc68302_device::irq1_ack_vector()`) and CPU-space IACK dispatch are
  implemented and verified. The naive unconditional policy — FDC INTRQ
  wired directly to CPU IPL1 at all times — was implemented, regression
  tested, and disproven: it breaks plain boot-to-FILE1 with `ERROR 129 -
  REBOOT`, a genuine 68000 Address Error exception (**corrected**: not a
  firmware-level detection as earlier documents in this project described
  it). Root cause fully measured in
  `../investigations/irq1-handler-chain-probe.md` and
  `../investigations/ready-line-artifact-probe.md`: the interrupt
  delivered was a ready-line-change artifact of MAME's FDC model, not a
  storage completion, crashing a dispatcher that dereferences an
  uninitialized continuation pointer. With the ready-line artifact fixed
  (`set_ready_line_connected(false)`, empirically safe — 5/5 regression
  including `nodisk`) alongside the same IRQ1 wiring, boot survives and the
  instrument-load completion chain runs correctly through RECALIBRATE,
  SEEK, and READ DATA, stopping at a legitimate `DISK ERROR - LOST DATA`
  (uPD765 overrun) because MC68302 IDMA is not implemented. Neither change
  is landed; both were reverted after measurement. See
  `../investigations/irq1-storage-completion-probe.md` for the original
  attempt (interpretation corrected there) and
  `../investigations/ready-line-artifact-probe.md` for the resolution.

Not ready / still OPEN:

- concrete post-class-`$03` `$23F6` consumer
- later request class transition
- `$043E` runtime activation
- storage -> sample/instrument bridge
- sample byte final destination
- full scheduler context semantics
- physical board IRQ wiring

## Next Implementation Target

Implement the smallest generic MC68302 external IRQ1/vector-`$51` path needed
to allow the already verified firmware completion chain to execute.

Generic MC68302 requirements:

- external IRQ1 input/state
- CPU level-1 assertion
- level-1 IACK
- `GIMR`/`IV1`-derived vector `$51`
- clean source assertion/deassertion contract

ASR-10 board-side policy:

- connect storage completion policy to MC68302 external IRQ1
- keep physical wiring documented as [OPEN] / explicit board policy
- do not encode FDC-specific firmware knowledge into the generic MC68302 model

## Recommended Implementation Order

1. Generic MC68302 external IRQ1.
2. ASR-10 storage IRQ board policy.
3. Runtime validation through RECALIBRATE and SEEK.
4. Runtime validation of class `$03` READ/IDMA.
5. Observe `$23F6` owner/resume behavior.
6. Identify the next request class.
7. Validate `$043E` activation.
8. Only then continue the storage -> sample/instrument bridge.

Scheduler/service primitives are already modeled well enough architecturally for
the IRQ1 experiment; this is not a requirement to reimplement the whole service
kernel before IRQ1.

## Post-Implementation Validation Plan

After IRQ1 implementation, repeat the `LOADING JM DIGI SYN` experiment and
verify in order:

1. RECALIBRATE completion
2. vector `$51` entry
3. SENSE INTERRUPT STATUS
4. `$BA5E` continuation
5. SEEK `0F 00 01`
6. next vector `$51`
7. class `$03` dispatch
8. `$FB9C5E/$FB9FE2`
9. `$FB84DA/$FB85C0`
10. IDMA register programming
11. FDC READ DATA `$46`
12. vector `$4B`
13. later vector `$51`
14. common exit
15. actual `$23F6` scheduler state
16. concrete owner consumer
17. next request class
18. whether payload `$02B600` survives to `$043E`

This is a validation plan, not an implementation design.

## Critical OPEN

Critical next:

- physical/board policy for storage IRQ1
- runtime validation through first vector `$51`
- concrete `$23F6` post-completion consumer

Next architecture:

- next request class after `$03`
- `$043E` activation
- storage -> sample/instrument bridge

Later:

- exact scheduler full-context semantics
- semantic names of descriptor fields
- UI-visible / voice-ready load boundary
- physical IRQ glue/wiring

## Documentation Inventory

Current core references:

- [`current-status.md`](../current-status.md): compact project status and
  nearest blocker.
- [`runtime-service-model.md`](runtime-service-model.md): service nodes, traps,
  target records, scheduler slots, and architectural interpretation.
- [`runtime-object-model.md`](runtime-object-model.md): storage request/payload
  hierarchy and current runtime-object boundaries.
- [`storage-completion-dispatch.md`](storage-completion-dispatch.md): `$0402`,
  vector `$4B`, vector `$51`, FDC and SCSI completion dispatch.
- [`instrument-to-otto-runtime.md`](instrument-to-otto-runtime.md): downstream
  instrument/sample/voice-to-OTTO path.
- [`audio-storage-architecture.md`](audio-storage-architecture.md):
  storage/audio boundary and Ensoniq audio-chip roles.
- [`vector-map.md`](vector-map.md): vector origins and runtime handlers.
- [`mc68302-status.md`](mc68302-status.md): MC68302 status, including external
  IRQ1 model gap.
- [`subroutine-index.md`](subroutine-index.md): routine index for key firmware
  addresses.

Investigation documents remain useful as evidence trails, but the reference
documents above should be treated as the current model.

Known unrelated dirty state at this handoff:

- `src/devices/sound/es5506.h`
- untracked Ensoniq/ASR-10 reference PDFs under `docs/`
