# Panel button sweep, V3.50

Scope: V3.50 `FILE 1  TUTORIAL BNK`, runtime receive path, no `-log`, no
`mem_map` change.

## Protocol source

[Verified] MAME's `esqpanel_device` implements the EPS-family panel protocol:

- button press: `($80 | button, $00)`
- button release: `(button, $00)`
- key down: `($80 | key, velocity 1-127)`
- key pressure: `($40 | key, pressure)`
- key up: `(key, $40)`

Evidence: `src/mame/ensoniq/esqpanel.cpp:685` (`set_button`), `:709`
(`key_down`), `:717` (`key_pressure`), and `:725` (`key_up`). The EPS-mode
panel's receive handler also supplies the expected reply bytes in
`rcv_complete()` at `src/mame/ensoniq/esqpanel.cpp:504`.

[Verified] `ESQPANEL1X22` is the 1x22 EPS-16/EPS-16+ panel and sets
`m_eps_mode = true` at `src/mame/ensoniq/esqpanel.cpp:739`. The EPS machine
configuration uses `ESQPANEL1X22` on DUART channel B, with MIDI on channel A,
at `src/mame/ensoniq/esq5505.cpp:827`. ASR-10's visible display strings are 22
characters.

[Verified] The coding matches the ASR-10 completion paths: `$B43E`/`$B4CC`/
`$B488` distinguish button/key/control classes, and `subi.b #$1c` in the key
path maps key `$40` to MIDI note 36, i.e. C2 and upward.

[Verified] The 192 earlier frames all used second byte `$40`. They were
keyboard events, not buttons. With no loaded instrument, zero display change and
zero ES5506 writes are valid behavior for that stimulus.

## Button sweep

Method:

```text
for button n = $00-$3F:
  boot cleanly to FILE 1  TUTORIAL BNK
  inject press   $80|n, $00
  wait 50 ms
  inject release n,     $00
  wait 50 ms
  read display and end the run
```

Each button was run independently from `FILE 1  TUTORIAL BNK`.

Generated data:
`docs/asr10/static/panel-button-sweep-v350.csv`.

The earlier table is preserved as
`docs/asr10/static/panel-button-sweep-v350-cumulative.csv`. It was cumulative:
when one button changed state, later buttons inherited that state. It is valid
history, but not an independent button map.

Display-changing buttons:

| button | display after press/release | notes |
|---:|---|---|
| `$05` | `31274` | ready slots observed: 2 after press, 1 after release |
| `$06` | `CREATE NEW INSTRUMENT` | display change, no ready slots |
| `$07` | `  HALL REVERBcrg` | display change, no ready slots |
| `$09` | `FILE 16 44LUSH PLATE` | display change, no ready slots |
| `$0A` | `FILE 2  JM DIGI SYN` | [Likely] nästa fil |
| `$0B` | `FILE 14 BLUES ORGAN` | display change, no ready slots |
| `$10` | `7     BLKS` | display change, no ready slots |
| `$11` | `7     BLKS` | display change, no ready slots |
| `$15` | `FILE 9  TUTORIAL SEQ` | display change, no ready slots |
| `$1B` | `NO DISK DIRECTORIES` | display change, no ready slots |
| `$20` | `LEFT` | display change, no ready slots |

[Verified dynamic] `$13`, `$19` and `$1F` were display-changing only in the
cumulative table and did not reproduce in the clean per-button runs.

[Verified dynamic] The runtime receive pipe is connected past `$03C4` for
button frames: valid button frames reach user-visible file-browser behavior.

[OPEN] Human names for the button numbers. Several effects look like browser
navigation or mode commands, but this pass does not label `$05`, `$06`, etc. as
specific ASR-10 front-panel labels.

## TX bytes and annunciator candidates

The clean sweep logged all channel B TX bytes for each button run. Every run
starts with the same idle/update sequence:

```text
15 78 0e 77 0e 77 07 7b 0b 7a 0b
```

Observed `$77-$7B` command/value pairs beyond that baseline:

| button | display | additional pairs |
|---:|---|---|
| `$05` | `31274` | `$78=$0F`, `$77=$05`, `$78=$07`, `$77=$0C`, `$78=$0C` |
| `$06` | `CREATE NEW INSTRUMENT` | `$78=$07`, `$78=$0F`, `$77=$0D` |
| `$09` | `FILE 16 44LUSH PLATE` | `$78=$0F`, `$79=$0F`, `$78=$07`, `$77=$0C`, `$7A=$7F`, `$7A=$78` |
| `$0A` | `FILE 2  JM DIGI SYN` | `$78=$07` |
| `$0B` | `FILE 14 BLUES ORGAN` | `$78=$07` |
| `$15` | `FILE 9  TUTORIAL SEQ` | `$78=$0F`, `$79=$0F`, `$78=$07`, `$77=$02`, `$78=$02` |
| `$1B` | `NO DISK DIRECTORIES` | `$78=$0F`, `$79=$0F`, `$78=$07`, `$77=$0C` |
| `$20` | `LEFT` | `$78=$0F`, `$77=$0D`, `$78=$07`, `$78=$0D` |

[Verified dynamic] `$77`, `$78`, `$79`, `$7A` and `$7B` behave as
two-byte display-side control/register writes during these runs.

Additional clean candidate runs with the working `ASR10PANEL` device captured
the final raw annunciator register state after the button press:

| button | display after press | final `$77-$7B` state |
|---:|---|---|
| `$05` | `FREE SYSTEM BLKS=31274` | `$77=$0C`, `$78=$0C`, `$79=$0F`, `$7A=$0B`, `$7B=$0B` |
| `$06` | `CREATE NEW INSTRUMENT` | `$77=$0D`, `$78=$0F`, `$79=$0F`, `$7A=$0B`, `$7B=$0B` |
| `$07` | `FX=INST    HALL REVERB` | `$77=$07`, `$78=$0E`, `$79=$0F`, `$7A=$0B`, `$7B=$0B` |
| `$1B` | `NO DISK DIRECTORIES` | `$77=$0C`, `$78=$0E`, `$79=$0F`, `$7A=$0B`, `$7B=$0B` |
| `$20` | `REC SRC=INPUTDRY LEFT` | `$77=$0D`, `$78=$0D`, `$79=$0F`, `$7A=$0B`, `$7B=$0B` |

Conservative bit observations from these five mode changes:

| command | bit/value | observed with | indicator name |
|---:|---:|---|---|
| `$77` | bit 0 set | `$06`, `$07`, `$20` | `[OPEN]` |
| `$77` | bit 1 set | `$07` only among this set | `[OPEN]` |
| `$77` | bit 2 set | `$05`, `$07`, `$1B` | `[OPEN]` |
| `$77` | bit 3 set | `$05`, `$06`, `$1B`, `$20` | `[OPEN]` |
| `$78` | bit 0 set | `$06`, `$20` | `[OPEN]` |
| `$78` | bit 1 set | `$07`, `$1B` | `[OPEN]` |
| `$78` | bit 2 set | `$05`, `$20` | `[OPEN]` |
| `$78` | bit 3 set | all five candidate runs | `[OPEN]` |
| `$79` | bits 0-3 set | all five candidate runs | `[OPEN]` |
| `$7A` | bits 0,1,3 set | all five candidate runs | `[OPEN]` |
| `$7B` | bits 0,1,3 set | all five candidate runs | `[OPEN]` |

[OPEN] Indicator names for individual bits. These runs read the ASR panel
device's raw annunciator registers, but there is still no independent
observation tying a raw bit to the printed labels `LOAD`, `CMD`, `EDIT`,
`INST`, etc. Do not name a bit until a label is observed changing.

## Button name hypotheses

Names below are based on visible effects from clean per-button runs. They are
`[Likely]` only; none yet has two independent effects.

| button | proposed name | support |
|---:|---|---|
| `$05` | `[Likely] SYSTEM` | Display changes to `FREE SYSTEM BLKS=31274`. |
| `$06` | `[Likely] INST` | Display changes to `CREATE NEW INSTRUMENT`. |
| `$07` | `[Likely] EFFECTS/FX` | Display changes to `FX=INST    HALL REVERB`. No exact printed `FX` label exists in the ASR-10 field list, so the front-panel label remains `[OPEN]`. |
| `$09` | `[Likely] previous file/page` | From `FILE 1`, display jumps to `FILE 16 44LUSH PLATE`, consistent with wraparound navigation. |
| `$0A` | `[Likely] next file` | From `FILE 1`, display changes to `FILE 2  JM DIGI SYN`. |
| `$0B` | `[Likely] previous-file/backward navigation variant` | From `FILE 1`, display changes to `FILE 14 BLUES ORGAN`; exact label `[OPEN]`. |
| `$10` | `[Likely] file info/storage` | Display changes to `7     BLKS`. Exact front-panel label `[OPEN]`. |
| `$11` | `[Likely] file info/storage` | Same visible `7     BLKS` effect as `$10`; exact distinction `[OPEN]`. |
| `$15` | `[Likely] SEQ` | Display changes to `FILE 9  TUTORIAL SEQ`. |
| `$1B` | `[Likely] directory` | Display changes to `NO DISK DIRECTORIES`. No exact printed label is assigned yet. |
| `$20` | `[Likely] record/input-source edit` | Display changes to `REC SRC=INPUTDRY LEFT`. |

## ASR-10 panel device pilot

[Verified] A new `ASR10PANEL` device subclasses `esqpanel_device`, sets
EPS-mode behavior, contains a 1x22 VFD path and raw `$77-$7B` annunciator
register outputs. It is wired as `panel.write_tx() -> DUART rx_b_w` and
`DUART b_tx_cb -> panel.rx_w`.

Verification:

| step | result |
|---:|---|
| 1. boot to `FILE 1  TUTORIAL BNK` | passed before and after external DUART clocks |
| 2. display text rendered via new panel unit | passed: `FILE 2  JM DIGI SYN` rendered 21/21 bytes through `ASR10PANEL` |
| 3. `$0A` button via panel unit gives `FILE 1 -> FILE 2` | passed after external DUART clocks |

[Verified] ASR-10 now clocks the DUART like the EPS/VFX-family panel path:
`set_clocks(500'000, 500'000, 1'000'000, 1'000'000)`. In MAME's 68681 model
that maps to IP3/IP4/IP5/IP6; CSRB `$EE` selects IP5/16 for channel B, i.e.
62,500 baud, matching `esqpanel_device::device_reset()`.

[Verified] The panel channel is half-duplex request/reply in the ASR-10 V3.50
file browser. Host writes one byte to THRB, polls SRB until RxRDY, then writes
the next byte. The `FILE 2  JM DIGI SYN` TX intervals after the first string
byte were:

```text
362.375, 361.000, 361.250, 361.500, 361.250, 361.500, 361.625,
361.500, 361.500, 361.125, 361.375, 361.625, 361.125, 361.250,
361.000, 361.375, 361.375, 361.625, 361.375, 422.500 us
```

One SRB read occurred before each THRB write. SRB was `$0D`, i.e. RxRDY +
TxRDY + TxEMT. The ~361 us cadence is two 62500-baud 8N2 character times
(176 us host byte + 176 us panel reply) plus firmware latency. IP5 is 1 MHz
with CSRB selector `$E`.

[Verified] The panel device renders the full `FILE 2  JM DIGI SYN` display:
21/21 bytes arrived at the panel device and `set_button($0A)` gives
`FILE 1 -> FILE 2`.

[Verified] The old direct `$FF` autoresponse harness is no longer the active
default path; it is opt-in only through `ASR10_PANEL_LEGACY_AUTORESPOND`.
The ASR-10 panel device itself supplies `$FF` as the serial idle reply.

[Verified] The `$FF` reply is an ASR-10 requirement in the current model:
when `ASR10PANEL` was changed to inherit the base EPS echo semantics, V3.50
stalled in `LOADING SYSTEM` and did not reach `FILE 1` within the 45 s test
window. Restoring the `$FF` reply restored boot, complete display rendering,
and `$0A` button behavior.

## `$03C8`

`$F97662`:

```asm
f97662  4a38 03c8       tst.b   $03c8.w
f97666  66e4            bne     $f9764c
f97668  2f04            move.l  D4,-(A7)
...
f97670  207c fffc3001   movea.l #$fffc3001,A0
f97678  05c8 0000       movep.l D2,($0,A0)
...
f9768c  383c 0140       move.w  #$0140,D4
f97690  6100 00e4       bsr     $f97776
...
f97698  617e            bsr     $f97718
f9769c  b497            cmp.l   (A7),D2
...
f976ae  4e75            rts
```

[Verified static] `$03C8` is tested as a byte boolean gate. If non-zero,
`$F97662` returns immediately. If zero, `$F97662` performs a hardware transaction
through the `$FFFC3001` host-port style window, writes via `movep`, reads back,
and retries comparison up to ten times.

[Verified static] `$03C8` is not used as an index and is not decremented by
`$F97662`.

Identified ROM writers in the same hardware-test cluster:

| address | instruction | effect |
|---:|---|---|
| `$F17D40` | `st $03c8.w` | inhibit `$F97662` |
| `$F17D64` | `clr.b $03c8.w` | enable `$F97662` |
| `$F17DB2` | `clr.b $03c8.w` | enable around transaction |
| `$F17DBC` | `st $03c8.w` | inhibit after transaction |
| `$F17E0E` | `clr.b $03c8.w` | enable around transaction |
| `$F17E18` | `st $03c8.w` | inhibit after transaction |
| `$F17E3E` | `st $03c8.w` | inhibit before test loop |
| `$F17E54` | `clr.b $03c8.w` | enable around transaction |
| `$F17E5E` | `st $03c8.w` | inhibit after transaction |
| `$F17F3A` | `st $03c8.w` | inhibit before test loop |
| `$F17F56` | `clr.b $03c8.w` | enable around transaction |
| `$F17F60` | `st $03c8.w` | inhibit after transaction |
| `$F17F80` | `clr.b $03c8.w` | enable before return path |

[Likely] `$03C8` is an inhibit flag for a low-level ES5506/OTIS host-port
verified write/read service. The surrounding `$F17Dxx-$F17Fxx` code uses it to
temporarily disable or enable `$F97662` while running hardware test/calibration
paths.

[Verified dynamic] In the button sweep, `$03C8` remained `$0000` before and
after every press/release frame. It was not the missing downstream signal for
button events in `FILE 1`.

[OPEN] Whether any runtime path outside this sweep toggles `$03C8`.
