# ASR-10 panel display protocol

Canonical reference for the host-to-front-panel byte stream. First inventoried
2026-08-24 and re-measured byte-by-byte on 2026-08-26. Detailed provenance:
`../investigations/display-protocol-inventory.md`,
`../investigations/partial-update-position-probe.md` and
`../investigations/display-protocol-state-machine-v350.md`.

## Current functional boundary

**[Verified, bounded]** Channel-B delivery, printable text, clear, absolute
cursor columns and full-redraw underline/field attributes work in the current
model. Numeric absolute partial update (`VOLUME=99 -> 98`) works visibly.

**[Verified protocol, not implemented]** `$62` establishes the selected-field
anchor used by `$63` partial rewrites. The current decoder ignores `$63`.
Consequently both `REC SRC LEFT -> RIGHT` and `TEMPO=90 -> 91` carry correct
field-relative firmware streams but render incorrectly. `EDIT SEQUENCE ->
TEMPO` is decoder fault A, not a bad firmware stream.

This is not a claim that display/UI behavior is complete. Undecoded panel
control traffic, output bit identities, blink and workflows outside the bounded
captures remain `[OPEN]`.

## Address and code path

```text
firmware
  -> DUART channel B THRB, CPU byte address $FC4817 (odd low-byte lane)
  -> scn2681_device::b_tx_cb
  -> asr10panel_device::rcv_complete()
  -> asr10panel_device::send_to_display(byte)
       intercepts $77-$7B + operand, forwards other bytes
  -> esq1x22_device::write_char()
       interprets $00-$1F, $20-$5F, $60, $62, $66, $72
       consumes one operand after $74-$76 but does not interpret it
       ignores $63/$67 and other unknowns
  -> esqvfd_device::update_display()
  -> vfd0-vfd21 glyphs, vfd22-vfd43 underlines, layout
```

**[DISPROVEN]** The earlier reference called the tap bucket CPU address
`$FC480D`. Current address-map and MC68681 code, the ROM's absolute accesses
and tap alignment agree on `$FC4817`: handler word offset `$0B`, MC68681
register `$0B`/THRB. Lua's 16-bit tap represents aligned `$FC4816/$FC4817`
with a low-nibble-6 bucket; converting that bucket as a byte offset caused the
old address error. The captured bytes were Channel-B traffic; only the address
label was wrong.

The combined THRB stream has multiple producers and is not self-describing
text. Raw range tests are applied only after pending-operand and frame context.

## Current split state

`asr10panel_device` keeps a 22-byte linear text shadow and shadow cursor;
`esq1x22_device` separately owns rendered chars, per-cell attributes, current
attribute and renderer cursor. The Lua display helper reads the rendered
`vfd0`-`vfd21` outputs, not `current_text()`.

The split is observably inconsistent:

- `VOLUME=99 -> 98`, stream `$14 39 38`: renderer relocates to column 20 and
  shows `98`; the shadow ignores `$14` and remains `99`.
- TEMPO Up, stream `$63 39 31 20 72`: both ignore `$63` and append `91 ` at
  columns 19-21; TEMPO Down then writes outside the visible range and leaves
  the bad display unchanged.

The shadow is not authoritative. The future decoder must have one ASR-owned
text/cursor/field state.

## Byte/command inventory

| Code | Bytes | Interpretation and state transition | Evidence/status |
|---|---:|---|---|
| `$20-$5F` in a text run | 1 | Printable ASCII at current cursor, then advance. Only text after pending/frame context is resolved | `[Verified, context-qualified]` |
| `$00-$1F` standalone | 1 | Direct cursor column. `$00` positions page/field overwrites; `$14` positions VOLUME digits at column 20 | `[Verified, context-qualified]`; identical values in operand/frame slots are not cursor commands |
| `$60 <attr>` | 2 | Begin attribute run. Observed `$01` normal, `$03` underline; bit `$02` selects underline | `[Verified]` |
| `$62` | 1 | Mark selected-field anchor for the following `$60 $03` run | `[Verified operational contract; exact vendor name OPEN]`; REC SRC anchors 17/8, BAR 0/17/20, TEMPO 6, FX 3 |
| `$63` | 1 | Restore selected-field anchor for printable partial rewrite ending at `$72` | `[Verified, not implemented]`; `63 "91 " 72` targets TEMPO column 6 and `63 "RIGHT" 72` targets REC SRC column 17 |
| `$66` | 1 | Standalone clear: renderer glyphs/attrs cleared, cursor 0, current attr normal; ASR shadow chars/cursor reset | `[Verified]`; retained `$77-$7B` outputs survive. A `$66` in an operand slot is data, not necessarily clear |
| `$67` | 1 | Reproducibly appears as empty `$67 $72` after `$63 $72` on FX pages; no printable payload/effect measured | `[OPEN]` |
| `$72` | 1 | End field/partial run; current attr -> normal | `[Verified]` |
| `$74-$76 <value>` | 2 on physical stream | Open panel control/output family; current VFD consumes next byte with no visible state change | `[OPEN, structure bounded]`; operands are not nibble-only: `$74 $40`, `$75 $00/$08`, `$76 $00` observed |
| `$77-$7B <value>` | 2 | Select/update one of five retained 8-bit output/annunciator registers; not forwarded to VFD | `[Verified structure]`; state retained across `$66` |
| `$77` bit 0 | — | Reversible in the isolated load/select/reselect case, but also changes in another context | `[Verified narrow]`; current mirror `asr10_instlamp0` must not be generalized |
| other 39 `$77-$7B` bits | — | Retained raw output bits | `[OPEN]` identities and solid/blink meaning |
| `$E7 $71` | unresolved | Fixed transition pair near boot/load redraws; no verified text-state effect | `[OPEN]`; do not name reset |
| `$7E/$FC/$FD/$FF/$D5` | unresolved | Panel control/frame data near transitions | `[OPEN]` |
| `$C0 00`; `$E0 00 40`; `$B0 01 7F`; `$46 00`; `$47 00`; `$D0 ...` | frame-dependent | Separate producer-frame traffic before instrument/VOLUME redraw | `[OPEN]`; callback-PC provenance retracts the claim that every low member is a display-column byte |
| `$90/$80/$3C/$64` | unresolved | Earlier VOLUME-context controls, absent from the 2026-08-26 bounded action streams | `[OPEN]`; retained as prior reproducible provenance |

### `$74-$76` correction

The old table called these opcode+nibble countdown/animation pairs. The new
capture disproves the operand-range claim (`$74 $40`) and shows ring and
separate-producer bytes interleaving on the physical stream. Countdown-like
subsequences remain observed, but do not establish animation semantics.
`$74-$76` are absent from stable TEMPO Up/Down and are not its missing
positioning mechanism.

## Transaction forms

| Form | Protocol shape | Retained state |
|---|---|---|
| Full redraw | `$66`, then printable/field runs | output registers survive |
| Absolute partial | standalone column, printable bytes | unrelated glyphs/attrs survive |
| Selected-field partial | `$63`, field text, `$72` | `$62`-established anchor and field attribute survive |
| Field/attribute redraw | `$00` or `$66`, `$60` runs, `$62` before selected `$60 $03` run | selected anchor rebuilt; REC SRC Left/Right redraw identical text with different underline |
| Output-only | `$77-$7B <value>`; `$74-$76` separately open | no text cursor effect |

No pure attribute-only transaction with zero text bytes was observed in this
bounded set.

## TEMPO acceptance stream and replay

Exact captured fixture: `../lua/fixtures/display_tempo_v350.lua`.

```text
full page:
66 60 01 "TEMPO=" 62 60 03 "90 " 72 60 01 "  LOOP=ON " 72

Up:   63 "91 " 72
Down: 63 "90 " 72
```

After the full page, renderer and shadow cursor are 19 and underline is 6-8.
Current `$63` no-op writes Up at 19-21, producing
`TEMPO=90   LOOP=ON 91 `. The required transition is cursor 19 -> selected
anchor 6 before writing, producing `TEMPO=91   LOOP=ON`, while retaining
underline 6-8.

`../lua/display_protocol_stream_replay.lua` deterministically reproduces both
the current failure and the measured field-aware result. It is an analysis
oracle, not yet a C++ device regression; the next implementation round should
make the real ASR decoder consume the same fixture.

## State ownership contract

The raw-byte owner should be ASR-specific (`asr10panel_device` or a dedicated
ASR child). It owns pending command/operand, cursor, `chars[22]`, `attrs[22]`,
current run attribute, selected-field anchor/attribute, rewrite state and
output-register routing. A generic `esq1x22_device` should accept renderer
operations/state, not interpret raw ASR opcodes. `esqvfd_device` remains the
generic glyph/underline output renderer.

Unknown observed controls stay explicit and loud; they must not fall through
as printable/cursor data merely because their value overlaps another class.

## Still OPEN

- Exact vendor names for `$62/$63`, and `$60` bits beyond observed `$01/$03`.
- `$67` beyond the empty FX sequence.
- Meaning/retention/rendering of `$74-$76`.
- Non-ring producer framing and high boot/transition controls.
- 39 output-bit identities and panel-local blink encoding.
- Real-hardware behavior for standalone columns 22-31.
- Master Tune's reported one-step lag and unmeasured UI workflows.

No permanent display behavior was changed during this analysis.
