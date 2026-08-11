# Panel raw-byte sweep against V3.50 runtime receive path

Scope: V3.50, start state `FILE 1  TUTORIAL BNK`, no `-log`, no `mem_map`
change. The generated per-byte result table is
`docs/asr10/static/panel-raw-byte-sweep-v350.csv`.

## Display readback

The previous display reading used `m_panel_text`. That buffer is a transient stream
accumulator for text received from the panel/display transmit path. It is flushed and
can be empty at exit, so it is not a reliable "current display" source.

The replacement reads the current emulated display cells from `m_display_chars` and trims
trailing blanks. It can be sampled at arbitrary points during a run.

Calibration in the sweep run:

```text
10/10 samples read "FILE 1  TUTORIAL BNK"
```

## Dispatch tail after RHRB

For raw `$22`, the runtime receive path consumed the byte and immediately jumped through
the state pointer at `$03C0`:

```asm
ffb0d4  move.b  $fffc4817.l,D1
ffb0da  movea.w $3c0.w,A0
ffb0de  jmp     (A0)
```

Observed at consumption:

```text
RHRB PC      $FFB0D4
value        $22
$03C0 word   $B392
target       $FFB392
```

The 64 bytes at `$FFB392` disassemble as:

```asm
ffb392  tst.b   $ccd1.w
ffb396  beq     $ffb3a0
ffb398  cmp.b   #$e0,D1
ffb39c  bcc     $ffb3c0
ffb39e  bra     $ffb3a6
ffb3a0  cmp.b   #$c0,D1
ffb3a4  bcc     $ffb3c0
ffb3a6  move.w  #$b20a,$3c0.w
ffb3ac  bclr    #$7,D1
ffb3b0  bne     $ffb3b8
ffb3b2  move.w  #$b0e0,$3c0.w
ffb3b8  move.b  D1,$3c4.w
ffb3bc  bra     $ffb2d6
```

For `$22`, the byte is stored in `$03C4`:

```text
ffb3a6: $03C0 $B392 -> $B20A
ffb3b2: $03C0 $B20A -> $B0E0
ffb3b8: $03C4 $4400 -> $2200
```

[Verified] In this runtime path, the consumed byte is routed through `$03C0` and stored
in `$03C4` for the `$22` case. The sweep shows additional state-machine targets
`$FFB0E0`, `$FFB20A`, `$FFB414`, and `$FFB3FE` for other byte values.

## Sweep result

One run covered raw `$00-$FF`.

```text
rows:                 256
consumed:             256 yes, 0 no
dispatch target:      256 x $FFB0BC
display verification: 256 x 10/10
display changes:      0
```

`$23` was included in the sweep and was consumed:

```text
raw=$23 consumed=yes consumed_value=$23 dispatch_target=$FFB0BC
before="FILE 1  TUTORIAL BNK"
after ="FILE 1  TUTORIAL BNK"
changed=no
tail: $03C0=$B0E0 -> target $FFB0E0, then store via $FFB1BE to $03C4
```

No raw byte produced a visible display change in this start state. This is a runtime
observation about the current state and transport path, not a conclusion that the byte
has no meaning in other states.

## Interpretation and caveats

[Verified] 256 raw values were consumed at `$FFB0D4`. Zero display changes were
observed.

[Verified] `$03C0` is rewritten by the handler: `$B392 -> $B20A -> $B0E0`.
`$03C4` accumulates the byte in the high halfword lane; the `$22` case changed
`$03C4` from `$4400` to `$2200`.

[Likely] The panel protocol is multi-byte. A single byte is not a complete panel
event.

[OPEN] Frame length and format.

Caveat: the `$00-$FF` sweep ran in one emulator run, so each byte advanced the state
machine for the next byte. The per-raw-value results are not independent; only the
aggregate result is valid: all 256 bytes were consumed, and none produced a display
change in that chained run.
