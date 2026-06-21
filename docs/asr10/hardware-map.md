# hardware-map.md

# ASR-10 hardware map

## Known or strongly suspected hardware

### Main CPU

Service/manual/board information indicates:

```text
MC68302 MCU
```

Current harness uses:

```cpp
M68000(config, m_maincpu, XTAL(16'000'000));
```

This is a 68000-compatible stand-in.

Implication:

```text
The current harness can execute ordinary 68k ROM code, but does not yet model all MC68302-specific modules.
```

MC68302-specific features likely needed later:

```text
- BAR/SCR remap/control
- chip selects
- interrupt controller
- timers
- ports
- SCC/serial channels
- DPRAM/parameter RAM
```

### FDC

Floppy controller:

```text
NEC uPD72069
```

Current candidate address window:

```text
$FC4000-$FC4003
```

Observed FDC commands:

```text
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

### SCSI

Likely SCSI controller:

```text
AM33C93A-16JC / WD33C93A-compatible
```

Current candidate address window:

```text
$FC5000-$FC501F
```

SCSI should be treated separately from FDC.

### Audio

Likely family audio path:

```text
ES5506 / OTIS -> pump -> ES5510 / ESP -> output
```

VFX/TS/SD MAME code is likely valuable reference for:

```text
- ES5506 setup
- ES5510 setup
- pump/routing
- host register patterns
- effect parameter writes
```

ASR-specific unknowns:

```text
- actual ES5506 address window
- actual ES5510 address window
- sample RAM mapping
- glue/chipselect behavior
- external delay/work RAM for ESP if applicable
```

### Panel/frontpanel

Evidence points to a panel/frontpanel/DUART-like window:

```text
$FC4800-$FC481F
```

Known candidates:

```text
$FC4817 = display/panel TX data candidate
$FC4809 bit 4 = input/event/status gate candidate
```

Possible hardware involvement:

```text
ENS5702000102 + 80C52 frontpanel/keyboard/display controller
```

Interpretation:

```text
Likely external panel/frontpanel controller or glue, not simply MC68302 internal UART.
```

### Super-GLU / ES5701 class

Reported/claimed functions for ES5701/Super-GLU-class ASIC:

```text
- 68000/68302 to ESP interface
- 68000/68302 to OTIS interface
- OTIS to static/sample memory interface
- clock generation
- glue/chipselect/waitstate/bus arbitration behavior
```

Do not try to emulate as a complete chip immediately. Model effects as discovered:

```text
- address decode
- sample RAM visibility
- OTIS/ESP host paths
- status bits
- port/bank/remap behavior
```


---
