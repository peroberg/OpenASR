# ASR-10 boot flow findings

This file tracks observed boot/runtime control flow for the ASR-10 MAME research harness.

Do not treat hypotheses as confirmed hardware behavior. The `asr10booth` target is still an instrumented harness, not a clean final driver.

## Service/manual boot expectation

Boot ROM contains a disk loader.

Expected user-visible sequence:

```text id="dp7q8r"
ENSONIQ ASR-10
SCSI INSTALLED, SEARCHING FOR SCSI DEVICE    [on SCSI-equipped units]
PLEASE INSERT DISK
LOADING SYSTEM
TUNING KBD - HANDS OFF
```

ASR-10 Rack reportedly released with 1.50B EPROMs and requires at least 1.50 disk OS.

## Current observed panel text

Observed through `$FC4817` text path:

```text id="jdn6qq"
ENSONIQ ASR-10
PLEASE INSERT DISK
LOADING SYSTEM
```

Important current status:

```text id="ru9frv"
LOADING SYSTEM is now reached.
The current blocker is after LOADING SYSTEM, not before it.
```

After `LOADING SYSTEM`, loaded runtime returns to dispatcher idle around:

```text id="3bhnw9"
f87f96 / f87f9a / f87fca
```

This is not treated as a crash. It is a dispatcher queue scan / idle state.

## Current high-level boot/runtime flow

Current observed flow:

```text id="5a3tzp"
ROM boots
-> panel text path works
-> earlier input/status and FDC/media gates are passed far enough to reach LOADING SYSTEM
-> accepted-looking MC68302/FC68xx 0x2400 service sequence runs
-> dispatcher takes slot 2
-> baseline slot 2 callback enters 007308
-> 007308 starts with jsr $fff8d01a
-> runtime service setter at 00bf1a runs
-> $0d06 is set
-> post-service/finalizer writes slot 2 to 8080
-> dispatcher returns to f87f9a idle
```

Current missing step:

```text id="j0i3h7"
No next producer/re-arm event is observed after slot 2 completion.
```

Current best hypothesis:

```text id="z3d9yr"
The missing piece is likely the callback-chain / scheduler re-arm / producer path after slot 2 completion.
```

## Display print path

Current evidence:

```text id="r0wh23"
ROM routine at $F89CB0 writes printable ASCII-like bytes to $FC4817.
```

Harness reconstructs text when:

```text id="x69qcj"
PC == $F89CB0
data byte in range 0x20..0x7E
write address == $FC4817
```

This creates logs like:

```text id="me6996"
ASR10PANEL text="   ENSONIQ  ASR-10    "
ASR10PANEL text="  PLEASE INSERT DISK  "
ASR10PANEL text="   LOADING SYSTEM     "
```

Caution:

```text id="cqynys"
This is a text sniffer, not yet a real display device.
Control bytes, cursor movement, row selection, clear-display, and handshaking may exist and are currently ignored.
```

## Current runtime dispatcher idle

The final current state usually lands around:

```text id="oe7v7x"
ASR10HANG pc=f87f9a previous_pc=f87f9e opcode=122a
```

Relevant dispatcher code:

```asm id="uyghh1"
f87f92: movea.w $00c6.w,A2
f87f96: move.b  $0002(A2),D0
f87f9a: move.b  $0003(A2),D1
f87f9e: cmp.b   D0,D1
f87fa0: beq     f87fc2
...
f87fc0: rte
f87fc2: adda.w  #$0016,A2
f87fc6: cmpa.w  $00c8.w,A2
f87fca: bcs     f87f96
f87fcc: move    #$2000,SR
f87fd0: bra     f87f92
```

Current interpretation:

```text id="mk73me"
Queue base pointer: $00c6
Queue end pointer:  $00c8
Queue slot stride:  0x16
A slot appears pending when byte2 != byte3.
When byte2 == byte3, the dispatcher treats the slot as idle/equalized.
```

Current blocker:

```text id="9ytfwz"
After slot 2 callback/service completion, no queue slot appears to become pending again.
```

## Dispatcher RTE callback frame

Documented/proven dispatcher frame builder:

```asm id="rteframe"
f87fa2  move.l $0006(A2),-(A7)   ; callback PC
f87fa6  move.w $000a(A2),-(A7)   ; stacked SR
f87fb4  move.w $000c(A2),D5
f87fb8  clr.w  $000c(A2)
f87fbc  move.w A2,$0b6a.w
f87fc0  rte
```

This is a normal 68000 short RTE frame.

Slot field candidate meanings:

```text id="rteslotfields"
+0x02/+0x03  pending/equalized bytes
+0x06        callback / RTE PC
+0x0a        stacked SR
+0x0c        dispatch context / continuation, moved to D5 and cleared
+0x0e        extra context / USP-ish candidate
+0x10        secondary continuation/list pointer candidate
+0x12        companion state to +0x10
```

Baseline slot 2:

```text id="rteslot2"
slot base=002400
pending word=002402
frame SR=0000
frame PC=007308
dispatcher SR before RTE=2700
first-PC diagnostics showed actual_pc=007308
no immediate IACK was observed in baseline
007308 executes and starts with jsr $fff8d01a
```

## Current MC68302 / FC68xx service sequence

Accepted-looking service source:

```text id="jzb9bz"
0x2400
```

Observed sequence:

```text id="tt1z2h"
FC6814 000b -> 240b
IACK vector 0x4e or 0x4f
FC6818 = 4000 or 8000 from vector handler
FC6814 240b -> 000b
runtime 00bf1a sets FC6816 c080 -> e480
runtime 00bf22 sets $0d06
optional gated experiment clears FC6816 e480 -> c080
dispatcher still returns to f87f9a idle
```

Vector findings:

```text id="pd0p4s"
0x4e -> f88f06 -> writes FC6818=4000
0x4f -> f88f22 -> writes FC6818=8000
```

Both `0x4e` and `0x4f` are accepted-looking and converge.

Wrong/deprioritized vectors:

```text id="lub8cj"
autovectors 0x19..0x1f -> ERROR 139 unused vector
0x40 -> ERROR 139 unused vector
0x46 -> ERROR 129 odd address error
```

## Runtime service setter at `00BF1A`

Confirmed loaded runtime code:

```asm id="7p9egr"
00bef2  writes FC6894 = 703b
00bf00  writes FC6884 = 703b
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

Confirmed context:

```text id="7kz7x8"
D0=00000004
$0e82=0004
$0d06=0000 before 00bf22
FC6884=703b
FC6894=703b
```

Current interpretation:

```text id="0s9ssr"
The IACK handler does not directly set FC6816 0x2400.
Runtime code sets it later at 00bf1a.
This looks like a real service/handshake routine.
```

Proven post-service observations:

```text id="postservicefacts"
FC6816 0x2400 is set by runtime at 00bf1a.
$0d06 is set by runtime at 00bf22.
$0e82 later changes 0004 -> 0010.
No proven post-set firmware read/test of FC6816 0x2400 is known in this sequence.
No proven consumption/clear of the new $0d06=ff00 before idle is known in this sequence.
```

## Line-A / A000 semantics

A000 in the service setter is explained.

Line-A vector #10 points to ROM `f882ca`:

```asm id="lineavector10"
f882ca: move.w D0,(A7)
f882cc: addq.l #2,2(A7)
f882d0: rte
```

Effect:

```text id="linearesult"
writes D0.w to stacked SR/CCR
skips the A000 opcode
returns to the instruction after A000
```

For `00bf18`, `D0=0004`, so A000 makes post-RTE SR/CCR `0004`. It does not directly create queue/event payload.

Important negative result:

```text id="cuk2ab"
A gated experiment can clear FC6816 e480 -> c080 after 00bf1a + $0d06 + dispatcher RTE, but this still does not advance beyond dispatcher idle.
```

## Common clear/set service paths

### Common clear path

ROM path:

```asm id="b6wt9j"
f8c0ec: move.w #$7033,$00fc6884.l
f8c0f4: move.w #$7033,$00fc6894.l
f8c0fc: andi.w #$dbff,$00fc6816.l
f8c104: andi.w #$dbff,$00fc6814.l
f8c10c: clr.b  $0d06.w
f8c110: move.w $0e82.w,D0
f8c114: LINE_A
f8c116: rts
```

`0xdbff = ~0x2400`.

This path clears both FC6816 and FC6814 bit `0x2400`, and clears `$0d06`.

### Runtime set path

Loaded runtime path:

```asm id="hfwktf"
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

Current interpretation:

```text id="nswnf6"
f8c0xx and 00bfxx appear to be paired clear/set service paths or related service-handshake routines.
```

Resolved/updated:

```text id="8eh3qq"
What does $0d06 mean?
What does $0e82 mean?
What should happen after $0d06 is set?
```

A000 / Line-A is no longer a leading open question for service payload production.

## Slot continuation and finalizer behavior

Continuation logic around `f880e0..f88100`:

```asm id="f880continuation"
f880e0: move.w  $0010(A2),D0
f880e4: beq.s   f880fc
f880e6: movea.w D0,A0
f880e8: move.w  A0,$000c(A2)
f880ec: move.w  (A0),$0010(A2)
f880f0: bne.s   f880f6
f880f2: clr.w   $0012(A2)
f880f6: bclr    D2,$0002(A2)
f880fa: bra.s   f88100
f880fc: bset    D2,$0002(A2)
f88100: bset    D2,$0003(A2)
```

With `D2=7`:

```text id="f880meaning"
+0x10 == 0 gives byte2 |= 0x80 and byte3 |= 0x80, so equalized/not pending.
+0x10 != 0 can give byte2 != byte3, so pending.
```

Current logs do not prove a nonzero continuation path through `f880e6/f880ec/f880f6`.

Finalizer/list-drain/completion candidate around `f8ce00..f8ce46`:

```asm id="f8cefinalizer"
f8ce00: move.w $0b6c.w,D1
f8ce04: move.w $000c(A2),D0
f8ce08: beq.s  f8ce1c
f8ce0a: move.w D0,$0b6c.w
f8ce0e: subq.b #1,$0b7f.w
f8ce12: move.w D0,D5
f8ce14: move.w D1,(A5)
f8ce16: move.w D0,D1
f8ce18: clr.w  $000c(A2)
f8ce1c: move.w $0010(A2),D0
f8ce20: beq.s  f8ce3a
f8ce22: move.w D0,D5
f8ce24: move.w D0,$0b6c.w
f8ce28: subq.b #1,$0b7f.w
f8ce2c: move.w (A5),D0
f8ce2e: beq.s  f8ce32
f8ce30: move.w D0,D5
f8ce32: bne.s  f8ce28
f8ce34: move.w D1,(A5)
f8ce36: clr.l  $0010(A2)
f8ce3a: move.w #$8080,$0002(A2)
f8ce40: move.w #$0000,D0
f8ce44: A000
f8ce46: rts
```

For slot 2:

```text id="slot2f8ce"
A2=002400.
f8ce36 clears 002410/002412 if that path runs.
f8ce3a writes 002402=8080.
8080 is equalized/not pending because byte2 == byte3.
This looks like completion/finalizer output, not failed enqueue.
```

Healthy pending production is distinct:

```asm id="healthyproducer"
f87f28: clr.b  $0002(A1)
f87f2c: move.b #$01,$0003(A1)
```

This creates `0001`, so byte2 != byte3 and the slot is pending. Code around `f88120/f88124` also appears capable of direct pending/equalize transitions for slot 4/5-like paths.

## `$049D` prompt/status decision

`$049D` is a key ROM decision/status field from earlier boot/media phases.

Known observed values:

```text id="rsqfc4"
05 -> PLEASE INSERT DISK path
0D -> disk/controller/media error path
```

Caution:

```text id="rxytjl"
These paths explain earlier PLEASE INSERT DISK / media failure behavior.
They are not the current immediate blocker once LOADING SYSTEM is reached.
```

## Input gate path to `$049D=05`

Relevant ROM path:

```asm id="gtl018"
FB7C7A move.b $04EE,D0
FB7C7E tst.b  D0
FB7C80 bpl    FB7C9C
FB7C82 clr.b  D0
FB7C84 btst   #4,$FFFC4809
FB7C8C beq    FB7C92
FB7C8E move.b #1,D0
FB7C92 move.b D0,-(A7)
FB7C94 bsr    FB8D78
FB7C98 move.b (A7)+,$04EE
FB7C9C bne    FB7CA4
FB7C9E move.b #$05,$049D
```

Interpretation:

```text id="ab78sb"
$04EE=FF means a pending input/panel status check.
ROM tests $FC4809 bit 4.
If bit 4 is clear, ROM writes $049D=05.
If bit 4 is set, ROM avoids this $049D=05 write and proceeds further.
```

This is a panel/input/status gate, not an FDC error.

Current caution:

```text id="vrvrw3"
Do not fake more panel input until logs show the loaded runtime is actually waiting for LOAD/CMD/EDIT/instrument-select events.
```

## FDC/media path to `$049D=0D`

Relevant path:

```asm id="fxyhi3"
FB8C46 tst.b  $049D
FB8C4A bne    FB8C6C
FB8C4E move.b $04C6,D2
FB8C52 andi.b #$C0,D2
FB8C56 bne    FB8C5E

FB8C5E move.b #$FF,$04AC
FB8C64 move.b #$2B,D3
FB8C68 bsr    FB81AE

FB81AE moveq  #$0D,D2
FB81B0 move.b D3,$04AE
FB81B4 move.b D2,$049D
```

Interpretation:

```text id="mxd1so"
ROM requires ($04C6 & 0xC0) == 0.
If top bits are set, it writes:
  $04AE = 2B
  $049D = 0D
```

With no media:

```text id="zet2ds"
$04C6=68
$04C6 & C0 = 40
=> rejected
```

Current caution:

```text id="7i05ub"
No post-service FDC activity has been observed in the latest dispatcher/service tests.
Do not assume FDC/media is the next missing behavior unless logs show new $FC4000-$FC4003 activity.
```

## Retry/countdown path to `$049D=05`

Known path:

```asm id="9c34kd"
FB91AC move.b #$08,$04B0
FB91CE subq.b #1,$04B0
FB91D2 bne    FB91B2
FB91D4 move.b #$05,$049D
```

Interpretation:

```text id="e4yysn"
After repeated retry countdown, ROM writes $049D=05 and panel goes to PLEASE INSERT DISK.
```

## Important branch facts

Known branch PCs in media retry routine:

```text id="jowol9"
FB9182
FB9192
FB91A6
FB91BA
FB91C8
FB91D2
```

Escape from one loop appears to require `$049D == 0` at the relevant test, especially around `FB91A6`.

Do not treat one prompt as a single cause. There are multiple ways to reach `$049D=05` or `$049D=0D`.

## Current next boot-flow question

The next boot-flow question is no longer:

```text id="qjy9vs"
How do we mount raw ASR .img?
```

It is:

```text id="l7nvrn"
After 00bf1a and $0d06 set, which queue slot, lowmem flag, timer register, or hardware completion signal should change to make the dispatcher leave f87f9a idle?
```

Focus areas:

```text id="d5onxq"
dispatcher queue base $00c6
dispatcher queue end $00c8
active/current record $0b6a
slot byte2/byte3
slot 2 callback/context
last non-idle event before final f87f9a
$0d06/$0e82 readers/writers
FC6884/FC6894 completion behavior
```

## Current one-line boot-flow summary

The boot flow now reaches `LOADING SYSTEM` and executes an accepted-looking MC68302/FC68xx `0x2400` service sequence through vectors `0x4e/0x4f`; FC6814 clears, runtime code at `00bf1a` sets FC6816 `c080 -> e480`, `$0d06` is set, and FC6816 can be gated-cleared back to `c080`, but the loaded runtime still returns to dispatcher idle at `f87f9a`, so the next missing behavior is likely a dispatcher event, queue payload, timer/completion signal, or related lowmem state transition rather than the older `$049D`/FDC prompt paths.
