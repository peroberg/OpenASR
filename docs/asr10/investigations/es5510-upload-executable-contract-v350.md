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

### Synthetic post-upload pump spike

One temporary C++ spike used the stock one-ES5506 KT/TS pump topology, leaving
the ASR ES5506 clock unchanged.  It was deliberately not an ASR wiring claim:

- every `$C0`/`$E0` instruction commit halted the generic pump;
- after 10 ms without a new commit, it scanned the PC-0 image;
- it released only when the first generic `END` was exactly the measured
  `$3B`/`$58` `$F040` endpoint;
- a temporary pump-side peak witness retained the six generic SER inputs and
  two SER3 outputs.

The normal V3.50 instrument/note workflow gave this bounded result:

| event | result |
|---|---|
| initial verified ROM-HALL image | synthetic release at `$58 = $3662FFFFF040` |
| generic frames executed before next upload | 300,000; no hang |
| SER0--2 input peaks in that pre-note interval | all zero |
| SER3 output peaks in that pre-note interval | both zero |
| later Instrument-select upload at `t=21.788` | new `$00..$58` image did **not** satisfy the measured ROM-HALL/44LUSH endpoint identity; pump remained safely halted |
| MIDI note-on at `t=23.120` | firmware received it and programmed 2,400 ES5506 voice writes, but occurred after the synthetic pump had halted |
| routed pump WAV | peak zero |

Thus **[Verified current MAME spike]** the reconstructed ROM-HALL program can
be repeatedly invoked by generic `run_once()` without the previous unbounded
loop.  It does **not** test ES5506-to-ES5510 signal flow: the only observed
executed interval had no serial input, and the later note belonged to a
different, unclassified program image.  Zero output is consequently not
evidence against the generic serial routing, nor evidence for ASR physical
routing.

All pump routing, synthetic HALT policy and temporary diagnostics were removed
after the run.  The retained reduced logs are outside the tree at
`/private/tmp/asr10-pump-spike-note.log` and
`/private/tmp/asr10-pump-commit-note.log`.

### Controlled known-program serial-input result

The follow-up corrected the prior lifecycle ambiguity with a normal panel
workflow: load/select `JM DIGI SYN`, enter FX Select with raw `$07`, then use
Up twice to restore `ROM-01 HALL REVERB` before injecting the known MIDI note.
The live pre-note witness was identical in two runs:

```text
t=25.110000  $0CE3=$00  display="FX?R0M-?1  HALL RE?ERB"
synthetic release: END $58 = $3662FFFFF040
```

The same temporary stock KT/TS topology fed ES5506 channels 2--7 to generic
ES5510 SER0--2.  After the witness it observed the note for four seconds.
Both runs gave:

| witness | result |
|---|---|
| MIDI receive / ES5506 programming | `rhra=3`, `voice_writes=6270` |
| generic pump frames after note | crossed 500,000 and 600,000 sample witnesses |
| SER0--2 input peaks (six lanes) | `[0,0,0,0,0,0]` |
| SER3 output peaks | `[0,0]` |

This is a true binary result for the tested integration:

```text
known ROM-HALL program + active ES5506 voice
    -> stock KT/TS ES5506 channels 2--7 -> generic SER0--2
    -> no sample data observed
```

It is **[Verified current MAME spike]** that the stock KT/TS pump mapping is
not a working ASR signal path under this controlled note.  It is **not** a
measurement of physical ASR SER pins: that topology was deliberately
synthetic, and its channels 0--1 are bypass outputs rather than generic ESP
inputs.  Therefore the result neither disproves a different ASR board serial
mapping nor identifies its actual routing.  It does rule out adopting the
stock KT/TS mapping as the ASR implementation without further evidence.

All temporary C++ and Lua instrumentation was removed after the two matching
runs. Reduced logs remain outside the tree at
`/private/tmp/asr10-pump-romhall-note.log` and
`/private/tmp/asr10-pump-romhall-note-r2.log`.

### Passive ES5506 lane observation

The next probe removed ES5510 and the pump entirely.  It temporarily exposed
the generic ES5506 model's four stereo stream pairs, retained raw pre-route
peak energy for lanes 0--7, and otherwise repeated the exact controlled
workflow above.  It did not alter the ES5506 clock, add a route, or execute
ESP code.

Both runs had the same ROM-HALL witness at `t=25.110`, then the same note
receive witness (`rhra=3`, `voice_writes=6270`).  The three post-note
100,000-sample windows were byte-for-byte identical:

| window | lanes 0--7 peak energy |
|---|---|
| 1 | `[46770,46770,0,0,0,0,0,0]` |
| 2 | `[63531,63531,0,0,0,0,0,0]` |
| 3 | `[22014,22014,0,0,0,0,0,0]` |

Therefore **[Verified current MAME ES5506-device model]** the controlled
single-instrument note occupies the first stereo pair only.  In particular,
the stock KT/TS pump's ESP inputs (ES5506 lanes 2--7) are correctly observed
as silent in the previous spike; that negative result is not an error in how
the pump sampled its input streams.  The stock topology instead treats the
active 0--1 pair as its bypass path.

This is not a physical ASR OEX pin map, nor a claim that lanes 2--7 are never
used by other ASR programs.  It is the required bounded observation that the
known default voice is on lanes 0--1 in the current model.  The temporary
four-pair configuration and raw-energy diagnostic were removed after both
runs. Logs remain outside the tree at
`/private/tmp/asr10-es5506-lanes-romhall.log` and
`/private/tmp/asr10-es5506-lanes-romhall-r2.log`.

### Synthetic lane-0/1 SER matrix

The final bounded spike retained the same V3.50 workflow, but drove the known
active ES5506 lane 0/1 pair into one generic pump pair at a time.  A temporary
post-ROM-HALL release was constrained to the already reconstructed PC-0 END
image `$58 = $3662FFFFF040`; it is explicitly synthetic, not an ASR HALT-pin
policy.  `-wavwrite` kept the sound streams live.  Each run had the same
pre-note witness:

```text
t=25.050000  $0CE3=00  FX?R0M-?1 HALL RE?ERB
rhra=3, voice_writes=6288
```

The generic device's firmware-programmed Host Serial Control is `$48`.  Its
current MAME decoder calls SER0, SER2 and SER3 inputs, and SER1 an output.
The original stock pump's implicit `SER3` output assumption was therefore not
a valid output witness for this program.  The spike recorded all eight
generic SER registers immediately before and after `run_once()` in three
100,000-frame post-note windows:

| injected ES5506 lane 0/1 | pre-run nonzero pair peaks | post-run nonzero pair peaks | result |
|---|---|---|---|
| generic SER0 | SER0 `3219/3219`, `3957/3957`, `1383/1383` | SER0 unchanged; SER1 `3523/3216`, `3500/3868`, `1417/1533` | [Verified current MAME spike] input reaches a program that emits SER1 |
| generic SER1 | SER1 same three input peak pairs | all SER registers zero after `run_once()` | no usable output; consistent with `$48` marking SER1 output |
| generic SER2 | SER2 same three input peak pairs | SER2 unchanged; SER1 the same `3523/3216`, `3500/3868`, `1417/1533` peaks | [Verified current MAME spike] input reaches a program that emits SER1 |

SER3 remained zero in all three cases.  Thus the measured generic execution
contract for the controlled ROM-HALL program is:

```text
ES5506 lane 0/1 -> generic SER0 or SER2 -> ROM-HALL run_once() -> generic SER1
```

This is sufficient to reject the previous stock KT/TS assumption (lanes 2--7
to SER0--2 and SER3 to outputs) as an ASR implementation.  It is not a
physical ASR board serial-net mapping: the source-routing, post-upload HALT
release and target selection were all synthetic, and the generic device does
not enforce physical serial direction at `ser_w()`.

All temporary C++ pump/routing/release instrumentation and the Lua workflow
were removed after the three runs.  The reduced logs remain outside the tree
at `/tmp/asr10-ser-matrix-ser0-post.log`,
`/tmp/asr10-ser-matrix-ser1-post.log`, and
`/tmp/asr10-ser-matrix-ser2-post.log`.

### Synthetic SER1-to-WAV output adapter

The next disposable spike kept the measured lane 0/1 -> generic SER0 input,
but temporarily sent generic SER1 (rather than the stock pump's SER3) to the
speaker/WAV outputs.  It changed neither ES5506 rate nor persistent routing.

For controlled ROM-01 HALL, the active-program witness was `$0CE3=$00`, END
`$58`, MIDI `rhra=3`, and `voice_writes=6288`.  The WAV capture passed the
existing independent measurement with `peak=3464` and `freq=262.3 Hz`.
Therefore **[Verified current MAME synthetic spike]** this complete generic
path produces reproducible non-silent audio:

```text
ES5506 lane 0/1 -> generic SER0 -> ROM-HALL program -> generic SER1 -> WAV
```

The corresponding 44LUSH control reached `$0CE3=$01`, received the MIDI note
(`rhra=3`, `voice_writes=2324`) and released the reconstructed END `$3B`, but
its SER1-adapter WAV had peak zero.  A bounded all-SER post-execution witness
showed only the injected SER0 pair (`2921/2921`) in its first post-note window;
SER1, SER2 and SER3 stayed zero.  This is a valid negative result for this
one synthetic mapping because the pump was live, the firmware mode/program
and note path were all witnessed, and the same adapter was positive for
ROM-HALL.  It does **not** say that 44LUSH cannot execute or that ASR's
physical wiring is absent: the 44LUSH program may require a distinct serial
input/output or frame contract.

Consequently the generic data-path result is deliberately split:

| claim | status |
|---|---|
| ROM-HALL has one working synthetic ES5506 -> ES5510 -> WAV path | [Verified current MAME synthetic spike] |
| one fixed synthetic SER0/SER1 path works for both ROM-HALL and 44LUSH | [DISPROVEN] |
| physical ASR serial routing / frame contract | [OPEN] |

Temporary code and Lua scripts were removed.  Reduced logs remain outside the
tree at `/tmp/asr10-ser1-output-romhall.log` and
`/tmp/asr10-ser1-output-44lush-r2.log`.

### 44LUSH direct SER-access boundary

One final temporary ES5510-internal witness marked SER special-register
accesses only while generic `run_once()` executed the exact reconstructed
44LUSH `$3B` image.  It excluded both host upload accesses and pump-side
`ser_w()` injection.  Across two live 100,000-frame post-note windows it
reported:

```text
reads = $F3 = SER0R, SER0L, SER2R, SER2L, SER3R, SER3L
writes = $0C = SER1R, SER1L
```

This is **[Verified current MAME synthetic execution]** for the generic
interpreter and the witnessed 44LUSH program image.  It gives a concrete
reason why a single SER0 source is insufficient, but it does not determine
the required relationship among the three input pairs.

The only directly justified final variant routed lane 0/1 to generic SER2,
while retaining generic SER1 -> WAV.  It again had `$0CE3=$01`, END `$3B`,
MIDI `rhra=3`, and `voice_writes=2324`, but WAV peak remained zero.  Thus:

| claim | status |
|---|---|
| 44LUSH reads SER0, SER2, SER3 and writes SER1 in the generic interpreter | [Verified current MAME synthetic execution] |
| lane 0/1 -> SER2 alone is sufficient for 44LUSH -> SER1 -> WAV | [DISPROVEN] |
| the required multi-input/frame relationship | [OPEN] |

Finding a working 44LUSH path now requires either combinations of SER0/SER2/
SER3 or a deeper serial/frame-contract analysis.  That exceeds the bounded
single-pair pump experiment.  The ES5510 pump topology is therefore parked:
ROM-HALL proves the generic execution principle, while a shared permanent
ASR pump mapping remains unestablished.  Temporary trace code was removed;
the reduced logs remain outside the tree at
`/tmp/asr10-44lush-ser-trace.log` and `/tmp/asr10-44lush-ser2.log`.

### Full-input adapter boundary

The earlier temporary full-input experiment failed before boot because it
incorrectly passed the ES5506 **master** clock into the pump's stream.  The
resulting resampler allocation fault (`exit 139`) was a MAME adapter bug, not
negative routing evidence.  The implemented replacement uses the same
`sample_rate_changed()` bridge as KT/TS: the pump receives ES5506's already
computed frame rate, while the existing ASR mode policy remains the only code
that selects the ES5506 master-clock value.

The resulting [HYPOTHESIS] is deliberately a small functional contract:

```text
ASR-owned frame adapter
  = halt on every $C0/$E0 program commit
  + release only 10 ms after the final commit, and only for the
    previously reconstructed ROM-HALL or 44LUSH PC-0/END images
  + ES5506 lane 0/1 duplicated to SER0, SER2 and SER3
  + SER1 as processed stereo output
  + exactly one generic run_once() per ES5506-derived pump frame
```

The generic pump gained a serial-route option rather than an ASR-specific
mixer.  Its established VFX route remains the default; the ASR hypothesis
selects the port relationship above.  While its program-image gate is closed,
the same adapter passes lane 0/1 dry.  This preserves audio for unclassified
instrument programs without pretending that they have a verified ESP frame
contract.  When the gate is open, main output is SER1 only.

## Functional acceptance

The adapter booted normally in both no-media and V3.50-media modes.  The
existing, unchanged `audio_rate_mode.lua` then exercised the established
ROM-HALL -> 44LUSH -> ROM-HALL sequence with a known C4 MIDI note.  It kept
the regular firmware mode witnesses and its WAV checks passed:

| state | `$0CE3` | WAV peak | measured frequency |
|---|---:|---:|---:|
| A, ROM HALL | `$00` | 10,818 | 262.3 Hz |
| B, 44LUSH | `$01` | 18,359 | 260.9 Hz |
| A2, ROM HALL | `$00` | 11,999 | 260.9 Hz |

The ordinary `note_audio` control also remains audible through the intentionally
dry closed-gate fallback (`peak=3852`, `262.3 Hz`).  The full V3.50 regression
control rows remain green after the adapter change.

This is sufficient to promote only the functional statement below:

| Claim | Status |
|---|---|
| The current MAME adapter can execute verified ROM HALL and 44LUSH images once per ES5506-derived frame and return audible SER1 output. | [Likely functional] |
| The same functional route supports the controlled 30k -> 44.1k -> 30k A/B/A note acceptance. | [Likely functional] |
| The route is ASR's physical SER wiring. | [OPEN] |
| The post-upload timer/image gate is ASR's physical HALT signal. | [OPEN] |
| The mode policy identifies physical clock mux/divider wiring. | [OPEN] |

## Stop boundary

The controlled point-2 contract and its point-4 A/B/A acceptance are complete
for ROM HALL and 44LUSH.  Do not use this result to infer board nets, or
expand it into a general routing search.  Further work may widen the
program-image admission set only when another effect has an independently
reconstructed safe endpoint and a focused acceptance case.

## Non-results / retained boundaries

- Physical 30/44.1-kHz clock/frame control: [OPEN].
- ES5701 role: [OPEN].
- ES5506 -> ES5510 serial-pair mapping: [OPEN].
- Actual ASR ESP HALT pin/source: [OPEN].
- The parked serialized effect `+$66` branch: unchanged.
