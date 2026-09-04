# Bank 1 Is CPU Lowmem, Not A Second Sample Pool — Real Audio Confirmed

## Premise Correction (User-Flagged)

An earlier turn implicitly treated "`$100000-$1FFFFF` still zero" as
evidence that no bank mapping could help. That was wrong: bank 1 on
real ES5506/ASR-10 hardware need not alias the same physical RAM as
bank 0 at all — it could be a different physical or CPU-visible
region, including the low RAM where the loaded instrument's own 172,544
bytes already sit. Bank 0's content says nothing about bank 1's.

## Del 1 — The Actual Values, Compared Explicitly

Voice 1 (panel key press) and voice 2 (MIDI note-on), from
`keyboard-and-sample-bridge-5.md`'s `select-and-play.lua`, final
register state (last write of each field wins):

```text
CR    = 0x00004018
  bit 15-14 = 01  -> bank 1  (the earlier high-page write, 0x00004300,
                               has the identical bank bits -- both
                               agree, the low-page write just landed
                               last chronologically)
START = 0x0FE8A800  -> word address 0x1FD15
END   = 0x153FF980  -> word address 0x2A7FF
ACCUM = 0x0B278000  -> word address 0x164F0 (position at note-on)

bankrelativt byteintervall = 2*0x1FD15 .. 2*0x2A7FF = 0x3FA2A .. 0x54FFE
```

Identical values for both voice 1 and voice 2 (verified by direct
comparison of the two write logs, not assumed).

**Compared against the three regions asked for:**

| Region | Range | Relation to `$3FA2A-$54FFE` |
|---|---|---|
| Real loaded payload | `$000944-$0552FF` | **Contains it entirely** — `0x000944 < 0x3FA2A` and `0x54FFE < 0x0552FF`, with ~1.8KB of slack at the end |
| Sample RAM candidate (bank 0 share) | `$100000-$1FFFFF` | Disjoint — the bank-relative range is a *byte offset within a bank*, not itself a CPU address, so this comparison only becomes meaningful once a base is assumed; see Del 2/3 |
| Other verified RAM/alias windows | CPU lowmem `$000000-$0FFFFF` (`mem_map`'s `low_rom_or_lowmem_r`/`lowmem_w`, 1MB) | The bank-relative range, read as a **direct offset from `$000000`**, falls inside this window and inside the loaded payload specifically |

**[Verified]** The bank-relative range is not an arbitrary-looking
number — it lands precisely inside the one CPU-visible region already
proven (`file-loaded-verification-probe.md`) to hold the real,
byte-identical loaded instrument data.

### `es5506.h`/`.cpp`, read only, not modified

`es5506_device::get_bank(u32 control)` (`es5506.h:187`): `return
(control >> 14) & 3;` — bits 15-14 of the 16-bit control register, 4
banks (0-3), matching `m_bank0_config`...`m_bank3_config` and
`memory_access<21,1,-1,ENDIANNESS_BIG>::cache m_cache[4]`
(`es5506.h:225`).

`es5505_device::get_bank(u32 control)` (`es5506.h:241`): `return
(control >> 2) & 1;` — bit 2 only, 2 banks (0-1). **ES5505 and ES5506
genuinely differ**: different bit position, different field width,
different bank count — not a naming variant of the same logic.

Register storage confirms our decoder's masks match the chip exactly
(`es5506.cpp:1193/1198`): `voice->start = data & 0xfffff800`,
`voice->end = data & 0xffffff80`, both masked in real hardware code,
not just our Lua tap. `get_integer_addr()` (`es5506.h:107`) right-shifts
the stored value by `ADDRESS_FRAC_BIT` (11) to produce the word address
`read_sample()` actually uses — confirming the `raw >> 11` conversion,
not an assumption. `es5506.cpp:1183-1204` (`reg_write_low`/
`reg_write_high`) confirms `CR` (register `0x00/8`) is the *same*
`voice->control` field whether written via the low-page or high-page
address, resolving why our decoder logged two different "CR" values —
they are two writes to one register, last write wins.

## Del 2 — What Bank 1 Must Correspond To

`shared-ram-content-witness.lua`: selected the instrument, played a
note via panel and MIDI, then checked `$100000-$1FFFFF` content — not
stopping at a zero result, per §8.7's witness requirement.

```text
SRCW_WITNESS es5506=18882 payload=5697286 samram_writes=524290
SRCW_CONTENT zero=0 sampled=256 zero_pct=0.00
SRCW_BYTE off=000000 value=04
SRCW_BYTE off=040000 value=05
SRCW_BYTE off=080000 value=06
SRCW_BYTE off=0C0000 value=07
SRCW_BYTE off=0FFFF0 value=07
```

**[Verified]** `samram_writes` stayed at exactly 524,290 — the
already-established one-time boot-sweep count
(`sample-ram-and-voice-registers.md`) — through the *entire*
select+play window, with both sibling witnesses alive throughout
(18,882 and 5,697,286 hits respectively, proving the tap mechanism
itself survived the window, not just its installation instant). Zero
new writes, genuinely witnessed, not assumed.

**[Verified, and a correction to prior phrasing]** The content is not
zero at all — dense dump confirms a repeating 16-bit pattern equal to
`word_address >> 10` (`$0400` at `$100000`, `$0500` at `$140000`,
`$07FF` at `$1FFFC0`): a classic address-derived RAM self-test
pattern, consistent with `mem_map`'s own comment ("sample RAM
candidate, directly tested by the boot ROM at 0x100000"). Prior docs'
"one-time clear sweep" language described this region's write
*count*, not its content — the boot-time pass is a RAM test, not a
zero-fill. This does not change any prior conclusion (the region is
still untouched by any note-play activity) but the content
characterization itself was previously imprecise.

**Numeric determination, per the three-way test:**

- **Hypothesis 1** (bank 1 = same RAM as bank 0's `$100000` alias,
  data not yet moved): **refuted**. Zero new writes to
  `$100000-$1FFFFF` occurred during play, witnessed live through the
  whole window — there is no in-flight "sample bridge" waiting to
  finish; nothing writes there at all in response to a note.
- **Hypothesis 2** (bank 1 = other CPU/board-visible memory, plausibly
  where the loaded instrument already sits): **confirmed** — Del 1's
  bank-relative range lands inside the real payload's own address
  range when read as a direct `$000000`-based offset, and `mem_map`'s
  lowmem region (`$000000-$0FFFFF`, `low_rom_or_lowmem_r`/`lowmem_w`,
  backed by `m_lowmem_shadow`) is exactly a 1MB word range — the same
  size as bank 0's own shared window, and it comfortably contains both
  the computed range and the loaded payload.
- **Hypothesis 3** (CR/bank interpretation mismatch): not applicable —
  see Del 3, where treating bank 1 as this exact hypothesis-2 region
  produced real audio, positively confirming the interpretation rather
  than forcing reconsideration of it.

## Del 3 — The Minimal Change, And What Came Out

Hypothesis 2, confirmed. Minimal change: gave ES5506 bank 1 its own
address map, `es5506_wavetable_bank1_map()`
(`src/mame/ensoniq/asr10_boot.cpp`), reusing `mem_map`'s own
`low_rom_or_lowmem_r`/`lowmem_w` member functions directly — the same
backing store (`m_lowmem_shadow`), not a private copy, over the same
`$000000-$07FFFF` word range (1MB) that mirrors `mem_map`'s own
`$000000-$0FFFFF` byte range exactly, the same technique already
validated for bank 0's `$100000-$1FFFFF` share. **`mem_map()` itself
is untouched** — only `es5506_wavetable_bank1_map()` (new) and the one
`set_addrmap(1, ...)` line changed. Banks 2/3 remain
`es5506_unpopulated_wavetable_map` (`.noprw()`), unchanged.

Rebuilt, regression 7/7 green, re-ran `select-and-play.lua` under
`-wavwrite`:

```text
channel 0: peak=0    nonzero=0/1200961        (unused/mono monitor channel)
channel 1: peak=3970 nonzero=90334/1200961    (SPEAKER left)
channel 2: peak=3970 nonzero=90334/1200961    (SPEAKER right)
first_nonzero_time = 23.135604s  (0.475ms after the last voice-register write)
last_nonzero_time  = 25.02s      (through the end of the capture window)
```

Sample trace at the onset (channels 1/2 identical, raw interleaved
values): `0, 3, 6, 10, 17, 25, 33, 43, 54, 66, 78, 91, 104, ..., 169,
169, 167, 161, ..., -139, ...` — a smooth, continuous ramp, not noise
or a click. A 100ms window 30ms into the note: peak 2932, 144
zero-crossings, **~720Hz**, clearly periodic.

**[Verified]** Real, sustained, periodic, non-clipping audio output
— amplitude ~12% of full scale, ~720Hz apparent pitch with a rising
envelope, starting within half a millisecond of the voice program
completing and lasting through the capture window. Not a click, not
noise: a played note.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, before and after the driver
  change.
- Only `es5506_wavetable_bank1_map()` (new function) and one
  `set_addrmap(1, ...)` line changed in `asr10_boot.cpp`. `mem_map()`
  itself untouched — no line in it added, removed, or reordered. No
  ES5510 change. `es5506.h`/`.cpp` read extensively this task, not
  modified, not committed.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` used as
  directed, analyzed externally (Python `wave`/`array`, not committed
  to the repo).
- No fork with an open mandate; two narrowly-scoped read-only research
  forks were used (manual/button-select lookup, routine-index source
  compilation), both independently verified against live measurement
  or existing file contents before being relied on.
- `git diff --check`: clean.

## Del 4 — Routine Catalog: Extended, Not Duplicated

The task asked for a new `docs/asr10/reference/routine-index.md`.
`docs/asr10/reference/subroutine-index.md` already exists, is
functionally identical in purpose (same evidence-level convention,
same "address is identity, name may change" philosophy, same
per-entry Inputs/Outputs/Calls format), and is the file every routine
address discovered in this project's earlier work already lives in.
Per this project's own rule 4 ("färdigställ eller radera innan du
bygger nytt"), **extended `subroutine-index.md` instead of creating a
disconnected second catalog.**

Added 15 new entries: `$F88078` (trap3_enqueue), `$F880A2`
(trap4_dequeue), `$F880D6` (trap6_rearm), `$F88138`
(trap9_slot_install), `$00740C` (jumptable_dispatch_15entry),
`$FF9650` (shared_absolute_jmp_vector_table), `$F88AA2`
(midi_panel_data_byte_handler), `$F884FC`
(duart_chan_a_continuation_stash), `$FFB392` (panel_frame_classify),
`$FFB20A` (panel_frame_second_byte_bit7), `$FFB43E`
(panel_midi_completion_consumer), `$FFB488`/`$FFB4CC` (its two
siblings), `$FFB56E` (completion_post_dispatch), `$FFB6C4`
(key_range_check), and `$FB8AA2` (fdc_msr_data_poll — the FDC data
loop, explicitly distinguished from `$F88AA2` since the two addresses
differ by exactly one hex digit and name a completely unrelated
routine in a different subsystem). Updated the "kanal A = MIDI"
evidence level from `[Likely]` to `[Verified]` (this session's direct
DUART-A wiring, reception, and firmware-parser trace supersedes the
prior `esq5505.cpp`-precedent-only claim). Updated the Coverage count
(36 -> 51 named addresses, 27 -> 42 identified ROM addresses).

The `$FFB43E` double-discovery this catalog exists to prevent is
documented directly in its own entry's opening line.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: +19 lines (one new address-map
  function reusing two existing member functions, one declaration
  line, one changed `set_addrmap` argument).
- `docs/asr10/lua/archive/`: one new script this task
  (`shared-ram-content-witness.lua`).
- `docs/asr10/reference/subroutine-index.md`: extended (15 new
  entries + 1 updated evidence level + updated coverage count), not
  duplicated.
