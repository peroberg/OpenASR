# ASR-10 display stability across practical V3.50 workflows (2026-08-26)

This is a bounded UI-workflow validation of the ASR-specific decoder at
`dc3e77f74cdc4a36d6c4a22f304809732490c70b`. It is not a new opcode search.
No firmware state was written, no display bytes were injected and no permanent
decoder behavior changed.

## Method and boundary

Each workflow started from a fresh V3.50 boot and used only established panel
events through the normal panel/DUART/firmware path. The observation read the
22 rendered `vfd0`-`vfd21` glyph outputs, the 22 underline outputs and, for the
annunciator case, the retained `asr10_annreg0`-`asr10_annreg4` outputs. The
annunciator case additionally retained a live Channel-B write-tap through
`lua/lib/asr10_taps.lua`; it captured 209 bytes by the boot witness and 72 bytes
in the relevant REC SRC transition.

The glyph reader deliberately preserves segment ambiguities: `O` appears as
`0`, `S` as `5`, and some punctuation/digit variants as `?`. Assertions therefore
use exact rendered output, positions and transitions rather than silently
normalising those cells into an intended application string.

## Results

| Workflow | Full redraw | Partial/value update | Cursor/position | Underline/selected field | Stale or appended state | Result |
|---|---|---|---|---|---|---|
| EDIT SEQUENCE -> TEMPO | `TEMPO=90   LOOP=ON` rebuilt after BAR | `90 -> 91 -> 92 -> 91 -> 90` stayed at columns 6-8 | correct; no drift across four rewrites | 6-8 throughout; BAR used 20-21 and TEMPO restored 6-8 | none; LOOP remained intact | `[Verified runtime]` pass |
| LOAD file browser | FILE 1 -> 2 -> 3 -> 4 -> 3 -> 2 | navigation redraws, not a claimed partial form | each exact 22-cell line aligned | not applicable | none across differing name lengths | `[Verified runtime]` pass |
| COMMAND / System / Master Tune | Instrument Command, free-block pages and Master Tune redrew cleanly | rightmost value cell changed baseline -> `1` -> the identical baseline immediately | value stayed at the same right-hand cells | no underline emitted in this workflow | none between Command pages | `[Verified runtime, bounded]` pass |
| EDIT INSTRUMENT layer page | `JM DIGI  LYR=1  WS=ALL` entered cleanly | selected layer changed `1 -> 5 -> 1` at the same cells | stable | Left/Right moved 13-14 -> 19-21 -> 13-14 | none | `[Verified runtime]` pass |
| SAMPLE / SOURCE SELECT, REC SRC | clean `REC SRC=INPUTDRY LEFT/RIGHT` redraws | Left/Right changed selected field; source value round-tripped LEFT -> RIGHT -> LEFT | stable | 17-21 -> 8-15 -> 17-21; value redraw retained 17-21 | none | `[Verified runtime]` pass |
| Instrument VOLUME | instrument/value line remained intact | absolute update `99 -> 98 -> 97 -> 96 -> 97 -> 98 -> 99` | digits remained at columns 20-21 | not applicable | none; no append at line end | `[Verified runtime]` pass |
| Annunciator plus REC SRC | REC SRC full redraw remained correct | following field navigation remained correct | text cursor unaffected by output writes | 17-21, then 8-15 | none; retained registers survived clear | `[Verified runtime]` pass |

The strengthened permanent firmware/device acceptance in
`lua/display_field_rewrite.lua` retains the BAR -> TEMPO field-state switch and
the repeated `90 -> 91 -> 92 -> 91 -> 90` rewrite. The broader one-off observer
was removed after the run under the project's instrumentation rule.

## Annunciator/clear witness

The real REC SRC transition contained this ordered suffix (other structurally
OPEN panel-control pairs omitted here only for readability):

```text
77 0D 78 0E 78 0D ... 76 08 66
60 01 "REC SRC=INPUTDRY" 72 60 01 " " 62 60 03 "LEFT " 72
```

Immediately before this transition the retained registers were
`0E:0E:0F:0B:0B`; after the explicit `$77/$78` writes and the later `$66` they
were `0D:0D:0F:0B:0B`. No output-register write followed `$66` in this
transaction. Thus the real workflow agrees with the replay contract: `$66`
clears text/attributes but does not clear the retained `$77-$7B` registers.
The subsequent `$00` field redraw moved underline 17-21 -> 8-15 without changing
those registers or corrupting text.

`$76 $08` is recorded only as a bounded command/operand pair. This run does not
assign it a name or display effect.

## Fault classification

No new reproducible display fault occurred, so no case required an A/B/C fault
classification or a decoder fix. In particular, the earlier manual report that
Master Tune could display one step behind was not reproduced in the bounded
baseline -> `1` -> baseline round-trip. Because the segment reader leaves the
baseline glyph ambiguous, that negative reproduction does not prove all Master
Tune values correct; its application semantics and broader range remain
`[OPEN]`.

## Resulting status

The current decoder is **workable/stable for practical navigation** across the
tested redraw, absolute-update, selected-field, attribute and retained-output
transaction classes. This is deliberately narrower than "the display protocol
is fully understood". `$67`, `$74-$76`, high/frame traffic, `$60` bits beyond
the observed `$01/$03`, physical columns 22-31 and 39 annunciator-bit identities
remain `[OPEN]` and unchanged.
