# RX event trace through the runtime receive path

Scope: V3.50, `FILE 1  TUTORIAL BNK`, no `-log`, no `mem_map` change, no CSV edits.
Instrumentation was gated by `ASR10_RX_EVENT_TRACE=1` and reused the existing
`ASR10_PANEL_SWEEP_RAW` injection point.

Previous runtime-architecture changes were committed first as
`075236af9b5 asr10: document runtime architecture profile`.

## Dispatch table at `$F884BE`

`$F884BE` reads DUART ISR from `$FFFC480B` and dispatches by bits:

| condition | instruction form | target source | runtime target |
|---|---|---|---|
| ISR bit 5, RxRDYB | `movea.l $00de.w,A0; jmp (A0)` | longword at `$0000DE` | `$FFB0BC` |
| ISR bit 1 or bit 2, channel A status group | `movea.l $00e2.w,A0; jmp (A0)` | longword at `$0000E2` | `$F88968` |
| ISR bit 0, TxRDYA | `movea.l $00e6.w,A0; jmp (A0)` | longword at `$0000E6` | `$F88554` |
| ISR bit 3, counter ready | `jmp $8638.w` | absolute short, sign-extended | `$FF8638` |
| none of the above | `moveq #-$6f,D0; trap #0` | immediate error path | ERROR 145 path |

[OPEN] Candidate for the first runtime-observed binding-slot call:
`$F884BE -> $8638.w -> $FF8638`. This pass observed repeated PC hits at `$FF8638`
after counter-ready IRQ6 dispatches, but no CSV edge was written.

## Scheduler table at `FILE 1`

The slot bounds read at the `FILE 1` precheck were:

```text
$0000C6 = $23F6
$0000C8 = $247A
span    = $0084
stride  = $0016, from `$F87FC2 adda.w #$16,A2`
slots   = 6, exact division
```

Pre-injection snapshot:

| slot | base | `+2` | `+3` | ready | words |
|---:|---:|---:|---:|---:|---|
| 0 | `$23F6` | `$00` | `$00` | no | `0000 0000 0000 0000 c7c8 0004 0000 1e20 0000 0000 0000` |
| 1 | `$240C` | `$80` | `$80` | no | `0000 8080 0000 ffff c8b0 0009 0000 1e84 0000 0000 0000` |
| 2 | `$2422` | `$80` | `$80` | no | `0000 8080 0000 0000 738e 0000 0000 1f14 0000 0000 0000` |
| 3 | `$2438` | `$80` | `$80` | no | `0000 8080 0000 fff8 f2fa 0008 0000 1fc8 0000 0000 0000` |
| 4 | `$244E` | `$00` | `$01` | yes | `0000 0001 0000 0000 69bc 0004 0000 1fec 0000 0000 0000` |
| 5 | `$2464` | `$00` | `$01` | yes | `005a 0001 0000 0000 780c 0000 0000 2068 0000 0000 005e` |

Observed slot activity during the `$22` run:

| slot | ready samples | writes | last writer |
|---:|---:|---:|---:|
| 0 | 1 | 32 | `$F87F8A` |
| 1 | 3 | 59 | `$F87F8A` |
| 2 | 0 | 1 | `$F8CE3A` |
| 4 | 6159 | 76269 | `$F88312` |
| 5 | 12397 | 130955 | `$F87F8A` |

`rte` from non-empty slots was observed. Examples:

```text
slot 5 -> SR $0000, PC $00780C
slot 4 -> SR $0004, PC $0069CC / $006876
```

These PCs are in the low-RAM view. The trace does not prove what semantic work those
tasks perform.

## Low-memory dump

The instrument emitted `$000000-$0003FF`, but the retained filtered log contains only
`$000000-$0000FF`; the full dump was lost when the first two full stdout captures were
truncated, and the third run was filtered too narrowly. The pass limit was already
reached, so this was not rerun.

Conservative classification of the retained `$000000-$0000FF`:

1. Known exception vectors:
   - `$000000-$000003`: initial SP/SSP value `$00000300`
   - `$000004-$000007`: reset PC `$FFF8000C`
   - `$000078-$00007B`: autovector 30 target `$FFF882DA`
2. Observed reads/writes or known runtime fields:
   - `$0000C6 = $23F6`, scheduler slot-table base
   - `$0000C8 = $247A`, scheduler slot-table end
   - `$0000DE = $FFFFB0BC`, runtime receive path RxRDYB handler pointer
   - `$0000E2 = $FFF88968`, channel A status-group handler pointer
   - `$0000E6 = $FFF88554`, TxRDYA handler pointer
3. Recurring structures:
   - `$000030-$00007F` is dominated by repeated `$FFF882DA` vector-stub pointers.
   - `$000080-$0000BB` contains a dense sequence of ROM service pointers.
   - `$0000C0-$0000EF` contains scheduler fields and DUART handler pointers mixed with
     ordinary vector-like longwords.
4. Unclassified:
   - Everything in `$000000-$0000FF` not listed above.
   - `$000100-$0003FF` was not retained and is therefore unclassified by this pass.

## `$22` chain

Retained chain:

```text
16.171608375  precheck: display "FILE 1  TUTORIAL BNK"
16.271608375  inject raw $22 at PC $F87F96
16.271608375  push: FIFO 0 -> 1, irq_pending=1
16.271609625  IRQ6 accept: ISR $20, vector $56, target $F884BE
16.271619625  dispatch: RxRDYB bit 5 -> lowmem pointer `$0000DE` -> `$FFB0BC`
16.271620625  `$FFB0BC` reads SRB `$FC4813`, value `$01`
16.271623500  `$FFB0D4` reads RHRB `$FC4817`, value `$22`, FIFO 1 -> 0, irq_pending=0
16.271802000  next IRQ6: ISR $08, counter-ready
16.271814625  dispatch: counter-ready bit 3 -> `$8638.w` -> `$FF8638`
16.271815250  PC marker: `$FF8638`
16.271901375  scheduler RTE: slot 5 -> PC `$00780C`
16.272899500  scheduler RTE: slot 4 -> PC `$0069CC`
```

The `$22` trace therefore reaches the runtime receive path and consumes the byte. The
visible display effect was not established: no retained `display_after` event fired, and
the exit-time `m_panel_text` accumulator was empty, so it is not a reliable final panel
state.

## `$23` chain

The `$23` run completed and injected one byte. Its retained summary matched the `$22`
run at the coarse counters:

```text
injected=1
slot_base=$23F6
slot_end=$247A
slot_count=6
fc4813_srb_reads=13
fc4817_rhrb_reads=13
queue_calls=13
fifo_overrun_pushes=0
```

The detailed injection window for `$23` was not retained because the unfiltered stdout
capture was truncated before the middle of the run. The third permitted run was used to
recover `$22`; rerunning `$23` would exceed the pass limit. The `$23` chain therefore
breaks at retained evidence: push/injection is known only from summary counters, not from
a timestamped link-by-link chain.

## Open items from this pass

- Full `$000000-$0003FF` classification was not completed because only `$000000-$0000FF`
  survived in the filtered log.
- `$23` was not reconstructed link by link.
- The first `$22` display-visible effect was not measured.
- No `static/*.csv` file was edited.
