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

[OPEN] Indicator names for individual bits. The TX stream identifies raw
register/value changes, but this pass did not observe physical annunciator
state and therefore does not map bits to `LOAD`, `CMD`, `EDIT`, etc.

## ASR-10 panel device pilot

[Verified] A new `ASR10PANEL` device subclasses `esqpanel_device`, sets
EPS-mode behavior, contains a 1x22 VFD path and raw `$77-$7B` annunciator
register outputs. It is wired as `panel.write_tx() -> DUART rx_b_w` and
`DUART b_tx_cb -> panel.rx_w`.

Verification:

| step | result |
|---:|---|
| 1. boot to `FILE 1  TUTORIAL BNK` | passed |
| 2. display text rendered via new panel unit | failed: panel-device text shadow stayed blank |
| 3. `$0A` button via panel unit gives `FILE 1 -> FILE 2` | failed: no display change |

[OPEN] Why the bit-serial panel path did not receive/render the boot text and
why `set_button($0A)` did not reach the runtime receive path. The direct
two-byte frame injection remains verified and is not invalidated by this pilot.

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
