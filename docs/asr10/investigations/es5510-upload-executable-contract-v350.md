# ES5510 upload -> executable-state contract — ASR-10 V3.50

## Question

What does the established V3.50 ES5510 upload path prove about an **executable**
ESP state, and what must be known before the generic `ESQ_5505_5510_PUMP`
can be enabled safely?

This is deliberately narrower than a pump or audio-routing implementation. It
does not connect any ES5506 output to ES5510, enable DSP execution, select an
audio rate, or infer ASR board wiring.

## Established starting boundary

The previous upload investigation already verifies:

```text
effect record
 -> F973F0 host-loader
 -> FC3000 latch bytes + FC31C1 combined commit
 -> stock ES5510 GPR/instruction arrays
 -> firmware read-select/readback comparison
```

The type-1 combined commit at `$FC31C1` is correctly adapted to generic host
offset `$E0`; 822 witnessed readback bytes matched firmware `D2` in the prior
run. That proves uploaded storage and firmware verification, but does not by
itself prove a safe per-audio-frame execution policy.

## Why the distinction matters

The generic pump calls `es5510_device::run_once()` when its private
`m_esp_halted` gate is false. In the current generic device this is not a
single-instruction operation:

```text
deassert HALT
run once to leave HALTED state
assert HALT
execute one instruction repeatedly until STATE_HALTED
```

`STATE_HALTED` is reached by an `END` instruction only when the sampled HALT
input is asserted. The loop has no instruction budget. Therefore the two
discarded pump-integration spikes that stalled MAME establish only this:

```text
uploaded ASR state + synthetic pump HALT release + unbounded run_once()
    is not a safe execution contract
```

They do **not** establish that ASR effect microcode is invalid, that the real
ASR uses the VFX HALT policy, or that ES5506/ES5510 physical routing is wrong.

## Method

Two bounded observation sources were used; neither changes emulated state.

1. The retained controlled `ROM HALL -> 44LUSH -> ROM HALL` write-census log
   (`/private/tmp/asr10-rate-write-census-r5.log`) was reduced only for host
   offsets `$12`, `$18`, and `$1f`.
2. A temporary Lua write tap watched `$FC3000-$FC303F` plus `$FC3180-$FC3181`
   and `$FC31C0-$FC31C1` during a V3.50 media boot. It retained all three tap
   references, printed a live witness before boot traffic, reconstructed the
   six instruction-latch bytes, and recognized a committed instruction whose
   ALU opcode nibble is `F` (generic ES5510 `END`). It was deleted after the
   run; its bounded output remains at `/private/tmp/asr10-es5510-contract.log`.

The latter is a host-write witness, not a claim that ES5510 executed those
instructions: ASR constructs this device with `set_disable()`.

## Observed host setup

The temporary boot probe had live traffic and observed this CPU-visible
sequence (PC is the tap's post-access sample):

| Host function | CPU address | value | sampled PC | Status |
|---|---:|---:|---:|---|
| Halt Enable | `$FC303E` | `$02` | `$F977C0` | [Verified runtime] |
| Halt Enable | `$FC303E` | `$FF` | `$F97354` | [Verified runtime] |
| Host Control | `$FC3024` | `$03` | `$F9738C` | [Verified runtime] |
| Host Control | `$FC3024` | `$02` | `$F97392` | [Verified runtime] |
| Host Serial Control | `$FC3030` | `$48` | `$F97398` | [Verified runtime] |

The address mapping is the existing low-byte host map: `$FC3024` is generic
host offset `$12`, `$FC3030` is `$18`, and `$FC303E` is `$1f`.

The generic implementation defines `$12` as Host Control, `$18` as Host
Serial Control, and `$1f` as Halt Enable. Its model decodes `$48` as slave,
Sony-format, with SER1 configured as output and SER0/SER2/SER3 as inputs.
That decoding is **[Verified MAME device-model behavior]**. It is not yet a
verified statement about ASR physical serial nets or the original chip's
board-level wiring.

During the already verified A->B and B->A effect loads, the census sees the
same Host Control reset/clear traffic (`$01/$02`) and `$FC303E <- $02`; it
sees no Host Serial Control write. Thus the observed mode transition uploads
new program/register state under a stable host-serial configuration. This is
[Verified runtime] for the two controlled transition windows, not a claim that
the host-serial setting is immutable elsewhere in the OS.

## Program-store initialization witness

The boot probe observed combined `$E0` commits at instruction indices
`$00..$0B` with reconstructed latch value `$FFFFFFFFFFFF`. In the generic
ES5510 decoder the ALU opcode is bits 15:12; it is `$F` for all those words,
which is `END`. The firmware is therefore observed filling at least the
bounded initial part of instruction storage with `END` before further program
loading.

This is useful but deliberately limited:

- [Verified] firmware performs an END-filled initialization through the same
  combined commit path;
- [Verified] later effect uploads overwrite selected instruction entries and
  pass host readback verification;
- [OPEN] the active ROM HALL or 44LUSH program's exact first reachable END
  index under a frame invocation;
- [OPEN] whether the real ASR frame boundary supplies the HALT behavior that
  generic `run_once()` imposes.

The `END` fill must not be mistaken for evidence that an arbitrary uploaded
effect program is bounded the same way.

## Contract classification

| Contract element | Status | Evidence |
|---|---|---|
| Program/GPR upload commits into generic ES5510 storage | [Verified] | prior readback witness |
| Firmware configures host-control, HALT-enable, and Host Serial Control before upload | [Verified] | bounded Lua host-write witness |
| Host Serial Control remains unchanged across measured A/B/A effect loads | [Verified] | retained A/B/A census |
| Generic pump's `run_once()` needs a reachable END/HALT termination | [Verified] | generic device source |
| ASR uploaded effect program satisfies that termination contract per frame | [OPEN] | no instruction-stream execution witness |
| Generic pump's SER0-2 in / SER3 out routing matches ASR | [OPEN] | `$48` makes that assumption especially unjustified |
| VFX DUART ESPHALT policy applies to ASR | [OPEN] | family precedent only |

## Conclusion

The firmware upload is no longer the blocker, but it is not a sufficient
execution-start contract. The correct boundary is:

```text
verified upload/readback + host serial/HALT setup
    != verified frame-safe generic run_once execution
```

Accordingly, enabling the pump by an invented configuration-time or ACTV-time
HALT release is not a defensible ASR change. No production model change is
justified.

## Single next experiment

Repeat the established controlled A/B/A effect-selection workflow with one
bounded Lua probe that reconstructs only actual instruction-latch writes and
their `$C0/$E0` commit indices for the two committed effects. It must identify
the first reachable generic `END` candidate after each upload (or explicitly
show that this cannot be inferred from upload order). Do not route audio or
call `run_once()` in that experiment. Its result decides whether a
budgeted/safe generic-pump execution spike is meaningful.

## Non-results / retained boundaries

- Physical 30/44.1-kHz clock/frame control: [OPEN].
- ES5701 role: [OPEN].
- ES5506 -> ES5510 serial-pair mapping: [OPEN].
- Actual ASR ESP HALT pin/source: [OPEN].
- The parked serialized effect `+$66` branch: unchanged.
