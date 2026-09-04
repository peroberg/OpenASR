# Minimal CP receive engine

Date: 2026-08-23
Machine: `asr10booth`, V3.50
Scope: Phase 3B, post-framing SCC RX byte to firmware-owned IDMA setup

## Question and result

Can a minimal receive engine in `mc68302_device` replace Phase 1's fabricated
descriptor completion and let ASR-10 firmware consume SCC RX normally?

**Yes, for the measured SCC1/full-MRBLR boundary.** A Lua source supplied only
800 deterministic bytes through a post-framing SCC1 ingress. The device, not
the experiment, then:

```text
wrote $F76600-$F7691F
updated BD0 length $0000 -> $0320
cleared E: $D000 -> $5000
advanced its producer position to BD1
set SCCE1 bit 0 and IPR bit 13
asserted level 4 and supplied vector $4D
```

Firmware reached `$00643C`, accepted the descriptor, entered recording state
3, displayed `REC0RDING 272 5EC LEFT`, and programmed IDMA with the same range
as Phase 1:

```text
SAPR = $F76606
DAPR = $02C110
BCR  = $031A
CMR  = $37A1
```

This upgrades **single-descriptor SCC1 CP receive completion** from
`[OPEN capability]` to `[Verified runtime]` at the post-framing byte boundary.
It does not identify or implement the physical RX source.

The current IDMA model still requires an external byte feed. Phase 3B did not
reintroduce Phase 1's separate IDMA helper, so it produced no destination
writes, no vector `$4B` and no `$00AA48` completion. Firmware did reach and
start the IDMA path. This is a separate, already-known downstream limitation,
not a failure of the new descriptor-completion boundary.

## Architecture boundary

### Experiment owns

The retained probe
`../lua/archive/scc-cp-rx-minimal-engine-probe.lua` owns only:

- panel input through the documented RECORD sequence;
- an 800-byte deterministic test vector;
- writes to hidden debugger state `SCC1RX`, which invokes the device's
  post-framing byte ingress;
- read-only taps and counters.

Lua does not write the RX buffer, descriptor, SCCE, IPR, ISR, CPU interrupt
line, IDMA registers or recording state.

The test vector is the Phase 1 vector:

```text
offset & $0F = 0 or 1    -> $00
all other offsets        -> offset & $FF
offset $0040/$0041       -> $7F/$FF
```

The explicit `$7FFF` word at offset `$40` preserves the already-verified
threshold-crossing stimulus.

### `mc68302_device` owns

`scc_rx_byte(channel, data)` is the reusable model boundary. The hidden state
entries are only a Lua transport into that API; no ASR-10 driver bridge or new
MMIO range was added.

The implemented receive slice is channel-parameterized and uses firmware
state rather than ASR-10 buffer addresses:

- accepts bytes only while `SCM.ENR` is set;
- reads current BD status, length and buffer pointer from parameter RAM;
- bounds reception by channel MRBLR;
- writes each accepted byte through the current BD buffer pointer;
- publishes received length;
- at MRBLR, clears E while preserving the remaining status/control bits;
- advances by one eight-byte descriptor or returns to BD0 on W;
- latches SCCE bit 0;
- gates the SCC source through SCCM and IMR into IPR/ISR;
- asserts CPU level 4 and supplies `$4D` for SCC1 or `$4A` for SCC2;
- implements SCCE, IPR and ISR write-one-to-clear behavior required by the
  firmware handler;
- resets the internal producer position for CP reset/ENTER HUNT commands.

The ASR-10 driver change only replaces level-4 autovector delivery with
`mc68302_device::irq4_ack_vector()`. `mem_map()` is unchanged.

## Not implemented

This remains a receive-only, post-framing slice. It does not implement:

- a serial RX pin, FIFO or shift register;
- SCC baud generation, clock recovery or bit timing;
- UART, HDLC, BISYNC or other framing;
- early descriptor completion, idle/break handling or receiver errors;
- transmit behavior;
- physical ADC routing or fabricated input samples in normal execution;
- PB9/PB10/PB11, ES5506 or ES5510 behavior;
- a general MC68302 interrupt controller beyond the SCC1/SCC2 receive causes;
- IDMA memory-source bus mastering or IDMA completion interrupt delivery.

Normal execution has no producer connected to `scc_rx_byte()`, so behavior is
unchanged unless an explicit test source supplies bytes.

## Live observation

Command, with no `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 60 \
  -autoboot_script docs/asr10/lua/archive/scc-cp-rx-minimal-engine-probe.lua
```

### Before byte ingress

```text
display     WAITING...272 5EC LEFT
$0D04       $0002
BD          $FC6400
status      $D000
length      $0000
buffer      $F76600
MRBLR       $0320
SCM1        $703B
SCCE1       $0000
SCCM1       $0500
IPR/IMR/ISR $0000/$E480/$0000
```

### Synchronous result after byte 800

Before firmware resumed:

```text
buffer writes       800
source byte matches 800/800
status              $5000
length              $0320
SCCE1               $0100
IPR                  $2000
ISR                  $0000
```

This snapshot isolates model production from firmware consumption. The CPU PC
was unchanged at `$F87FC8` while Lua supplied the vector.

### Firmware consumption

```text
level-4 IACK/vector $4D       1
$00643C SCC common handler    reached
$0064BA range continuation    reached
$0065CC state-3 producer      reached
$00B478 range-to-IDMA         reached
$00B4C0 IDMA start            reached
final $0D04                   $0003
final display                 REC0RDING 272 5EC LEFT
firmware consumer index       0 -> 1
SCCE1/IPR/ISR after handler   $0000/$0000/$0000
```

The descriptor firmware next exposes at `$FC6408` remained CPU-initialized as
`$D000/$0000`, confirming separation between the CP producer position and the
firmware consumer index.

### Negative downstream observation with witness

```text
level-4 vector $4B       0
recording-destination writes 0
low-RAM live witness     660747 writes
```

The live witness proves the destination tap remained active for the entire
post-ingress interval. IDMA nevertheless held the exact expected programmed
registers, so the first stop is after IDMA start: no modeled memory-source
transfer feeds the existing byte-input IDMA path.

## Phase 1 comparison

| Function | Phase 1 bridge | Phase 3B |
|---|---|---|
| deterministic RX bytes | experiment | experiment |
| buffer fill | experiment wrote memory | `mc68302_device` |
| received length | experiment wrote BD | `mc68302_device` |
| E clear and producer advance | experiment fabricated completion | `mc68302_device` |
| SCCE/IPR/level 4/vector `$4D` | experiment | `mc68302_device` |
| `$00643C` and range selection | firmware | firmware |
| RECORDING and IDMA programming | firmware | firmware |
| IDMA source-byte feed/vector `$4B` | separate experiment helper | intentionally absent |
| destination byte comparison | 793/794 bytes written and matched | 0 writes; source buffer 800/800 matched |

The requested boundary has therefore moved one step backward. The experiment
produces bytes, not CP completion state.

## Status discipline

| Claim | Previous | Observation | New status / falsifier |
|---|---|---|---|
| current model can produce SCC1 RX completion | `[OPEN capability]` | isolated pre-handler snapshot `$5000/$0320`, SCCE `$0100`, IPR `$2000`; then `$4D/$00643C` | `[Verified runtime]` for one full-MRBLR descriptor; falsified by a repeatable identical ingress that fails before `$00643C` |
| firmware accepts model-produced completion | Phase 1 verified only a bridge product | model-only completion reached state 3, `RECORDING` and `$00B4C0` | `[Verified runtime]`; falsified if firmware rejects the device-produced descriptor while accepting the same Phase 1 state |
| SCC2 uses the same receive engine | implemented from the documented channel offsets and vector source | no SCC2 bytefeed run | `[OPEN runtime]`; verify with a firmware mode that arms and consumes SCC2 |
| eight-entry producer advance and W wrap | implemented from E/W and the documented ring layout | only BD0 -> BD1 was observed live | `[OPEN runtime]` beyond first advance; verify all eight completions and post-W return to BD0 |
| physical ADC/serial source feeds ingress | `[OPEN]` | no physical source modeled or measured | unchanged `[OPEN]`; continuity/schematic or pin capture remains required |
| IDMA memory-source transfer completes recording copy | known separate model gap | `$37A1` start but zero destination writes with 660747-write witness | unchanged `[OPEN implementation]`; a native memory-source transfer reaching `$4B/$00AA48` would verify it |

The simplest surviving explanation is that the missing Phase 2 boundary was
the absent receive-engine slice now implemented. No additional firmware or
descriptor field was required for the measured success case.

## Build and regression

The checkout's working build command is:

```sh
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j4
```

`make SUBTARGET=asr10boot` has no target definition in this tree.

`docs/asr10/regression-test.sh` passed after the implementation: 8 test cases
and 10 `PASS` lines (`boot`, `display`, `button`, `button_upper`, `nodisk`,
`file_loaded`, `mc68302_guards`, `note_audio`, `note_audio_wav`, `regression`).

## Line and deletion accounting

Executable source delta relative to HEAD before this task:

```text
mc68302.cpp      +154 / -1
mc68302.h         +20 / -6
asr10_boot.cpp     +1 / -1 (no net harness growth)
```

The Lua observer is 235 lines and contains all task-only driving and
instrumentation. No temporary C++ instrumentation was added, so there is no
C++ deletion item after measurement. The observer is retained under
`lua/archive/` as reproducible provenance. `asr10_boot.cpp` remains 1,078 lines;
the MC68302 device plus SIM implementation is 1,200 lines.
