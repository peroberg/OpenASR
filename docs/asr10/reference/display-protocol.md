# ASR-10 panel display protocol

Canonical reference for the host-to-front-panel byte stream. First inventoried
2026-08-24 and re-measured byte-by-byte on 2026-08-26. Detailed provenance:
`../investigations/display-protocol-inventory.md`,
`../investigations/partial-update-position-probe.md` and
`../investigations/display-protocol-state-machine-v350.md`. Practical workflow
coverage is recorded in
`../investigations/display-stability-workflows-v350.md`.

## Current functional boundary

**[Verified, bounded]** Channel-B delivery, printable text, clear, absolute
cursor columns and full-redraw underline/field attributes work in the current
model. Numeric absolute partial update (`VOLUME=99 -> 98`) works visibly.

**[Verified mechanism, implemented]** `$62` establishes the selected-field
anchor used by `$63` partial rewrites. `asr10panel_device` now restores that
anchor and its underline attribute. The real V3.50 `EDIT SEQUENCE -> TEMPO`
path visibly round-trips `90 -> 91 -> 90` without trailing text; REC SRC
field-relative updates use the same mechanism.

**[Verified runtime, bounded stability]** Fresh-boot V3.50 validation covers
TEMPO/BAR, LOAD file browsing, Command/Master Tune, an Edit Instrument layer
page, REC SRC, VOLUME and an annunciator-plus-clear transition. No cursor drift,
appended garbage, stale glyphs or field-attribute leakage was observed. This is
enough to call the decoder workable/stable for practical navigation in those
workflows, not complete.

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
       owns ASR cursor, attribute, selected-field and operand state
       retains $77-$7B output registers separately from text
  -> esq1x22_device::render_character(column, glyph, underline) / clear()
       generic explicit-position renderer operations, no ASR opcode decoding
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

## Current state ownership

`asr10panel_device` is the authoritative ASR raw-byte decoder. It owns pending
operand class, logical cursor, current underline state, selected-field anchor
and retained selected-field attribute. `$77-$7B` output registers remain in
the same ASR device but are routed before text state.

The old `m_text_chars`/`m_text_position` linear shadow and `current_text()` API
were removed: they had no runtime consumer and were already proven stale after
`VOLUME` absolute updates. `esq1x22_device` now receives explicit position,
glyph and underline renderer operations on the ASR path. Its legacy raw-byte
parser remains available to the separate EPS 1x22 panel path; `esqvfd_device`
remains generic output rendering.

## Byte/command inventory

| Code | Bytes | Interpretation and state transition | Evidence/status |
|---|---:|---|---|
| `$20-$5F` in a text run | 1 | Printable ASCII at current cursor, then advance. Only text after pending/frame context is resolved | `[Verified, context-qualified]` |
| `$00-$1F` standalone | 1 | Direct cursor column. `$00` positions page/field overwrites; `$14` positions VOLUME digits at column 20 | `[Verified, context-qualified]`; identical values in operand/frame slots are not cursor commands |
| `$60 <attr>` | 2 | Begin attribute run. Observed `$01` normal, `$03` underline; bit `$02` selects underline | `[Verified]` |
| `$62` | 1 | Mark selected-field anchor for the following `$60 $03` run | `[Verified operational contract; exact vendor name OPEN]`; REC SRC anchors 17/8, BAR 0/17/20, TEMPO 6, FX 3 |
| `$63` | 1 | Restore selected-field anchor and retained attribute for printable partial rewrite ending at `$72` | `[Verified mechanism, implemented]`; `63 "91 " 72` targets TEMPO column 6 and `63 "RIGHT" 72` targets REC SRC column 17 |
| `$66` | 1 | Standalone clear: renderer glyphs/attrs cleared, ASR cursor 0, current attr normal | `[Verified]`; retained `$77-$7B` outputs survive. A `$66` in an operand slot is data, not necessarily clear |
| `$67` | 1 | Reproducibly appears as empty `$67 $72` after `$63 $72` on FX pages; no printable payload/effect measured | `[OPEN]` |
| `$72` | 1 | End field/partial run; current attr -> normal | `[Verified]` |
| `$74-$76 <value>` | 2 on physical stream | Open panel control/output family; ASR decoder consumes the bounded operand with no visible state change | `[OPEN, structure bounded]`; operands are not nibble-only: `$74 $40`, `$75 $00/$08`, `$76 $00` observed |
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

After the full page, the ASR cursor is 19 and underline is 6-8. `$63` now moves
the cursor to saved anchor 6 and restores the selected underline before writing.
Up therefore produces `TEMPO=91   LOOP=ON`; Down restores `90` at the same
columns and ends at cursor 9 without overflow or LOOP corruption.

`../lua/display_protocol_stream_replay.lua` locks the transaction contract;
`../lua/display_field_rewrite.lua` locks the same transition through the real
V3.50 firmware/device path, including a BAR -> TEMPO state round-trip and
repeated `90 -> 91 -> 92 -> 91 -> 90` rewrites.

## State ownership contract

The implemented raw-byte owner is `asr10panel_device`. It owns pending
command/operand, cursor, current run attribute, selected-field anchor/attribute
and output-register routing. `esq1x22_device` accepts explicit renderer
operations on this path; `esqvfd_device` remains the generic glyph/underline
output renderer.

Known operand contexts are routed before printable/cursor ranges. Unknown
observed controls stay explicit and loud; unresolved producer-frame grammar
remains `[OPEN]` rather than acquiring inferred rendering semantics.

## Still OPEN

- Exact vendor names for `$62/$63`, and `$60` bits beyond observed `$01/$03`.
- `$67` beyond the empty FX sequence.
- Meaning/retention/rendering of `$74-$76`.
- Non-ring producer framing and high boot/transition controls.
- 39 output-bit identities and panel-local blink encoding.
- Real-hardware behavior for standalone columns 22-31.
- Master Tune outside the bounded baseline -> `1` -> baseline workflow, and
  unmeasured UI workflows. The previously reported one-step lag did not
  reproduce in that bounded round-trip, whose baseline glyph remains ambiguous,
  but is not globally disproven.

The implemented mechanism is bounded to the verified grammar above; it does not
make the complete ASR-10 display protocol solved.
