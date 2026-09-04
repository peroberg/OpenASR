# Panel frame completion paths, V3.50

Scope: runtime receive path in V3.50, no `-log`, no `mem_map` change.

## Completion table

Assumption for the concrete table below: `$CCD1 == 0`, which matches the clean
FILE 1 probes where raw `$CC/$DD/$EE/$FF` entered `$FFB3C0` from `$FFB392`.

| completion | reached from | required byte N | frame length | concrete frame class |
| --- | --- | --- | --- | --- |
| `$FFB43E` | `$FFB20A` | second byte `$01-$BF`, after first byte selected `$B20A` | 2 bytes | `[$80-$BF] [$01-$BF]` |
| `$FFB488` | `$FFB0E0` | second byte `$01-$BF`, after first byte selected `$B0E0` with stored first byte `< $40` | 2 bytes | `[$00-$3F] [$01-$BF]` |
| `$FFB4CC` | `$FFB0E0` | second byte `$00-$BF`, after first byte selected `$B0E0` with stored first byte `>= $40` | 2 bytes | `[$40-$7F] [$00-$BF]` |

[Verified static] `$FFB392` is the first-byte state. It stores the first byte
in `$03C4` and selects the second-byte state:

```text
first byte $00-$7F  -> $03C0 = $B0E0
first byte $80-$BF  -> $03C0 = $B20A, with bit 7 cleared before storing
first byte $C0-$FF  -> $FFB3C0 control branch
```

[Verified static] `$FFB3C0` is the high-control branch:

```text
$FF -> call $F89A9A, then return through $B2D6
$FC -> set $03C0 = $B3FE; next byte stores to $03C6, calls $F89A9A, restores previous state
$F7 -> clear $03C5; set $03C0 = $B414; next byte is reprocessed through $B392
other $C0-$FF -> restore previous $03C0 from $03C2
```

[Likely] `$FC xx` and `$F7 xx` are control frames, not direct key-event
completion frames. This pass did not assign their semantics.

## FILE 1 TX window

[Verified dynamic] The machine still transmits on channel B after
`FILE 1  TUTORIAL BNK` is visible. In a 20 s window after the display reached
FILE 1, 11 THRB writes were observed:

```text
15 78 0e 77 0e 77 07 7b 0b 7a 0b
```

No `$74` occurred in this FILE 1 window. The generated log is in
`docs/asr10/static/panel-file1-tx-window-v350.csv`.

[Likely] Substitution remains the right class of stimulus because the link is
still active, but the tuning-phase `$74 nn` scanpoint is not the FILE 1 runtime
poll observed here.

## Candidate frame injection

Candidate sent: `$40 $01`.

Reason: from `$FFB392`, first byte `$40` selects `$FFB0E0` and stores `$40` in
`$03C4`; second byte `$01` satisfies the `$FFB0E0` branch where stored first
byte is `>= $40`, so the static path is `$FFB0E0 -> $FFB4CC`.

Measured run:

```text
before: display "FILE 1  TUTORIAL BNK", $03C0=$B392, $03C4=$0000
inject $40
  before queue: $03C0=$B392, $03C4=$0000
inject $01 after 24 us
  before queue: $03C0=$B0E0, $03C4=$4000
after 20 ms settle:
  completion_pc marker: not observed
  display unchanged
  $03C0=$B392
  $03C4=$2400
  ready scheduler slots: 0
```

[Verified dynamic] The first byte was consumed and advanced the state machine to
`$B0E0` with `$03C4=$4000`.

[Verified dynamic] After the second byte, the state returned to `$B392` and
`$03C4` became `$2400`, matching the static `$B4CC` path's `subi.b #$1c,$03c4`
effect.

[OPEN] The PC marker did not directly observe `$FFB4CC`. Treat `$40 $01 ->
$B4CC` as statically derived and dynamically supported by state effects, not as
a directly observed completion-PC hit.

[Verified dynamic] `$40 $01` did not change the visible FILE 1 display in this
run and did not leave any scheduler slot with `+2 != +3` at the result sample.

Generated run data is in
`docs/asr10/static/panel-frame-injection-v350-4001.csv`.
