# Sample RAM And Voice Registers: Mapping The Sound Path

## Scope

`file-loaded-verification-probe.md` established that instrument data lands
in low RAM (`$000944-$0552FF`) and never touches `$100000-$1FFFFF` during
the load itself, and that ES5506/ES5510 register traffic is continuous and
richly varied from near-reset onward. The user's follow-up: that
destination fact has three possible explanations that point in different
directions (a later copy this measurement window missed; a wrong
`mem_map` attribution; or the ES5506 reading samples over its own address
space, disconnected from the CPU's). This task measures which, and reads
`es5506.h`/`.cpp` (unmodified) to establish what the chip actually
requires. Pure observation and source reading — no ES5506/ES5510/mem_map
code changed.

## Del 1 — Does Sample Data Ever Reach `$100000-$1FFFFF`?

`docs/asr10/lua/archive/sample-ram-and-voice-registers-probe.lua`: taps
all writes to `$100000-$1FFFFF` from reset through 10 seconds past `FILE
LOADED` (t=21.79 to t=31.79, a longer window than the previous task's, per
instruction — a post-load copy could happen after the display already
updated). Witnessed with a sibling tap on `$000000-$05FFFF`, installed the
same way at the same time: an identical install method with a
known-nonzero result throughout the same window proves the tap
methodology itself is alive, so a zero result on the target range is a
real negative, not a dead tap (`methods-static-analysis.md` §8.7).

```text
SRVR_WITNESS label=start t=0.000000     witness_writes=0
SRVR_FILE1            t=16.300000  samram_writes=524290
SRVR_LOADED            t=21.790000  samram_writes=524290  witness_writes=1158818
SRVR_WITNESS label=end t=31.790000                        witness_writes=1994268
SRVR_SAMRAM_TOTAL writes=524290 t=31.790000
```

- **[Verified]** 524,290 writes to `$100000-$1FFFFF` total — but **all of
  them land before `t=16.3s`** (before `FILE 1` is even reached), and the
  count is **unchanged** across the entire instrument-load-plus-10s-idle
  window (`t=21.79` to `t=31.79`). 524,290 ≈ 0x100000 bytes / 2 — one
  write per 16-bit word across the whole 1MB range once. **[Likely]** this
  is a one-time boot-time RAM clear/test pass, not a data-load path; it
  finishes long before any instrument is selected.
- **[Verified]** The witness tap (`$000000-$05FFFF`, installed identically,
  same script, same run) grew from 0 to 1,158,818 by `FILE 1` and to
  1,994,268 by end of window — proving taps in general kept firing for
  the entire measurement, including well past `t=16.3s`. `$100000-$1FFFFF`
  going completely silent for the whole load-and-idle window is therefore
  a real negative result, not an artifact of a tap dying.
- **Conclusion: sample data does not reach `$100000-$1FFFFF` at any point
  from reset through 10s past `FILE LOADED`, for this load.** The
  "sample RAM candidate" comment on `mem_map`'s line 308 is not wrong
  about the range being real RAM (524,290 real writes prove it is live,
  addressable RAM, not a dead/misattributed region) — it is that nothing
  in the currently-modeled boot/load path ever uses it as a sample
  destination. Whether a later, unexercised code path (actual playback
  start, a different instrument, or sample recording) would write there
  is **[OPEN]** — not measured this task.

## Del 2 — What Do The Voice Registers Point At?

`es5506_device::write()` (`es5506.cpp:1328-1353`) dispatches on a
page/register protocol: `m_current_page` (set by writing register index 15,
the PAGE register) selects a voice (`page & 0x1f`) and a register group
(`page<0x20` → "low", `<0x40` → "high", else → "test"); each register is
written as 4 accumulated bytes into a **single shared latch**
(`m_write_latch`) that only clears after a completed dispatch — there is
no "start of burst" requirement, and a dispatch fires whenever the 4th
byte lands, using whatever the other three lanes currently hold. High-page
register 0 is `CR` (`&0xffff`, bank = `(CR>>14)&3`), register 1 is `START`
(`&0xfffff800`), register 2 is `END` (`&0xffffff80`), register 3 is
`ACCUM` (unmasked). `get_accum_mask(21, 11)` in `device_start()` computes
`m_address_acc_shift = ADDRESS_FRAC_BIT(11) - address_frac(11) = 0` for
ES5506 specifically — the shift is a no-op, so the raw masked register
value **is** the internal accumulator value, and the integer word address
`read_sample()` actually indexes is `raw >> 11` (11 fraction bits).

### Method note: a decoder bug found and fixed the same way as Del 1 of the previous task

A first version of the Lua decoder required byte lane 0 to "start" a fresh
4-byte accumulation, mirroring a plausible but wrong mental model. It
decoded only 194 of an expected ~3,000+ register completions (2 PAGE
writes total, 1 voice touched). A raw offset/data/mask diagnostic
(`docs/asr10/lua/archive/es5506-offset-diagnostic.lua`) showed firmware
repeatedly writing *only* the last byte lane of the PAGE register
(`$FC207E`, lane 3) with varying values, never re-sending lanes 0-2 — which
is exactly what the real device supports (mask discards the unused upper
bits, so stale lanes are harmless) but which the "must start at lane 0"
gate silently dropped every time. Fixed by mirroring `m_write_latch`'s real
semantics exactly: a persistent per-lane accumulator, dispatch on lane 3
regardless of how the other lanes got there. Corrected result: 672 PAGE
writes, 2,292 voice-field writes, all 32 voices touched.

### Measured voice state (end of a full sweep, sourced field values, not display-string display)

```text
SRVR_VOICE n=0  cr=0308 bank=0 start=00000000 start_int=00000000 end=10000000 end_int=00020000 accum=10000000 accum_int=00020000
SRVR_VOICE n=1  cr=4300 bank=1 start=DC4BB800 start_int=001B8977 end=DC4BC800 end_int=001B8979 accum=DC4BC000 accum_int=001B8978
SRVR_VOICE n=2..31: identical to n=1 (cr=4300 bank=1 start_int=001B8977 end_int=001B8979 accum_int=001B8978)
```

- **[Verified]** All 32 voices get `CR`/`START`/`END`/`ACCUM` written in a
  periodic sweep (~100ms cadence, e.g. voice 1's cycle repeats at
  `t=0.0009`, `0.0027`, `15.044`, `15.145`, `16.273`, `21.799` — a regular
  tick, not load-triggered). The **last** completed register write in the
  whole ~32-second run is at `t=21.802` (right at `FILE LOADED`,
  `t=21.79`); **zero** further PAGE/voice-register completions occur in
  the following 10-second idle window, even though raw ES5506 bus traffic
  (reads and partial/incomplete writes) continues per the previous task's
  measurement. This refines that earlier finding: the *committed*
  per-voice configuration is set once, in a sweep that ends around load
  completion, not continuously reprogrammed — the continuous traffic
  observed earlier is real but does not keep changing voice state.
- **[Verified]** Voice 0 is distinct: `bank=0`, `CR=0x0308`. `END`/`ACCUM`
  are set (`0x10000000`, integer address `0x00020000`), but `START` stays
  `0x00000000` throughout the observed window — never written a nonzero
  value.
- **[Verified]** Voices 1 through 31 — **all 31 of them** — share the
  exact same final `CR=0x4300` (`bank=(0x4300>>14)&3=1`), and the exact
  same `START`/`END`/`ACCUM` integer addresses
  (`0x1B8977`/`0x1B8979`/`0x1B8978` — a 2-word span). This is consistent
  with a shared "idle/silent voice" default pointer, not per-voice
  instrument data (a real multi-sample drum/synth patch would not need 31
  identical voices).
- **Answering the task's direct question — raw values, not interpretation:**
  voice 0 uses bank 0; voices 1-31 uniformly use **bank 1**. Neither
  matches the CPU's `$100000-$1FFFFF` numbering directly (that comparison
  does not apply — see Del 3: bank space and CPU space are different
  address spaces with different units, word- vs byte-addressed). What
  *does* matter structurally: **bank 1 is `es5506_unpopulated_wavetable_map`
  — `.noprw()`, completely unmapped** in this driver's machine config
  (`asr10_boot.cpp:904`). 31 of the 32 voices point at a bank with no
  memory behind it at all, not even empty `.ram()`.
- **[OPEN]** Whether these specific pointer values (voice 0's zero START;
  voices 1-31's identical bank-1 pointer) are firmware's genuine intended
  state or a downstream symptom of Del 3's structural gap — e.g., if
  firmware normally derives real per-voice sample pointers from data that
  was supposed to arrive via a CPU-RAM-to-ES5506-RAM bridge that doesn't
  exist in this driver, these values could be firmware's own safe/silent
  default rather than a real target. Not resolved this task; flagged so a
  future fix doesn't mistake "voices point at bank 1" for a bug to patch
  directly without first fixing Del 3's missing bridge.

## Del 3 — What Does `es5506_device` Actually Require?

Read `src/devices/sound/es5506.h` and `.cpp` in full for the relevant
sections; not modified, not committed (excluded from this and all ASR-10
commits per standing project rule).

### Sample memory: a private, per-device address space, not a shared region

`es5506_device` owns 4 independent address spaces (`memory_space_config()`,
`es5506.h:337-345`: `m_bank0_config`..`m_bank3_config`, one per 2-bit bank
selector in `CR`). `read_sample()` (`es5506.h:196`) reads exclusively
through `m_cache[get_bank(control)].read_word(addr)` — **never** through
the CPU's own address space. `device_start()` (`es5506.cpp:245-255`) has a
ROM-region fallback (`if (m_region0 && !has_configured_map(0))
space(0).install_rom(...)`) used when a driver calls `set_region0()`
instead of `set_addrmap()` — **not used here**: `asr10_boot.cpp` never
calls `set_region0..3()` (confirmed by grep, zero matches), so this path
is entirely inactive. The only backing for any bank comes from
`asr10_boot_state`'s own `set_addrmap()` calls
(`asr10_boot.cpp:901-905`):

```cpp
es5506_host.set_addrmap(0, &asr10_boot_state::es5506_wavetable_map);          // .ram(), $000000-$1FFFFF, device's OWN space
es5506_host.set_addrmap(1, &asr10_boot_state::es5506_unpopulated_wavetable_map); // .noprw()
es5506_host.set_addrmap(2, &asr10_boot_state::es5506_unpopulated_wavetable_map); // .noprw()
es5506_host.set_addrmap(3, &asr10_boot_state::es5506_unpopulated_wavetable_map); // .noprw()
```

- **[Verified]** Bank 0's `.ram()` block is a **separate, private
  allocation** from the CPU's own `$100000-$1FFFFF` (`mem_map` line 308,
  a different `address_map` function entirely, no `.share()` tag
  connecting them). Nothing in this driver ever writes to bank 0's
  backing store — it stays all-zero for the whole run (consistent with
  Del 1's finding that nothing feeds it).
- **[Verified]** Units also differ, not just ownership: the CPU's
  `$100000-$1FFFFF` is a byte-addressed 1MB range;
  `memory_access<21, 1, -1, ENDIANNESS_BIG>::cache` (`es5506.h:206`) is a
  **word-addressed**, 21-bit-address (2M-word / 4MB) space. A future fix
  that tries to alias these two ranges by address number alone would be
  wrong without also reconciling word- vs byte-addressing and the range
  size mismatch.
- **[Verified]** No indirect host-register path exists to poke bytes into
  bank memory either: the "test" page (`reg_write_test`, page ≥0x40) is
  per-channel diagnostic output taps (`"Channel 0 left test write"`, etc.,
  `es5506.cpp:1266-1267`), not a memory-access port. The only two ways
  sample data could reach a bank's backing store are (a) a real,
  board-proven shared-memory wiring (`.share()` between CPU RAM and a
  bank's `.ram()`), if real ASR-10 hardware genuinely has one physical RAM
  chip on both busses, or (b) a real DMA/bus-arbitration path this driver
  does not yet model. Which of the two is correct is **[OPEN]** —
  requires real hardware/schematic evidence, not available locally this
  task.

### Output routing, channels, clock: compared against family precedent

`grep -c "SPEAKER\|add_route\|set_region\|set_channels"` on
`asr10_boot.cpp`: **zero matches**, all four.

- **[Verified]** No `SPEAKER` device, no `add_route()` call for
  `es5506_host` anywhere in `asr10_boot.cpp` — even with correct sample
  data, there is currently no path from the device's output stream to any
  audible sink.
- **[Verified]** No `set_channels()` call — `es5506_device::device_start()`
  (`es5506.cpp:238-239`) falls back to `channels = 1` (mono) when
  `m_channels` is outside `1..6`, i.e. unset.
- **Family precedent, read for comparison, not modified:**
  - `esq5505.cpp` (ES5505, same `es550x_device` base class): `set_region0
    ("waverom")`/`set_region1("waverom2")` (real ROM data — fits ROM-resident
    factory sounds, not ASR-10's floppy-loaded RAM sampler model),
    `set_channels(4)`, and 8 `add_route()` calls per chip through an
    intermediate `m_pump` filter device into `SPEAKER(config, "speaker", 2)`.
  - `esqkt.cpp` (ES5506 — **the same chip** as `asr10_boot.cpp`, KT-76/88):
    same pattern, `set_region0..3()` (all four banks populated with real
    ROM), `set_channels(4)`, 8 `add_route()` calls per chip, `SPEAKER`.
    **`ES5506(config, "ensoniq1", 16_MHz_XTAL)` — the exact same 16MHz
    clock `asr10_boot.cpp` guesses.** This is real, cited family
    precedent for the same chip, not a bare guess: **[Likely, supported
    by family precedent]**, upgraded from the code comment's current
    "no ASR-10-specific clock citation exists" framing — still not
    ASR-10-schematic-proven, but not baseless either.
  - Neither precedent driver uses a `.ram()`-backed bank the way
    `asr10_boot.cpp` does — both are ROM-sample instruments. ASR-10's
    RAM-based approach (floppy-loaded samples, not factory ROM) is the
    architecturally correct shape for *this* instrument's design; the gap
    is not the map type, it is the missing write path into it (Del 3
    above) plus the missing output wiring (this section).
- **Minimal requirement to produce any audio at all, once the sample-data
  bridge is resolved:** a `SPEAKER` device, `add_route()` from
  `es5506_host` to it, and `set_channels()` matching the intended output
  count. The `m_pump`-style intermediate filter both precedents use is an
  enhancement (period-accurate analog-filter modeling), not a hard
  requirement — a direct `add_route()` to `SPEAKER` is sufficient to be
  minimally correct.

## Del 4 — Remediation Order (Sketch Only — Not Built)

1. **Determine the real sample-memory topology** (highest leverage,
   currently blocking everything else). Is ES5506 bank RAM genuinely the
   same physical chip as CPU-visible low RAM on real ASR-10 hardware (a
   `.share()` fix, simple), or a separately DMA'd/bus-arbitrated block (a
   real DMA model, comparable in shape to the MC68302 IDMA work already
   done for the floppy path)? This determines whether steps 2-3 below are
   even meaningful to attempt yet — until sample data can reach a bank
   ES5506 actually reads from, any output wiring produces only silence or
   noise from empty RAM.
2. **Re-examine the bank-1 voice assignment** once step 1 is resolved —
   don't patch "voices point at bank 1" in isolation; confirm first
   whether that's firmware's real target or a downstream symptom of the
   missing bridge (Del 2's open item).
3. **Add `SPEAKER`/`add_route`/`set_channels`** for `es5506_host` — low
   risk, well-precedented (`esqkt.cpp`'s exact idiom, same chip), but
   only useful after 1-2 land; wiring output for a chip reading zeroed,
   disconnected RAM produces nothing worth verifying.
4. **Clock**: keep `16'000'000` — now supported by `esqkt.cpp` family
   precedent (same chip, same clock), not just a placeholder; revisit only
   if ASR-10-specific schematic/firmware-timing evidence surfaces.
5. **Keyboard/note input modeling** (separate track, not sound-path
   critical): `asr10panel_device` has no piano-keyboard ioport.
   `esqpanel_device` (`esqpanel.h:36-39`, the base class) already declares
   `key_down(u8 key, u8 velocity)`, `key_pressure(u8 key, u8 pressure)`,
   `key_up(u8 key)` virtuals — other Ensoniq panel subclasses
   (`esqpanel2x40_vfx_device` at minimum) already wire a `key_change`
   `INPUT_CHANGED_MEMBER` and `required_ioport`s for this. For
   `asr10panel_device`, this is realistically: a new `PORT_START` block
   (likely one row per octave or a scan-matrix pair, matching real ASR-10
   keybed wiring — not yet looked up), a `key_change` handler analogous to
   `button_change`, and calling the inherited `key_down()`/`key_up()`
   which the base class presumably already threads somewhere sensible (not
   traced this task). Sketched only, per instruction — do this *after* the
   sound path, since a key press is only useful once it can produce sound.

## Verification

- `docs/asr10/regression-test.sh`: 6/6, unaffected (no C++ changed this
  task; confirmed both before and after).
- No `mem_map` change, despite Del 1 pointing at a real-but-unused
  `$100000-$1FFFFF` region — reported, not touched, per instruction.
- `src/devices/sound/es5506.h`/`.cpp` read only, not modified, not staged,
  not committed.
- No `-log`.
- `git diff --check`: clean.

## Line Count

- `docs/asr10/lua/archive/sample-ram-and-voice-registers-probe.lua`:
  written, run, archived — Del 1 + Del 2's measurement instrument.
- `docs/asr10/lua/archive/es5506-offset-diagnostic.lua`: written, run,
  archived — the throwaway diagnostic that found and fixed the decoder's
  offset-convention bug; kept for traceability per this project's existing
  precedent (`idma_register_probe.lua` → `idma_register_probe2.lua`).
- No C++ changed. No `mem_map` change.
