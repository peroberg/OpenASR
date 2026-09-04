# Ensoniq audio-architecture comparison — VFX, SD-1 and ASR-10

## 1. Purpose and evidence boundary

This is a comparative architecture check, not a rate-switch implementation.  It
asks what the immediately preceding MAME-supported Ensoniq family actually
establishes about ASR-10's missing board-level audio path.

Evidence domains are deliberately separate:

- **[Verified MAME code]**: current source configures a device or route this
  way; it is not PCB-wiring evidence.
- **[Verified source]**: a local Ensoniq manual/data sheet says so.
- **[Verified ASR firmware/runtime]**: existing V3.50 evidence cited below.
- **[External]**: a cited external document states this, only.
- **[OPEN]**: this comparison did not establish it.

No C++ was changed, no probe was retained, and no non-ASR firmware was run.
`./mame -verifyroms sd132` reports `romset "sd132" not found` in this
workspace.  Thus there is no local SD-1/32 ROM image suitable for a static or
runtime ACT trace; a ROM set declared in source is not an available ROM.

## 2. ASR-10 baseline

| Attribute | ASR-10 V3.50/current harness |
|---|---|
| CPU | MC68302, 16 MHz [Verified board inventory / MAME code] |
| Generator | ES5506/OTTO, U29 [Verified source/board inventory] |
| ESP | ES5510, U43 [Verified source/board inventory] |
| Glue | U41 is reported ES5701-family; exact ASR wiring [OPEN] |
| Oscillators | Y1 16 MHz, Y2 30.47618 MHz, Y3 33.8688 MHz [Verified source] |
| Generator clock in MAME | direct Y2, 30.47618 MHz [Verified MAME code]; physical path [OPEN] |
| ESP in MAME | 10 MHz plus `set_disable()` [Verified MAME code]; board clock [OPEN] |
| Documented mode | 29.7619 kHz / 31 voices; 44.1000 kHz / 23 voices; current effect determines it [Verified source] |
| Firmware state | descriptor +$66 -> runtime $0CE3 -> $0D5E+1 -> D3/D4 -> ACTV [Verified ASR firmware/runtime] |
| Active slots | mode 0: ACTV $1f = 32; mode 1: ACTV $17 = 24 [Verified ASR runtime] |
| 31/23 versus slots | 31/23 -> 32/24 is [Likely], allocator provenance [OPEN] |
| Serial path | ES5506 MODE=$0d is boot-written; physical BCLK/LRCLK/ESP/DAC path [OPEN] |
| Current output model | ES5506 -> speakers, synthetic 44.1-kHz PB3 LRCLK, no pump/ES5701/ESP audio path [Verified MAME code] |

The established ASR A -> B -> A effect transition proves ACTV $1f -> $17 ->
$1f.  It does **not** prove Y2/Y3 selection, a /2 stage, a mux, an ES5701
rate register, or ESP frame timing.

## 3. Sources and scope of the comparison

The immediately preceding family lives in
`src/mame/ensoniq/esq5505.cpp`.  Its heading explicitly covers VFX, VFX-SD,
SD-1 (21 voice), and SD-1 (32 voice), and describes itself as an **ES5505 +
ES5510** driver.  It is not an ES5506 driver.

Its VFX-family board-layout comment names OTISR2, ESP, ENSONIQ GLU, ENSONIQ
SUPERGLU, 16 MHz and 30.47618 MHz crystals.  This is source-family
provenance, not proof that every SD-1 revision has the same PCB routing or
that a given label is an ES5701 part number.

Consulted evidence:

- local MAME `esq5505.cpp`, `es5506.cpp`, and `esqpump.cpp`;
- local ES5506/ES5510/ES5701 documents and the existing ASR investigations;
- [Ensoniq VFX/SD-1 Service Manual](https://www.vintagesynthparts.com/wp-content/uploads/2018/01/Ensoniq-VFX-SD1-Service-Manual.pdf)
  [External, accessed 2026-08-27], used only for its stated VFX/VFX-SD/SD-1
  product-family scope, not as a clock schematic.

## 4. Actual MAME machine inventory

| Machine | CPU/current MAME clock | Generator | ESP | Glue evidence | Host mapping | Current MAME route |
|---|---|---|---|---|---|---|
| VFX | M68000, 10 MHz | ES5505, 10 MHz | ES5510, 10 MHz, disabled | GLU/SuperGLU source-comment label | OTIS $200000; ESP $260000, low byte lane | ES5505 -> pump -> main |
| VFX-SD | M68000, 10 MHz | ES5505, 10 MHz | ES5510, 10 MHz, disabled | same family evidence | same, plus FDC/seq RAM | pump main plus Aux bypass |
| SD-1 (21) | inherits VFX-SD | ES5505, 10 MHz | ES5510, 10 MHz, disabled | no SD-1-specific identity here | same | same pump route |
| SD-1 (32) | SD-1 then CPU/OTIS/pump set to 30.47618 MHz / 2 | **ES5505**, not ES5506 | inherited 10 MHz, disabled | physical board/net [OPEN] | same | same pump route |
| ASR-10 | MC68302, 16 MHz | ES5506, direct 30.47618 MHz in MAME | ES5510, 10 MHz, disabled | U41 ES5701-family reported; path [OPEN] | ES5506 $fc2000; ESP $fc3000-$fc31ff | ES5506 direct -> speakers |

All predecessor rows are [Verified MAME code] except explicitly limited cells.
In particular, `sd132()` calls `sd1()`, then changes CPU, ES5505 and pump
clocks.  It neither instantiates ES5506 nor changes the inherited ES5510
10-MHz configuration.

## 5. Chip-generation comparison

| Property | ES5505 / OTIS | ES5506 / OTTO |
|---|---|---|
| Source-header maximum | 10 MHz | 16 MHz |
| Voice array | 32 | 32 |
| Active register | ACT, zero-based last active voice | ACTV, zero-based last active voice |
| Generic MAME rate | master_clock / (16 * (active + 1)) | same |
| Serial interface in source header | 4-channel stereo serial port | 6-channel stereo serial port; programmable serial clocks |
| Other stated distinction | no hardware envelope/compressed-data path in MAME | envelope, compressed-data and dual-OTTO support |

Both implementations recalculate sample rate when ACT/ACTV changes, then
iterate voices zero through that register value once per generated stream
sample.  Therefore more active slots -> lower per-voice sample rate is a
generic OTIS/OTTO principle [Verified MAME code].  That does not establish an
effect-controlled rate policy in VFX or SD-1.

## 6. Rate, active-voice and polyphony comparison

| Machine | Product-facing figure available here | Documented system-rate mode in this pass | Effect dependency |
|---|---:|---|---|
| VFX | [OPEN] from local primary material | [OPEN] | [OPEN] |
| VFX-SD | [OPEN] from local primary material | [OPEN] | [OPEN] |
| SD-1 | 21 voices [Verified MAME metadata] | [OPEN] | [OPEN] |
| SD-1/32 | 32 voices [Verified MAME metadata] | [OPEN] | [OPEN] |
| ASR-10 | 31 / 23 [Verified source] | 29.7619 / 44.1000 kHz [Verified source] | current effect determines mode [Verified source] |

MAME resets ES5505 to active-last-voice 31.  It has a generic
`sample_rate_changed() -> pump.set_unscaled_clock()` bridge, but
`esq5505.cpp` has no effect-specific policy.  With no SD-1/32 ROM image this
pass cannot establish boot/runtime ACT writes, much less a $1f/$17 pair.

MAME's SD-1/32 configuration sets ES5505 to
30.47618 MHz / 2 = 15.23809 MHz.  With 32 slots the generic formula produces
29.7619 kHz.  This is a [Verified MAME code] halved-crystal precedent and
[Arithmetic/model precedent] only for ASR.  It is not evidence for ASR Y2/2,
Y3/2, a 44.1-kHz source, or a runtime switch.

## 7. ESP host and audio path

### 7.1 Host contract

VFX/VFX-SD/SD-1 map generic ES5510 host access directly at
$260000-$2601ff on the low byte lane.  The prior archived VFX comparison
static-checked VFX-SD and SD-1 firmware against ASR V3.50: their
GPR/INSTR record dispatch, commit, read-select and readback structure is
shared, with the device-base literal changed.

ASR's now-verified $fc3000-$fc31ff mapping reaches the same generic host
contract through ASR-specific wrappers.  This transfers the **CPU -> ESP host
protocol**, not physical board glue or clock routing.

### 7.2 The predecessor pump

`esq_5505_5510_pump_device` is explicitly modeled for the VFX family:

```text
ES5505 eight L/R serial stream inputs
    -> pump
       -> ES5510 SER0..SER2 writes
       -> one ESP run_once() per pump stream sample when not halted
       -> ES5510 SER3 reads
       -> Main stereo output

ES5505 pair 0 -> pump Aux stereo output (bypass ESP) on VFX-SD and later
```

This is [Verified MAME code] for a generator -> ESP -> output architectural
layer.  It is not a physical BCLK/LRCLK/WCLK model: it uses synchronous MAME
sound streams plus `ser_w`/`ser_r`, not board nets.

All these MAME configs call `set_disable()` on ESP.  Host state is still
live, but DSP execution is not; therefore the predecessor does not supply a
live effects/frame-rate oracle.  ASR currently bypasses this entire layer.

## 8. Glue and clock topology

| Family | Established | Not established |
|---|---|---|
| VFX | GLU/SuperGLU labels, OTISR2, ESP and 16/30.47618-MHz parts occur in MAME's board-layout comment | chip part IDs, clock-to-pin routing, divider nets, BCLK/LRCLK direction, runtime clock selection |
| SD-1/32 | MAME uses 30.47618/2 for CPU and generator; ESP remains 10 MHz | physical oscillator list, /2 routing, shared frame clock, effect-controlled selection |
| ASR-10 | Y1/Y2/Y3, U29/U41/U43 inventory, ACTV transition and ES5701 silicon capability are known | U41 wiring, Y2/Y3 selection, /2, mux/divider, ESP timing, serial/DAC path, board rate-control write |

The ES5701 specification proves CPU<->ESP/OTIS bus glue, sound-memory
adaptation and fixed divider capabilities.  It does not prove a selectable
ASR rate control.  The predecessor's SuperGLU label is compatible with an
audio-glue family role, but does not prove ES5701 presence or a rate switch in
VFX/SD-1.

## 9. Current-MAME comparison

| Aspect | VFX/VFX-SD/SD-1 family | ASR-10 harness | Transfer limit |
|---|---|---|---|
| Generator | ES5505 | ES5506 | share ACT formula, not serial/clock identity |
| ESP host | direct generic mapping | ASR wrappers, now verified | generic host semantics transfer |
| ESP execution | disabled | disabled | neither is a live DSP timing oracle |
| Audio routing | generator -> pump -> ESP abstraction -> output | ES5506 -> speakers | pump is a boundary reference, not an ES5506 drop-in |
| Clock relation | generator rate callback sets pump stream rate | direct Y2 and synthetic PB3 LRCLK | model precedent, not physical ASR wiring |
| Serial signals | abstracted | physical path unmodeled | no reusable BCLK/LRCLK wire model |

## 10. Hypothesis verdicts

| Hypothesis | Verdict | Evidence and limit |
|---|---|---|
| H1: effect-dependent active-voice/sample-rate tradeoff is a general OTIS/OTTO design principle | **PARTIALLY SUPPORTED** | Both chips have the mechanism; only ASR demonstrates effect-selected ACTV here. |
| H2: ACTV $1f/$17 recurs in SD-1/32 | **OPEN** | SD-1/32 uses ES5505 ACT in MAME; no local ROM/runtime witness. |
| H3: 31/23 playable voices are a family reserved-voice convention over 32/24 slots | **OPEN** | No allocator or slots-minus-one dataflow. |
| H4: ES5506 and ES5510 share/coordinate a system audio frame rate | **PARTIALLY SUPPORTED** | Pump runs ESP once per generator-derived sample; ASR physical common frame remains [OPEN]. |
| H5: ES5701/glue is for clock/frame/host coordination rather than synthesis/DSP | **PARTIALLY SUPPORTED** | ES5701 spec proves host/sound-memory glue and fixed dividers; runtime-selectable timing is unproven. |
| H6: ASR direct ES5506->speaker skips an architectural layer modeled by a related driver | **SUPPORTED** | VFX pump provides generator->ESP->output; ASR has ESP hardware but bypasses it in current MAME. |

## 11. What transfers, and what does not

**Transfers**

- Generic active-slot sample-rate semantics.
- The CPU-to-ES5510 program upload/readback contract.
- A separate generator -> ESP -> output boundary as the correct shape for a
  future ASR design.

**Does not transfer**

- SD-1/32 is not an OTTO or ASR clock-topology facsimile: current MAME uses
  ES5505.
- The pump accepts four serial input pairs; ES5506 provides six.  It cannot be
  inserted unchanged in ASR.
- Disabled ESP means neither family supplies live DSP/frame evidence.
- SD-1/32's 30.47618/2 model does not prove ASR Y2/Y3 routing or switching.

## 12. One minimal next ASR experiment

Do not search oscillators first.  At the already verified A -> B -> A effect
transition, retain an execution witness at the ACTV writer ($00e826) and
capture only the MC68302 PB3/LRCLK input cadence plus any firmware-visible
write which changes that boundary.

Prediction: a board-level shared-frame model gives a reversible,
mode-correlated cadence or a concrete controlling write.  If PB3 remains the
same synthetic input and firmware performs no related write in a live A/B/A
window, only that observed boundary candidate is falsified; Y2/Y3 and ES5701
routing remain [OPEN].  Use a temporary Lua probe and remove it afterwards.

## 13. Comparative summary

| Attribute | VFX | VFX-SD | SD-1 | SD-1/32 | ASR-10 |
|---|---|---|---|---|---|
| generator | ES5505 [MAME] | ES5505 [MAME] | ES5505 [MAME] | ES5505 [MAME] | ES5506 [source/board] |
| ESP | ES5510 disabled [MAME] | same | same | same | ES5510 disabled [MAME] |
| glue | GLU/SuperGLU label [source comment] | same family [Likely] | [OPEN] | [OPEN] | U41 ES5701-family [board], wiring [OPEN] |
| generator clock | 10 MHz [MAME] | 10 MHz | 10 MHz | 15.23809 MHz [MAME] | 30.47618 MHz direct [MAME], physical [OPEN] |
| ESP clock | 10 MHz [MAME] | 10 MHz | 10 MHz | inherited 10 MHz | 10 MHz placeholder |
| active mode | generic ACT [device] | same | same | same | ACTV $1f/$17 [runtime] |
| rate/polyphony modes | [OPEN] | [OPEN] | [OPEN] | [OPEN] | 29.7619/31, 44.1/23 [source] |
| serial source | [OPEN] | [OPEN] | [OPEN] | [OPEN] | [OPEN] |
| generator -> ESP | pump [MAME] | pump | pump | pump | [OPEN] |
| MAME fidelity | imperfect, ESP disabled | imperfect | imperfect | imperfect | research harness; audio board path missing |

```text
VFX:
    generator = ES5505 in current MAME
    ESP = ES5510, host-mapped but execution disabled
    glue = GLU/SuperGLU labels; part/net OPEN
    rate model = generic ACT formula; effect policy OPEN

VFX-SD:
    generator = ES5505 in current MAME
    ESP = ES5510, disabled
    glue = VFX-family evidence only
    rate model = generic ACT formula; effect policy OPEN

SD-1:
    generator = ES5505 in current MAME
    ESP = ES5510, disabled
    glue = OPEN
    rate model = generic ACT formula; no local runtime trace

SD-1/32:
    generator = ES5505 in current MAME, not ES5506
    ESP = ES5510, inherited 10 MHz and disabled
    glue = physical topology OPEN
    rate model = MAME uses 30.47618 MHz / 2; no firmware ACT witness

ASR-10:
    generator = ES5506
    ESP = ES5510
    glue = U41 ES5701-family reported; physical path OPEN
    rate model = effect mode -> ACTV $1F/$17 + other resource changes

MOST RELEVANT PREDECESSOR:
    VFX/SD driver and its pump for the generator -> ESP -> output boundary,
    not SD-1/32 as an OTTO clock-topology facsimile.

REUSED ARCHITECTURAL CONTRACTS:
    active-slot rate mechanism; CPU -> ES5510 host upload/readback; a distinct
    generator/ESP/output boundary.

ASR-SPECIFIC DIFFERENCES:
    ES5506 rather than ES5505; six rather than four serial input pairs;
    verified ACTV transition; documented system modes; U41/Y2/Y3.

ES5701 VERDICT:
    silicon glue capabilities verified; ASR wiring/rate selection OPEN.

CLOCK/RATE LESSON:
    SD-1/32's MAME /2 is useful precedent only, never ASR routing evidence.

SERIAL AUDIO LESSON:
    MAME has a family pump abstraction, not physical BCLK/LRCLK wiring.

MAME MODEL LESSON:
    ASR skips the generator -> ESP -> output layer; pump is a boundary
    reference, not an insertable ES5506 solution.

NEXT MINIMAL ASR EXPERIMENT:
    live A/B/A correlation of the ACTV writer with the PB3/frame boundary.
```
