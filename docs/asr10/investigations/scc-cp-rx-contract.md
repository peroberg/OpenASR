# Minimal SCC/CP receive contract

Date: 2026-08-23
Scope: ASR-10 V3.50 sampling receive path and the current
`mc68302_device`

## Question

What is the smallest MC68302 CP/SCC receive behavior that can replace the
Phase 1 descriptor-completion bridge while leaving ASR-10 firmware in control
of descriptor consumption, threshold detection, IDMA setup and recording?

This is a contract analysis, not an implementation. No executable code,
memory map, ADC path, Port B source, ES5506 or ES5510 behavior was changed.

## Answer

The minimum useful boundary is a **post-framing receive-byte ingress** in the
MC68302 device. Bytes presented there must be accepted only by an enabled SCC
receiver and must drive these CP-owned effects:

```text
RX byte ingress
  -> write bytes through the current RX descriptor buffer address
  -> accumulate at most MRBLR bytes
  -> write the received byte count
  -> clear descriptor E/ownership while preserving control and wrap bits
  -> advance the CP receive descriptor, honoring W/wrap
  -> set SCCE receive event bit 0
  -> gate SCCE through SCCM and the channel IMR bit
  -> assert internal level 4
  -> supply vector $4D for SCC1 or $4A for SCC2 on IACK
```

For the already-measured sampling case, completing one SCC1 descriptor with
800 controlled bytes is sufficient. Firmware then performs every later step:
`$00643C`, threshold/pretrigger selection, range publication, IDMA programming
and recording-state transition.

The descriptor state that firmware accepts is `[Verified]` from firmware and
Phase 1 runtime for the full-buffer SCC1 case. CP production of that state has
not yet run in the model; it remains the next implementation boundary. Bit
timing, physical pins, serial framing, early frame termination and the analog
source remain `[OPEN]` and are outside this contract.

## Evidence boundary

The contract combines four independent evidence classes:

- `[Verified firmware]` ROM `$F8C0CE-$F8C222` initializes the two channels,
  rings and event masks and issues CP commands.
- `[Verified firmware]` SCC handlers `$008D56/$008D92` and common routine
  `$00643C` consume SCC events and completed receive descriptors.
- `[Verified runtime]` Phase 1 showed that a CP-shaped SCC1 completion reaches
  `RECORDING` and supplies the firmware-selected IDMA source range.
- `[Verified source/model]` the present `mc68302_device` shadows SCC parameter
  RAM and registers but has no receive ingress, descriptor engine, SCC event
  semantics or internal level-4 controller.

Normative detail and provenance are in
`../investigations/virtual-scc-rx-chain.md`,
`../investigations/scc-cp-rx-chain.md`,
`../investigations/scc-rx-source-and-consumer.md`,
`../reference/mc68302-status.md`, and
`../../mc68302/interrupt-source-map.md`.

## 1. Register contract

### Channel-independent CP and serial registers

| Address | Direction and observed value | When | Minimum required behavior |
|---|---|---|---|
| `$FC6860` CP command register, high byte | CPU polls FLG bit 0, writes `$81`, `$21` or `$23`, then polls FLG again | CP reset and each SCC reinitialization | Complete the FLG handshake. `$81` resets CP state; `$21/$23` put SCC1/SCC2 in ENTER HUNT MODE. The exact command effect on an in-progress descriptor pointer is `[OPEN]`; the freshly initialized acceptance case begins at BD0. |
| `$FC68B4` SIMODE | CPU writes `$4189` | initial and repeated SCC setup | Retain the value. Its precise routing significance for the post-framing byte boundary is `[OPEN]`; bit-level ingress must not be claimed without decoding it. |
| `$FC6812` GIMR | CPU writes `$8040` once | early boot | Retain vector base `$40` and internal-request mode needed for SCC vectors. |
| `$FC6814` IPR | CPU writes W1C during reset/shutdown; CP/controller produces pending state | boot, SCC shutdown, receive event | Represent SCC1 bit 13 and SCC2 bit 10 pending state, with local SCCE clearing controlling event-backed pending state. |
| `$FC6816` IMR | CPU writes `$E480` armed and `$C080` disarmed | SCC arm/disarm | Bit 13 enables SCC1 and bit 10 enables SCC2. An SCC event may latch while masked, but must assert level 4 only while eligible. |
| `$FC6818` ISR | CPU initializes/clears W1C; controller sets source on IACK | boot and handler EOI | Set the selected SCC source in service on IACK; clear SCC1 on firmware `$2000`, SCC2 on `$0400`. |

`[Verified model]`: the current `$FC6860` helper only delays and clears FLG. It
does not execute `$81/$21/$23`. GIMR/IPR/IMR/ISR are shadow storage, and level-4
IACK currently returns the 68000 autovector. The existing behavior lets the
firmware's polls finish but does not satisfy the receive contract.

### Per-channel SCC registers

| Function | SCC1 | SCC2 | Firmware use and observed value | Minimum required behavior |
|---|---:|---:|---|---|
| register base | `$FC6880` | `$FC6890` | passed to the channel ISR and `$00643C` | select the channel instance |
| SCON | `$FC6882` | `$FC6892` | CPU writes `$7000` during setup | retain configuration; exact clock-source decode is `[OPEN]` for a post-framing byte ingress |
| SCM | `$FC6884` | `$FC6894` | `$7033` disarmed, `$703B` armed | implement at least ENR bit 3; reject or defer ingress while ENR is clear |
| DSR | `$FC6886` | `$FC6896` | no verified firmware access; probe reads `$0000` from shadow storage | no demonstrated functional requirement for this receive slice |
| SCCE, high byte | `$FC6888` | `$FC6898` | CPU writes `$FF` to clear; `$00643C` reads it and W1C-clears bits 2 or 0 | latch CP events independently of the mask and implement W1C |
| SCCM, high byte | `$FC688A` | `$FC689A` | CPU writes `$05` | enable SCCE bits 2 and 0 as interrupt causes |

`SCCE` bit 0 is the accepted receive-completion event in this firmware path.
That classification is `[Verified]` by `$00643C` descriptor handling and the
Phase 1 `$01` event. Bit 2 enters the separately verified shutdown/abort path;
its exact protocol-level name is not needed for the minimal success path and
remains `[OPEN]` here.

The Phase 1/full-RECORD snapshots displayed `SCCE=$FF00`. That is not a valid
silicon event-state observation: firmware's earlier `$FF` clear write remains
stored because the current model treats SCCE as ordinary RAM. A real SCCE must
read cleared after that W1C operation.

The exact protocol represented by `SCM=$703B`, the SCON clock selection and
the physical SCC pin assignment remain `[OPEN]`. Only the `$7033/$703B` ENR
difference is required and directly verified for this contract.

### Which register semantics are actually load-bearing?

For a byte ingress placed after framing, the minimum functional set is:

```text
CP command FLG plus reset/enter-hunt effect
SCM.ENR
SCCE W1C and event latching
SCCM event enable
GIMR vector base
IPR/IMR/ISR SCC source state
parameter RAM descriptor traversal
```

SCON, SIMODE and the remaining protocol fields must retain firmware values but
do not need speculative clock/framing decode at this boundary. DSR and SCCS
have no verified firmware dependency in the measured receive path.

## 2. Parameter RAM and descriptor contract

### CPU initialization

ROM helper `$F8C1BE-$F8C222`, called by both `$F8C16C` and `$F8C188`,
constructs the rings from control-object fields rather than hard-coded buffer
addresses:

```text
object +$08 = descriptor count       $0008
object +$0A = buffer stride/MRBLR    $0320
object +$28 = object-relative first-buffer offset
```

It writes eight descriptors, clears object indices `+$0C/+$0E/+$10`, writes
the parameter word at channel offset `+$82` to `$0320`, clears selected
parameter fields, clears SCCE and writes SCCM `$05`. It then issues ENTER HUNT
MODE for the channel.

The verified receive rings are:

| Channel | Descriptor range | Buffers | Last descriptor |
|---|---|---|---|
| SCC1 | `$FC6400-$FC643F` | `$F76600-$F77EFF`, eight `$0320`-byte ranges | `$FC6438`, status `$F000` |
| SCC2 | `$FC6500-$FC653F` | `$F74B00-$F763FF`, eight `$0320`-byte ranges | `$FC6538`, status `$F000` |

The first SCC1 and SCC2 buffers are object-relative `+$0200` from objects
`$F76400` and `$F74900` respectively.

The CPU also writes these channel-relative parameter locations:

| SCC1 / SCC2 | CPU value | Classification |
|---|---:|---|
| `$FC6480 / $FC6580` | `$0000` | `[Verified write]`; exact field name not required here |
| `$FC6482 / $FC6582` | `$0320` | `[Verified]` MRBLR |
| low byte `$FC6487 / $FC6587` | `$00` | `[Verified write]`; exact CP-pointer/state name `[OPEN]` |
| `$FC64AC/$FC64AE` and `$FC65AC/$FC65AE` | `$0000` | `[Verified write]`; exact protocol-specific names `[OPEN]` |

Earlier probes printed friendly names such as `RBPTR`, `CURRENT` and `RXTMP`.
Only the addresses and values above are direct evidence in this tree. Those
names must not become implementation requirements until checked against the
applicable MC68302 parameter-RAM layout.

### Descriptor format

Each receive descriptor is eight bytes:

| Offset | Width | CPU initializes | CP must produce |
|---:|---:|---|---|
| `+$00` | 16 | status/control `$D000`; last `$F000` | clear E on completion; preserve control/W; supply valid low status |
| `+$02` | 16 | byte count `$0000` | actual received length, bounded by MRBLR |
| `+$04` | 32 | payload buffer address | use as SDMA destination; do not replace it |

Verified status bits used by firmware:

- word bit 15, E/ownership: CPU sets it to give an empty descriptor to CP;
  CP clears it to publish completion;
- word bit 13, W/wrap: set only on descriptor 7; firmware and CP return to
  descriptor 0 after it;
- low status byte bit 1: firmware raises ERROR 006 when set;
- low status byte bit 0: firmware rechecks SCC events instead of accepting the
  descriptor while set;
- the other high control bits present in `$D000/$F000` are `[OPEN]` and must be
  preserved rather than reinterpreted.

The branch-aligned decode matters. Normal completion reaches `$00648A`, which
tests low-byte bit 1 at descriptor `+$01`; `$00649A` tests low-byte bit 0. A
linear disassembly beginning at the non-returning error-trap padding misdecodes
those bytes as `ori` instructions.

### Ownership boundary

CPU and CP responsibilities are therefore exact:

| CPU/firmware initializes or consumes | CP/SCC receive engine produces |
|---|---|
| descriptor count and buffer addresses | payload writes through current descriptor `+$04` |
| MRBLR `$0320` | received length at `+$02` |
| E=1 and W on the final descriptor | E clear on completion, W/control preserved |
| software consumer index at object `+$0C` | independent CP producer/current-descriptor state |
| SCCM and IMR masks | SCCE event and pending internal interrupt |
| sets E again at `$00647C` after consumption | advances to the next CP descriptor, wrapping on W |

`[Verified firmware]`: no CPU path in the receive handler fills payload bytes,
writes the completed length or clears E. Firmware can return ownership, but an
external receive engine must create the completed state.

The need for a CP producer position and W-based advance is `[Likely contract]`
for repeated reception: firmware builds eight entries, marks the eighth W and
advances its independent consumer index. Phase 1 completed only BD0 and did not
observe the CP's producer pointer itself, so no particular parameter-RAM field
is assigned that identity here.

For the first constrained implementation, descriptor completion at MRBLR is
the measured success case. Completion on an earlier frame, idle, break or
protocol delimiter is `[OPEN]` and must not be invented to satisfy Phase 3A.

## 3. Interrupt contract

All MC68302 internal requests arrive at CPU level 4. With `GIMR=$8040`, the
verified vectors relevant here are:

| Vector | Source | Meaning established by firmware/runtime |
|---:|---|---|
| `$4D` | SCC1, source bit 13 | SCC1 channel interrupt. It is receive completion only when SCCE bit 0 is set. |
| `$4A` | SCC2, source bit 10 | SCC2 channel interrupt; same SCCE decoding. Not exercised by the Phase 1 source-mode-0 run. |
| `$4B` | IDMA, source bit 11 | IDMA normal/error completion selected by CSR/CMR, not an SCC RX event. |

This resolves the requested `$4D/$4B` distinction:

- `[Verified runtime]` Phase 1 received `$4D`, entered the SCC1 handler and
  consumed SCCE bit 0 and descriptor 0.
- `[Verified firmware/runtime]` after firmware programmed IDMA, `$4B` ran the
  separate IDMA completion continuation `$00AA48`.
- `[DISPROVEN]` `$4B` as an SCC descriptor-completion vector.

Minimum SCC interrupt sequence:

1. CP sets SCCE bit 0 after publishing buffer, count and E-clear state.
2. SCCE bit 0 and SCCM bit 0 make the SCC source pending.
3. IMR bit 13 or 10 makes that pending source eligible and asserts level 4.
4. IACK returns `$4D` or `$4A` from the GIMR base/source code and sets ISR.
5. `$00643C` reads SCCE and W1C-clears bit 0.
6. The channel ISR writes `$2000` or `$0400` to ISR as EOI.

The event must be latched independently of SCCM/IMR; those registers gate
interrupt eligibility, not receipt of the event itself. Exact priority
interaction with unrelated level-4 sources can follow the documented MC68302
controller rules, but the discriminating SCC1-only test needs only one
eligible internal source.

## 4. IDMA boundary

The SCC receive engine does not program IDMA. It delivers a completed
descriptor to firmware.

For the successful Phase 1 descriptor:

```text
descriptor buffer   $F76600
received length     $0320
accepted status     $5000 (E clear, low status clear)
threshold crossing  payload offset $0040
pretrigger extent   $003A
firmware range      $F76606..$F76920
```

Firmware `$0064BA-$0066E6` reads only the descriptor buffer address and length
to construct the received range after `$00643C` has validated status. The
type `$0E` consumer `$00B478` then computes and writes:

```text
SAPR = object + range start   $F76606
DAPR = object +$20            $02C110 in the measured run
BCR  = range end - start      $031A
CMR  = $37A1                  start
```

Thus the minimum CP metadata is:

```text
payload bytes at the CPU-initialized buffer address
correct received byte count
E clear
W/control preserved and low status accepted
SCCE receive event
```

No CP-written IDMA register, recording destination, range-table entry or
sampling-state variable is required or permitted. The existing one-byte IDMA
count discrepancy is a separate model issue and does not enlarge the SCC RX
contract.

## 5. Phase 1 comparison

| Function | Phase 1 bridge | Required CP/SCC behavior |
|---|---|---|
| RX byte reception | generated an 800-byte deterministic vector outside the device | accept bytes through one post-framing channel ingress |
| receiver enable | waited for sampling state but did not derive acceptance from SCM | accept only while channel SCM.ENR is set |
| buffer fill | wrote `$F76600-$F7691F` directly | write through current BD `+$04`, bounded by MRBLR |
| byte count | wrote `$0320` to `$FC6402` | count accepted bytes and publish actual length |
| ownership | changed `$D000 -> $5000` | clear E while preserving W/control and valid low status |
| descriptor advance | selected firmware object index; did not model a CP producer pointer | maintain CP current BD and wrap on W |
| SCC event | wrote SCCE1 high byte `$01` | set SCCE bit 0 after descriptor publication |
| SCC interrupt | asserted level 4 and supplied `$4D` | SCCE/SCCM -> IPR/IMR -> level 4 -> vector `$4D/$4A` |
| event clear/EOI | firmware executed them after the forced vector | implement SCCE and ISR W1C semantics |
| IDMA setup | none; firmware programmed the registers | none; remains firmware-owned |
| IDMA source bytes | bridge later fed the firmware-selected range to `idma_transfer_in()` | separate existing IDMA limitation, not SCC receive behavior |
| IDMA completion | bridge supplied `$4B` | separate IDMA interrupt-controller behavior |

The replacement test has moved behind Phase 1 only if its source invokes the
RX-byte ingress and none of the descriptor, SCCE, IPR/ISR or CPU interrupt
state is written by the test source.

## 6. Recommendation

Choose **A: a minimal ASR-10-constrained SCC receive path**, implemented inside
`mc68302_device`, not as another ASR-10 driver bridge.

The slice should be channel-parameterized and consume firmware-programmed
addresses/registers; "ASR-10-constrained" means only the observed post-framing,
receive-only behavior and MRBLR completion are in scope. It must not hard-code
`$FC6400`, `$F76600` or sampling state. SCC1 is the first acceptance case;
the same mechanics should be reusable for SCC2 by channel selection.

Do not choose a full generic SCC engine yet. A generic engine would require
unverified choices for SCM protocol mode, SCON clocking, serial framing,
early-frame termination, transmit behavior and physical pin routing. None is
needed to test the byte-to-descriptor CP boundary, and implementing them now
would convert `[OPEN]` fields into guesses.

Further observation is not required for the post-framing CP contract. It is
required before moving the ingress boundary backward to SCC pins or an ADC.

### Acceptance observation for the next phase

The smallest discriminating run is the existing Phase 1 panel sequence with
one change: provide the same 800-byte threshold vector only through the new
SCC1 byte ingress. A passing result requires all of these without direct test
writes after ingress:

```text
BD0 buffer contains the supplied vector
BD0 length becomes $0320
BD0 E clears and status becomes accepted
CP current descriptor advances to BD1
SCCE1 bit 0 sets
level-4 IACK returns $4D
$00643C is entered by normal firmware interrupt dispatch
firmware reaches RECORDING and programs the verified IDMA range
```

If the first missing observation is before SCCE, the receive/descriptor slice
is incomplete. If SCCE sets but no `$4D` arrives, the missing component is the
internal interrupt path. If `$4D` arrives but firmware rejects the descriptor,
the first discrepancy is descriptor metadata/status, not ADC routing.

## Status and falsifiers

| Claim | Status | Falsifying observation |
|---|---|---|
| ASR-10 sampling accepts a full SCC1 RX descriptor with buffer, `$0320`, E clear and SCCE bit 0 | `[Verified runtime]` | a repeatable CP-shaped completion that reaches `$00643C` but is rejected before the documented threshold path |
| the accepted first-descriptor product is payload, length, E clear and SCCE/IRQ | `[Verified firmware/runtime]` for the measured full-buffer path | firmware access to another CP-produced field that is necessary before `$0064BA` can consume the same completion |
| repeated receive requires an independent CP producer position and W-based advance | `[Likely]` from the eight-entry ring and independent firmware consumer | a second completion being produced correctly without CP descriptor advance or equivalent producer state |
| full-buffer completion at MRBLR is sufficient for firmware | `[Verified runtime]` | a byte-identical descriptor/SCCE/IACK state at the firmware boundary that does not reproduce the Phase 1 downstream path |
| earlier/partial-frame completion semantics | `[OPEN]` | requires a real capture, applicable protocol decode or a discriminating firmware run |
| physical serial mode, clock, SCC pins and ADC source | `[OPEN]` here | schematic/continuity evidence or captured pin traffic |
| current MAME CP/SCC receive capability | `[OPEN capability]`, absent implementation | a callable receive path shown to produce CP-owned descriptor completion without direct test writes |

The simplest surviving explanation for the present WAITING behavior is still
that no model component can produce this contract. Phase 3A changes no status
in `current-status.md`; it makes the missing behavior precise enough for the
next implementation task without pretending the physical source is known.

## Verification accounting

- Executable or probe files changed: 0.
- Runtime experiments: 0.
- Regression tests run: 0; documentation only.
- `static/*.csv` files changed: 0.
- `-log` runs: 0.
