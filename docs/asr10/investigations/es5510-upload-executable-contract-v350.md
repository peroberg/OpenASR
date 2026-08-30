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

## Active-program END reconstruction

The follow-up used one temporary, retained three-tap Lua probe and only normal
panel edges:

```text
FILE 16 44LUSH PLATE -> Enter/Yes             = B
FX Select -> Down -> ROM-01 HALL REVERB       = A2
Up -> loaded 44LUSH PLATE                     = B2
Down -> ROM-01 HALL REVERB                    = A3
```

For every `$C0` or `$E0` commit below `$A0`, the probe reconstructed the six
previous instruction-latch bytes and retained the latest word for that index.
It did not invoke ES5510 execution.  The mode/object witnesses were:

| snapshot | `$0CE3` | current-effect object | commits so far | first `END` |
|---|---:|---:|---:|---:|
| B, 44LUSH | `$01` | `$0062B600` | 471 | `$3B` |
| A2, ROM-01 HALL | `$00` | `$FFF9B626` | 560 | `$58` |
| B2, 44LUSH | `$01` | `$0062B600` | 620 | `$3B` |
| A3, ROM-01 HALL | `$00` | `$FFF9B626` | 709 | `$58` |

The actual terminating committed words are:

```text
44LUSH:     index $3B = $7A81FFFFF040
ROM-01 HALL:index $58 = $3662FFFFF040
```

Both have the generic decoder's ALU opcode `$F` at bits 15:12.  The current
generic `execute_run()` has no program branch mechanism: it starts a released
halted processor at PC 0, increments PC after every instruction, and handles
opcode `$F` as `END` regardless of the instruction skip condition.  Therefore
these are not merely candidate values in storage: **[Verified current MAME
device model]** `run_once()` beginning at PC 0 has a finite endpoint after 60
instructions for 44LUSH and 89 for ROM-01 HALL, provided its HALT line is
asserted before that endpoint is sampled.

The byte-for-byte index/instruction lists through those endpoints were
identical for B/B2 and A2/A3 respectively (`diff -u` exit 0).  SHA-256 of the
first-observed normalized lists was:

```text
44LUSH $00..$3B:     35810521e01b2168ac528ae1c7fd228368aeb0313ebc877f8a11d3a5962c3770
ROM-01 HALL $00..$58:fa65ea95fab476bf2da29b66bdd6aeb7995319495701c70c565b08aa06431004
```

The temporary probe was removed after the run. Its reduced raw output remains
outside the tree at `/private/tmp/asr10-es5510-program-aba-r2.log`.

## Contract classification

| Contract element | Status | Evidence |
|---|---|---|
| Program/GPR upload commits into generic ES5510 storage | [Verified] | prior readback witness |
| Firmware configures host-control, HALT-enable, and Host Serial Control before upload | [Verified] | bounded Lua host-write witness |
| Host Serial Control remains unchanged across measured A/B/A effect loads | [Verified] | retained A/B/A census |
| Generic pump's `run_once()` needs a reachable END/HALT termination | [Verified] | generic device source |
| Measured ASR uploaded program reaches generic `END` from PC 0 | [Verified current MAME device model] | B/A2/B2/A3 latch/commit reconstruction |
| Actual ASR frame boundary supplies the physical HALT behavior | [OPEN] | no board/pin witness |
| Generic pump's SER0-2 in / SER3 out routing matches ASR | [OPEN] | `$48` makes that assumption especially unjustified |
| VFX DUART ESPHALT policy applies to ASR | [OPEN] | family precedent only |

## Conclusion

The firmware upload is no longer the blocker. For the two controlled programs,
the generic interpreter now has a finite PC-0-to-END path. The remaining
execution contract is narrower:

```text
verified upload/readback + finite generic program endpoint
    != verified ASR physical HALT policy or serial routing
```

Accordingly, a pump can now be tested with a **clearly synthetic, post-upload
HALT policy** without risking an unbounded generic program loop for these two
effects.  That policy remains an experiment, not ASR hardware evidence. No
production model change is justified by this reconstruction alone.

## Single next experiment

Make one temporary generic-pump integration spike only after a verified effect
commit: retain the existing ES5506 rate, configure the stock pump as in the
one-ES5506 KT/TS reference, use an explicitly synthetic post-upload HALT
release, and capture whether ES5510 produces bounded serial output.  Do not
claim the routing or HALT policy is physical ASR wiring; remove the spike if it
does not produce a clean result.

## Non-results / retained boundaries

- Physical 30/44.1-kHz clock/frame control: [OPEN].
- ES5701 role: [OPEN].
- ES5506 -> ES5510 serial-pair mapping: [OPEN].
- Actual ASR ESP HALT pin/source: [OPEN].
- The parked serialized effect `+$66` branch: unchanged.
