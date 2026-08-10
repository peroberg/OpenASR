# Runtime and static observations, 2026-08-10

Scope: ASR-10 MAME dynamic-verification round 1 at HEAD
`0917f39c89c` on branch `asr10-architecture-cleanup`.

No emulator source code was changed. `asr10_boot.cpp` `mem_map` was not edited.
Runs did not use MAME `-log`.

## W1C close-out

Evidence level: [Verified].

`src/devices/machine/mc68302.h` states that this step has no interrupt
controller, timer, IDMA or communications processor. In
`src/devices/machine/mc68302.cpp`, `classify_offset()` classifies the interrupt
controller range `$0812-$0819` (`GIMR/IPR/IMR/ISR`) as
`known_unimplemented`. The same function also classifies SCC/SMC parameter RAM,
IDMA, Port A, Timer 1/watchdog, Timer 2, and SCC1-3/SMC/SCP as
`known_unimplemented`.

Conclusion: W1C semantics for IPR/ISR are not implemented at current HEAD. All
internal MC68302 interrupt sources lack a working model. The working interrupt
path is external IRQ6 through `irq6_ack_vector()`, which explains why the panel
path works while SCC, Timer 2 and PB9-PB11 appear inactive.

## E2 high-window read tap

Evidence level: [Verified] for current MAME behavior on this V3.50 boot path.

Command, without `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mess asr10booth -flop1 floppies/asr10booth/V350.img -video none -sound none -nothrottle -seconds_to_run 60 -autoboot_script docs/asr10/lua/asr10_e2_mirror_probe.lua -autoboot_delay 0
```

Lua installed a program read tap over `$FF8000-$FFFFFF`. MAME did not expose an
opcode space to Lua in this run, so instruction-fetch classification was not
available.

Observed summary:

```text
E2_TAP_INSTALLED kind=data range=FF8000-FFFFFF
E2_OPCODE_SPACE missing
E2_SUMMARY total=566229 data_reads=566229 instruction_reads=0 opcode_tap_installed=0 lines_emitted=512 max_lines=512
E2_FIRST kind=data address=FF8D44 pc=F87EC2 low_address=008D44 value=F9
```

First comparisons disproved equality for the tested accesses:

```text
E2_ACCESS seq=1 kind=data address=FF8D44 pc=F87EC2 value=F9 low_address=008D44 low_value=00 match=0
E2_ACCESS seq=2 kind=data address=FF8D46 pc=F87EC2 value=F8 low_address=008D46 low_value=00 match=0
E2_ACCESS seq=22 kind=data address=FFB092 pc=00B024 value=F8 low_address=00B092 low_value=4E match=0
```

Most-read addresses:

```text
E2_HIST rank=1 address=FFD0B0 count=182928
E2_HIST rank=2 address=FF8638 count=44942
E2_HIST rank=3 address=FF863A count=44942
E2_HIST rank=4 address=FF863C count=44942
E2_HIST rank=5 address=FF80FE count=24607
E2_HIST rank=6 address=FF80FC count=24606
```

Conclusion: `$FF8000-$FFFFFF` is accessed during the V3.50 reference boot, but
the sampled high-window data is not the same as corresponding `$00xxxx`
contents in current MAME. The broad `$FFxxxx == $00xxxx` mirror hypothesis is
therefore not valid for this observed MAME execution. Generated call-graph CSVs
were not edited manually.

Coverage: data reads only; opcode-fetch visibility remains [OPEN] because Lua
reported no opcode space.

## V1.61 boot display

Evidence level: [Verified] for current MAME display output, with glyph
ambiguity noted.

Command, without `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mess asr10booth -flop1 floppies/asr10booth/V161.img -video none -sound none -nothrottle -seconds_to_run 90 -autoboot_script docs/asr10/lua/asr10_display_probe.lua -autoboot_delay 0
```

Final raw glyph-decoded line:

```text
DISPLAY_SUMMARY changes=18 final="N0 IN5T 0R BANK FILE5 "
```

The 14-segment glyphs for `O`/`0` and `S`/`5` are ambiguous in this inverse
decoder. Normalized panel text:

```text
NO INST OR BANK FILES
```

This means V1.61 does not land on a `FILE 1 ...` bank entry. It is useful for OS
and effect-file experiments, but not as a direct replacement for the V3.50
file-browser baseline.

## Deterministic file-browse test

Evidence level: [OPEN], blocked without implementation changes.

The requested one-`DOWN` run was not performed. Current HEAD has no ASR-10 input
ports (`INPUT_PORTS_START(asr10_boot)` is empty), and the existing panel harness
injects only panel acknowledgement/status bytes into SCN2681 channel B through
`panel_autorespond_fire()` and `panel_c_queue_rx()`. There is no existing
external control surface found for injecting a user `DOWN` key byte.

Because no actual `DOWN` event can be driven at HEAD without adding a hook or
stub, no call-graph edges were marked as `executed=observed`. The generated
`static/call-graph-edges.csv` was not edited manually.

## PB9/PB10/PB11 static pass

Evidence level: [Verified] for identified ROM code; [OPEN] for physical source
and consumers.

ROM `$F87F0A` performs:

```text
ori.w #$C080,$FC6816.l
```

This unmasks PB11, PB10 and PB9 in IMR. PBDDR `$F097` leaves PB11-PB8 as inputs.
At current HEAD, `read_pbdat()` reads externally supplied levels through
`set_external_input()`, so undriven PB9-PB11 inputs read as zero and no real
activity can be observed by tapping them at runtime.

PB10 handler:

```text
$F88F06  tst.b  $0C3A.w
$F88F0C  move.b #$0C,$0C3A.w
$F88F12  move.b #$01,$0C36.w
$F88F18  move.w #$4000,$FC6818.l
$F88F20  rte
```

PB11 handler:

```text
$F88F22  tst.b  $0C3B.w
$F88F28  move.b #$0C,$0C3B.w
$F88F2E  move.b #$01,$0C37.w
$F88F34  move.w #$8000,$FC6818.l
$F88F3C  rte
```

The structure is [Likely] a shared debounce or periodic-service pattern, but the
producer/consumer chain is not verified.

Search method:

- ROM bytes were interleaved as the 68k sees them and disassembled with
  `unidasm -arch m68000`.
- V1.61 and V3.50 disk images were searched for absolute-short and absolute-long
  byte patterns for `$0C36/$0C37/$0C3A/$0C3B`.
- Direct decrement patterns such as `subq/subi` against `$0C3A.w` and `$0C3B.w`
  were also searched.

Result:

- No absolute-long references to `$00000C36/$00000C37/$00000C3A/$00000C3B` were
  identified in ROM.
- Direct ROM absolute-short references identify the PB10/PB11 handlers above.
- No direct absolute-short `subq/subi` decrementer of `$0C3A` or `$0C3B` was
  identified by this method.
- No direct absolute-short consumer of `$0C36` or `$0C37` was identified by this
  method outside the handler writes.
- PB9 ISR acknowledgement writes with `#$0080` were identified in ROM at
  `$F8CFE0`, `$F8CFF0`, `$F8D076`, and in OS-image code at V350 RAM `$014862`
  / V161 RAM `$014A18` by the same segment-2 mapping method. The surrounding
  code points at audio/ES5506-service paths, but the PB9 source and consumer
  remain [OPEN].

Noll identifierade referenser betyder här: noll identifierade referenser inom
den använda metoden. `(d,An)` och other register-relative forms are not covered
by this absolute-search pass.
