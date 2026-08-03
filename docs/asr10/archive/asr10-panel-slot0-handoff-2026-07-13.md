# ASR-10 Panel/Slot0 Handoff

**Date:** 2026-07-13  
**Scope:** V161 boot, Channel B panel-output completion, slot0 wake, queued
`14f4/89a2` node.

Evidence labels:

- **[DYN]** observed in runtime logs.
- **[STAT]** verified from firmware/image/static disassembly.
- **[MODEL]** strong working model.
- **[HYP]** plausible but not proven.
- **[OPEN]** unresolved.
- **[RETRACTED]** superseded or falsified.

Read first:

- `docs/asr10/architecture.md`
- `docs/asr10/current-blocker.md`
- `docs/asr10/panel-protocol.md`

Do not rely on older archived handoffs where they conflict with these files.

---

## 1. Current Objective

**[DYN]** The clean ASR-10 V161 boot reaches post-load dispatcher idle. Later in
the run, slot0 returns to:

```text
slot0 +02/+03 = 0202
slot0 +10/+12 = 14f4/14f4
node 14f4 +02 = 89a2
```

The node is queued but not consumed. The immediate objective is to prove the
natural panel-return traffic needed to drain the Channel B output ring, drive
`$03bc` to zero at the relevant time, clear slot0 bit1 via `f89ac2`, and then
observe whether slot0 dispatches and consumes/promotes node `14f4/89a2`.

---

## 2. Current Architecture

**[MODEL]** Current panel/slot0 path:

```text
68k firmware
-> SCN2681 DUART Channel B
-> keypad/display controller
-> Channel B return traffic
-> IRQ6 / vector 0x56
-> firmware RHRB parser
-> panel completion path
-> panel ring counter byte[$03bc]
-> possible slot0 bit1 clear at f89ac2
```

Relevant firmware path:

```text
f89a72/f89a8a  enqueue Channel B ring byte and increment byte[$03bc]
f89a9a/f89aa4  send next ring byte to THRB / FC4817
ffb22a/ffb242  Channel B RX parser reads RHRB / FC4817
ffb3e4         FF high-control completion call site
f89a9a/f89ab8  completion/send path and byte[$03bc] decrement
f89abe/f89ac2  zero-count path, bclr #1 on slot record at [$00d8]
```

---

## 3. Proven Facts

### 3.1 Diagnostic C: `71 -> 00`

**[DYN]** Diagnostic C proved the transport path works:

```text
natural THRB 71
-> injected RX 00 queued through Channel B RX FIFO
-> RxRDYB / ISR bit5
-> IRQ6
-> vector 0x56
-> normal DUART demux
-> firmware RHRB read at ffb242
-> RX FIFO pop
```

**[DYN]** From parser state `B3BA`, byte `00` is not a completion ACK. It is
treated as the first byte of a stateful message:

```text
$03c0: B3BA -> B24E
$03c4: 00
```

No `F89A9A`, `F89AB8`, `FFB3E4`, `FFB424`, or `F89AEC` completion path is reached
for this single byte.

### 3.2 Diagnostic D1: `71 -> FF`

**[DYN]** Diagnostic D1 proved that `FF` from parser state `B3BA` takes the
completion branch:

```text
natural THRB 71
-> RX FF
-> RHRB FF
-> parser state B3BA
-> ffb3e4
-> f89a9a
-> f89ab8
-> byte[$03bc]: 0e -> 0d
-> next natural THRB 7e
```

### 3.3 Diagnostic D2: `71, 7e -> FF, FF`

**[DYN]** Diagnostic D2 proved two repeated completion cycles:

```text
THRB 71 -> RX FF -> completion -> THRB 7e
THRB 7e -> RX FF -> completion -> THRB fc
```

In both cases:

```text
parser state before RX FF = B3BA
RX FF takes ffb3e4
f89a9a/f89ab8 runs
byte[$03bc] decrements by one
parser state returns to B3BA
```

### 3.4 Diagnostic E: first known ring drain

**[DYN]** Diagnostic E drained only the observed natural Channel B ring sequence:

```text
71 7e fc 74 07 74 06 74 05 74 04 74 03 74 02
```

Each cycle used the normal path:

```text
RX FIFO/state
-> RxRDYB
-> ISR bit5
-> IRQ6/vector 0x56
-> RHRB
-> parser
-> f89a9a/f89ab8
```

**[DYN]** Repeated `FF` completion probes decremented `$03bc` and naturally caused
the next THRB byte to be transmitted.

**[DYN]** `$03bc` reached zero through firmware, and `f89abe` and `f89ac2` were
reached.

**[DYN]** At that first zero crossing:

```text
slot0 state = 0000
slot0 queue head/tail = 0000/0000
node 14f4/89a2 did not yet exist
```

The wake therefore did not test the later `0202 -> 0002` transition.

**[DYN]** After that first zero crossing, a second natural panel ring was
enqueued, beginning:

```text
74 01 74 00 74 0f 74 0e 74 0d 74 0c 74 0b ...
```

Diagnostic E stopped after the first known ring. The second ring remained
undrained. Later, node `14f4/89a2` was posted and slot0 again ended at `0202`.

---

## 4. Retracted Hypotheses

**[RETRACTED]** The first panel ring directly held the later `14f4/89a2` node.

The first zero crossing happens before the later `14f4/89a2` node exists.

**[RETRACTED]** A single `00` is an ASR-10 ACK/completion.

Diagnostic C shows `00` from `B3BA` starts a stateful parser message instead of
reaching completion.

**[RETRACTED]** `FF` has been proven to be the authentic physical ASR-10 panel
response.

Current status:

```text
FF is a valid diagnostic completion stimulus accepted by the firmware parser.
It is not yet proven to be the real panel-controller response.
```

---

## 5. Existing Diagnostics and Environment Variables

The panel reply experiments are behavior-changing diagnostics and must remain
default-off. They are mutually exclusive in `machine_reset()` by priority:

```text
ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO
ASR10_EXPERIMENT_PANEL_REPLY_71_FF
ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF
ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING
```

Only the first enabled experiment in that priority order runs.

### `ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO`

**[DYN]** Diagnostic C. After natural Channel B THRB `71`, queue exactly one
Channel B RX byte `00` through the diagnostic RX FIFO path. It proved the RX
transport and parser state transition but did not decrement `$03bc`.

### `ASR10_DIAG_PANEL_C_PARSER_TRACE`

Enables the narrow parser trace after the diagnostic byte is read at `ffb242`.
It traces up to a bounded instruction count or until a completion/stop target is
reached. D1/D2/E also enable parser trace implicitly.

### `ASR10_EXPERIMENT_PANEL_REPLY_71_FF`

**[DYN]** Diagnostic D1. After natural THRB `71`, queue exactly one RX `FF`. It
proved the `B3BA -> ffb3e4 -> f89a9a/f89ab8` completion path and natural next
THRB `7e`.

### `ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF`

**[DYN]** Diagnostic D2. Preserve the first `71 -> FF` response, then respond to
the resulting natural THRB `7e` with one more RX `FF`. It proved two consecutive
completion cycles and natural next THRB `fc`.

### `ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING`

**[DYN]** Diagnostic E. Drain only the first observed natural ring:

```text
71 7e fc 74 07 74 06 74 05 74 04 74 03 74 02
```

It replies with one RX `FF` per natural ring THRB byte, only through the normal
Channel B RX/IRQ/parser path, and stops at the first zero crossing.

### Always-on diagnostic B markers

`ASR10_DIAG_PANEL_B` is currently a compile-time diagnostic constant. It logs:

```text
PANEL_ENQUEUE
PANEL_THRB
PANEL_RHRB
PANEL_COMPLETE
PANEL_WAKE
```

These logs correlate ring enqueues, THRB writes, RHRB reads, parser completions,
and zero-count slot wake attempts.

---

## 6. Relevant Addresses and State

```text
FC4817  write = THRB, read = RHRB
FFB242  firmware RHRB read in Channel B parser
FFB3BA  initial parser state
FFB3E4  FF completion call site
F89A9A  panel completion/send path
F89AB8  byte[$03bc] decrement area
F89ABE  zero-count path after decrement
F89AC2  bclr #1 on slot record pointed to by $00d8

$03BA   panel ring read pointer
$03BC   remaining ring byte count / outstanding ring units
$03C0   parser state pointer
$00D8   slot0 record pointer

0023D4  observed slot0 record
0023E4  slot0 queue head
0023E6  slot0 queue tail

0014F4  later queued node
89A2    later node payload/type at 2(14f4)
```

Important counter detail:

```text
$03bc is byte-oriented.
Report byte[$03bc] and byte[$03bd] separately.
Do not treat word[$03bc] as the counter.
```

---

## 7. Build and Run Commands

Verified build:

```sh
git diff --check
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

Baseline clean V161 run:

```sh
rm -f error.log
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop floppies/asr10booth/V161.img \
  -bench 45 \
  -skip_gameinfo \
  -log
```

Diagnostic C:

```sh
rm -f error.log
SDL_VIDEODRIVER=dummy \
ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO=1 \
./mame asr10booth \
  -flop floppies/asr10booth/V161.img \
  -bench 45 \
  -skip_gameinfo \
  -log
```

Diagnostic D1:

```sh
rm -f error.log
SDL_VIDEODRIVER=dummy \
ASR10_EXPERIMENT_PANEL_REPLY_71_FF=1 \
./mame asr10booth \
  -flop floppies/asr10booth/V161.img \
  -bench 45 \
  -skip_gameinfo \
  -log
```

Diagnostic D2:

```sh
rm -f error.log
SDL_VIDEODRIVER=dummy \
ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF=1 \
./mame asr10booth \
  -flop floppies/asr10booth/V161.img \
  -bench 45 \
  -skip_gameinfo \
  -log
```

Diagnostic E:

```sh
rm -f error.log
SDL_VIDEODRIVER=dummy \
ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING=1 \
./mame asr10booth \
  -flop floppies/asr10booth/V161.img \
  -bench 45 \
  -skip_gameinfo \
  -log
```

Useful extraction:

```sh
rg "ASR10_DIAG_PANEL_B|ASR10_EXPERIMENT_PANEL|PANEL_ENQUEUE|PANEL_THRB|PANEL_RHRB|PANEL_COMPLETE|PANEL_WAKE|f89abe|f89ac2|03bc|slot0|14f4|89a2" error.log
```

---

## 8. Working Tree State

Captured immediately before creating this handoff file.

`git status --short`:

```text
AM docs/asr10/architecture.md
A  docs/asr10/asr10-boot-slot0-2026-07-13.md
A  docs/asr10/asr10-mame-handoff-addendum-2026-07-09.md
A  docs/asr10/asr10-mame-handoff-duart-fdc-scheduler-2026-07-11.md
A  docs/asr10/asr10-mame-irq6-duart-handoff-2026-07-12.md
A  docs/asr10/current-blocker.md
A  docs/asr10/floppy-investigation-2026-06-29.md
A  docs/asr10/mc68302-sib-investigation-2026-06-29.md
A  docs/asr10/panel-protocol.md
A  docs/asr10/scheduler-slot0-continuation-findings-2026-06-29.md
A  docs/asr10/superglu-investigation-2026-06-29.md
 M src/mame/ensoniq/asr10_boot.cpp
```

`git diff --stat`:

```text
 docs/asr10/architecture.md      | 133 +++++--
 src/mame/ensoniq/asr10_boot.cpp | 781 +++++++++++++++++++++++++++++++++++++++-
 2 files changed, 886 insertions(+), 28 deletions(-)
```

`git diff --check`:

```text
```

Do not commit anything unless explicitly instructed.

---

## 9. Smallest Next Evidence-Based Experiment

**[OPEN]** The next experiment should continue normal `FF` completion probes for
later natural panel-ring traffic. It must not respond to early polled boot
display writes.

Suggested scope:

```text
Drain the next natural Channel B ring after Diagnostic E's first zero crossing,
one natural THRB byte at a time, with bounded FF completion probes.
```

For every `$03bc 01 -> 00` crossing, correlate:

```text
slot0 state before/after
slot0 queue head/tail
whether node 14f4/89a2 exists yet
whether f89ac2 changes 0202 -> 0002
whether dispatcher subsequently executes slot0
whether node 14f4 is consumed, promoted, modified, or released
```

Hard stop conditions:

```text
successful slot0 wake and dispatch
unexpected parser state
unexpected THRB source/path
RX still pending
completion fails to decrement byte[$03bc]
reasonable cycle/ring safety limit
```

The experiment should be env-gated, bounded, and must not:

```text
call parser/completion routines directly
write $03bc
write $03c0
write slot state
manipulate queues
alter scheduler behavior
alter FDC/MC68302/DUART device behavior outside diagnostic RX injection
```

---

## 10. Open Protocol Question

**[HYP]** `src/mame/ensoniq/esqpanel.cpp` provides useful EPS/VFX protocol
evidence. It includes a throwaway-reply pattern in EPS mode:

```cpp
if (m_eps_mode)
{
    if (data == 0xe7)
        xmit_char(0x00);
    else if (data == 0x71)
        xmit_char(0x00);
    else
        xmit_char(data);
}
```

**[OPEN]** ASR-10 protocol identity is not proven. The service manual establishes
that the keypad/display board is an active controller and has a self-test mode
when proper digital-board communication is absent. That supports the active
panel-controller model but does not prove byte-level ASR-10 responses.

Current careful wording:

```text
FF is a diagnostic completion stimulus accepted by ASR-10 firmware.
It is not yet proven to be the authentic physical ASR-10 panel response.
```

