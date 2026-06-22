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
-> runtime service setter at 00bf1a runs
-> $0d06 is set
-> dispatcher returns to f87f9a idle
```

Current missing step:

```text id="j0i3h7"
No next queue event/payload/timer completion is observed after the service sequence.
```

Current best hypothesis:

```text id="z3d9yr"
The missing piece is likely dispatcher queue re-arm, event payload, timer cadence, FC6884/FC6894 completion behavior, or lowmem service state around $0d06/$0e82.
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
After the accepted-looking service sequence, no queue slot appears to become pending again.
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

Open questions:

```text id="8eh3qq"
What does $0d06 mean?
What does $0e82 mean?
What does A000 / LINE-A do in this runtime context?
What should happen after $0d06 is set?
```

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
