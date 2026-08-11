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
  inject press   $80|n, $00
  wait 50 ms
  inject release n,     $00
  wait 50 ms
```

Each run started from `FILE 1  TUTORIAL BNK`. If a button changed the display,
the run stopped and the next run resumed from the following button number.

Generated data:
`docs/asr10/static/panel-button-sweep-v350.csv`.

Display-changing buttons:

| button | display after press/release | notes |
|---:|---|---|
| `$05` | `31274` | ready slots observed: 2 after press, 1 after release |
| `$06` | `CREATE NEW INSTRUMENT` | display change, no ready slots |
| `$07` | `  HALL REVERBcrg` | display change, no ready slots |
| `$09` | `FILE 16 44LUSH PLATE` | display change, no ready slots |
| `$0A` | `FILE 2  JM DIGI SYN` | display change, no ready slots |
| `$0B` | `FILE 14 BLUES ORGAN` | display change, no ready slots |
| `$10` | `7     BLKS` | ready slot observed after release |
| `$11` | `7     BLKS` | display change, no ready slots |
| `$13` | `FILE 23 44EQ+DDL+CHO` | display change, no ready slots |
| `$15` | `FILE 9  TUTORIAL SEQ` | display change, no ready slots |
| `$19` | `NO SUCH FILE` | display change, no ready slots |
| `$1B` | `NO DISK DIRECTORIES` | display change, no ready slots |
| `$1F` | `NO SUCH FILE` | display change, no ready slots |
| `$20` | `LEFT` | display change, no ready slots |

[Verified dynamic] The runtime receive pipe is connected past `$03C4` for
button frames: valid button frames reach user-visible file-browser behavior.

[OPEN] Human names for the button numbers. Several effects look like browser
navigation or mode commands, but this pass does not label `$05`, `$06`, etc. as
specific ASR-10 front-panel labels.

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
