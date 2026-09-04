# ASR-10 panel manual notes

Source: `docs/asr10/sources/ASR10_manual.pdf`, extracted text from the
ASR-10 Musician's Manual. Page references below use the manual's printed
section/page labels, not PDF page numbers.

## Front panel buttons

### Main mode and page area

Reference: Section 1, "Front Panel Controls", pp. 6-7.

The panel is organized around three mode buttons:

| physical group | printed label | manual role |
|---|---|---|
| Mode | `Load` | enters Load mode; flashing Load means disk files are shown |
| Mode | `Command` | enters Command mode |
| Mode | `Edit` | enters Edit mode |

The page buttons select pages within the current mode. The manual identifies
fourteen page buttons:

| physical/page group | printed label |
|---|---|
| Object pages | `Instrument`, `Seq-Song`, `System-MIDI`, `Effects` |
| Instrument edit pages | `Env 1`, `Env 2`, `Env 3`, `Pitch`, `Filters`, `Amp`, `LFO`, `Wave`, `Layer` |
| Track page | `Track` |

The ten numbered page buttons also act as the numeric keypad:

| numeric key | printed function |
|---:|---|
| `1` | `Env 1` |
| `2` | `Env 2` |
| `3` | `Env 3` |
| `4` | `Pitch` |
| `5` | `Filters` |
| `6` | `Amp` |
| `7` | `LFO` |
| `8` | `Wave` |
| `9` | `Layer` |
| `0` | `Track` |

The manual says the numbered page buttons double as a numeric keypad for
direct-dialing disk files, commands, parameters, and MIDI Program Changes.
Direct Dial is used after the relevant mode/page selection when the manual
lists a direct-dial number for the target screen. Reference: Section 1,
"Parameter Illustrations", p. 32; Section 2, `MIDI PROG CHANGE`, p. 20.

### Data entry and confirmation

Reference: Section 1, "Front Panel Controls", p. 7.

The manual calls the four cursor controls:

| printed/manual name | primary manual role |
|---|---|
| `Up Arrow` | next file/value/input choice |
| `Down Arrow` | previous file/value/input choice |
| `Left Arrow` | previous parameter/command or file-size toggle in Load mode |
| `Right Arrow` | next parameter/command or file-size toggle in Load mode |

The physical panel diagram places `Cancel`/`No` and `Enter`/`Yes` at the far
right of the main panel control area, to the right of the Data Entry section.
`Cancel-No` is the left member of the pair and `Enter-Yes` is the right member.
The manual describes them as proceed/cancel controls for the function currently
shown on the display.

### Display and indicator area

Reference: Section 1, "Additional Front Panel Controls", pp. 8-9.

The display has two parts: indicator lights in the upper half and a 22-character
alphanumeric display in the lower half. The manual names mode indicators
(`LOAD`, `COMMAND`, `EDIT`), page indicators such as `INST`, and sequencer
status indicators such as `STOP`.

### Additional front-panel controls

Reference: Section 1, "Additional Front Panel Controls", pp. 8-10.

| physical group | printed label |
|---|---|
| Input meters | `Left`, `Right`, `Signal`, `Peak`, `Input Level` |
| Sampling | `Sample-Source Select` |
| Effects | `FX Select-FX Bypass` |
| Instrument/sequence tracks | `1` through `8` |
| Audio tracks | `A`, `B` |
| Sequencer transport | `Record`, `Stop-Continue`, `Play` |

Performance controls outside the main data-entry panel include the two
`Patch Select` buttons, the Pitch Bend wheel, and the Modulation wheel.
Reference: Section 1, "Performance Controllers", p. 10.

## Procedures

### Load an instrument from disk

Reference: Section 1, "Loading an Instrument", p. 19.

Button sequence:

1. Insert a disk containing instrument files.
2. Press `Load`.
3. Press `Instrument`.
4. Use `Up Arrow` / `Down Arrow` or the Data Entry Slider until the desired
   instrument file is displayed. `Left Arrow` / `Right Arrow` toggles between
   file name and file size.
5. Press `Enter-Yes`.
6. Press one of the eight `Instrument-Sequence Track` buttons.

Expected manual displays include `NO INST OR BANK FILES` if no instrument or
bank files are present, `PICK INSTRUMENT BUTTON` after `Enter-Yes`, and
`LOADING FILE...` during load.

### Select or load an effect

Reference for selecting an internal effect: Section 5, "Selecting Effects",
pp. 2-3. Reference for loading an effect file: Section 5, "Loading an Effect
File", p. 6.

To select the current effect mode/effect:

1. Press `FX Select-FX Bypass`.
2. Use the Data Entry controls to choose among `OFF`, `INST`, `BANK`, and ROM
   effect selections.
3. Editing a ROM effect on the `Edit` / `Effects` page copies it into the Bank
   Effect location.
4. Press `Cancel-No` from the FX Select page to return to the ROM Effect
   location after a ROM effect has become the Bank Effect.

To load an effect file from disk:

1. Insert a disk containing effect files.
2. Press `Load`.
3. Press `Effects`.
4. Use `Up Arrow` / `Down Arrow` or the Data Entry Slider until the desired
   effect file is displayed. `Left Arrow` / `Right Arrow` toggles between file
   name and file size.
5. Press `Enter-Yes`.

The manual states that a loaded effect file replaces the Bank Effect. Expected
manual displays include `NO EFFECT FILES` if no effect files are present and
`DISK COMMAND COMPLETED` when loading finishes.

### Reach ESP TESTS or equivalent diagnostics

Reference: searched `ASR10_manual.pdf` for `ESP TESTS`, `ESP TEST`, diagnostic
terms, test mode, self-test, service, and hardware/software test terms.

[OPEN] No ESP TESTS procedure, service diagnostic entry, or equivalent
front-panel diagnostic sequence was found in the available Musician's Manual.
The source tree currently contains no separate ASR-10 service manual under
`docs/asr10/sources/`. This procedure is therefore not available from the
manual source used in this pass.

## Error messages

Reference: Section 14, "ASR-10 Disk Messages", pp. 22-23.

The manual lists disk warnings and disk error messages, but not numeric error
codes. It does not list `ERROR DOWNLOADING EFFECT`.

Disk warnings listed:

| message | manual meaning |
|---|---|
| `DISK COMMAND COMPLETED` | operation completed |
| `DISK WRITE-PROTECTED` | save/delete attempted on write-protected disk |
| `DISK HAS BEEN CHANGED` | disk changed since directory was loaded |
| `DISK DRIVE NOT READY` | no diskette or possible drive hardware issue |
| `NOT ENOUGH DISK SPACE` | insufficient sectors to save |
| `FILE DOES NOT EXIST` | load/delete requested when no matching file exists |
| `NO SYS-EX DATA TO SAVE` | Sys-Ex recorder has no data |
| `FILE TOO LARGE TO LOAD` | sequence file exceeds free memory |

Disk error messages listed:

| message | manual meaning |
|---|---|
| `DISK DRIVE NOT RESPONDING` | no diskette or hardware problem |
| `DISK NOT FORMATTED` | disk format not recognized |
| `NOT ASR-10 DISK` | recognized format but not ASR-10 data |
| `DISK ERROR - WRITE VERIFY` | written data could not be verified |
| `DISK ERROR - LOST DATA` | missed data during disk read |
| `FILE OPERATION ERROR` | fatal low-level file operation error |
| `DISK ERROR - BAD DATA` | data block CRC failed |
| `DISK ERROR - BAD DISK O.S.` | Disk OS Program Control Block CRC failed |
| `DISK ERROR - BAD DIRECTORY` | Directory block CRC failed |
| `DISK ERROR - BAD FAT` | FAT CRC failed |
| `DISK ERROR - BAD DEVICE ID` | Device ID block CRC failed |
| `FORMAT FAILED - BAD DISK` | bad sector detected during format |
| `O.S. NOT ON DISK` | OS file could not be loaded |
| `DISK COPY NOT COMPLETED` | disk copy was canceled or not completed |

[OPEN] The relationship between `$049D`/`$04AE` and manual-visible disk error
messages remains firmware-side evidence, not a manual mapping. The available
manual source does not provide numeric error-code tables.

## Master Tune

Reference: Section 2, "Edit/System-MIDI Page", `MASTER TUNE`, p. 33.

Manual access path:

1. Press `Edit`.
2. Press `System-MIDI`.
3. Press direct-dial `1`.

The page adjusts the ASR-10's master tuning up or down by as much as one
semitone. The displayed range is `-99` to `+99` cents, with `+0` corresponding
to concert A=440.

The manual does not describe any delayed display update. Under the general Data
Entry rules in Section 1, `Up Arrow` / `Down Arrow` and the Data Entry Slider
change the current parameter value. Therefore the expected manual behavior is
that the Master Tune value shown on the current page follows the edited value;
the previously observed one-step-late display remains `[OPEN]` as either an
emulation/display-path issue or a firmware behavior not described by this
manual.
