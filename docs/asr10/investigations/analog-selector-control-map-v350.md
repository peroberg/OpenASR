# ASR-10 V3.50 analog selector/control map

Date: 2026-08-26

Baseline: `4c02e003505 asr10: characterize analog control acquisition`

Scope: static reconstruction plus bounded runtime observation; no hardware model
or firmware writes.

## Question and falsifiable claims

The prior acquisition round proved that V3.50 writes MC68302 PBDAT PB2-PB0,
waits, and reads the ES5506 PAR/POT register. This round asks what all eight
selector values mean and what contract a later board model must implement.

The bounded claims were:

1. every selector used by the V3.50 acquisition routine has an identifiable
   generator or remains explicitly `[OPEN]`;
2. selector 1's conditional branch can be tied to a measurable firmware state;
3. selector 6 is not called unused merely because normal runtime omitted it;
4. current MAME host controls can be compared against the firmware selector,
   raw-value and ownership contracts.

A selector is not considered used merely because an arbitrary PBDAT write has
the same low bits. It must belong to the verified select -> settle -> PAR-read
path. Runtime PCs below are access provenance; no read-tap hit is promoted to an
instruction-execution entry claim.

## Evidence domains

- **Firmware/static:** V3.50 code loaded at its established RAM addresses and
  the ASR-10 boot ROM/model identity path.
- **Runtime:** retained Lua taps installed after the MC68302 internal window was
  established, with live PAR and RAM witnesses throughout each measurement.
- **Vendor:** ASR-10 Musician's Manual and Service Manual terminology and
  diagnostic limits. These describe intended controls; they do not prove PCB
  nets.
- **Source/model:** current `asr10_boot_state`, `asr10panel_device` and ES5506
  callback plumbing.
- **Physical board:** U55 is an MC74HC4051N `[Verified hardware]`; COM, select,
  enable and channel nets remain `[OPEN board-level provenance]`.

## Acquisition controller

### Hard-coded scan

`$0068BC` is a hard-coded control flow, not a selector table:

```text
$0068C8  selector 7, periodically when $0DF0 expires
$006950  selector 0 -> $F8D920
$00695A  selector 2 -> $F8D992
$006964  selector 5 -> $F8D9DE
$00696E  selector 3 -> $F8DA58
$006978  selector 4 -> $F8DA98
$006982  test $FFCCD1
$006988  test $FFD0EA
$00698E  selector 1 -> $000172EC, only when the tests permit
$00699A  otherwise decrement $FFD0EA
```

`$0069A2` masks PBDAT with `$F8`, ORs the requested selector into PB2-PB0,
then yields through the established settle path before the producer reads PAR.
Selector 7 is scheduled when byte `$0DF0` counts down; it is reloaded with 60.
At the measured roughly 100 ordinary scans/s this gives about 1.7 reference
updates/s. No configuration-dependent scan mask or selector table was found in
this path.

### Selector 1 condition

The byte tested at absolute-short `$CCD1` is runtime `$FFCCD1`. Boot ROM code at
`$FFB0AC` compares the model word at `$FFFB8FCC` with ASCII `$3838` (`"88"`)
and stores `SEQ` into `$FFCCD1`. The current model word is `$3130` (`"10"`),
so `$FFCCD1 = 0` on ASR-10 and selector 1 is skipped.

`$FFD0EA` is a countdown. A qualifying musical key/event path at `$000171D0`
stores 4 there. When the model flag is nonzero, the scanner decrements the
countdown once per cycle; selector 1 is sampled when it reaches zero. A bounded
existing-key stimulus changed `$FFD0EA` from 0 to 4 on the current ASR-10, but
the zero model flag correctly prevented both countdown use and selector 1.

The selector-1 producer at `$000172EC`:

- reads PAR through `$F8DAFE` and filters it into the `$FFD0DC` block;
- applies a model-specific table selected by byte `$01A3` plus the reference
  factor at `$0DF2`;
- clamps the processed result to 0..127;
- stores it at `$FFD0E0`; and
- dispatches shared controller index `$0E` through `$F8DC66`.

The gate is therefore `[Verified firmware] ASR-88 model plus post-key-event
countdown`. The service manual's ASR-88-only mono-pressure circuit, the
key-event arming and the 0..127 controller result jointly make selector 1
`[Likely firmware semantic: ASR-88 mono/channel pressure]`. It is not
`[Verified semantic]`: no ASR-88 ROM/runtime witness or direct name for shared
controller index `$0E` is present in this tree.

### Selector 6 proof search

An exhaustive search of the established V3.50 acquisition code found these and
only these calls to `$0069A2`: selector 7 at `$0068CC`, 0 at `$006952`, 2 at
`$00695C`, 5 at `$006966`, 3 at `$006970`, 4 at `$00697A`, and 1 at `$006990`.
Every literal V3.50 access to `$FC6829` was also inspected: boot calibration
selects 7, 5 and 0; the shared selector helper handles the list above; the
remaining accesses test PB3 or set PB4. No selector table or indirect selector
6 generator was found in this bounded path.

Runtime final PBDAT writes at access-provenance PC `$0069B2` provided live
witnesses for 0,2,3,4,5,7 and zero occurrences of 6. Selector 6 is therefore
`[Verified unreachable in the analyzed V3.50 acquisition path]`, not globally
proved absent from every firmware or physically unused. Its pin/control role is
`[OPEN physical]`.

## Runtime discrimination

The temporary probe retained taps through four phases and made no firmware
writes. It distinguished the helper's clear write at `$0069AC` from the final
selector write at `$0069B2`.

| Phase | Complete PAR reads | Final selector occurrences | Model flag / countdown |
|---|---:|---|---|
| FILE 1 idle, 6 s | 2,975 | 0=591, 2=591, 3=590, 4=590, 5=591, 7=7; 1=0, 6=0 | `$00` / `$00` |
| Load transition | 2,720 | 0=542, 2=542, 3=542, 4=542, 5=542, 7=10; 1=0, 6=0 | `$00` / `$00` |
| Instrument selected, 4 s | 2,392 | 0=474, 2=474, 3=474, 4=474, 5=474, 7=7; 1=0, 6=0 | `$00` / `$00` |
| After one musical key, 5 s | 2,650 | 0=529, 2=528, 3=528, 4=528, 5=528, 7=9; 1=0, 6=0 | `$00` / `$04` |

Total: 10,737 complete PAR reads. High-RAM writes remained live in every phase.
This validates the negative selector-1/6 observation only for the tested
ASR-10 model and states; the static condition supplies the separate selector-1
explanation.

## Complete selector map

| Selector | Runtime occurrence | Selection condition | PAR read / producer | Semantic control | RAM state | Diagnostic identity | Current MAME candidate | Status |
|---:|---|---|---|---|---|---|---|---|
| 0 | Every ordinary cycle | Unconditional | yes; `$F8D920` | Pitch wheel | block `$0D8A`; viewer `$0D8F`; dead zone `$0DDE/$0DE0` | `PITCHWHL` | none | `[Verified firmware + runtime]` |
| 1 | None on current ASR-10 | `$FFCCD1 != 0` (model `88`) and `$FFD0EA == 0` after key-event countdown | yes; `$000172EC` | ASR-88 mono/channel pressure | block `$D0DC`; filter `$D0E2`; processed `$D0E0`; shared controller index `$0E` | none among six analog pages | none | condition/path `[Verified]`; semantic `[Likely]` |
| 2 | Every ordinary cycle | Unconditional | yes; `$F8D992` | Mod wheel | block `$0D98`; viewer `$0D9D` | `MODWHEEL` | none | `[Verified firmware + runtime]` |
| 3 | Every ordinary cycle | Unconditional | yes; `$F8DA58`, then smoothing/slew | Volume | block `$0DB4`; viewer `$0DB9` | `VOLUME` | Volume adjuster, currently routed to emulator index 5 | `[Verified firmware + runtime]` |
| 4 | Every ordinary cycle | Unconditional | yes; `$F8DA98` | Pedal/CV | block `$0DA6`; viewer `$0DAB` | `PEDAL` | no pedal; Input Level adjuster currently occupies emulator index 4 | `[Verified firmware + runtime]` |
| 5 | Every ordinary cycle | Unconditional | yes; `$F8D9DE` | MR. KNOB / Data Entry slider | block `$0DC2`; viewer `$0DC7` | `MR. KNOB` | Data Entry adjuster, currently routed to emulator index 3 | `[Verified firmware + runtime + vendor terminology]` |
| 6 | Never | no generator in analyzed path | none | `[OPEN]` (unused/reserved physically possible) | none | none | none | `[Verified unreachable in analyzed V3.50 acquisition path]` |
| 7 | About every 60 scans; also boot calibration | countdown `$0DF0`; explicit boot calls | yes; `$0068C8/$F8DB4C`, boot `$0067EC/$006864` | Calibration reference | block `$0DD0`; displayed high byte `$0DD6`; factor `$0DF2` | `REFRENCE` | none; should not be a user control | `[Verified calibration/reference role]` |

## Pre-implementation MAME control inventory

### Analog inputs

All three current controls are `IPT_ADJUSTER`, range 0..1023, transported as a
left-shifted 16-bit value through `asr10panel_device::analog_changed()` ->
`write_analog` -> `asr10_boot_state::analog_w(index)`. The ES5506 callback then
incorrectly chooses `m_analog_values[m_duart_io & 7]`; it does not use PBDAT.

| Host/UI name | Default | Callback parameter | Current destination | Firmware through PAR? | Assessment |
|---|---:|---:|---|---|---|
| Data Entry | 512 | 3 | `m_analog_values[3]` | no | semantic control exists, but selector should be 5 and selection source is wrong |
| Input Level | 512 | 4 | `m_analog_values[4]` | no | collides numerically with firmware Pedal selector; Input Level is not in this scan |
| Volume | 1023 | 5 | `m_analog_values[5]` | no | semantic control exists, but selector should be 3 and selection source is wrong |

There are no ASR-10 host inputs for pitch wheel, mod wheel, pedal/CV or
ASR-88 mono pressure. There are also no modeled footswitch or Patch Select
inputs. The current musical typing keys are digital panel/key messages with
fixed velocity, not an analog keybed/controller acquisition model. Footswitch
and Patch Select are digital tests in the service flow and are not members of
the PAR selector domain.

### MR. KNOB and Data Entry

**[Verified vendor terminology/functional identity]** The Service Manual says
that testing the *Data Entry Slider* produces the diagnostic label `MR. KNOB`
and specifies the same 255..0 endpoint values. These are one physical semantic
control with two names, not two controls. A future model should expose one
`DataEntry` identity and document `MR. KNOB` as the diagnostic alias.

### Input Level

**[DISPROVEN as a member of the verified V3.50 PBDAT/PAR scan].** Input Level
does not appear among the six analog diagnostic pages; selectors 0,2,3,4,5,7
are mapped, selector 1 is the ASR-88 conditional path, and selector 6 is
unreachable in this acquisition routine. Vendor documentation describes the
Input Level Trim Control as gain for the external audio input, while the four
Input Level lamps meter the pre-effects audio input. Thus it belongs to the
audio-input/gain subsystem, not automatically to U55/PAR. Its exact electrical
and emulated acquisition/control mechanism remains `[OPEN]`.

The existing Input Level adjuster is consequently both disconnected from
firmware acquisition and dangerous to retain at selector-like index 4, whose
actual firmware identity is Pedal.

## Selector 7: reference contract

Selector 7 is not a spare host control. During boot `$0067EC` takes eight
samples through `$006864`, stores the sum/high-byte state in the reference
block, and computes a 0.16 fixed-point factor as `$A3480000 / sample_sum`.
Zero raises the previously verified divide-by-zero path (ERROR 130); overflow
is deliberately saturated. `$0068C8` later samples the same channel, applies a
three-quarter old plus one-quarter new filter, and recomputes `$0DF2` when the
reference changes.

The Service Manual requires diagnostic `REFRENCE` to read at least 190. The
viewer presents the high byte of the left-shifted ten-bit domain, approximately
`raw >> 2`; therefore a service-conforming raw source is at least about 760.
The current default raw 512 displays 128 and is sufficient only to avoid the
boot divide-by-zero. The exact voltage, nominal value and tolerance are
`[OPEN board-level]`; a future implementation must supply a stable,
nonzero calibration source rather than expose selector 7 as a user adjuster.

## Raw-value contract

The generic ES5506 read-port callback contract is a right-justified ten-bit
value `0..1023`; the device masks it with `$03FF`. Firmware, not the board
callback, then shifts left six and performs control-specific calibration,
filtering, dead-zone handling, multiplication, clamping and volume slew.

Consequently the ASR-10 board callback returns **raw 10-bit values**,
not preprocessed 7-bit or 8-bit controller values. The earlier universal
`raw >> 3`/`raw >> 2` processing hypothesis remains `[DISPROVEN]`; the
diagnostic viewer's displayed byte is not the producer contract.

Verified monotonic firmware paths map increasing raw values to increasing
pitch, modulation, volume and Data Entry results. Pedal has firmware mode/
polarity handling and should not be assigned a physical direction without a
targeted test. Pitch center is boot-calibrated and must be stable, but its exact
physical raw midpoint is not measured. Selector 1's ASR-88 curve table cannot
be characterized from the current ASR-10 ROM/model.

## Implemented emulator contract

Ownership should be:

```text
MC68302 generic PBDAT output
        -> ASR-10 board/machine policy: selector = PBDAT & 7
        -> semantic raw-source mux
        -> generic ES5506 10-bit read-port callback
```

The generic MC68302 must not know ASR-10 control names. The generic ES5506 must
not know U55 or selector meanings. `asr10_boot_state` (or a small ASR-specific
board helper) should own selector routing. The panel/input device may expose
semantic host controls and raw values, but should neither select the mux nor
bypass PBDAT. Input Level belongs to a future audio-input path.

Implemented in `asr10_boot_state`: `mc68302_device::pbdat_latch()` exposes the
generic CPU output latch; `analog_r()` masks PB2-PB0 and selects the ASR-owned
raw source before the generic ES5506 PAR callback. The panel exposes 10-bit
Pitch, Mod, Volume, Pedal/CV and Data Entry inputs at selectors 0, 2, 3, 4 and
5. Selector 7 is the fixed `$300` calibration source. Selectors 1 and 6 remain
neutral, unlabelled board fallbacks: this ASR-10 model supplies no pressure
producer and assigns no invented selector-6 role. Input Level was removed from
this mux domain.

`lua/analog_pot_wiring_verify.lua` runtime-verifies selectors 0, 2, 3, 4, 5
and 7 through firmware PBDAT writes and ES5506 PAR reads, with a retained RAM
witness. It also reaches `EXAMINE ANALOG INPUTS` and observes `PITCHWHL 64`.

## Status and remaining questions

| Claim | Status |
|---|---|
| PBDAT PB2-PB0 selects the functional PAR source | `[Verified firmware + runtime]` |
| selectors 0/2/3/4/5/7 identities | `[Verified firmware + runtime]` |
| selector 1 path is gated by model `88` and key-event countdown | `[Verified firmware]` |
| selector 1 is ASR-88 mono/channel pressure | `[Likely]` |
| selector 6 is unreachable in this V3.50 acquisition path | `[Verified, bounded]` |
| MR. KNOB is the Data Entry slider | `[Verified vendor terminology]` |
| Input Level belongs to this PAR scan | `[DISPROVEN]` |
| selector 7 is a calibration/reference source, not a host control | `[Verified firmware]` |
| U55 is the functional acquisition mux | `[Likely]` |
| U55 COM/select/enable/X0-X7 physical nets | `[OPEN board-level provenance]` |
| PBDAT-selected ASR semantic-source mux | `[Implemented; runtime-verified for 0/2/3/4/5/7]` |

The smallest remaining falsifiable experiment is an ASR-88-specific run with
the correct boot ROM/model: produce one qualifying key/pressure event and
witness selector 1 -> PAR -> `$FFD0E0` -> shared controller index `$0E` while
varying a real raw pressure input. This would promote or disprove the semantic
identity without touching selector 6 or guessing U55 nets. It is not required
to implement the ASR-10 functional board contract.
