# ASR-10 panel, display and input findings

This file tracks ASR-10 panel, display, and input findings.

Current important context:

```text
Panel/display findings are valid, but panel input is probably not the immediate current blocker while loaded runtime remains at LOADING SYSTEM and dispatcher idle.
```

Do not fake LOAD/CMD/EDIT/instrument-select input until logs show the runtime is actually waiting for user events.

## Candidate window

Current candidate panel/frontpanel/DUART window:

```text
$FC4800-$FC481F
```

This is separate from current MC68302 internal / board-control candidate window:

```text
$FC6800-$FC68FF
```

Interpretation:

```text
Display and button input likely go through an external panel/frontpanel/DUART/glue path, not directly through MC68302 internal SCC in the current evidence.
```

Known board clue:

```text
ENS5702000102 + 80C52 may be frontpanel/keyboard/display logic.
```

Possible model:

```text
frontpanel buttons/display
  -> 80C52 or panel MCU / DUART / glue
      -> $FC4800-$FC481F
          -> main CPU reads status/events and writes display bytes
```

## Current relevance

The panel/display path has already proven useful because it shows visible boot progress.

Observed panel text includes:

```text
ENSONIQ ASR-10
PLEASE INSERT DISK
LOADING SYSTEM
```

Current blocker after `LOADING SYSTEM`:

```text
loaded runtime returns to dispatcher idle around f87f96 / f87f9a / f87fca
```

This means:

```text
The immediate missing behavior is likely dispatcher queue re-arm, event payload, timer cadence, FC6884/FC6894 completion, or lowmem service state around $0d06/$0e82.
```

Panel input should be revisited when logs show one of:

```text
- runtime reads $FC4800-$FC481F after LOADING SYSTEM while waiting for input
- dispatcher event/callback points into panel/event handling code
- panel status/event bits are polled in the final idle loop
- UI advances to a prompt that clearly expects user action
```

Until then, do not assume more fake key input is the right next step.

## Display output

Known display/text path:

```text
ROM print routine at $F89CB0 writes printable ASCII-like bytes to $FC4817.
```

Harness logic:

```cpp
if (address == 0x00fc4817 && ACCESSING_BITS_0_7)
{
    const u8 character = u8(data);
    panel_text_byte(character, pc);
}
```

Text is reconstructed only when:

```text
PC == $F89CB0
byte is printable ASCII: 0x20..0x7E
write address == $FC4817
```

Observed panel logs:

```text
ASR10PANEL text="   ENSONIQ  ASR-10    "
ASR10PANEL text="  PLEASE INSERT DISK  "
ASR10PANEL text="   LOADING SYSTEM     "
```

Conclusion:

```text
The ROM sends display text as ASCII-compatible bytes to $FC4817.
```

Caution:

```text
This is a log sniffer, not yet a real display device.
Control bytes, cursor movement, clear display, row selection, row addressing, ready/busy status, and handshaking may exist and are currently ignored.
```

## Display device model status

Current state:

```text
display output is sniffed from writes
display state is reconstructed for logs
display hardware protocol is not yet modeled
```

What is not yet known:

```text
- whether $FC4817 is real display TX, panel MCU TX, or glue latch
- whether writes require ready/busy handshaking
- how clear display / cursor / row selection are encoded
- whether panel MCU echoes or acknowledges display bytes
- whether display TX and input RX share related registers
```

Future functional model should start behavioral:

```text
- accept display bytes
- maintain a simple display buffer
- model minimal status/ready bits only when firmware proves they matter
```

Do not attempt exact 80C52/panel-MCU emulation initially unless required.

## Input/event gate

Current note, 2026-08-12: the PC-specific
`ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84` no longer exists in
`asr10_boot.cpp`. A first ASR-10 Disk Ready attempt drove DUART IP0 from
floppy loaded + motor-active state, but that model did not carry V3.50 past
`PLEASE INSERT DISK`. Current code drives DUART IP0 from the uPD72069 index
callback instead.

Known input/status check:

```asm
FB7C84 btst #4,$FFFC4809
```

Observed behavior:

```text
If $FC4809 bit 4 is clear:
  $04EE becomes 00
  ROM writes $049D=05

If $FC4809 bit 4 is experimentally set:
  $04EE becomes 01
  ROM avoids that $049D=05 write
  ROM progresses to later FDC/media path
```

Interpretation:

```text
$FC4809 bit 4 is likely an input-change/event-available/status bit.
```

It is not necessarily the actual button code.

Current/preferred experiment flag:

```cpp
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84
```

Caution:

```text
This experiment only proves what ROM does if this bit is set at FB7C84.
It does not prove that real hardware always returns bit 4 set.
It does not identify actual key data.
```

## Actual button data unknown

Current known:

```text
$FC4809 bit 4 = event/status gate candidate
```

Unknown:

```text
which register contains actual button/event code
which register acknowledges/clears event
whether RX/TX share $FC4817 or use adjacent addresses
how panel MCU encodes keys
whether keys are scancodes, ASCII-like bytes, command bytes, or packets
whether events are edge-triggered, level-triggered, or queued
```

Important distinction:

```text
$FC4809 bit 4 appears to be a status/event-available gate.
It should not be treated as the actual button value.
```

## Likely panel model

Possible model:

```text
frontpanel buttons/display
  -> 80C52 or panel MCU / DUART / glue
      -> status register
      -> data register
      -> acknowledge/clear behavior
      -> main CPU-visible window at $FC4800-$FC481F
```

Likely responsibilities:

```text
- accept display bytes from main CPU
- expose display-ready/status bits
- queue key/button events
- expose event-available status
- return event/button code
- clear or acknowledge events when read or explicitly acknowledged
```

Do not assume the panel model is identical to VFX/TS panel hardware without ASR-10 ROM/runtime evidence.

## Relation to current dispatcher/service blocker

The current runtime blocker is after:

```text
LOADING SYSTEM
```

Current accepted-looking MC68302/FC68xx service sequence:

```text
FC6814 000b -> 240b
IACK vector 0x4e or 0x4f
FC6818 = 4000 or 8000
FC6814 240b -> 000b
runtime 00bf1a sets FC6816 c080 -> e480
runtime 00bf22 sets $0d06
optional gated experiment clears FC6816 e480 -> c080
dispatcher returns to f87f9a idle
```

Latest relevant negative result:

```text
No panel advance beyond LOADING SYSTEM.
No post-clear FDC activity.
No new error.
Final state remains dispatcher idle at f87f9a.
```

Implication:

```text
The next missing behavior is not currently proven to be panel input.
```

The next useful panel-related evidence would be:

```text
- reads from $FC4800-$FC481F after the service sequence
- writes to panel/display control registers after LOADING SYSTEM
- dispatcher slot/callback identified as panel event code
- lowmem state indicating input wait
```

## Future minimal MAME model

Start behavioral:

```text
- display buffer accepts bytes written to $FC4817
- status register reports ready/event bits
- input port events create queued panel event
- ROM/runtime reads event status and event code
- event is cleared/acknowledged according to observed firmware behavior
```

Do not attempt exact 80C52 emulation initially unless required.

Candidate MAME input mapping later:

```text
frontpanel buttons -> MAME input ports -> panel event queue
```

Possible first key set later:

```text
- LOAD
- COMMAND
- EDIT
- ENTER/YES
- CANCEL/NO
- arrow/navigation keys
- soft buttons if present
```

But do not implement or fake these until the runtime is demonstrably waiting for panel events.

## Next trace task

When panel investigation becomes current again, trace all reads/writes in:

```text
$FC4800-$FC481F
```

Log:

```text
pc
address
value
mem_mask
D0-D3
A0-A1
return address
nearby opcodes
current panel text
dispatcher slot/context if applicable
```

Goal:

```text
identify event available bit
identify actual event/button code register
identify acknowledge behavior
separate display TX, status, RX/input paths
identify display-ready or busy semantics
identify whether panel reads occur after LOADING SYSTEM dispatcher idle
```

## Current one-line panel takeaway

The ASR-10 ROM display text path is clearly visible through `$FC4817`, and `$FC4809` bit 4 is a useful input/status gate path-opener, but the current post-`LOADING SYSTEM` blocker is not yet proven to be panel input; focus should remain on dispatcher queue/event payload and MC68302/FC68xx service completion until logs show runtime panel-event polling.
