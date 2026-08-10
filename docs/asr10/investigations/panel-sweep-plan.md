# ASR-10 panel raw-byte sweep plan

Date: 2026-08-10. Scope: execution plan only. No MAME run was performed in this pass.

## Preconditions

Use V3.50 and start from the visible browser state:

```text
FILE 1  TUTORIAL BNK
```

The sweep must not mark call-graph edges as observed unless the injected byte produces
an actual observed firmware path. "No visible effect" is a result to journal, not proof
that the raw value is semantically unused.

Start each run from a clean V3.50 boot to the visible `FILE 1  TUTORIAL BNK` state.
Inject one raw byte, wait 500 ms of emulated time, record the observations below, then
reset to the same start state before the next raw byte. Do not carry browser state
between candidate bytes in the first sweep.

Missing precondition from the first control attempt: before running the `$23/$22`
control pair, verify that the consumer is active in the start state. A no-injection
V3.50 run must show `$FC4813` (`SRB`) polling or `$FC4817` (`RHRB`) reads after
`FILE 1  TUTORIAL BNK` is observed. Without that, a zero-effect control result is
not interpretable.

## Transport

Static evidence says the receiver is a 68681-compatible DUART channel B path:

```text
SRB bit 0 ready at $FC4813
RHRB byte read at $FC4817
raw byte -> ROM table $F82484 + raw -> mapped byte
```

The routine at `$F89D8E` calls `$F89CCA`, which polls `$FFFC4813` bit 0 before reading
one byte from `$FFFC4817`. No start byte, length field, or checksum was identified in
that routine. The transport required by this path is therefore one already-framed
channel-B receive byte with SRB RxRDY set.

Important exception: raw `$23` is tested before the table lookup at `$F89D94` and returns
via `$F89D9A rts` if present. It is still part of the verified `$00-$25` data prefix,
but this routine treats it as a raw-protocol exit byte.

## Injection Point

Current harness injection point:

- `src/mame/ensoniq/asr10_boot.cpp:1903-1915`
- `panel_c_queue_rx(u8 data, ...)`
- actual FIFO insertion: `src/mame/ensoniq/asr10_boot.cpp:1908`,
  `m_duart->m_chanB->rx_fifo_push(data, 0)`
- scriptable parameter: `ASR10_PANEL_SWEEP_RAW=0xNN`. The harness waits until
  `FILE 1  TUTORIAL BNK` is observed, prints a precheck line, then queues exactly one
  raw byte through `panel_c_queue_rx()`.

A future sweep should inject raw bytes through the same channel-B FIFO path. It should
not write directly to the post-lookup mapped value and should not bypass SRB/RHRB
semantics.

## Control Pair

Run these first:

```text
inject $23  -> expected effect: NONE (filtered before lookup)
inject $22  -> expected effect: SOME (maps to $05)
```

The result validates the transport before the remaining candidate raw values are
interpreted.
If `$22` has no effect, the injection path is broken and the sweep must stop, not be
interpreted. If `$23` has an effect, the filter model is wrong and the `$23` conclusion
returns to `[OPEN]`.

## Candidate Raw Values

| raw | mapped | note |
|---:|---:|---|
| `$00` | `$3A` |  |
| `$01` | `$3B` |  |
| `$02` | `$00` |  |
| `$03` | `$40` |  |
| `$04` | `$04` |  |
| `$05` | `$13` |  |
| `$06` | `$11` |  |
| `$07` | `$12` |  |
| `$08` | `$01` |  |
| `$09` | `$18` |  |
| `$0A` | `$20` |  |
| `$0B` | `$21` |  |
| `$0C` | `$30` |  |
| `$0D` | `$31` |  |
| `$0E` | `$02` |  |
| `$0F` | `$17` |  |
| `$10` | `$22` |  |
| `$11` | `$24` |  |
| `$12` | `$32` |  |
| `$13` | `$33` |  |
| `$14` | `$03` |  |
| `$15` | `$16` |  |
| `$16` | `$07` |  |
| `$17` | `$43` |  |
| `$18` | `$34` |  |
| `$19` | `$35` |  |
| `$1A` | `$14` |  |
| `$1B` | `$15` |  |
| `$1C` | `$06` |  |
| `$1D` | `$41` |  |
| `$1E` | `$36` |  |
| `$1F` | `$37` |  |
| `$20` | `$10` |  |
| `$21` | `$23` |  |
| `$22` | `$05` |  |
| `$23` | `$25` | raw exit in `$F89D94` routine before lookup |
| `$24` | `$38` |  |
| `$25` | `$39` |  |

Do not sweep `$26-$FF` as panel candidates in the first pass. Those bytes are retained
in `static/panel-raw-map.csv` as dump data beyond the verified mapping prefix, but they
overlap ROM strings, pointer data, and padding.

## Observable Effects

For each raw value, capture:

- whether SRB RxRDY and RHRB pop occur on channel B
- whether the byte reaches `$F89CEA`
- mapped value, if lookup occurs
- branch outcome at `$F89D94`, `$F89DA6`, `$F89DB0`, `$F89DB8`, `$F89DC0`, `$F89DC6`
- elapsed time from injection to first branch/logged effect
- any scheduler/binding-slot activity that follows
- any panel TX and resulting display text
- whether the file-browser selection changes from `FILE 1  TUTORIAL BNK`

An observed browser response is a visible file index/display transition, not merely
successful FIFO insertion. A non-response should be recorded as:

```text
raw=$xx mapped=$yy reached_rhrb=yes/no reached_lookup=yes/no visible_effect=no
elapsed_ms=500 notes=<branch point or timeout>
```

This prevents "no visible effect" from being converted into "raw value has no meaning."

If the control pair passes, continue through `$00-$25` in ascending raw order, but skip
re-running `$22/$23` unless the harness was changed. Every run should write one
journal row even when the only result is the calibrated non-response format above.
