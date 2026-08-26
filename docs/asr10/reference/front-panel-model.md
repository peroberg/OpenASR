# ASR-10 semantic front-panel model

Canonical, layout-independent specification for the ASR-10 front panel. This
document separates physical controls, panel-wire codes, host bindings and
rendering. It is the source model for future rack and keyboard layouts; it is
not a claim that the current MAME layout already implements the model.

Evidence status follows `methods-hypothesis-management.md`. Physical labels and
grouping come from `panel-manual.md` and the ASR-10 Musician's Manual. Raw-code
claims come from the runtime investigations named below. A label in
`src/mame/layout/asr10_panel.lay` is not evidence for a raw-code identity.

## 1. Semantic model and implementation boundary

The stable identity is the semantic control, not `BTN_xx` and not a host key:

```text
Semantic ASR-10 panel model
  PanelControl / PanelOutput
          |
          +---- Rack layout placement
          +---- Keyboard layout placement
          +---- physical panel button instance
                    |
                    +---- verified raw panel code, if known
                    +---- host key, if deliberately mapped
                    +---- pressed state (down/up), where applicable
          +---- indicator source, independently evidenced
```

For example:

```text
PanelControl::EnterYes
  physical label: ENTER • YES
  raw panel code: $23 [Verified runtime]
  current host key: Enter/Return
```

The future implementation should represent these as separate bindings to one
semantic identity, not encode the user-facing identity as `BTN_23`. A control
record needs, conceptually:

- stable semantic ID and logical group;
- primary and optional secondary physical labels;
- physical kind (button, continuous control, display or indicator);
- optional raw-code binding with evidence status and context;
- optional host binding;
- optional indicator binding with its own evidence status;
- physical pressed state for buttons, including simultaneous holds;
- layout placement owned by the rack or keyboard variant.

This phase defines that contract only. It does not change C++, the MAME layout,
the display implementation or the input map.

## 2. Logical groups

### Display

The panel has one 22-character alphanumeric display. It is visually and
functionally coupled to the eight Instrument / Sequence Track controls directly
below it. The two layers must remain distinct:

```text
ASR-10 display protocol and state machine
  command framing, cursor column, fields, attributes, partial updates,
  annunciators and panel-local behavior
                       |
                       v
generic 1 x 22 VFD rendering
  glyph segments, underline outputs and final layout placement
```

The current generic `esq1x22_device` contains some ASR-specific command handling;
that placement is an implementation fact, not proof that a generic 1 x 22 VFD is
a complete ASR-10 protocol model. See section 10.

### Instrument / Sequence Tracks

Eight buttons, numbered 1 through 8, sit directly below the display. Each has a
semantic Instrument / Sequence Track role and an associated lamp/indication.
Track 1 raw `$02` is `[Verified runtime]`; Track 2 through Track 8 remain
`[OPEN]`. No contiguous raw-code series may be inferred from Track 1.

### Mode

The semantic order is:

```text
Load   Command   Edit
```

Mode precedes Category in the ASR-10 working model. The groups may be oriented
differently on rack and keyboard variants, but their order and relationship are
invariants. Command raw `$06` is `[Verified runtime]`; Load and Edit are
`[OPEN]`.

### Category

The four categories are, in order:

```text
Instrument   Seq • Song   System • MIDI   Effects
```

Mode and Category are related visually and structurally but remain two separate
groups. Seq • Song raw `$15` is `[Verified runtime]`. The other category raw
codes remain `[OPEN]`.

### Navigation

Navigation is one six-button group:

```text
Up   Down   Left   Right   Enter • Yes   Cancel • No
```

All six raw bindings in the control table are `[Verified runtime]`. Enter/Yes
and Cancel/No are confirmation choices, not aliases for generic application
actions.

### Numeric / Parameter Select

These are ten physical, dual-role buttons, not twenty controls. The parameter
label is above the numeric label:

```text
Env 1        Env 2        Env 3
  1            2            3

Pitch        Filters      Amp
  4            5            6

LFO          Wave         Layer
 7             8            9

             Track
               0
```

Track/0 is centered below the 3 x 3 group. The role selected by firmware context
may be parameter-page selection or numeric/direct-dial input; the physical
button remains one control. No raw binding is currently verified for any of the
ten buttons.

### Audio Tracks

`Audio Track 1` and `Audio Track 2` are the stable semantic names required by
this model and form a two-control group functionally tied to transport. The
current manual extraction in `panel-manual.md` records their printed labels as
`A` and `B`; variant artwork must preserve that source distinction rather than
silently relabel it. They are not the Up/Down buttons: the earlier `$0A`/`$0B`
attribution is excluded by their verified navigation behavior. Both Audio Track
raw codes remain `[OPEN]`.

### Input Level indicators

The semantic output group is labelled `INPUT LEVEL` and contains four outputs:

```text
Left Signal   Left Peak   Right Signal   Right Peak
```

They are outputs, not panel buttons. Their raw annunciator/register sources are
`[OPEN]`; no meaning is assigned to the currently exposed anonymous bits. In
the panel projection this feedback sits above the Audio Track controls.

### Sequencer Transport

The three controls are:

```text
Record   Stop • Continue   Play
```

Play raw `$1D` is `[Verified runtime]` when a playable sequence is loaded.
Stop/Continue raw `$17` is `[Verified runtime]` during active playback. `$17`
is context-dependent: it is inert in some states and has a separately observed
Create New Sequence role in a deep Command context. It is therefore not a
global abstract `Stop` action. Record remains `[OPEN]`.

Every transport button must preserve physical down/up state. The real recording
gesture must be representable as ordinary panel edges:

```text
Record down
Play down
Play up
Record up
```

No `start_recording()` shortcut, firmware-state write or UI-state bypass belongs
in the panel model.

### Sample / Source Select

`Sample • Source Select` is one independent semantic control. Raw `$20` is
`[Verified runtime]`. Physical proximity to another auxiliary control in a
layout does not merge their functions.

### FX Select / FX Bypass

`FX Select • FX Bypass` is one independent semantic control. Its raw code is
`[OPEN]`. The earlier `$07` observation is only `[Likely]` from a display
signature and is deliberately not installed in the raw-code column.

### Continuous controls

The source model already exposes `Volume`, `Data Entry` and `Input Level` as
continuous host inputs. They do not use the button raw-code protocol and are
listed for completeness. `MR. KNOB` is `[Verified vendor terminology]` for the
same physical Data Entry slider, not another semantic control. The three current
adjusters do not reach firmware through its PBDAT-selected PAR path; their
numeric indexes also conflict with the verified selector map. Input Level is
`[DISPROVEN]` as a member of that scan and instead belongs to the separately
open audio-input/gain model. Keybed, pitch/modulation wheels, Patch Select and
foot controls remain performance-controller work outside this front-panel
specification.

## 3. Layout invariants

### Common invariants

- Both variants reference the same semantic IDs; semantics are never copied into
  two layout-specific tables.
- Display, its fields and Instrument / Sequence Tracks 1-8 are one visual area.
- Mode precedes Category; the groups are related but remain visibly separate.
- Navigation remains a distinct six-control group.
- Numeric / Parameter Select remains 3 x 3 plus centered Track/0, with parameter
  label above number on each button.
- Audio Tracks, Input Level and Transport read as one functional area, with the
  Input Level feedback above the Audio Track controls.
- Sample/Source Select and FX Select/FX Bypass remain separate auxiliary
  controls even when adjacent.
- Layout placement never supplies raw-code evidence.

### Rack layout invariants

The ASR-10R-oriented layout should place the display centrally in the upper
area, with Tracks 1-8 immediately below it. Mode and Category stay near that
area as separate related groups. Navigation is its own six-button block.
Numeric / Parameter Select preserves the 3 x 3 + centered Track/0 shape.
Audio Tracks, Input Level and Transport occupy one functionally coherent area;
the two auxiliary controls remain individually identifiable. The target is the
ASR-10R's working logic, not symmetry for its own sake.

### Keyboard layout invariants

The keyboard variant may orient and space these groups differently around the
keybed, but it references the same semantic objects and raw bindings. Physical
adjacency of Sample/Source Select and FX Select/FX Bypass does not make them a
single group. Keybed and performance controllers are adjacent subsystems, not
duplicates of front-panel controls.

The current `asr10_panel.lay` is an 8 x 8 diagnostic raw-button grid. It is not a
rack or keyboard semantic layout. Its labels, including labels placed on raw
codes without runtime proof, are not canonical assignments.

## 4. Control table

`—` in Raw means either `[OPEN]` for a button/source or not applicable for a
non-button. Evidence describes the semantic raw binding, not merely the
existence of the physical label in the manual.

| Group | Semantic control | Physical label | Secondary label | Raw panel code | Evidence | Existing host key | Lamp/indicator | Notes |
|---|---|---|---|---|---|---|---|---|
| Display | `Display1x22` | 22-character display | — | — | `[Verified, bounded]` protocol slice | — | Glyph + underline outputs | ASR state machine is not equivalent to generic rendering |
| Instrument/Track | `InstrumentTrack1` | 1 | — | `$02` | `[Verified runtime]` | `1` | Track 1 lamp; `$77` bit 0 only `[Verified narrow]` | Selects slot 1 in measured load/select flows |
| Instrument/Track | `InstrumentTrack2` | 2 | — | — | `[OPEN]` | — | Track 2 lamp source `[OPEN]` | Do not infer from Track 1 |
| Instrument/Track | `InstrumentTrack3` | 3 | — | — | `[OPEN]` | — | Track 3 lamp source `[OPEN]` | Do not infer a series |
| Instrument/Track | `InstrumentTrack4` | 4 | — | — | `[OPEN]` | — | Track 4 lamp source `[OPEN]` | — |
| Instrument/Track | `InstrumentTrack5` | 5 | — | — | `[OPEN]` | — | Track 5 lamp source `[OPEN]` | — |
| Instrument/Track | `InstrumentTrack6` | 6 | — | — | `[OPEN]` | — | Track 6 lamp source `[OPEN]` | — |
| Instrument/Track | `InstrumentTrack7` | 7 | — | — | `[OPEN]` | — | Track 7 lamp source `[OPEN]` | — |
| Instrument/Track | `InstrumentTrack8` | 8 | — | — | `[OPEN]` | — | Track 8 lamp source `[OPEN]` | — |
| Mode | `ModeLoad` | LOAD | — | — | `[OPEN]` | — | Mode indicator source `[OPEN]` | Physical blink is panel-local; encoding unknown |
| Mode | `ModeCommand` | COMMAND | — | `$06` | `[Verified runtime]` | — | Mode indicator source `[OPEN]` | Reaches Command pages |
| Mode | `ModeEdit` | EDIT | — | — | `[OPEN]` | — | Mode indicator source `[OPEN]` | Layout label is not evidence |
| Category | `CategoryInstrument` | INSTRUMENT | — | — | `[OPEN]` | — | Page indicator source `[OPEN]` | Distinct from InstrumentTrack1 |
| Category | `CategorySeqSong` | SEQ • SONG | — | `$15` | `[Verified runtime]` | `Q` | Page indicator source `[OPEN]` | Reaches real sequence-file listing |
| Category | `CategorySystemMidi` | SYSTEM • MIDI | — | — | `[OPEN]` | — | Page indicator source `[OPEN]` | — |
| Category | `CategoryEffects` | EFFECTS | — | — | `[OPEN]` | — | Page indicator source `[OPEN]` | Distinct from FX Select/Bypass |
| Navigation | `NavUp` | UP | — | `$0A` | `[Verified runtime]` | Up Arrow | — | Increases value/choice |
| Navigation | `NavDown` | DOWN | — | `$0B` | `[Verified runtime]` | Down Arrow | — | Decreases value/choice |
| Navigation | `NavLeft` | LEFT | — | `$10` | `[Verified runtime]` | Left Arrow | — | Moves underline Field 2 -> Field 1 in REC SRC |
| Navigation | `NavRight` | RIGHT | — | `$11` | `[Verified runtime]` | Right Arrow | — | Moves underline Field 1 -> Field 2 in REC SRC |
| Navigation | `EnterYes` | ENTER | YES | `$23` | `[Verified runtime]` | Enter/Return | — | Confirmation/proceed choice |
| Navigation | `CancelNo` | CANCEL | NO | `$21` | `[Verified runtime]` | — | — | `$22` is `[DISPROVEN]` as Cancel/No |
| Numeric/Parameter | `Env1Numeric1` | ENV 1 | 1 | `$0D` | `[Verified runtime, Command/service context]` | — | — | Opens the service/diagnostic command family; `$0C` as Env1 is `[DISPROVEN]` in that context |
| Numeric/Parameter | `Env2Numeric2` | ENV 2 | 2 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `Env3Numeric3` | ENV 3 | 3 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `PitchNumeric4` | PITCH | 4 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `FiltersNumeric5` | FILTERS | 5 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `AmpNumeric6` | AMP | 6 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `LfoNumeric7` | LFO | 7 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `WaveNumeric8` | WAVE | 8 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `LayerNumeric9` | LAYER | 9 | — | `[OPEN]` | — | — | One dual-role physical button |
| Numeric/Parameter | `TrackNumeric0` | TRACK | 0 | — | `[OPEN]` | — | — | Centered below the 3 x 3 group |
| Audio Tracks | `AudioTrack1` | AUDIO TRACK A | — | — | `[OPEN]` | — | Indicator source `[OPEN]` | Semantic Track 1; manual label A; tied to transport |
| Audio Tracks | `AudioTrack2` | AUDIO TRACK B | — | — | `[OPEN]` | — | Indicator source `[OPEN]` | Semantic Track 2; manual label B; tied to transport |
| Input Level | `InputLeftSignal` | LEFT SIGNAL | — | — | raw source `[OPEN]` | — | Output itself | Under common INPUT LEVEL identity |
| Input Level | `InputLeftPeak` | LEFT PEAK | — | — | raw source `[OPEN]` | — | Output itself | Under common INPUT LEVEL identity |
| Input Level | `InputRightSignal` | RIGHT SIGNAL | — | — | raw source `[OPEN]` | — | Output itself | Under common INPUT LEVEL identity |
| Input Level | `InputRightPeak` | RIGHT PEAK | — | — | raw source `[OPEN]` | — | Output itself | Under common INPUT LEVEL identity |
| Transport | `TransportRecord` | RECORD | — | — | `[OPEN]` | — | Status indicator source `[OPEN]` | Must retain pressed state |
| Transport | `TransportStopContinue` | STOP | CONTINUE | `$17` | `[Verified runtime, active playback]` | — | Status indicator source `[OPEN]` | Context-dependent raw code |
| Transport | `TransportPlay` | PLAY | — | `$1D` | `[Verified runtime, sequence loaded]` | — | Status indicator source `[OPEN]` | Starts sequencer activity |
| Auxiliary | `SampleSourceSelect` | SAMPLE | SOURCE SELECT | `$20` | `[Verified runtime]` | `S` | Indicator source `[OPEN]` | `S` currently collides with musical C-sharp |
| Auxiliary | `FxSelectBypass` | FX SELECT | FX BYPASS | — | `[OPEN]` | — | Indicator source `[OPEN]` | Prior `$07` candidate remains only `[Likely]` |
| Continuous | `Volume` | VOLUME | — | not applicable | `[Verified source-level host input, disconnected/misrouted acquisition]` | MAME adjuster | — | Writes emulator channel 5, while V3.50 uses PBDAT selector 3; min/mid/max did not affect PAR |
| Continuous | `DataEntry` | DATA ENTRY | MR. KNOB (diagnostic alias) | not applicable | `[Verified identity; disconnected/misrouted acquisition]` | MAME adjuster | — | Writes emulator channel 3, while V3.50 uses selector 5; min/mid/max did not affect PAR |
| Continuous | `InputLevel` | INPUT LEVEL | — | not applicable | `[Verified source-level host input; DISPROVEN as PAR-scan member]` | MAME adjuster | — | Writes emulator channel 4, which V3.50 uses for PEDAL; belongs to open audio-input/gain path and is distinct from four feedback indicators |

The `$77` bit-0 observation is deliberately not generalized to a globally unique
Track 1 lamp source: it toggles correctly in an isolated Instrument 1
select/deselect sequence but also changes in sampling contexts. Tracks 2-8 and
all four Input Level indicators remain semantic outputs with unknown raw sources.

## 5. Verified, OPEN and retracted raw bindings

### Verified semantic button bindings

| Raw | Semantic control | Scope and evidence |
|---:|---|---|
| `$02` | Instrument / Sequence Track 1 | Instrument selection and single-file destination flow |
| `$06` | Command | Reaches Command mode and its category pages |
| `$0A` | Up | Increases enumerated/numeric values; VOLUME ceiling witness |
| `$0B` | Down | Decreases VOLUME 99 -> 98 and enumerated values |
| `$10` | Left | REC SRC underline moves Field 2 -> Field 1 |
| `$11` | Right | REC SRC underline moves Field 1 -> Field 2 |
| `$15` | Seq • Song | Reaches the real `TUTORIAL SEQ` listing |
| `$17` | Stop • Continue | Active playback only; context-dependent elsewhere |
| `$1D` | Play | Loaded playable-sequence context |
| `$20` | Sample • Source Select | Reaches REC SRC workflow |
| `$21` | Cancel • No | Backs out of a genuine confirmation sub-step |
| `$23` | Enter • Yes | Advances confirmation and load flows |

### Controls whose button raw code remains OPEN

- Mode: Load, Edit.
- Category: Instrument, System • MIDI, Effects.
- Instrument / Sequence Tracks 2-8.
- Numeric / Parameter Select: Env 1/1 through Layer/9 and Track/0.
- Audio Track 1 and Audio Track 2.
- Transport: Record.
- FX Select • FX Bypass.

The four Input Level indicators and most other panel lamps have `[OPEN]` raw
sources rather than button codes. Continuous controls do not use button codes.

### Candidates and retractions

- `$07` as FX Select/Bypass is `[Likely]`, based on one appropriate display
  signature, but lacks the discriminating runtime confirmation needed for a raw
  binding.
- `$0C`/`$0D` as Left/Right are `[DISPROVEN]`; they reach Audio Track utility
  handlers instead. The verified arrows are `$10`/`$11`.
- `$22` as Cancel/No is `[DISPROVEN]` by a confirmation-screen A/B test; `$21`
  uniquely performs Cancel/No there.
- Labels such as `$05=EDIT`, `$19=RECORD` or `$1A=LOAD` in the current diagnostic
  layout are not evidence and are not carried into this model.
- The ROM raw-to-mapped table suggests groups, but a statically grouped or
  adjacent entry is not a verified physical identity. In particular,
  Track 1 `$02` does not imply any raw code for Tracks 2-8.

## 6. Current host input inventory

The authoritative inventory is `INPUT_PORTS_START(asr10panel_device)` in
`src/mame/ensoniq/esqpanel.cpp`, not the older pilot table in
`investigations/panel-keymap.md`.

### Front-panel keyboard mappings

| Host key | MAME field | Semantic control | Raw |
|---|---|---|---:|
| `1` | `BTN_02` | Instrument / Sequence Track 1 | `$02` |
| Up Arrow | `BTN_0A` | Up | `$0A` |
| Down Arrow | `BTN_0B` | Down | `$0B` |
| Left Arrow | `BTN_10` | Left | `$10` |
| Right Arrow | `BTN_11` | Right | `$11` |
| `Q` | `BTN_15` | Seq • Song | `$15` |
| `S` | `BTN_20` | Sample • Source Select | `$20` |
| Enter/Return | `BTN_23` | Enter • Yes | `$23` |

All 64 `BTN_00`-`BTN_3F` fields remain clickable in the diagnostic layout, but
clickability is not a semantic assignment. No current host key is bound to
Command, Cancel/No, Play or Stop/Continue even though their raw identities are
verified.

### Musical typing keyboard

The separate `keys_0` port supplies one computer-keyboard octave at fixed
velocity 100. The absolute note is `octave * 12 + offset`; octave positions are
0-4 and the top C overlaps the next octave.

| Host key | Musical field | Offset |
|---|---|---:|
| `Z` | C | 0 |
| `S` | C-sharp | 1 |
| `X` | D | 2 |
| `D` | D-sharp | 3 |
| `C` | E | 4 |
| `V` | F | 5 |
| `G` | F-sharp | 6 |
| `B` | G | 7 |
| `H` | G-sharp | 8 |
| `N` | A | 9 |
| `J` | A-sharp | 10 |
| `M` | B | 11 |
| Comma | upper C | 12 |
| Minus | octave down | — |
| Equals | octave up | — |

### Current and future collisions

- `S` is an actual current collision: it triggers both Sample/Source Select and
  musical C-sharp.
- `C` currently triggers musical E, so the proposed future mnemonic
  `C -> Command` would collide and must not be added silently.
- Enter is also MAME's common UI-select key. The current panel binding exists,
  but UI focus/capture behavior must be considered in the final host-control
  design.
- Future `P -> Play` conflicts with MAME's default pause convention unless the
  host-input design deliberately resolves it.

No collision redesign belongs in this phase.

## 7. Physical button events and transport gestures

Current front-panel fields call `asr10panel_device::button_change()`, which calls
`esqpanel_device::set_button(code, pressed)`. `set_button()` suppresses duplicate
state, stores the pressed set and transmits ordinary two-byte frames:

```text
down: (0x80 | raw_code), $00
up:   raw_code,          $00
```

The existing regression has verified that two buttons can be held at once and
produce four distinct ordered events. This is the required infrastructure for
Record+Play once Record is identified and host conflicts are resolved. A future
host mapping must bind physical edges to the semantic controls; it must not map a
key directly to a firmware function or inferred action.

## 8. Indicator model

The manual identifies mode, page and sequencer-status indicators, eight
Instrument/Track indications and the four Input Level outputs. The current model
exposes five raw annunciator registers (`$77`-`$7B`, 40 bits), but only `$77` bit
0 has a narrow reversible correlation. The remaining 39 bits are `[OPEN]` and
must stay semantically unnamed.

LOAD blinking has no periodic host display traffic in the measured idle window,
and `asr10panel_device` currently has no blink state machine. A sibling Ensoniq
panel class supports panel-local blink attributes, but that analogy is not
ASR-10 evidence. Indicator identity, on/off source and blink attribute must be
separate fields in any future output model.

## 9. Rack/keyboard projection contract

Future layouts consume the semantic table rather than raw code labels:

```text
semantic ASR-10 panel model
              |
      +-------+--------+
      |                |
ASR-10R rack       ASR-10 keyboard
layout projection  layout projection
```

Each projection owns geometry, orientation, artwork and physical label
placement. Neither owns raw-code truth, host bindings or indicator semantics.
Those live once in the semantic model and may remain `[OPEN]` while both layouts
still place the known physical control.

## 10. Display architecture boundary

### Current byte path

```text
firmware panel-display writer
  -> DUART channel B THRB
  -> mc68681 channel B serial TX
  -> asr10panel_device::rcv_complete()
       - returns the ASR path's observed $FF response
  -> asr10panel_device::send_to_display()
       - owns ASR cursor, field, attribute and operand state
  -> esq1x22_device::render_character(column, glyph, underline) / clear()
  -> esqvfd_device::update_display()
  -> MAME outputs and layout renderer
```

### ASR-specific state in `asr10panel_device`

- explicit 62,500-baud panel cadence and the `$FF` host response;
- interception of `$77`-`$7B <value>` annunciator writes;
- raw annunciator outputs and the narrow `$77` bit-0 mirror;
- pending operand class, logical cursor, current underline, selected-field
  anchor and retained selected-field attribute;
- interpretation of direct columns, printable bytes, `$60`, `$62`, `$63`,
  `$66` and `$72` before generic rendering;
- bounded one-operand consumption for `$74-$76` without assigning semantics;
- first-occurrence reporting of display codes it does not recognize.

The former linear `m_text_chars`/`m_text_position` shadow and `current_text()`
API were removed. They had no runtime consumer and were already known to remain
stale after absolute partial updates.

### Generic 1 x 22 renderer boundary

The ASR path calls explicit-position `render_character()` and generic `clear()`;
`esq1x22_device` owns only character/attribute cells for that path. Its legacy
`write_char()` raw-byte parser remains for the separate EPS 1x22 panel and was
not generally refactored in this bounded ASR task.

`esqvfd_device::update_display()` remains the generic rendering layer: it turns
character/attribute cells into glyph-segment and underline outputs. The layout
only places those outputs.

### State ownership

| State | Current owner | Boundary consequence |
|---|---|---|
| Raw annunciator register bytes | `asr10panel_device` | Not forwarded to VFD |
| ASR operand/cursor/field/attribute state | `asr10panel_device` | One authoritative protocol decoder |
| Rendered characters/attributes | `esq1x22_device` | Updated by explicit column/glyph/underline operation |
| Segment and underline outputs | `esqvfd_device` | Generic output generation |
| Geometry and artwork | `asr10_panel.lay` | Must not define protocol meaning |

Lua regression display reads continue to use visible `vfd0`-`vfd43` outputs,
which now reflect the ASR-owned logical cursor and field state without a second
independently advanced text shadow.

### Verified display commands and OPEN surface

`[Verified, bounded]`: printable text, clear, cursor columns, `$60` underline
attributes, `$62`/`$72` field boundaries, annunciator register capture and the
named partial-update cases in `display-protocol.md`.

`[Verified mechanism, implemented]`: `$62` marks the selected-field anchor and
`$63` restores it and the field attribute for a partial rewrite ending at
`$72`. The exact vendor names remain open. The operational contract is measured
on REC SRC and runtime-regression-locked on TEMPO.

`[OPEN]`: the `$74`/`$75`/`$76` panel-control family; `$E7 $71` and adjacent
`$7E`/`$FC`/`$FD`/`$FF`/`$D5` transition codes; the
`$E0`/`$B0 <value>`/`$7F`/`$C0` cluster; the
`$90`/`$80`/`$3C`/`$64` VOLUME-context cluster; `$67` near the FX
algorithm list; the other 39 annunciator bits; panel-local blink encoding; and
application-level cursor/field/value behavior outside the measured screens.
Unknown commands must not acquire rendering or semantic names from proximity or
layout appearance.

## 11. Known UI defects and acceptance boundary

- **[Verified fixed mechanism] EDIT SEQUENCE -> TEMPO:** the full page marks
  columns 6-8, Up's `$63 "91 " $72` changes `90 -> 91`, and Down changes it
  back to `90`. Underline remains 6-8; no text is appended and LOOP is intact.
- **[OPEN] Master Tune value display:** a prior manual GUI observation reports
  the display one value-step behind the edited value. The manual expects the
  shown current value to follow the edit; firmware behavior versus display-model
  behavior is not yet discriminated.
- The display protocol mechanisms verified on REC SRC, FX Select and VOLUME do
  not establish a complete application-level UI state machine.
- The current 8 x 8 diagnostic button layout is not an ASR-10R panel. Its guessed
  labels and the standing local label edits are not semantic evidence.
- Most indicator identities and all ASR-specific blink encoding remain open.

Phase 2 analysis and implementation are complete for the bounded verified
grammar. This does not close the OPEN commands or prove every UI workflow.

## 12. Next implementation stages

### Phase 2 — ASR-specific display protocol/state machine — complete, bounded

`EDIT SEQUENCE -> TEMPO` passes through the real firmware/device path with the
correct column, selected value, underline and partial rewrites. Unknown controls
remain loud and semantically OPEN.

### Phase 3 — rack front-panel layout

Project the semantic model into an ASR-10R-oriented layout while preserving the
invariants in section 3. Physical controls whose raw code is `[OPEN]` may be
placed and labelled, but must not be wired to a guessed `BTN_xx`.

### Phase 4 — host controls and real pressed-state gestures

Design the host map with the musical keyboard and MAME UI collisions visible.
Candidate mnemonics include `L` Load, `C` Command, `E` Edit, `I` Instrument, `S`
Seq/Song, `Y` System/MIDI, `F` Effects, `1`-`8` Instrument/Track, `R` Record and
`P` Play. A binding lands only after its raw button is verified and its collision
is consciously resolved. Record+Play uses ordinary held-button edges, never a
special recording action.

## References

- `panel-manual.md`
- `display-protocol.md`
- `subroutine-index.md`, DUART/panel entries
- `../investigations/display-protocol-inventory.md`
- `../investigations/partial-update-position-probe.md`
- `../investigations/panel-button-and-transport-map.md`
- `../investigations/sequencer-clock-and-service-menu.md`
- `../investigations/bank-loading-and-transport-context.md`
- `../investigations/command-pages-and-clock-verdict.md`
- `../investigations/transport-ab-test-play-stop-continue.md`
- `src/mame/ensoniq/esqpanel.cpp` and `src/mame/ensoniq/esqpanel.h`
- `src/mame/ensoniq/esqvfd.cpp` and `src/mame/ensoniq/esqvfd.h`
- `src/mame/layout/asr10_panel.lay`
