# ASR-10 display protocol state machine, before implementation (2026-08-26)

> **Implementation follow-up, 2026-08-26:** the measured `$62/$63/$72`
> selected-field mechanism is now implemented in `asr10panel_device` and
> regression-locked by `lua/display_field_rewrite.lua`. The analysis and old
> failure below remain provenance for why the implementation changed. `$67`,
> `$74-$76` semantics and the other named OPEN classes remain `[OPEN]`. The
> post-implementation suite passes 16 tests, 17 `PASS` lines, exit 0.

This is Phase 2 of the semantic front-panel work. It changes no display or
panel behavior. All runtime observations use V3.50, Channel-B transmit
write-taps, saved-device-state reads and rendered `vfd0`-`vfd43` outputs. No
firmware value was written, no `-log` was used and no broad button sweep was
performed. Status words have their project meanings: `[Verified]`, `[Likely]`,
`[OPEN]`, `[DISPROVEN]`.

## 1. Baseline and instrument validity

Baseline HEAD was `a0631f766ffeb22007cbf0030a0123246e0466f0`. The established
regression passed before measurement: 15 tests, 16 `PASS` lines, exit 0.

The passive capture retained its write-tap through
`lua/lib/asr10_taps.lua`; the live witness counted 260-538 Channel-B bytes per
case. For every byte it recorded order/time, raw value, callback PC, pending
operand class, current renderer cursor/attribute and ASR shadow cursor before
and after the byte. Renderer state was read from the device's registered save
items and checked against `vfd0`-`vfd43`, not inferred only from the text
shadow.

The PC column is provenance for an observed **write bus transaction**. It is
not an instruction-address read-tap and therefore cannot be caused by 68000
prefetch. The common `$F89AA8` callback PC is the post-store context of the
known ring-drain THRB write at `$F89AA4`; it is not renamed as a new routine or
used as a stronger entry-point claim. This preserves the §8.10 boundary.

The acceptance run's complete write-callback PC histogram was:

```text
F885DA:3 F885E4:4 F885F0:4 F89AA8:468 F89C4E:12 F89CB6:44
F89CFE:1 F89D16:1 F89D2A:1
```

These are transaction-provenance buckets, not inferred call targets. Their
distinct values are sufficient to show that bytes from the ring drain and
other producer contexts share the physical stream; no semantic routine name
or execution-entry claim is assigned from the callback PC alone.

### Address correction

**[DISPROVEN]** The previous reference called word-tap bucket 6 CPU address
`$FC480D`. Current address-map and `mc68681` register code, the ROM's own
absolute accesses and the tap alignment agree instead:

```text
CPU byte address $FC4817 (odd low-byte lane)
  -> handler word offset $0B
  -> MC68681 register $0B, THRB
  -> Channel B -> asr10panel_device
```

Lua's 16-bit memory tap reports the aligned address bucket with low nibble 6
for `$FC4816/$FC4817`; that callback representation caused the old `$FC480D`
conversion error. The captured bytes are genuine panel Channel-B traffic, but
the documented CPU address is `$FC4817`.

## 2. Bounded workflows and exact streams

The small coverage set was chosen for protocol idioms, not button discovery.
Raw codes used here were already present in the project or were the existing
layout's Edit candidate needed for the stated acceptance path.

| Case | Action | Exact Channel-B bytes | Rendered result |
|---|---|---|---|
| File browser / full redraw | Seq/Song `$15` from `FILE 1` | `78 0F 79 0F 78 07 78 0E 77 02 66 46 49 4C 45 20 39 20 20 54 55 54 4F 52 49 41 4C 20 53 45 51 15 78 07 78 02 77 02 78 07` | `FILE 9  TUTORIAL SEQ` plus two blank columns |
| REC SRC / full redraw | Sample/Source `$20` | `74 0F 74 0E 74 0D 74 0C 74 0B 74 0A 74 09 74 08 78 0F 77 0D 78 07 78 0E 78 0D 74 0F 74 0E 74 0D 74 0C 74 0B 74 0A 74 09 74 08 66 60 01 52 45 43 20 53 52 43 3D 49 4E 50 55 54 44 52 59 72 60 01 20 62 60 03 4C 45 46 54 20 72` | `REC SRC=INPUTDRY LEFT `, underline 17-21 |
| REC SRC / field navigation | Left `$10` | `00 60 01 52 45 43 20 53 52 43 3D 62 60 03 49 4E 50 55 54 44 52 59 72 60 01 20 4C 45 46 54 20 72` | same glyphs, underline 8-15 |
| REC SRC / field navigation | Right `$11` | `00 60 01 52 45 43 20 53 52 43 3D 49 4E 50 55 54 44 52 59 72 60 01 20 62 60 03 4C 45 46 54 20 72` | same glyphs, underline 17-21 |
| REC SRC / selected value | Up `$0A` | `74 0F 74 0E 74 0D 74 0C 74 0B 74 0A 74 09 74 08 63 52 49 47 48 54 72` | current decoder wrongly leaves `LEFT ` visible |
| REC SRC / selected value | Down `$0B` | `74 0F 74 0E 74 0D 74 0C 74 0B 74 0A 74 09 74 08 63 4C 45 46 54 20 72` | current decoder still leaves `LEFT ` visible |
| Numeric partial update | `VOLUME=99`, Down `$0B` | `14 39 38` | `[Verified]` overwrites columns 20-21 with `98` |
| FX fields | enter FX `$07` | `66 60 01 46 58 3D 62 60 03 49 4E 53 54 20 20 72 60 01 20 20 48 41 4C 4C 20 52 45 56 45 52 42 63 72 67 72` | `FX=INST    HALL REVERB`, underline 3-8 |

The TEMPO stream is retained as executable evidence in
`lua/fixtures/display_tempo_v350.lua`. Its path is:

```text
load TUTORIAL SEQ
raw $05 (existing Edit candidate)
Seq/Song $15
Right $11 three times -> TEMPO=90   LOOP=ON
Left, Right, Up, Down
```

The raw `$05` identity is not promoted to `[Verified]` by this display task;
only the resulting reproducible navigation path is used.

## 3. EDIT SEQUENCE -> TEMPO, byte by byte

### Enter and stabilize

The third Right emits:

```text
66 60 01 54 45 4D 50 4F 3D 62 60 03 39 30 20 72
60 01 20 20 4C 4F 4F 50 3D 4F 4E 20 72
```

| Byte(s), in order | Class | State transition / rendered position |
|---|---|---|
| `66` | clear | cursor 22 -> 0; glyphs/attributes cleared; current attribute -> normal |
| `60 01` | field-attribute command + operand | pending none -> `$60` -> none; normal attribute |
| `54 45 4D 50 4F 3D` (`TEMPO=`) | printable | columns 0-5; cursor 0 -> 6 |
| `62` | selected-field anchor marker | selected field begins at current column 6; current implementation records no anchor |
| `60 03` | field-attribute command + operand | current attribute normal -> underline |
| `39 30 20` (`90 `) | printable selected field | columns 6-8; cursor 6 -> 9; underline remains on 6-8 |
| `72` | field-run terminator | current attribute underline -> normal; cursor stays 9 |
| `60 01` | field-attribute command + operand | normal run |
| `20 20` | printable | columns 9-10; cursor 9 -> 11 |
| `4C 4F 4F 50 3D 4F 4E 20` (`LOOP=ON `) | printable | columns 11-18; cursor 11 -> 19 |
| `72` | field-run terminator | attribute normal; cursor stays 19 |

Actual renderer, ASR shadow and the independent capture model all end at
cursor/shadow position 19 with text `TEMPO=90   LOOP=ON` and three unwritten
trailing columns. Rendered underline is exactly columns 6-8.

### Left and Right

Left produces a `$66` full redraw of the BAR page and leaves its third field
(columns 20-21) underlined. Right returns to TEMPO with the byte-identical
29-byte full redraw above. This establishes that page navigation itself and
the full-redraw field description are correct; the defect begins on value-only
updates.

### Up

Exact stream:

```text
63 39 31 20 72       # $63, "91 ", $72
```

| Byte | Current state before | Current transition | Required protocol transition |
|---|---|---|---|
| `63` | cursor 19, attr normal, selected field visibly at 6-8 | ignored; cursor remains 19 | restore selected-field write position to column 6, retaining that field's underline state |
| `39` | cursor 19 | writes `9` at 19; cursor 20 | write `9` at 6; cursor 7 |
| `31` | cursor 20 | writes `1` at 20; cursor 21 | write `1` at 7; cursor 8 |
| `20` | cursor 21 | writes space at 21; cursor 22 | write space at 8; cursor 9 |
| `72` | cursor 22 | terminates normal run | terminate selected-field rewrite; cursor 9 |

Observed current output and shadow both become
`TEMPO=90   LOOP=ON 91 `; the selected value at columns 6-8 remains `90 `.
The firmware did emit both the field-relative update command and the new value.

### Down

Exact stream:

```text
63 39 30 20 72       # $63, "90 ", $72
```

Current cursor is already 22. Ignored `$63` leaves it there; the first glyph is
stored outside the 22 visible outputs at internal column 22, and later glyphs
are clamped at internal column 23. Both visible display and full ASR shadow stay
at the erroneous appended-`91` state. A field-aware replay restores column 6
and returns the selected field to `90 `.

**Fault classification: A.** `[Verified]` The firmware stream contains the
necessary selected-field positioning relation and correct new values, but the
current decoder does not implement `$62/$63` selected-field state. This is not
case C: `esq1x22_device` does not interpret `$63` correctly before the shadow
diverges. The duplicate shadow is an additional ownership defect, but not a
second independent firmware-stream fault.

## 4. Byte and command inventory

The table covers every byte class present in the bounded captures. A low byte
is interpreted only after pending-operand and producer/frame context has been
considered; range alone is insufficient.

| Byte/range | Class | Operands | State transition | Observed contexts | Generic `esq1x22` today | ASR-10 conclusion | Status |
|---|---|---:|---|---|---|---|---|
| `$00-$1F` standalone | direct column | 0 | renderer cursor := byte | `$00` before field/page overwrite; `$14` before `VOLUME` digits; other bounded values in earlier navigation | sets `m_cursx` | `$00` and `$14` are value-confirmed columns. Values that are operands or frame data are not cursor commands | `[Verified, context-qualified]` |
| `$20-$5F` in text runs | printable | 0 | write glyph at cursor, advance | every screen | writes glyph+current attr | uppercase/number/punctuation display data | `[Verified]` |
| `$60` | field attribute | 1 | pending attr; operand `$01` normal, `$03` underline | REC SRC, BAR, TEMPO, FX | one-byte lookback sets `m_curattr` from bit `$02` | attribute run command | `[Verified]` |
| `$62` | selected-field anchor marker | 0 | identifies the cursor at which the following selected `$60 03` run starts | col 17 REC SRC, col 0/17/20 BAR, col 6 TEMPO, col 3 FX | only resets current attr | the exact hardware name is open, but this marker/run relation supplies the anchor later consumed by `$63` | `[Verified contract; name OPEN]` |
| `$63` | selected-field partial rewrite | 0 | restore the selected-field anchor; following text replaces that field until `$72` | `RIGHT`/`LEFT` at REC SRC col 17; `91 `/`90 ` at TEMPO col 6 | ignored | missing state transition causing both reproduced defects | `[Verified]` |
| `$66` | clear/full-redraw start | 0 | clear renderer glyphs/attrs; cursor 0; current attr normal; reset ASR shadow glyphs/cursor | file, REC SRC, FX, TEMPO | implemented | does **not** clear retained `$77-$7B` register values. A byte received while another command is pending is still that command's operand, not necessarily clear | `[Verified]` |
| `$67` | open field/control opcode | 0 in captured sequence | no visible/current-state effect; always captured as `$67 $72` after `$63 $72` on FX | FX full redraw and value redraws | ignored | reproducible contract only; no printable payload observed | `[OPEN]` |
| `$72` | field-run terminator | 0 | current attr -> normal; ends `$63` rewrite | after every attributed/partial run | resets current attr | field/run terminator | `[Verified]` |
| `$74-$76` | panel control/output command family | 1 on the physical stream | next byte consumed by current lookback; no glyph/cursor/underline effect observed | boot/menu bursts; `$74` frequently; `$75/$76` at boundaries/load | consumes next byte, otherwise no-op | operands are **not nibble-only**: `$74 $40`, `$75 $00/$08`, `$76 $00` occurred. Exact output/state meaning remains unknown | `[OPEN, structure bounded]` |
| `$77-$7B` | retained panel output/annunciator register select | 1 | update one retained 8-bit register; not sent to VFD | before/after redraws, selection state | intercepted by `asr10panel_device` | five separate output registers; state retained across `$66` | `[Verified structure]` |
| `$77` bit 0 | output bit | — | reversible transition in the already documented isolated load/select/reselect case | instrument selection and also other contexts | mirrored to `asr10_instlamp0` | semantic claim remains narrow | `[Verified narrow]` |
| other 39 `$77-$7B` bits | output bits | — | retained raw state | all workflows | exposed raw | identity/solid-vs-blink semantics unknown | `[OPEN]` |
| `$E7 $71` | fixed transition pair | unresolved | no text/cursor/underline effect established | boot and load transitions | ignored | exact context retained; not named reset | `[OPEN]` |
| `$7E/$FC/$FD/$FF/$D5` | panel control/frame data | unresolved | no verified text-state transition | boot/transition clusters | ignored | combined THRB is not self-describing text | `[OPEN]` |
| `$C0 00`; `$E0 00 40`; `$B0 01 7F`; `$46 00`; `$47 00`; `$D0 ...` | separate producer-frame traffic | frame-dependent | no verified text-state transition | immediately before instrument/VOLUME redraw | current byte-only decoder can misread low members as columns and `$40/$46/$47` as text | PC provenance groups these bytes outside the ring text item; exact panel function remains unknown | `[OPEN]` |
| `$90/$80/$3C/$64` | prior VOLUME-context controls | unresolved | no verified transition | earlier inventory; absent from this capture set | ignored or printable by range if not gated | retained as historical reproducible context, not renamed | `[OPEN]` |

### Correction to the old `$74-$76` account

The older inventory said these were always opcode+nibble countdown pairs. The
new all-source capture disproves the operand-range part: `$74 $40` is present,
and callback-PC provenance shows traffic from the ring and separate producer
PCs interleaving on the physical serial stream. The physical next-byte
relationship is real for the current decoder, but a pretty ring-only
`$0F->$00` projection is not sufficient to name the command. `$74/$75/$76`
remain `[OPEN]`; they are not present in the stable TEMPO Up/Down transactions
and are not the missing TEMPO mechanism.

## 5. Transaction classes

| Class | Start/boundary | Cursor/field behavior | Retained state |
|---|---|---|---|
| A. Full redraw | `$66` | cursor 0; subsequent field runs rebuild text and underline | annunciator/output registers survive |
| B. Absolute partial overwrite | standalone column `$00-$15`, then printable data | exact absolute cursor; VOLUME uses `$14` | existing glyphs/attrs outside overwritten range survive |
| C. Selected-field partial rewrite | `$63`, printable field, `$72` | restore the `$62`-established selected-field anchor; preserve field attribute | page text and selected-field identity survive |
| D. Field/attribute redraw | `$00` or `$66`, `$60` runs, `$62` before selected run | text may be identical while underline moves (REC SRC Left/Right) | selected anchor is rebuilt |
| E. Output-register-only | `$77-$7B <value>` (and open `$74-$76` family separately) | no text cursor effect | register values retained until overwritten/reset |

No pure attribute-only transaction with zero text bytes was observed in this
bounded set. REC SRC Left/Right redraw the line while changing attributes; it
would be an overclaim to say the protocol has a verified attribute-only form.

## 6. Clear/reset boundary

For a standalone `$66`, live save-item reads establish:

- renderer cursor -> 0;
- renderer glyph and per-cell attribute arrays cleared;
- current renderer attribute -> normal;
- ASR shadow chars -> spaces and shadow position -> 0;
- `$77-$7B` retained output registers unchanged.

`$66` is not globally self-synchronising in the current byte decoder: pending
annunciator, `$60`, or `$74-$76` handling is tested before the clear branch, so
the same raw value in an operand slot is consumed as data. No captured stream
puts `$66` in such an operand slot. Pending-command reset beyond the standalone
case therefore remains `[OPEN]` hardware semantics.

## 7. Shadow state versus renderer state

Two independent cursor/text models exist today:

| Case | Renderer | `asr10panel_device` shadow | Result |
|---|---|---|---|
| full redraw | cursor/text agree | cursor/text agree | correct bounded output |
| `VOLUME=99 -> 98`, `$14 39 38` | relocates to col 20, renders `98` | ignores `$14`; already at 22, remains `99` | visible renderer correct, shadow stale |
| TEMPO Up, `$63 39 31 20 72` | ignores `$63`, appends at cols 19-21 | ignores `$63`, appends at shadow positions 19-21 | both wrong in the same visible way |
| TEMPO Down after the append | cursor 22/23, writes outside visible 0-21 | shadow full at 22, drops glyphs | both remain stale; internal cursors diverge 23 vs 22 |

At the analysis checkpoint, `current_text()` was not used by visible output and
was not authoritative. The implementation follow-up removes that shadow/API;
the regression helper continues to read `vfd0`-`vfd21`.

## 8. Replay control

`lua/display_protocol_stream_replay.lua` consumes the saved TEMPO fixture as a
contract test for the implemented state machine. It checks `90 -> 91 -> 90`,
cursor 9 after each rewrite, underline retention, `$14` absolute overwrite,
annunciator retention across `$66` and no text effect from bounded `$74 <op>`.

It passes as:

```text
PASS display_protocol_stream_replay tempo=90->91->90 absolute=true underline=true annunciator_retained=true open_pairs_ignored=true
```

`lua/display_field_rewrite.lua` separately drives the same path through V3.50,
the DUART, `asr10panel_device` and visible VFD outputs. It verifies anchor and
underline columns 6-8 and rejects trailing text.

## 9. Ownership decision and state-machine contract

**Implemented boundary:** `asr10panel_device` owns the raw ASR panel stream and
its authoritative protocol state. It sends explicit column/glyph/underline
operations to `esq1x22_device`; `esqvfd_device` remains
glyph/underline-to-output rendering. The separate EPS 1x22 raw parser remains
available and was not generally refactored.

The required observed state is:

```text
Asr10PanelProtocolState
  pending command/operand class
  text cursor column
  current run attribute
  selected field anchor + retained field attribute
  selected-field definition validity/state
  output_registers[5] for $77-$7B
  explicit OPEN register/control observations for $74-$76 and others

GenericRendererState
  chars/attrs cells
  glyph and underline outputs
```

Required operations, named by measured effect rather than guessed hardware
terminology:

```text
clear_text()
set_absolute_column(column)
begin_attribute_run(operand)
mark_selected_field_anchor()
begin_selected_field_rewrite()
end_field_run()
render_character(column, byte, underline)
write_output_register(index, operand)
record_open_control(opcode, operand/context)
```

The decoder must route output-register/frame traffic separately before the
text state machine. Unknown observed bytes stay explicit and loud; they must
not fall through as printable or cursor data merely because their numeric
value overlaps those ranges in another context.

## 10. Remaining OPEN questions

- Exact vendor names and any additional bit semantics for `$60` operands. Only
  `$01` normal and `$03` underline were observed.
- Whether `$62` itself means save/mark cursor or arms a selected-field
  definition completed by the following `$60 03`; the combined operational
  contract is verified.
- `$67` beyond the repeatable empty `$67 $72` FX context.
- Output/state meaning, retention and real panel rendering for `$74-$76`.
- Exact framing/operand grammar for the non-ring `$C0/E0/B0/D0/...` producer.
- Meaning of 39 annunciator bits and panel-local solid/blink encoding.
- High boot/transition controls `$E7/$71/$7E/$FC/$FD/$FF/$D5`.
- Behavior of standalone columns 22-31 on real hardware. No accepted workflow
  writes visible text there; current renderer's internal clamp-to-23 is not
  hardware evidence.
- Master Tune's reported one-step lag and other application workflows outside
  the bounded coverage set.

No permanent decoder behavior is changed by this investigation.
