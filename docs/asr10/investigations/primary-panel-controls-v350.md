# Primary front-panel controls, targeted V3.50 witnesses

This round tested only candidates already isolated by the physical panel
structure and prior raw-map evidence. It was not a matrix sweep. Every run
started from a clean V3.50 `FILE 1  TUTORIAL BNK` boot and injected ordinary
press/release panel frames through `asr10panel_device`.

## Verified raw codes

| Control | Raw | Before | After / witness |
|---|---:|---|---|
| Load | `$1A` | selected `JM DIGI SYN  VOLUME=99` | `FILE 2  JM DIGI SYN`: firmware file browser |
| Edit | `$05` | selected `JM DIGI SYN  VOLUME=99` | `JM DIGI  LYR=1  WV=ALL`: instrument layer/wave parameter page |
| Effects | `$09` | `FILE 1  TUTORIAL BNK` | `FILE 16  LUSH PLATE`: real effect-file browser |

`$09` identifies the physical Effects category even though any later ES5510
effect-download failure remains a separate subsystem issue. The bindings are
physical `button_change -> set_button(raw, pressed)` edges; no firmware action
is synthesized in the host layer.

## Still open

| Control(s) | Candidate tested | Result | Status |
|---|---:|---|---|
| Record | `$19`, held while `$1D` Play was pressed in a loaded-sequence page | Result `FREE SYSTEM BLKS=3614` was not a discriminating Record witness. | `[OPEN]` |
| Track 2–8 | `$08`, `$0E`, `$14`, `$04`, `$22`, `$1C`, `$16` | With only Instrument 1 loaded, none produced an individual display or annunciator-state selection witness. | `[OPEN]` |

Track 1 remains `$02 [Verified]`. Neither the physical layout ordering nor the
candidate list is sufficient to infer the remaining Track identities.

## Display-context observation

The temporary probe (removed after the run) tapped the existing Channel-B
display-write stream at `$FC4817`. For the first interesting/open byte per
button phase it retained 12 prior and 12 following bytes, display text,
annunciator-register snapshot, last physical button edge and elapsed time.
`$74`–`$76` explicitly consumed one following operand, so operands were not
reported as independent opcodes.

Examples measured in this round, without semantic promotion:

- `$74` and `$75` occurred immediately after `$1A` Load from the instrument
  page, before the file-browser redraw.
- `$74` and `$75` occurred after `$05` Edit before the layer/wave redraw.
- `$7F` occurred during the `$09` Effects file-browser transition.
- `$7C` occurred immediately after Play was pressed in the `$19`+`$1D` trial.

These are UI-context correlations only. They do not define opcode semantics;
the pre-existing boot bytes (`$E7`, `$71`, `$7E`, `$FC`, `$7F`, `$FF`, `$FD`,
`$D5`) likewise remain open.
