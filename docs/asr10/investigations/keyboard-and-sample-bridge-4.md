# The MIDI Gate, And Where The Note Actually Stops

## Del 1 — MIDI Note-On: A Hard Gate

Channel A of the DUART was unconnected until this task. Wired it,
following the `esq5505.cpp` precedent for the same SCN2681 family
(`src/mame/ensoniq/asr10_boot.cpp`'s `asr10_boot()`):

```cpp
m_duart->a_tx_cb().set(m_mdout, FUNC(midi_port_device::write_txd));
auto &mdin(MIDI_PORT(config, "mdin"));
midiin_slot(mdin);
mdin.rxd_handler().set(m_duart, FUNC(scn2681_device::rx_a_w));
midiout_slot(MIDI_PORT(config, "mdout"));
```

This is the one machine-config change this task is scoped to. No
`mem_map` change, no ES5510 change, `es5506.h` not read.

**Injection mechanism**: `MIDI_PORT`'s default `"midiin"` option
(`midiin_port_device`) wraps a `MIDIIN` image device
(`src/devices/imagedev/midiin.h`) that parses a loaded file as a
Standard MIDI File and transmits its events over real 31250-baud
`device_serial_interface` timing into the wired `rxd_handler`. A
headless Lua script can drive this without any OS MIDI hardware by
calling `image:load(path)` on the device (found via
`manager.machine.images`, tag `:mdin:midiin:midiinimg`) — the same
binding used for `-flop`/mounted-image regression tests. Built a
minimal 30-byte format-0 SMF
(`docs/asr10/lua/fixtures/noteon.mid`): one track, delta 0, `$90 $3C
$64` (note-on, channel 1, note 60, velocity 100), then end-of-track.
`image:load()` returns `nil` on success, an error string on failure —
a single value, not `(ok, err)`; got `nil`, confirming clean load.

`docs/asr10/lua/archive/midi-note-on-gate.lua`: waits for `FILE
LOADED` (same load sequence as `file_loaded.lua`), loads the fixture,
and taps RHRA (`$FC4806/07`, register index `$03` in
`duart_panel_asr_candidate_r/w`'s `word=offset&0xf` decode — the same
formula that already placed RHRB at `$FC4816/17` in
`key-press-response-probe.lua`), the ES5506 voice-register decoder
verified in `keyboard-and-sample-bridge.md`, and `$100000-$1FFFFF`.

```text
MNOG_DELTAS rhra=3 voice_writes=0 samram_writes=0 display_changed=false
```

**[Verified]** Three RHRA reads — exactly the three bytes of the
message, no more, no fewer — confirm the byte stream was received and
consumed by firmware, not just latched in the DUART. **[Verified]**
Zero ES5506 voice-register writes, zero `$100000-$1FFFFF` writes, no
display change.

**Result: Silent.** MIDI reaches the receiver and is genuinely
consumed, but produces no audible/measurable effect, matching the
panel path's own signature exactly. Per the task's own branch: **the
panel dispatch chain is exonerated as the cause.** The three-turn
descent through TRAP #9/scheduler/TRAP #6/jump tables
(`keyboard-and-sample-bridge-3.md`) was real, correct tracing — it
just wasn't chasing a panel-specific bug, because there isn't one.

## Del 2 — Where MIDI And Panel Converge, And Where They Stop

`docs/asr10/lua/archive/midi-scheduler-correlation.lua` repeated the
`keyboard-and-sample-bridge-3.md` six-slot-scheduler correlation, but
with the note-on load as the stimulus instead of a key press.

```text
MSC_RHRA_READS count=3   (pc=F889A2 for all three)
MSC_TRAP9_ENTRIES ...  a1=002422 in_slot_range=2 a5=001504
                       a1=002438 in_slot_range=3 a5=0014FC
MSC_TRAP6_ENTRIES count=2
MSC_DISPATCH ... a2=002422 in_slot_range=2
MSC_DISPATCH ... a2=002438 in_slot_range=3
```

**[Verified]** MIDI reception wakes the *identical* two scheduler
slots (2 and 3, same queue nodes `$1504`/`$14FC`) that a key press
woke in the previous task. This is not a coincidence of shared
plumbing — it is the same event, through a different front end.

### Following the byte parser, not the scheduler

Rather than re-descend the already-traced `$740C`/`$FF9650` jump
tables (which the previous task established are generic,
non-note-specific dispatch), followed the *byte parser* forward from
the `$F889A2` read point, since that is what interprets note content
before anything reaches the scheduler.
`docs/asr10/lua/archive/completion-consumer-static-dump.lua` captured
the static code; a scratchpad-only Capstone pipeline (not part of this
repo, same technique as prior tasks) disassembled it:

- `$F88960-$F889A2`: DUART channel-A interrupt entry. Checks
  `SRA` (`$FFFC4803`) for error bits; on a clean receive, falls to
  `$F8899A`, reads RHRA (`$FFFC4807`) into D1.
- `$F889A2 bmi.b $f889aa`: **the byte-class test.** Bit 7 set (a MIDI
  status byte, our `$90`) branches to `$FF877E` (sign-extended
  `$877E.w` — all the `.w`-suffixed addresses below are `$FFxxxx`,
  not `$00xxxx`, confirmed by resolving the actual bytes at each
  address). Bit 7 clear (a data byte, our `$3C`/`$64`) instead jumps
  through a vector kept at `$FF87BC` — classic MIDI-parser state
  machine.
- `$FF877E`: `jmp $F889B2` — classifies channel (`d1&$f`, stored at
  `$FF87C2`) and message-type nibble (`d1&$70`), then computes
  `table[(type>>2)]` from a base at `$FF871E` (verified via live dump:
  index for Note On, `$90&$70=$10`, `>>2=4` → address `$FF8722` →
  table entry `$FFF88AA2`) and stores it into `$FF87BC` — this is the
  vector the *next* byte (the note number) will be dispatched through.
- `$F88AA2` (data-byte handler for Note On): stores the byte into
  `$FF87C4` (note number), calls `$F8892A`, re-arms `$FF87BC` to
  itself (supports MIDI running status), then **on a nonzero note
  value** falls into a small classifier and executes `jmp $8788.w`.
- `$FF8788`: `jmp $FFB43E`.

**[Verified]** `$FFB43E` is the exact address
`panel-completion-consumer-v350.md` (pre-existing project document,
not fork output) already identified as the panel protocol's own
completion consumer. **MIDI note data and panel key/button data
funnel into the identical consumer function**, reached by a
completely different front end (DUART channel A vs. channel B/the
panel device), confirmed by direct code-address match, not inference.

### Into `$FFB43E`: the documented decline branch, live-verified

`panel-completion-consumer-v350.md` had already statically described
`$FFB43E`'s shape (trap #3, `jsr $F87FD2`, trap #4, `jsr $B6C4`,
"conditionally branch through `$B56E`, otherwise `rts` at `$FFB486`")
without identifying the *specific* branch condition. Live disassembly
fills that in:

```asm
ffb46e  jsr     $b6c4.w
ffb472  cmpi.b  #$1,$171.w
ffb478  bne.b   $ffb480          ; $171 != 1 -> proceed
ffb47a  and.b   $cde.w,d3        ; $171 == 1: d3 &= lowmem[$CDE]
ffb47e  beq.b   $ffb486          ; result zero -> DECLINE (rts)
ffb480  suba.l  a1,a1
ffb482  bra.w   $ffb56e          ; proceed
ffb486  rts                      ; <- the decline
```

**This is the specific declining read Del 2 asked for**: `$FFB43E`
silently returns at `$FFB486` if and only if `lowmem[$171]==1` *and*
(`$FFB6C4`'s D3 result `& lowmem[$CDE]`) `==0`. `$FFB6C4` itself:

```asm
ffb6c4  moveq   #0,d3
ffb6ca  move.w  $31c.w,d0
ffb6ce  beq.b   $ffb6e6                  ; lowmem[$31C]==0 -> different path
ffb6d0  movea.w $330.w,a1                ; a1 = lowmem[$330]  (a pointer)
ffb6d4  cmp.b   $3c(a1),d2                ; d2 = note number
ffb6d8  bcs.b   $ffb6e4                   ; note < low bound -> d3 stays 0
ffb6da  cmp.b   $3e(a1),d2
ffb6de  bhi.b   $ffb6e4                   ; note > high bound -> d3 stays 0
ffb6e0  move.b  $332.w,d3                 ; in range -> d3 = lowmem[$332]
ffb6e4  bra.b   $ffb736
ffb6e6  lea.l   $b5d2.w,a2
ffb6ea  bsr.w   $ffb5ac                   ; a different resolution path
ffb6ee  bra.b   $ffb736
```

**[Verified static]** When `lowmem[$31C]!=0`, `$FFB6C4` performs a
**key-range check**: it reads `lowmem[$330]` as a pointer to an
instrument/keygroup descriptor and compares the incoming note number
against a low/high split-point pair at `(that pointer)+$3C` and
`+$3E`. This is the closest thing in the traced chain to "does this
note belong to a playable range" — not a residency flag, but a range
gate that plausibly exists for exactly that purpose (split points,
key-range-per-sample assignment).

### What actually happened for our note (`$3C`=60, velocity `$64`=100)

`docs/asr10/lua/archive/decline-condition-probe.lua` and
`b56e-branch-probe.lua` instrumented every branch above and re-sent
the same note-on:

```text
DCP_LOWMEM_BEFORE 31C=0000 330=0000 CDE=01 171=00
DCP_EVENT B6C4_ENTRY           d2=60
DCP_EVENT RANGE_PATH_TAKEN     d2=60 d3=0
DCP_EVENT 31C_ZERO_PATH_TAKEN
DCP_EVENT B6C4_RETURN          d3=0
DCP_EVENT 171_CHECK            d3=0
DCP_EVENT PROCEED_B56E
```

**[Verified dynamic]** `lowmem[$171]==0` (not 1) at the check point,
so the `$FFB486` decline was **not** the branch taken — `bne.b
$ffb480` fires unconditionally down the "proceed" side regardless of
the key-range result. **[Verified dynamic, reproducible across
re-runs]** Both `$FFB6D0` (the range-check path, `lowmem[$31C]!=0`
branch) and `$FFB6E6` (the `lowmem[$31C]==0` branch) fired in the same
window, which is only possible if `$FFB6C4` is entered more than once
for this one message (plausible: once each for the note-number and
velocity data bytes) with `lowmem[$31C]` in different states between
calls. **[OPEN]** which of the two calls corresponds to which data
byte, and why `$31C` differs between them — not resolved this task,
and not needed for the result below.

Continuing into `$FFB56E` (the "proceed" target):

```text
DB56E_LOWMEM_BEFORE 171=00 3BD=01 163=00
DB56E_EVENT B56E_ENTRY           d1=00008064   (bit15 | velocity)
DB56E_EVENT B56E_BCLR_BIT15      d1=00008064
DB56E_EVENT B56E_171_TEST        d1=00008064
DB56E_EVENT B56E_TRAP4           d1=00000064
DB56E_EVENT B56E_BSR_55A_POST    d1=00000064   (writes 6-byte record via $B55A)
DB56E_EVENT B56E_EXIT_RTS
```

**[Verified dynamic]** For this note, `$FFB56E` did not early-exit
either (`trap #2` didn't set carry, `lowmem[$171]==0` sent it straight
to `trap #4`). A second pass reached the `$B55A` record-write +
`trap #d` "post" path (`panel-completion-consumer-v350.md` already
documented `$B55A`'s six-byte record format). **TRAP #d is a new
primitive, not previously identified**: disassembly
(`completion-consumer-static-dump.lua`) shows it is a deferred-work
queue — if the target record's `+$10` "started" flag is `0`, it links
the item in and **synchronously calls the function pointer stored at
the record's own `+0`**; otherwise it just appends to a linked list
for something else to drain later. The target record used here
(`$14C0`, set up via `movea.w #$14c0,a1` in `$FFB56E`) had `+$10==0`
and `+0==$FFF884FC`, so `$F884FC` ran synchronously in the same
handler.

`$F884FC` (`completion-consumer-static-dump.lua`'s dump,
disassembled): writes `$04` to the DUART's own Channel-A command
register (`$FFFC4805`, register index 2 — `CRA`), stashes A5 and a
continuation function pointer (`$FFF8857A`) into `$FF86F8`/`$FF86FC`,
and returns. **This is generic UART-channel control/continuation
plumbing, not voice- or sample-specific code.**

**Trace conclusion for Del 2**: the note-on was not silently dropped
at a single guard. It was **recognized, range-checked, and posted**
through a real, multi-stage pipeline (`$FFB43E` → `$FFB6C4` key-range
check → `$FFB56E` → `TRAP #4`/`TRAP #D` → `$F884FC`) — and the trace
run out at `$F884FC` landed in shared serial-transport/continuation
machinery, past the point where note- or voice-specific logic would
plausibly still be running. **No ES5506-reaching code was found along
this path**, and no single "the decline" read exists here the way the
task's framing expected — the pipeline does not decline, it completes
and hands off to infrastructure that has nothing to do with voices.

## Del 3 — The Sample-Residency Hypothesis: Not Confirmed As Framed

The hypothesis was: a voice allocator checks wavesample residency,
finds `$100000-$140000` zeroed, and declines silently. **[Disproven
as stated]**: the actual gate found in Del 2 is a **key-range check**
(`(lowmem[$330])+$3C`/`+$3E` against the note number), not a
residency/pointer/length check, and in the live measurement this gate
did not even fire the decline branch — `lowmem[$171]==0` routed
around it entirely. The chain instead reached a generic UART
continuation primitive with no sample-table read anywhere in it.

This does not rule out sample residency as a downstream cause — it
means the **traced path never reaches the code that would check it**.
`lowmem[$330]` read `0000` in this boot/load state (before the note
arrived), which — if that pointer is meant to reference the *current
voice/keygroup descriptor* — would itself be consistent with "no
voice/keygroup has been set up to receive this note yet," a distinct
and arguably prior-order problem to "is the sample resident." That
reading is **[HYPOTHESIS]**, not measured this task.

## Verification

- `docs/asr10/regression-test.sh`: 7/7, before and after the MIDI
  wiring change.
- Machine-config change: DUART channel A wired to `MIDI_PORT`
  in/out (the one permitted change). No `mem_map` change, no ES5510
  change, `es5506.h` not read/modified/committed.
- No `-log`; every loud signal used Lua `print()`.
- No protocol sweep (bytes `$00-$BF` already swept per
  `panel-completion-consumer-v350.md`; this task followed a
  specific, real MIDI/panel-shared code path instead).
- No fork launched this task; nothing from the previously-quarantined
  fork (`keyboard-and-sample-bridge-3.md`'s Del 0) used as evidence.
- Bank 1 stays `[OPEN]`. CMR table stays `[Likely]`.
- `git diff --check`: clean.

## Line Count

- `src/mame/ensoniq/asr10_boot.cpp`: +12 lines (MIDI wiring: one
  `#include`, one member + one ctor-init line, six machine-config
  lines with a comment).
- `docs/asr10/lua/archive/`: five new scripts this task
  (`midi-note-on-gate.lua`, `midi-scheduler-correlation.lua`,
  `completion-consumer-static-dump.lua`, `decline-condition-probe.lua`,
  `b56e-branch-probe.lua`) plus one fixture
  (`docs/asr10/lua/fixtures/noteon.mid`, 30 bytes) — written, run,
  archived. The Capstone disassembler and its venv remain
  scratchpad-only, not part of the repo.
