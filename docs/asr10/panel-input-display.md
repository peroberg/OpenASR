
# panel-input-display.md

# ASR-10 panel, display and input findings

## Candidate window

Current candidate panel/frontpanel/DUART window:

```text
$FC4800-$FC481F
```

This is separate from current MC68302 internal candidate window:

```text
$FC6800-$FC68FF
```

Interpretation:

```text
Display and button input likely go through an external panel/frontpanel/DUART/glue path, not directly through MC68302 internal SCC in the current evidence.
```

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
```

Observed panel logs:

```text
ASR10PANEL text="   ENSONIQ  ASR-10    "
ASR10PANEL text="  PLEASE INSERT DISK  "
```

Conclusion:

```text
The ROM sends display text as ASCII-compatible bytes to $FC4817.
```

Caution:

```text
This is a log sniffer, not yet a real display device.
Control bytes, cursor movement, clear display, row selection and handshaking may exist and are currently ignored.
```

## Input/event gate

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
```

## Likely panel model

Possible model:

```text
frontpanel buttons/display
  -> 80C52 or panel MCU / DUART / glue
      -> $FC4800-$FC481F
          -> main CPU reads status/events and writes display bytes
```

Known board clue:

```text
ENS5702000102 + 80C52 may be frontpanel/keyboard/display logic.
```

## Future minimal MAME model

Start behavioral:

```text
- display buffer accepts bytes written to $FC4817
- status register reports ready/event bits
- input port events create queued panel event
- ROM reads event status and event code
```

Do not attempt exact 80C52 emulation initially unless required.

## Next trace task

After passing the bit-4 gate, trace all reads/writes in:

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
```

Goal:

```text
identify event available bit
identify actual event/button code register
identify acknowledge behavior
separate display TX, status, RX/input paths
```


---

