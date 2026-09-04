# Physical SCC Audio-Input Boundary

Date: 2026-08-23

## Scope

This pass investigates only the physical boundary between the ASR-10 analog
board and digital board. It does not infer board routing from component
datasheets and does not implement SCC, ADC, clock, glue, or interrupt behavior.

The starting firmware result is documented in
`scc-rx-source-and-consumer.md`: a completed sampling SCC range is copied by
MC68302 IDMA to the recording destination. The remaining question is which
physical board signals reach the SCC receiver.

## Result

The board boundary is narrower than previously documented, but the SCC pin is
not yet identified:

```text
analog board J1
        |
        | 34-pin ribbon cable
        | (or through the optional SP-3 SCSI board)
        v
digital board J6
        |
        | exact nets and intervening glue [OPEN]
        v
MC68302 RXD1 / RXD2 and PB3 candidates
```

- **[Verified external manual]** The direct analog/digital connection is a
  34-pin ribbon cable from analog-board `J1` to digital-board `J6`.
- **[Verified external manual]** When the SP-3 SCSI board is installed, it is
  inserted into this boundary: analog `J1 -> SP-3 J4`, then `SP-3 J2 -> digital
  J6`. The separate SP-3 `J3 -> digital J4` cable carries the SCSI-side
  connection and must not be confused with the analog boundary.
- **[Verified physical photo]** The stereo A/D device visible on photographed
  keyboard and rack analog boards is marked `ANALOG DEVICES AD1879JD`.
  Analog Devices identifies that part as an 18-bit stereo audio ADC. This
  identifies the board component; its serial pins are not mapped here.
- **[Verified external manual]** ERROR 009 is `No LRCLK input to 68302`, and the
  manual says that clock comes from the analog board.
- **[Verified firmware]** ASR-10 configures PB3 as a GPIO input and phases the
  SCC receiver-enable sequence against that input.
- **[OPEN]** No located source assigns any of the 34 connector positions to
  LRCLK, bit clock, serial data, ADC output, SCC RXD1/RXD2, or PB3.
- **[OPEN]** No located source or legible photograph shows whether the ADC
  reaches MC68302 directly or through a buffer, PAL, mux, SP-3 isolation stage,
  or other glue on the digital board.

Therefore no MC68302 package pin number, SCC receive channel, or connector pin
can be promoted to `[Verified physical]` in this pass.

## Source inventory

### ASR service manual

The local `docs/asr10/sources/ASR10_service_manual.pdf` was inspected as text
and as rendered pages, not only through search snippets:

- printed page 8: the analog board converts analog audio to digital audio and
  passes it to the digital board;
- printed pages 10-11: the troubleshooting section says to reconnect analog
  and digital boards with a 34-pin cable from digital `J6` to analog `J1`;
- printed page 23: ERROR 009 names missing LRCLK input to the 68302 and says the
  clock comes from the analog board;
- printed pages 41-42 and figures 17-18: board replacement identifies digital
  `J6` and analog `J1`, but the figures are placement drawings, not schematics;
- printed page 50: the SP-3 installation explicitly breaks the direct cable and
  interposes SP-3 `J4/J2` between analog `J1` and digital `J6`.

The manual contains no 34-pin pinout, net names, test points, component-level
analog-board schematic, or digital-board schematic.

### External board photographs

Syntaur's photographs were inspected at their original resolution:

- ASR-10/ASR-88 analog board, replacement part 4153;
- ASR-10 Rack analog board, replacement part 4223;
- ASR-10 digital main board, replacement part 4145.

The two analog-board photographs independently show `AD1879JD`. They also show
the 34-pin board connector and through-hole components that can be reached for
continuity work. The digital-board photograph shows the populated main board,
but it is a top-side overview of a four-layer board. It cannot establish hidden
traces from J6 to the MC68302, and the relevant silkscreen/net labels are not
legible enough to assign pins.

The photographs show component designators but no visible `LRCLK`, `BCLK`,
`SDATA`, `RXD1`, `RXD2`, or test-point labels around the ADC/connector boundary.

### Schematic availability

The current R-Massive Ensoniq document inventory explicitly lists the ASR-10
mainboard and analog-board schematics as unavailable. Web searches for the
board assembly number, connector pair, SCC pins, ADC, and signal names returned
the same service manual, board-sale photographs, and chip-level documents, not
a board schematic or netlist.

A Straylight Engineering report from reverse engineering the SP-3 says that the
board isolates signals to and from the DAC and ADC, including a `256Fs` clock.
Its ASR-10 project report also names serial clock, serial data, and sample clock
as signals observed with an oscilloscope. This is useful evidence that the
interposed 34-pin boundary carries digital audio clocks/data, but it supplies no
connector-pin or MC68302 mapping. It is retained as `[External observation]`,
not elevated to an ASR-10 netlist.

## Requested signal search

| Item | Result at the physical boundary | Status |
|---|---|---|
| SCC receive pin | Firmware has SCC1/SCC2 receive paths. `INPUTDRY LEFT` selects SCC1's continuation, while both receivers are armed. No physical trace identifies RXD1 or RXD2 at J6. | `[OPEN physical]` |
| MC68302 PB3 / LRCLK | PB3 is a firmware-verified GPIO input; the service manual independently names LRCLK input to the 68302 from the analog board. Connector position and package lead are not documented. | function `[Likely physical]`; exact net `[OPEN]` |
| BCLK | No `BCLK` label or pinout was found. The SP-3 evidence names a `256Fs` clock, but equivalence to a particular SCC receive clock is not established. | `[OPEN]` |
| SDATA | No `SDATA` label or connector pin was found. External SP-3 evidence only establishes serial data somewhere across the ADC/DAC boundary. | `[OPEN]` |
| ADC | Photographed analog boards carry `AD1879JD`; the service manual assigns A/D conversion to this board. | component `[Verified physical photo]`; routing `[OPEN]` |
| CS5336 | No ASR-10 source or inspected board photograph contains this part. The photographed revisions use AD1879JD instead. | `[DISPROVEN for photographed boards]`; other revisions `[OPEN]` |
| ES5701 | The available material is chip-level bus/glue documentation. No board source places ES5701 on the ADC-to-J1-to-J6-to-SCC route. | `[OPEN]`; no observed boundary relevance |
| ES5506 | OTTO/ES5506 is on the digital board, and the analog board also converts digital audio to analog output. No source maps an ES5506 serial pin to J6 or shows it sharing the ADC receive path. | `[OPEN physical]` |
| ADC-to-SCC glue | No schematic, trace, or continuity result identifies direct wiring versus buffer/mux/PAL/glue. | `[OPEN]` |

The generic MC68302 names `RXD1` and `RXD2` are only names for the two logical
receive inputs. They are measurement targets, not evidence that ASR-10 connects
either one to a particular J6 pin.

## Physical measurement checklist

### Record the hardware variant first

1. Photograph both sides of the analog and digital boards at sufficient
   resolution to read assembly number, revision, reference designators, and pin
   1 markers.
2. Record whether the machine is keyboard, rack, or ASR-88 and whether an SP-3
   board is installed.
3. Record the direct cable orientation at analog `J1` and digital `J6`. If SP-3
   is present, record both 34-pin cable orientations at SP-3 `J4` and `J2`.
4. Confirm the ADC marking and reference designator. Existing photos predict
   `AD1879JD`; a different part is evidence of another board revision, not a
   reason to reuse this pass's classification.

### Power-off continuity matrix

Use a meter in continuity/resistance mode with the unit disconnected from
mains. Record resistance, not only beep/no-beep:

1. Map every analog `J1` pin to the corresponding digital `J6` pin through the
   direct 34-pin cable.
2. If SP-3 is installed, map analog `J1 -> SP-3 J4` and `SP-3 J2 -> digital J6`
   separately. Do not assume straight-through mapping through the isolation
   circuitry.
3. From the ADC package, identify which package leads have continuity to analog
   `J1`. Record package lead number and any intervening resistor/buffer
   reference designator without assigning a signal name yet.
4. From digital `J6`, test continuity to the MC68302 package leads that the chip
   documentation names RXD1, RXD2, receive clocks, and PB3. A board connection
   becomes verified only when this continuity is measured.
5. For every non-direct path, identify the first intervening component on each
   board and repeat continuity on its far side. This distinguishes direct ADC
   wiring from buffer, mux, PAL, or isolation stages.
6. Map ground pins separately. With SP-3 present, verify rather than assume
   whether analog and digital grounds are isolated.

The output should be a 34-row table:

```text
J1 pin | J6 pin | direction | first analog-side component/pin |
first digital-side component/pin | measured resistance | signal (only if proven)
```

### Powered oscilloscope checks

No undocumented board point is currently a verified test point. The accessible
measurement points are connector pins and component leads found by the
power-off continuity pass.

1. Use the correct local ground reference. Do not bridge analog/digital
   isolation with an earth-grounded probe when SP-3 is installed; use an
   appropriate differential/isolated measurement setup.
2. Observe each continuity-identified clock candidate at analog `J1`, digital
   `J6`, and both sides of any intervening component.
3. Test whether one candidate has the manual's LRCLK behavior and reaches the
   measured MC68302 PB3 lead.
4. Record the separate high-rate clock candidate. Do not name it BCLK or 256Fs
   until frequency and endpoint both identify it.
5. During `WAITING`, apply a known left-channel input above threshold and
   observe which ADC-originating data line changes and which MC68302 RX lead
   receives the same transitions.
6. Repeat for right and stereo source modes. This is required before assigning
   SCC1/SCC2 to physical left/right channels.

No dedicated `TP` location for LRCLK, bit clock, serial data, or SCC RX was
found in the manual or inspected photographs. The connector/component points
above are therefore measurement candidates, not documented factory test
points.

## Hypothesis revision

| Hypothesis | Previous | Observation | New status | Falsifier / discriminator |
|---|---|---|---|---|
| analog-board A/D serial stream is the sampling SCC source | `[Likely]` | the physical boundary is now verified as analog J1 to digital J6; AD1879JD is visible on two board variants; SP-3 evidence independently places ADC data/clocks across this boundary | remains `[Likely]`, stronger boundary evidence but no SCC pin continuity | continuity shows ADC output reaches another consumer and SCC RX comes from a different J6 source |
| ADC is CS5336 | `[OPEN/search target]` | both inspected ASR-10 analog-board variants visibly carry AD1879JD | `[DISPROVEN for photographed boards]` | a documented ASR-10 board revision or physical board visibly carrying CS5336 |
| ADC reaches SCC directly | `[OPEN]` | no schematic or trace through the four-layer digital board | unchanged `[OPEN]` | continuity identifying either a direct path or an intervening component |
| PB3 carries analog-board LRCLK | `[Likely]` | firmware configures PB3 as input; manual names LRCLK to 68302 from analog board; exact board net remains unseen | unchanged `[Likely physical]` | continuity/scope shows LRCLK reaches another 68302 input and PB3 carries a different signal |
| SCC1 is physical left input and SCC2 right | `[OPEN]` | LEFT mode selects SCC1 continuation, but both receivers arm and no physical data arrived | unchanged `[OPEN]` | channel-selective scope/continuity plus successful receive distinguishes RXD1/RXD2 |

## Minimum discriminating experiment

With power removed, produce the 34-pin continuity matrix from analog `J1` to
digital `J6`, then extend only the candidate ADC-originating lines to the
AD1879JD and MC68302 package leads. This single measurement can identify:

- which J1/J6 pins carry ADC-originating data and clocks;
- whether PB3 is physically the LRCLK line;
- whether RXD1, RXD2, or intervening glue receives ADC data;
- whether the path is direct or buffered.

Do not implement SCC input before that matrix exists.

## External references

- ENSONIQ ASR Service Manual, local source:
  `../sources/ASR10_service_manual.pdf`
- Syntaur analog-board photo, keyboard/ASR-88:
  <https://cdn.syntaur.com/images/4153-Lg.jpg>
- Syntaur analog-board photo, rack:
  <https://cdn.syntaur.com/images/4223-Lg.jpg>
- Syntaur digital-main-board photo:
  <https://cdn.syntaur.com/images/4145-Lg.jpg>
- Analog Devices AD1879 product identification:
  <https://www.analog.com/en/products/ad1879.html>
- R-Massive Ensoniq schematic inventory:
  <https://zine.r-massive.com/ensoniq-technical-documents-and-schematics/>
- Straylight SP-3 boundary observation:
  <https://www.straylightengineering.com/asr-10-scsi-boards-continued/>
- Straylight ASR-10 project observations:
  <https://www.straylightengineering.com/projects/ensoniq-samplers/ensoniq-asr-10/>

No emulator or runtime experiment was run for this investigation. No source
code, machine model, memory map, clock, bank, or device implementation changed.
