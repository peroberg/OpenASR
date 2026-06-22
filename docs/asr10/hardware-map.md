# ASR-10 hardware map

This file tracks known, strongly suspected, and currently hypothesized ASR-10 hardware blocks.

Do not treat hypotheses as confirmed hardware behavior. The current `asr10booth` target is still an instrumented research harness, not a clean final production driver.

## Current hardware-side blocker

The emulator reaches:

```text id="uzwgdm"
ENSONIQ ASR-10
LOADING SYSTEM
```

Then loaded runtime code returns to dispatcher idle around:

```text id="s94gzs"
f87f96 / f87f9a / f87fca
```

This is not currently treated as a crash. It is a firmware dispatcher queue scan / idle state.

The current blocker is probably not raw panel input, FDC media format, or simple interrupt vectoring.

Current best hardware-side suspects:

```text id="9ks6xd"
- MC68302 timer/service completion
- dispatcher queue re-arm
- FC6884/FC6894 completion behavior
- lowmem service state around $0d06/$0e82
- missing event payload after accepted 0x2400 service sequence
```

## Known or strongly suspected hardware

### Main CPU

Service/manual/board information indicates:

```text id="yplczl"
MC68302 MCU
```

Current harness uses:

```cpp id="11v4kv"
M68000(config, m_maincpu, XTAL(16'000'000));
```

This is a 68000-compatible stand-in.

Implication:

```text id="ij07x5"
The current harness can execute ordinary 68k ROM/runtime code, but does not yet model all MC68302-specific modules.
```

MC68302-specific features likely needed later:

```text id="841gbe"
- BAR/SCR remap/control
- chip selects
- interrupt controller
- timers
- ports
- SCC/serial channels
- DPRAM/parameter RAM
```

Do not immediately implement a full MC68302. Current work should continue reconstructing the minimum control-plane behavior required by the ASR-10 firmware.

### MC68302 / FC68xx current findings

Current candidate internal/register window:

```text id="7o3o9d"
$FC6800-$FC68FF  MC68302 internal / board-control candidate
```

Important current-phase registers:

```text id="h0e4e1"
$FC6814  pending/status candidate
$FC6816  service/in-service candidate
$FC6818  control/ack/EOI-ish candidate
$FC6884  timer/control/reload candidate
$FC6894  timer/control/reload candidate
```

Current accepted-looking service source:

```text id="z1maen"
0x2400
```

Observed lifecycle:

```text id="3cb36h"
FC6814 000b -> 240b   source/pending injected
IACK vector 0x4e/0x4f
FC6818 = 4000 or 8000 from vector handler
FC6814 240b -> 000b   clears naturally
runtime 00bf1a sets FC6816 c080 -> e480
runtime 00bf22 sets $0d06
optional gated experiment clears FC6816 e480 -> c080
dispatcher still returns to f87f9a idle
```

Vector findings:

```text id="yu01rq"
0x4e -> f88f06 -> writes FC6818=4000
0x4f -> f88f22 -> writes FC6818=8000
```

Both vectors are accepted-looking and converge. The current blocker is not simply choosing between `0x4e` and `0x4f`.

Wrong/deprioritized vectors:

```text id="42qw1o"
autovectors 0x19..0x1f -> ERROR 139 unused vector
0x40 -> ERROR 139 unused vector
0x46 -> ERROR 129 odd address error
```

Timer/control candidates:

```text id="0s50lv"
FC6850=003b
FC6852=3f01
FC6884=703b
FC6894=703b
```

Hypothesis:

```text id="w84yl0"
0x3b may be timer/count/period-related.
0x703b may be mode/control plus count/reload.
```

Open question:

```text id="urgjby"
Should FC6884/FC6894 produce a later timer/completion event that re-arms the firmware dispatcher queue?
```

### FDC

Floppy controller:

```text id="h247go"
NEC uPD72069
```

Current candidate address window:

```text id="qdt4l4"
$FC4000-$FC4003
```

Observed FDC commands:

```text id="nmjwtc"
36
0B
4F
1E
0E
88
F3
03
07
08
46
```

FDC/media questions are still valid, but FDC is not the current immediate blocker once the emulator reaches `LOADING SYSTEM` and idles in the dispatcher.

Current caution:

```text id="q7sfwx"
Do not fake new FDC behavior to solve the current f87f9a dispatcher idle unless logs show a post-service FDC path is actually being attempted.
```

### SCSI

Likely SCSI controller:

```text id="gzi1rt"
AM33C93A-16JC / WD33C93A-compatible
```

Current candidate address window:

```text id="r0oy7s"
$FC5000-$FC501F
```

SCSI should be treated separately from FDC.

Open questions:

```text id="vfxvz4"
- Confirm exact SCSI controller.
- Confirm exact ASR SCSI register window.
- Determine boot priority and probe behavior.
- Determine minimum SCSI behavior needed for OS/runtime to continue.
```

### Audio

Likely family audio path:

```text id="19puuo"
ES5506 / OTIS -> pump -> ES5510 / ESP -> output
```

VFX/TS/SD MAME code is likely valuable reference for:

```text id="oz4qy7"
- ES5506 setup
- ES5510 setup
- pump/routing
- host register patterns
- effect parameter writes
```

ASR-specific unknowns:

```text id="c0q1pt"
- actual ES5506 address window
- actual ES5510 address window
- sample RAM mapping
- glue/chipselect behavior
- external delay/work RAM for ESP if applicable
```

A/D and PCM caution:

```text id="wsxtw2"
Audio sample data probably does not flow through the MC68302 as ordinary CPU-readable input.
More likely path:
analog input -> ADC/codec -> serial audio clock/data -> OTIS/ESP/audio ASIC path -> sample RAM / DSP path
```

MC68302 may still observe or control audio-related clocks/status. This is relevant because of:

```text id="vgh2pr"
ERROR 009 No LRCLK input to 68302
```

But do not assume PCM sample data itself is transported through MC68302 registers.

### Panel/frontpanel

Evidence points to a panel/frontpanel/DUART-like window:

```text id="yk162b"
$FC4800-$FC481F
```

Known candidates:

```text id="7hds5b"
$FC4817 = display/panel TX data candidate
$FC4809 bit 4 = input/event/status gate candidate
```

Possible hardware involvement:

```text id="e3hmyn"
ENS5702000102 + 80C52 frontpanel/keyboard/display controller
```

Interpretation:

```text id="ngb6f1"
Likely external panel/frontpanel controller or glue, not simply MC68302 internal UART.
```

Current caution:

```text id="f6t3y0"
Panel input is probably not the immediate blocker while the machine is still in LOADING SYSTEM and dispatcher idle. Do not fake LOAD/CMD/EDIT/instrument-select input until logs show the runtime is actually waiting for user events.
```

### Super-GLU / ES5701 class

Reported/claimed functions for ES5701/Super-GLU-class ASIC:

```text id="bqshj8"
- 68000/68302 to ESP interface
- 68000/68302 to OTIS interface
- OTIS to static/sample memory interface
- clock generation
- glue/chipselect/waitstate/bus arbitration behavior
```

Do not try to emulate as a complete chip immediately. Model effects as discovered:

```text id="pc82vu"
- address decode
- sample RAM visibility
- OTIS/ESP host paths
- status bits
- port/bank/remap behavior
- interrupt/completion behavior
```

Possible relation to current blocker:

```text id="l393bq"
If FC6884/FC6894 or the 0x2400 service source are not pure MC68302 internals, they may be board-glue or Super-GLU-adjacent completion/timer/status behavior.
```

## Current control-plane model

The current boot/runtime path suggests the following control-plane chain:

```text id="2bsw73"
MC68302/board service source 0x2400
-> FC6814 pending bit
-> M68K IACK
-> vector 0x4e or 0x4f
-> FC6818 handler write
-> FC6814 pending clear
-> runtime service setter at 00bf1a
-> FC6816 service/in-service bit set
-> $0d06 service flag set
-> dispatcher returns to f87f9a idle
```

What is still missing:

```text id="18yn8k"
A later event, queue mutation, timer tick, or hardware completion signal that causes the dispatcher to leave idle and continue beyond LOADING SYSTEM.
```

## Current one-line hardware takeaway

The current hardware problem is no longer simply "what chip exists where?". The immediate problem is the missing control-plane side effect after the accepted-looking MC68302/FC68xx `0x2400` service sequence: FC6814 clears, FC6816 is set at runtime `00bf1a`, `$0d06` is set, FC6816 can be experimentally cleared back to `c080`, but the firmware dispatcher still idles at `f87f9a` with no panel or FDC progress.
