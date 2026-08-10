# ASR-10 panel input model

Date: 2026-08-10. Scope: correction pass after `47318563942`, task 5.

No code changes were made. `INPUT_PORTS_START(asr10_boot)` remains empty at
`src/mame/ensoniq/asr10_boot.cpp:4610-4611`.

## Verified: current harness path

The existing harness injects panel ACK/status bytes through SCN2681 channel B, not
through MAME input ports.

Code path:

```text
panel_autorespond_fire()
  -> panel_c_queue_rx(0xff, "autorespond_fc4817_write", write_pc)
      -> m_duart->m_chanB->rx_fifo_push(data, 0)
```

Relevant code:

- `src/mame/ensoniq/asr10_boot.cpp:1872-1883`: `panel_c_queue_rx()` pushes one byte
  directly into `m_duart->m_chanB->rx_fifo_push(data, 0)`.
- `src/mame/ensoniq/asr10_boot.cpp:1894-1905`: `panel_autorespond_fire()` always injects
  byte `$FF` as the current autoresponse.
- `src/mame/ensoniq/asr10_boot.cpp:2927-2934`: panel output bytes written by firmware
  schedule the autoresponse timer.
- `src/mame/ensoniq/asr10_boot.cpp:2810-2816`: reads from `$FC4816/$FC4817` pop channel B
  RHRB when SRB had RxRDY.

Firmware dispatch path:

```text
panel byte -> DUART channel B RX-FIFO -> SRB RxRDY -> ISR bit 5 -> IRQ6
  -> $F884BE -> ($00DE).w handler -> RHRB pop
```

Supporting code/documentation:

- `$F884BE` reads DUART ISR `$FFFC480B`; bit 5 dispatches through `($00DE).w`.
- `reference/vector-map.md` and `reference/subroutine-index.md` already describe the
  channel B RX path as verified.

## Verified: raw byte mapping exists

The current driver contains a diagnostic log for the ROM's raw panel-byte mapping table:

- `src/mame/ensoniq/asr10_boot.cpp:2840-2850` reads raw RHRB byte at `$F89CEA`,
  indexes ROM table `$F82484 + raw`, and logs the mapped byte.

Targeted extraction from the interleaved ROM image gives these examples:

| raw byte | mapped byte |
|---:|---:|
| `$15` | `$16` |
| `$0F` | `$17` |
| `$21` | `$23` |
| `$3B` | `$23` |
| `$03` | `$40` |

This proves that user/panel bytes are not necessarily consumed as raw bytes. The ROM
applies a translation table before later semantics.

## Likely: user input uses the same channel B receive transport

[Likely] Front-panel user input enters through the same DUART channel B receive path as
panel ACK/status:

- Existing documentation (`investigations/panel-protocol.md`) describes panel input
  keys/ACK/status as arriving on RHRB and routed through the OS RX parser.
- The ROM code around `$F89CEA` explicitly treats RHRB data as a raw panel input byte and
  maps it through `$F82484`.
- No MAME input ports exist, and no separate current source for panel user events is
  implemented.

This is still not [Verified] for file-browser keys specifically. ACK/status bytes and
button events may share transport while using different packet/state contexts.

## Open: key semantics

The exact `DOWN`, `UP` and `ENTER` byte values are not identified.

Known candidates from current code/log focus are mapped bytes `$16`, `$17`, `$23` and
`$40`, because `asr10_boot.cpp:2846-2850` already logs those as named diagnostic
booleans. However, the current evidence does not bind any of them to `DOWN`, `UP` or
`ENTER` in the V3.50 file browser.

Therefore no `INPUT_PORTS` implementation should be written yet. A guessed mapping would
create false `executed=observed` edges in the call-graph database.

## Minimal input-port shape once mapping is known

The smallest useful MAME surface is probably:

```text
PORT_NAME("Down")
PORT_NAME("Up")
PORT_NAME("Enter")
```

But each press must be converted to the authentic raw panel byte, not directly to a
post-table mapped byte, unless later evidence proves the harness should bypass the raw
mapping table.

The conversion point would need to be before channel B FIFO insertion:

```text
MAME input field transition
  -> ASR-10 raw panel byte for that key
  -> channel B RX FIFO insertion equivalent to panel_c_queue_rx()
  -> DUART RxRDYB/IRQ6 path
```

In code terms, the eventual implementation needs to bridge from an input-port transition
to the same receive side currently reached at
`src/mame/ensoniq/asr10_boot.cpp:1877`, while preserving that firmware performs its own
raw-to-mapped lookup at `$F82484`.

## Next evidence needed

Do not add input ports until at least one of these is true:

- a real or trusted ASR-10 panel trace identifies raw bytes for DOWN/UP/ENTER
- runtime instrumentation observes a file-browser key event and its raw RHRB byte
- static analysis identifies the consumer that labels mapped bytes as browser
  DOWN/UP/ENTER

Until then, the deterministic file-browse test remains blocked by missing input
semantics, not merely by missing `INPUT_PORTS` syntax.
