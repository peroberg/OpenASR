# Instrument Selection Was The Missing Step — Aborted Per Task Branch

Per the task's own instruction: voice activity was produced, so this
task stops here and reports. Del 2/Del 3 (the everyday-first
"TUNING KEYBOARD" and TRAP #D fan-out questions) do not apply.

## Del 1 — Is The Instrument Even Selected?

The ASR-10 manual (`docs/asr10/sources/ASR10_manual.pdf`) states
explicitly: *"it won't make any sound until you LOAD an instrument
into its internal memory and then select that instrument by pressing
its Instrument•Sequence Track button."* Loading and selecting are two
separate operations. Eight numbered Instrument•Sequence Track buttons
exist; red LED = loaded, yellow LED = selected; *"If none of the
yellow LEDs are lit, ... playing the keyboard won't make any sound."*
This project's entire prior investigation (turns 1-4 of this series)
never pressed a select button after loading — every "silent" result
was measured against an instrument that was loaded but never selected.

### Finding the select button

`docs/asr10/lua/archive/find-instrument-select.lua` swept all 64
panel buttons after `FILE LOADED`, watching lowmem `$330`/`$332` — the
exact addresses `keyboard-and-sample-bridge-4.md`'s `$FFB6C4` trace
found being read as an instrument-slot pointer/bitmask — and the
display text:

```text
FIS_BASELINE 330=0000 332=00 display="FILE L0ADED           "
FIS_BUTTON btn=02(2) port=:panel:buttons_0 330=1098 332=01 changed=true display="JM DIGI 5YN  ?0LUME?99"
FIS_FOUND btn=02(2) port=:panel:buttons_0
```

**[Verified]** `BTN_02` (`":panel:buttons_0"`, bit 2), pressed from the
idle `FILE LOADED` screen, is the Instrument #1 select button: `$330`
went `$0000 -> $1098` (a pointer) and `$332` went `$00 -> $01` (bit 0
set), exactly matching `$FFB6C4`'s own reads
(`keyboard-and-sample-bridge-4.md`'s Del 2). The display changed to
`JM DIGI 5YN  ?0LUME?99` — the loaded instrument's name plus what
segment-decode noise renders as `VOLUME 99` — an instrument-select
screen, not a load screen. Note: this is the *same* button code
(`BTN_02`) used as the third press in this project's own load sequence
(`BTN_0A`/`BTN_23`/`BTN_02`) — the front panel is context-sensitive;
within the file-load dialog `BTN_02` confirms a menu choice, and from
the idle screen the identical physical button selects Instrument
slot 1. Both uses were already present in every prior test in this
series; only the second (post-load, idle-context) press was new.

### Playing with the instrument selected

`docs/asr10/lua/archive/select-and-play.lua`: after `FILE LOADED`,
pressed `BTN_02` again (select), then played a note two ways — a real
panel key press and a real MIDI note-on
(`docs/asr10/lua/fixtures/noteon.mid`, the same fixture and injection
technique as `keyboard-and-sample-bridge-4.md`'s Del 1) — reusing the
ES5506 voice-register decoder verified in
`keyboard-and-sample-bridge.md`.

```text
SAP_AFTER_SELECT 330=1098 332=01 display="JM DIGI 5YN  ?0LUME?99"

SAP_KEY_DELTAS voice_writes=12 samram=0
SAP_KEY_VOICE_WRITE ... voice=1 field=END   value=DC4BC800 bank=1
SAP_KEY_VOICE_WRITE ... voice=1 field=START value=DC4BB800 bank=1
SAP_KEY_VOICE_WRITE ... voice=1 field=ACCUM value=DC4BC000 bank=1
SAP_KEY_VOICE_WRITE ... voice=1 field=CR    value=00004300 bank=1
... (12 writes total: CR/START/END/ACCUM, each written twice)

SAP_MIDI_DELTAS voice_writes=12 samram=0
SAP_MIDI_VOICE_WRITE ... voice=2 field=END   value=DC4BC800 bank=1
SAP_MIDI_VOICE_WRITE ... voice=2 field=START value=DC4BB800 bank=1
... (identical shape, voice 2 instead of voice 1)
```

**Voice activity ⇒ abort per the task's own branch.** With the
instrument selected:

- **[Verified]** A panel key press allocates and programs **voice 1**
  with real `CR`/`START`/`END`/`ACCUM` values. A MIDI note-on
  (independently, same session) allocates **voice 2** with the
  identical value pattern — genuine, distinct polyphonic voice
  allocation, not a fluke or a shared/stale register echo.
- **[Verified]** This is categorically different from every previous
  measurement in this series, which recorded zero voice-register
  writes for the same stimuli. The three-turn "declining condition"
  descent (`keyboard-and-sample-bridge-3.md`,
  `-4.md`) was tracing correctly-behaving code that legitimately does
  nothing when no instrument is selected — not a bug.

### Why it is still silent

`-wavwrite` capture (`select_and_play.wav`) over this exact session:
complete silence, `peak=0`, despite the real voice writes above.
**[Verified]** Both voice 1 and voice 2's `CR` value is `$4300`;
`(CR>>14)&3 = 1` — **bank 1**. `es5506_wavetable_map`
(`asr10_boot.cpp:416-439`) only populates bank 0 (shared with CPU RAM
via `.share(":asr10_sample_ram")`); banks 1-3 use
`es5506_unpopulated_wavetable_map`, i.e. `.noprw()` —
**entirely unmapped**. This was already known structurally
(`sample-ram-and-voice-registers.md`: "voice 0 uses the one populated
bank (0); all 31 others point at bank 1, which is entirely unmapped")
but not previously connected to an actual live note-play event, since
no prior test ever got this far. Voice 0 was never exercised by either
stimulus in this session — the firmware's voice allocator picked
voices 1 and 2, consistent with voice 0 being reserved for something
else (background housekeeping's own `START`/`END`/`ACCUM` traffic,
already documented as continuous from near-reset in
`file-loaded-verification-probe.md`).

## Next lead (not chased further this task, per its own scope)

The remaining gap is narrow and concrete: **why does voice allocation
land in bank 1** (unmapped) **instead of bank 0** (mapped, shared with
CPU RAM)? Two non-exclusive candidates, neither measured yet:

- The firmware's bank selection is itself instrument-data-driven
  (reads a bank field from the loaded instrument/sample descriptor)
  and this disk image's `JM DIGI SYN` genuinely specifies bank 1 on
  real hardware too — in which case the fix is mapping/sharing bank 1
  the same way bank 0 already is.
- Voice 0 is reserved by firmware convention (e.g. for a system/click
  voice) and real hardware's bank-0/bank-1 split differs from what
  this driver currently wires — in which case the fix is on the
  `es5506_wavetable_map` side, not the firmware side.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, unaffected — no C++ or driver
  change this task, Lua probes and documentation only.
- No `mem_map` change, no ES5510 change, `es5506.h` not read.
- No `-log`; every loud signal used Lua `print()`. `-wavwrite` used as
  directed.
- No fork with an open mandate launched this task (one narrowly-scoped
  read-only research fork was used for the manual/button-naming
  lookup that opened this task; its findings — the manual quotes and
  the `filesystem-browser-map.md` cross-reference — were independently
  verified against the manual text and the live `$330`/`$332` sweep
  before being relied on, not taken on trust).
- `git diff --check`: clean.

## Line Count

- `docs/asr10/lua/archive/`: two new scripts this task
  (`find-instrument-select.lua`, `select-and-play.lua`) plus reuse of
  the existing `docs/asr10/lua/fixtures/noteon.mid` fixture — written,
  run, archived. No C++ changed this task.
