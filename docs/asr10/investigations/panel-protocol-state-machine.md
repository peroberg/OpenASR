# Panel protocol state machine

Scope: V3.50, channel B runtime receive path, no `-log`. This pass did not
modify `mem_map`.

## State machine

[Verified] The runtime receive path enters the handler through the low-memory
state pointer at `$03C0`. The decoded persisted state targets are:

| `$03C0` target | Role observed in disassembly |
| --- | --- |
| `$FFB392` | First-byte state. Tests the received byte in `D1`, stores it in `$03C4`, and selects the next state. |
| `$FFB0E0` | Second-byte state for first bytes with bit 7 clear. Resets `$03C0` to `$B392` before interpreting the second byte. |
| `$FFB20A` | Second-byte state for first bytes with bit 7 set. Resets `$03C0` to `$B392` before interpreting the second byte. |
| `$FFB3FE` | One-byte follow-up after control byte `$FC`; stores the byte at `$03C6`, calls `$F89A9A`, then restores the previous state. |
| `$FFB414` | Follow-up after control byte `$F7`; resets `$03C0` to `$B392` and immediately re-enters `$B392` with the same byte. |

[Verified] `$FFB3C0` is a control branch target, not a state value observed
being stored directly in `$03C0` in this pass. It saves the previous state in
`$03C2` and handles high control values: `$FF`, `$FC`, and `$F7`.

[Verified] Normal first bytes below the threshold select the next state by bit
7. With `$CCD1 == 0`, bytes `>= $C0` branch to `$FFB3C0`; bytes `< $C0` are
stored in the high byte lane of `$03C4`.

```asm
ffb392  tst.b   $ccd1.w
ffb396  beq     $ffb3a0
ffb398  cmp.b   #$e0,D1
ffb39c  bcc     $ffb3c0
ffb3a0  cmp.b   #$c0,D1
ffb3a4  bcc     $ffb3c0
ffb3a6  move.w  #$b20a,$3c0.w
ffb3ac  bclr    #$7,D1
ffb3b0  bne     $ffb3b8
ffb3b2  move.w  #$b0e0,$3c0.w
ffb3b8  move.b  D1,$3c4.w
```

[Likely] The normal panel event frame is at least two bytes. The first byte
stores a base value in `$03C4` and selects either `$FFB0E0` or `$FFB20A`; the
second-byte state then either completes the decoded event through `$FFB43E`,
`$FFB488`, or `$FFB4CC`, or returns without completion.

[OPEN] The exact frame format and decoded semantic fields. The disassembly
shows where frames complete, but this pass did not identify the final queue or
consumer semantic for every completion path.

## Boot channel B conversation

[Verified] A reset-to-`FILE 1  TUTORIAL BNK` V3.50 run logged 394 channel B
events before the display reached the target state:

```text
THRB writes: 198
RHRB reads:  196
```

The full generated sequence is in
`docs/asr10/static/panel-channel-b-conversation-v350.csv`.

The first events are ROM-path traffic:

```text
> e7  < ff
> 71  < ff
> 66  < ff
> 20  < ff
```

The end of the sequence is runtime-path traffic at `$FFB0D4`, while ROM code at
`$F89AA4` sends the visible display text:

```text
> 46 < ff  > 49 < ff  > 4c < ff  > 45 < ff
> 20 < ff  > 31 < ff  > 20 < ff  > 20 < ff
> 54 < ff  > 55 < ff  > 54 < ff  > 4f < ff
> 52 < ff  > 49 < ff  > 41 < ff  > 4c < ff
> 20 < ff  > 42 < ff  > 4e < ff  > 4b
```

[Verified] In this run every logged RHRB byte was `$FF`. The current panel
harness responds with status/ACK bytes while the host transmits display bytes;
the log therefore captures the implemented harness conversation, not a complete
external panel capture.

[OPEN] Real panel frame boundaries. The channel log samples `$03C0/$03C4`
around the DUART access itself; subsequent handler writes to `$03C0/$03C4`
occur in later instructions and are not represented as after-values in that
same row.

## Clean one-byte probe

Sixteen values were tested as one fresh V3.50 run per byte:

| raw | consumed | path | `$03C4` write | display changed |
| --- | --- | --- | --- | --- |
| `$00` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $0000` | no |
| `$11` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $1100` | no |
| `$22` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $2200` | no |
| `$33` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $3300` | no |
| `$44` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $4400` | no |
| `$55` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $5500` | no |
| `$66` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $6600` | no |
| `$77` | yes | `$B392 -> $B20A -> $B0E0` | `$0000 -> $7700` | no |
| `$88` | yes | `$B392 -> $B20A` | `$0000 -> $0800` | no |
| `$99` | yes | `$B392 -> $B20A` | `$0000 -> $1900` | no |
| `$AA` | yes | `$B392 -> $B20A` | `$0000 -> $2A00` | no |
| `$BB` | yes | `$B392 -> $B20A` | `$0000 -> $3B00` | no |
| `$CC` | yes | `$B392 -> $B3C0 -> $B392` | none observed | no |
| `$DD` | yes | `$B392 -> $B3C0 -> $B392` | none observed | no |
| `$EE` | yes | `$B392 -> $B3C0 -> $B392` | none observed | no |
| `$FF` | yes | `$B392 -> $B3C0` | `$0000 -> $0000` via `$F89ACE` | no |

The generated table is in
`docs/asr10/static/panel-clean-byte-probe-v350.csv`.

[Verified] First-byte state advancement is value-dependent. Values with bit 7
clear select `$FFB0E0`; values `$80-$BF` select `$FFB20A` after clearing bit 7;
values at or above the active threshold enter the `$FFB3C0` control branch.

[Verified] A single isolated byte still produced no display change in any of
the sixteen clean runs.
