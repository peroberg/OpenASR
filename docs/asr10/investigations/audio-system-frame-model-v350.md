# ASR-10 V3.50 — smallest defensible audio frame model

## Question

How can the real ASR-10 establish the documented 29.7619 kHz and 44.1000 kHz
system audio-frame classes without inventing a board clock mux, an ES5701
control register, or an ACTV-only emulator policy?

This is an architecture synthesis.  It does not reopen the parked serialized
effect `+$66` artifact branch, investigate sequencer symptoms, or change MAME.

## Evidence domains and scope

| Claim | Evidence domain | Status |
|---|---|---|
| The manuals expose 29.7619 kHz/31-voice and 44.1000 kHz/23-voice system classes. | ASR documentation | `[Verified source]` |
| ES5506 output sample rate is `master_clock / (16 * active_voices)`. | ES5506 specification | `[Verified silicon spec]` |
| V3.50 A/B/A switches ACTV `$1F -> $17 -> $1F` and programs a reversible pitch-domain offset. | firmware/current MAME runtime | `[Verified firmware/runtime]` |
| ES5701 documents two fixed divider paths, 20 MHz -> 10 MHz and 16 MHz -> 8 MHz. | ES5701 specification and local VHDL reconstruction | `[Verified silicon spec]`; VHDL corroboration only |
| The ASR-10's exact audio-clock nets, select mechanism and ES5510 synchronization wiring are known. | physical board | `[OPEN]` |

The user-facing polyphony labels and OTTO slot counts must stay separate:
the manual says 31/23 voices, while the observed zero-based ACTV fields denote
32/24 ES5506 slots.  This one-slot difference is not resolved here.

## Chip-interface constraints

### ES5506 / OTTO

The OTTO specification, rev. 2.3 p. 6, explicitly says that ACTV plus one is
the active-voice count and gives:

```
frame/output sample rate = master clock / (16 * active voices)
```

The same page gives the two relevant examples: 32 voices at 16 MHz produce
31.2 kHz, and 24 voices at 16.9 MHz produce 44.1 kHz.  Thus an ES5506 audio
frame is the rate-producing boundary; BCLK/WCLK/LRCLK format serial transport
but do not replace this equation.

For the firmware-observed slot counts, the documented system rates impose the
following required *effective ES5506 master clocks* if they are the same two
system states:

| Effect-selected state | ACTV / slots | documented system frame rate | required ES5506 master clock | Status |
|---|---:|---:|---:|---|
| mode 0 | `$1F` / 32 | 29,761.9 Hz | 15,238,090 Hz | `[Derived constraint]` |
| mode 1 | `$17` / 24 | 44,100 Hz | 16,934,400 Hz | `[Derived constraint]` |

This is a constraint from the chip interface and the already observed firmware
state, not a recovered ASR-10 netlist.  In particular, it does not prove that
the clocks are `Y2/2` and `Y3/2`, even though those two arithmetic identities
are attractive candidates.

### ES5510 / ESP

The ESP specification has its own `CLK_IN` and documents `HALT`-based sample
rate synchronization.  It therefore supports two compatible board designs:
an independently clocked ESP synchronized at a frame boundary, or a serially
coupled ESP.  It does not identify which design ASR-10 uses.  ES5510 execution
and its serial/HALT wiring remain `[OPEN]`; they must not be used to select an
ES5506 master clock today.

### ES5701 / SuperGLU

The local ES5701 specification names 20 MHz and 16 MHz oscillator inputs and
their buffered /2 outputs, 10 MHz and 8 MHz.  The local `sources/es5701.vhd`
implements exactly those two independent flip-flop dividers (lines 73–89),
with no mode input or clock mux.

This establishes a narrow negative boundary: the available ES5701 material
does **not** document a Y2/Y3 selector, 15.23809 MHz or 16.9344 MHz output,
or a runtime 30 kHz/44.1 kHz control.  It neither disproves an external ASR
divider/mux nor proves that ASR U41 is wired like the documented part.

## Verified firmware-rate contract

The controlled ROM HALL -> 44LUSH -> ROM HALL experiment establishes one
reversible firmware transaction:

```
current effect +$66
       -> $0CE3 = 00 / 01
       -> ACTV = $1F / $17
       -> pre-note pitch offset and ES5506 FC programming
```

The pitch offset is not a physical-clock observation.  Its independently
measured 256-units-per-semitone scale nevertheless yields an implied linear
ratio 0.674992, close to the documented 29.7619/44.1 ratio.  It is therefore
strong evidence that the firmware treats the two states as coordinated
pitch/rate classes, while the board source that realizes the corresponding
frame rate remains unknown.

The rejected ACTV-clock proxy is retained as a constraint: changing MAME's
effective ES5506 clock on ACTV alone changed the known note from about
262.3 Hz to 196.7 Hz (ratio about 0.750), so ACTV is not a sufficient MAME
clock-policy boundary.  No compensation or retry follows from this synthesis.

## Smallest defensible model

```
                 firmware-visible effect operating mode
                              |
              +---------------+----------------+
              |                                |
              v                                v
       ES5506 ACTV/slot count          pitch-domain voice setup
          ($1F / $17)                    then FC programming
              |
              |                         [Verified firmware/runtime]
              v
  board-specific audio clock/frame source and routing
              |
              v
        effective ES5506 master clock
              |
              v
  ES5506 frame rate = master / (16 * active slots)
              |
              +--> serial/ESP synchronization, if wired [OPEN]
```

The only board-specific element that must be introduced conceptually is an
**unidentified audio clock/frame source and routing boundary**.  It may be a
clock network selected by the same higher-level mode, a common rate state that
also drives firmware configuration, or another board mechanism.  The evidence
does not discriminate among those alternatives.

Accordingly, the smallest defensible statement is not “ES5701 switches the
clock” and not “ACTV selects the clock.”  It is:

> The real system must provide an ES5506-effective clock consistent with its
> documented frame class, while V3.50 simultaneously selects the corresponding
> slot count and pitch-domain setup.  The physical producer and route are open.

## Current MAME separation

Current MAME instead has three independent mechanisms:

```
Y2 direct -> ES5506 generic stream rate, changed by ACTV
synthetic 44.1 kHz timer -> MC68302 PB3 only
ES5510 @ 10 MHz -> disabled host-register placeholder
```

That is `[Verified code]`, not a model of the real rate-selection network.
It explains why current MAME's A/B pitch behavior is a useful firmware
witness but cannot determine the physical ASR clock topology.

## Hypothesis verdicts

| Hypothesis | Verdict | Reason |
|---|---|---|
| ES5506 is the rate-producing frame boundary. | `SUPPORTED` | Its documented formula directly relates master clock, slots and output sample rate. |
| Effect mode coordinates ACTV and pitch setup. | `SUPPORTED` | Reversible V3.50 A/B/A firmware witnesses. |
| The two documented system classes require different effective ES5506 clocks if the observed slot counts apply. | `SUPPORTED` as a derived interface constraint | Exact arithmetic; no board route claimed. |
| ES5701 is the ASR rate selector or Y2/Y3 mux. | `OPEN` | Available spec/VHDL supplies no such control or output. |
| ES5510 determines the ES5506 clock. | `OPEN` | Its independent clock and optional synchronization do not establish ASR wiring. |
| ACTV alone is a valid emulator clock-select boundary. | `DISPROVEN` | The reverted known-note proxy failed its witness. |

## What this establishes and what it does not

Established: the correct abstraction boundary for a future implementation is
an ASR board-level audio-rate/frame provider coupled to the already verified
firmware mode contract, not an effect-name conditional inside ES5506.

Not established: a physical Y2/Y3 mux, a `/2` component, U41/ES5701
participation, a PB3 role, ES5510 synchronization wiring, or the actual
clock-source/select signal.  `MODEL CHANGE JUSTIFIED: NO`.

## Single next experiment

Obtain a main digital/audio-board schematic, netlist, or board photographs
clear enough to trace ES5506 `CLK`, Y2/Y3 (or their successors), divider/mux
logic and the ES5701 pins.  This is the smallest discriminating experiment:
it separates an external Y2/Y3 network from an unrelated clock source without
reopening firmware, artifact, sequencer, or DSP analysis.
