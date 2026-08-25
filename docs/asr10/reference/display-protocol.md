# ASR-10 panel display protocol

Reference table for the byte stream the host (MC68302, DUART channel B,
`asr10_boot.cpp`'s `duart_panel_asr_candidate_w`) sends to the front-panel
VFD. Measured 2026-08-24 by tapping the DUART's channel-B THRB register
directly (word offset 6 within the `$FC4800-$FC481F` CS window, i.e.
CPU address `$FC480D`, low byte lane — found by tapping the *whole*
16-register window and letting the histogram show which register
dominates, not by assuming the standard SCN2681 register map's usual
THRB slot). Investigation: `../investigations/display-protocol-inventory.md`.

## Current functional boundary

**[Verified, bounded]** The DUART-to-display path, printable text,
cursor-column opcodes, underline/field attributes and the partial-update cases
named in the table below work in the current model.

**[OPEN]** This is not a claim that the display or front-panel UI is complete
or fully correct. Cursor behavior, parameter-field selection and value editing
still need validation through normal user workflows, and the undecoded control
codes and annunciator meanings below remain open. Protocol mechanisms verified
in isolated screens must not be generalized to complete application-level UI
behavior without corresponding runtime coverage.

## Code path (Del 0)

```
firmware -> MC68302 SCC/SIB -> DUART channel B THRB ($FC480D)
  -> scn2681_device -> b_tx_cb -> asr10panel_device::rx_w() (device_serial_interface)
  -> asr10panel_device::rcv_complete() -> send_to_display(byte)
  -> asr10panel_device::send_to_display() [src/mame/ensoniq/esqpanel.cpp]
       - intercepts $77-$7b (2-byte annunciator opcode+value) -- does NOT forward to m_vfd
       - intercepts nothing else; forwards everything else to:
  -> esq1x22_device::write_char() [src/mame/ensoniq/esqvfd.cpp]
       - interprets cursor $00-$1f (with operand lookback),
         $66/$60+operand/$62/$72/$20-$5f
       - unrecognized codes: silently ignored (no-op), no longer printf'd
  -> esqvfd_device::update_display() [shared base class, esqvfd.cpp]
       - pushes segment codes to output "vfd0".."vfd21"
       - pushes underline state to output "vfd22".."vfd43"
  -> src/mame/layout/asr10_panel.lay renders both
```

`asr10panel_device` also keeps its own parallel mirror
(`m_text_chars`/`m_text_position`, used by `current_text()` and every
Lua regression test's `display.read_raw()`), independent of
`esq1x22_device`'s own internal `m_chars`/`m_attrs` state — the two
copies are kept in step because both are driven from the same
`send_to_display()` call for every printable byte, not because one
reads the other.

Unrecognized codes are not silently dropped: `send_to_display()` calls
`report_unhandled_display_code()`, which fires `osd_printf_error()`
once per distinct code value (aggregated, first occurrence only — Del
2). `esq1x22_device`'s own former per-byte `printf("Unhandled control
code...")` (the source of the "Unhandled control code NN" noise filtered
out of every Lua probe's output all session) is removed; the one
aggregated alarm in `asr10panel_device` is authoritative.

## Code table

Evidence levels: `[Verified]` = implemented and regression-tested against
a live, known-correct rendering; `[Verified narrow]` = confirmed for the
specific measured context only, not generalized; `[Derived]` = confidently
reconstructed from stream structure and cross-checked against ground-truth
display text, not directly stated by any source; `[OPEN]` = observed,
not decoded — flagged by the Del 2 alarm, not guessed at.

| Code | Bytes | Meaning | Evidence | Notes |
|---|---|---|---|---|
| `$20-$5F` | 1 | Printable ASCII character; write glyph at cursor, advance, clamp at column 23 | `[Verified, coverage: full-redraw]` | Downgraded 2026-08-24 (`../investigations/partial-update-position-probe.md` Del 1). The full-stream replay that validated this only covered 18 *full-screen redraws* (each starting after a `$66` clear, so sequential placement from column 0 is sufficient on its own). It does not cover partial updates, which need the cursor-position opcode below and were the actual gap: without it, a changed value was appended after the old one instead of overwriting it (measured live on the "VOLUME=99" screen: `"VOLUME=99"` -> `"VOLUME=9998"` -> ... across repeated value changes) |
| `$00-$1F` | 1 | Cursor-position opcode: sets the write column directly to the byte's own value (`$00`-`$15`, matching the 22-column display) | `[Verified]` | Measured live, not inferred: `$14` (=20 decimal) precedes the two value digits of "VOLUME=99" every time Up/Down changes it, an exact match to that field's column; `$00` (=column 0) precedes REC SRC's own field-switch redraw. Implemented `esq1x22_device::write_char()`, fixes the append bug above; regression-tested (`panel_navigation.lua`). A follow-up sweep independently observed values `$00`-`$0A`, `$0C`-`$0F`, `$15` in this role, all within the valid 0-21 column range, none exceeding it — corroborating, not just the one value. Must not be misread when it's actually the operand of `$74`/`$75`/`$76` (see below); disambiguated by lookback exactly like `$60`'s own operand |
| `$66` (`'f'`) | 1 | Clear screen: reset cursor, chars, attributes, and current attribute to normal | `[Verified]` | Pre-existing |
| `$60 <attr>` | 2 | Set current text attribute for the next run of printable characters. `attr & 0x02` != 0 selects underline | `[Verified]` | Implemented `esq1x22_device::write_char()`; regression-tested (`display_protocol.lua`) against the REC SRC screen's Field 2 value, both `LEFT ` and `RIGHT`, both correctly underlined at columns 17-21 |
| `$62` | 1 | "Next field": resets current attribute to normal | `[Derived]`/`[Verified]` | Appears once between two attributed field runs on every multi-field screen observed (REC SRC, FX Select); no operand |
| `$72` | 1 | "End of field": resets current attribute to normal | `[Derived]`/`[Verified]` | Appears after every attributed field run observed; no operand |
| `$77`-`$7b <value>` | 2 | Select one of 5 annunciator registers, write `<value>` | `[Verified]` | Pre-existing. Intercepted in `asr10panel_device`, not forwarded to the VFD |
| `$77` bit 0 | — | Set during the load->select->reselect sequence when Instrument 1 is selected (BTN_02 from idle FILE LOADED); clears on reselect (deselect) | `[Verified narrow]` | Also changes when entering Sample-Source Select / Level Detect — NOT confirmed to mean "instrument 1 selected" in general, only confirmed reversible in the specific isolated sequence tested. Mirrored to output `asr10_instlamp0` |
| `$77`-`$7b`, other 39 bits | — | Unknown | `[OPEN]` | Wired raw to `asr10_annbit0`-`asr10_annbit39` (5 registers x 8 bits). The layout marks their meaning as open; rendering is not evidence for a semantic assignment |
| `$74 <nibble>` | 2 | Repeating, wrapping 4-bit countdown (`$0F`->`$00`), ~8 ticks per burst, bursts recur every ~150-800ms across boot/load/note/menu-nav contexts | `[OPEN]` | Almost certainly an animated busy/activity indicator; exact visual meaning not established (no manual description, no real-hardware reference available). See Del 1 in the investigation doc for the full timing analysis. Its operand is disambiguated from the cursor-position opcode above by lookback, same technique as `$60`'s operand |
| `$75 <nibble>`, `$76 <nibble>` | 2 | Same countdown family as `$74`; used specifically for the terminal tick of some (not all) bursts | `[OPEN]` | Not distinguished further; may encode an outer pass/phase counter |
| `$E7 $71` | 2 (fixed pair) | Always appears together, at boot start and before other full-screen redraws during the load/"shuffling" phase | `[OPEN]` | Candidate: a display-reset/init pair preceding a fresh full-line redraw |
| `$7E`, `$FC`, `$FD`, `$FF`, `$D5` | 1 each | Appear clustered with `$E7 $71` near screen transitions (boot greeting, "KEYBOARD TUNED") | `[OPEN]` | Not decoded. (`$15`, previously listed in this cluster, is now understood as the cursor-position opcode above — column 21 — and removed from here, 2026-08-24) |
| `$E0`, `$B0 <val>`, `$7F`, `$C0` | 1 each | Appear clustered right after "KEYBOARD TUNED" text and around "FILE LOADED" | `[OPEN]` | Not decoded. Previously logged as pairs `$E0 $00`/`$C0 $00`; the trailing `$00` in each is now understood as the cursor-position opcode (column 0) above, a separate, already-explained byte, not part of `$E0`/`$C0`'s own (still unknown) meaning — corrected 2026-08-24 |
| `$90`, `$80`, `$3C`, `$64` | 1 each | Appear during the "JM DIGI SYN  VOLUME=99" context | `[OPEN]` | Candidate: a volume/VU bar-graph sub-protocol, given the context; not decoded |
| `$63`, `$67` | 1 each | Appear near "...CHOR+REV+DDL" (FX Select algorithm list) | `[OPEN]` | Not decoded |

## What is NOT implemented

No rendering was built for any `[OPEN]` code. Per instruction, unknown
control codes are made loud (Del 2's aggregated alarm), not guessed at.
Future work narrowing any `[OPEN]` row should update this table in
place with the new evidence level, not create a duplicate table.
