# ASR-10 runtime service model

Dispatcher-kön, servicefälten och MC68302:ans avbrottslivscykel som de observerats vid
körning. Utbrutet ur `memory-map.md` 2026-08-04; adress- och avkodningsfakta ligger kvar
där.

**Alla runtimeobservationer i det här dokumentet kommer från en V1.61-körning** och hör
till en historisk harness-utredning. Nuvarande flagglösa HEAD med V3.50 når
`KEYBOARD TUNED` och `FILE 1  TUTORIAL BNK`. Samma kod ligger i V3.50 på
RAM `$00E49A`-`$00E4D4`, inte på `$00BEF2`-`$00BF26`.

---

## Registerrättelser 2026-08-04

Registeridentiteterna nedan kommer nu ur MC68302 UM Table 2-9, korsvaliderade mot tre
kända skrivningar (WRR `$0000` på `$FC684A`, TRR2 `$3F01` på `$FC6852`, TCN2 `$FC6856`
som bara V3.50 läser).

| adress | står nedan som | **är** |
|---|---|---|
| `$FC6814` | "pending/status candidate" | **IPR** — gissningen stämde |
| `$FC6816` | "service/in-service candidate" | **IMR** — Interrupt Mask Register |
| `$FC6818` | "control/ack/EOI-ish candidate" | **ISR** — In-Service Register |
| `$FC6884` | "timer/control/reload candidate" | **SCM1** — SCC1 Mode Register |
| `$FC6894` | "timer/control/reload candidate" | **SCM2** — SCC2 Mode Register |

Fyra påståenden nedan blir därmed fel. De är kvar som historik, men gäller inte.

**1. `$2400` är inte en service-latch.** `$2400` = IMR bit 13 + bit 10 = **SCC1 + SCC2**.
`ori.w #$2400,($FC6816)` på `$00BF1A` **avmaskerar SCC1- och SCC2-avbrott**. Slutsatsen
nedan att *"clearing FC6816 0x2400 works mechanically, but does not advance boot"* är
väntad: att rensa IMR maskerar avbrottet, det kvitterar ingenting. **Spåret "rensa
FC6816" är avskrivet.** EOI sker till ISR (`$FC6818`), vilket OS:ets SCC-hanterare
redan gör korrekt.

**2. `$FC6884`/`$FC6894` är inte timers.** Enligt SCM-bitkartan (UM §4.5.3) är bit 3 =
ENR, bit 2 = ENT, bit 1-0 = MODE, bit 5-4 = DIAG.

```
$7033  =  MODE=11 (BISYNC/Transparent), ENT=0, ENR=0, DIAG=11    ROM $F8C0EC/$F8C0F4
$703B  =  MODE=11,                      ENT=0, ENR=1, DIAG=11    OS  $00BF00/$00BEF2
```

Skillnaden är exakt **ENR**. Sekvensen `$00BEF2`-`$00BF1A` blir sammanhängande: slå på
mottagaren på SCC2, slå på den på SCC1, avmaskera deras avbrott. Se
`mc68302-status.md` §3.

**3. `$00BF0E` är `4EB9 FFFF8ECA`** — `jsr` med **lång absolut** operand, inte
opcodeformen `jsr $8ECA.w`.

```
instruktionsoperand:          $FFFF8ECA
24-bitars effektiv adress:    $FF8ECA
logisk låg-RAM-slot:          $008ECA, endast under speglingshypotesen
```

Under den hypotesen pekar slotten på ROM `$F8E054` i V1.61 och på **OS-kod `$00ECB0`**
i V3.50 — ett runtime-observerat exempel på patchmekanismen. Grannslotarna `$8EBE` och
`$8EC4` är också omdirigerade till OS i V3.50.

**4. IPR och ISR är write-1-to-clear.** UM §3.2.5.2/§3.2.5.4: bitar som skrivs som nollor
påverkas inte.

```
skriv 1 -> rensa biten
skriv 0 -> lämna biten oförändrad
```

`andi.w #$dbff,(IPR)` skriver nollor i bit 13/10 och **ettor** i alla andra satta bitar —
den rensar alltså allt *utom* SCC1/SCC2. Att `$FC6814` observerats gå `240b -> 000b`
måste därför bero på IACK-rensningen (manualens normalväg i vektoriserad miljö) eller på
att emulerade IPR inte implementerar W1C.

**Kontrolluppgift kvar:** granska `mc68302.cpp` — implementerar IPR W1C, gör ISR det,
flyttar IACK pending till in-service på rätt punkt, sätts ISR-biten för källan som tas i
service, respekteras byte-lanes, ger read-modify-write hårdvarusemantik? Ett verkligt
modellfel kan finnas här, men **inget sådant är visat**. Dra inga slutsatser om modellen
innan implementationen är granskad.

---

## Runtime dispatcher / service fields

These fields are current-phase important. They are involved after the emulator reaches `LOADING SYSTEM`.

### Dispatcher queue pointers

#### `$00C6`

Dispatcher queue base pointer.

Used around:

```asm
f87f92: movea.w $00c6.w,A2
f87f96: move.b  $0002(A2),D0
f87f9a: move.b  $0003(A2),D1
f87f9e: cmp.b   D0,D1
```

Current interpretation:

```text
A queue slot appears pending when byte2 != byte3.
When byte2 == byte3, the dispatcher treats the slot as idle/equalized.
```

#### `$00C8`

Dispatcher queue end pointer.

Used around:

```asm
f87fc2: adda.w  #$0016,A2
f87fc6: cmpa.w  $00c8.w,A2
f87fca: bcs     f87f96
```

Current interpretation:

```text
Queue record stride is 0x16.
Dispatcher scans records from $00c6 to $00c8.
```

#### `$0B6A`

Active/current dispatcher record pointer candidate.

Observed at dispatcher RTE:

```asm
f87fbc: move.w A2,$0b6a.w
```

Current interpretation:

```text
$0b6a holds the active/current slot record when the dispatcher transfers control through RTE.
```

Current question:

```text
Which caller/branch returns from the slot 2 callback chain into f8ce00..f8ce46?
```

### Queue slot record fields

Current candidate layout for a 0x16-byte slot record:

```text
+0x02/+0x03  pending/equalized bytes
+0x06        callback / RTE PC
+0x0a        stacked SR
+0x0c        dispatch context / continuation, moved to D5 and cleared
+0x0e        extra context / USP-ish candidate
+0x10        secondary continuation/list pointer candidate
+0x12        companion state to +0x10
```

Baseline slot 2:

```text
slot base=002400
pending word=002402
+0x06/+0x08 frame PC=007308
+0x0a frame SR=0000
+0x10=002410
+0x12=002412
```

First-PC diagnostics showed dispatcher `f87fc0: rte` entered `007308` in baseline, with no immediate IACK observed.

## Runtime service flags

### `$0D06`

Current interpretation:

```text
Service status / service-complete / service-active flag candidate.
```

Observed behavior:

```text
f8c10c clears $0d06.
00bf22 sets $0d06 via ST $0d06.
```

Context:

```asm
00bf1a: ori.w #$2400,$00fc6816.l
00bf22: st     $0d06.w
```

Important caution:

```text
Exact meaning is unknown.
Do not treat it as confirmed complete/active semantics yet.
No proven consumption/clear of the new $0d06=ff00 before idle is known in the current sequence.
```

### `$0E82`

Current interpretation:

```text
Service argument / scratch / selector candidate.
```

Observed behavior:

```text
written as 0008
later cleared
later written as 0004
read into D0 at 00bf14
later changes 0004 -> 0010 after service path
```

Context:

```asm
00bf14: move.w $0e82.w,D0
00bf18: a000
```

Open question:

```text
What do values 0004, 0008, and 0010 mean?
Is 0010 a service lifecycle state, a callback state, or unrelated to slot offset +0x10?
```

## Runtime service setter around `00BF1A`

Confirmed runtime code:

```asm
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

Confirmed context:

```text
D0=00000004
$0e82=0004
$0d06=0000 before 00bf22
FC6884=703b
FC6894=703b
```

Current interpretation:

```text
This is a real runtime service/handshake routine.
The IACK handler does not directly set FC6816 0x2400.
Runtime code sets it later at 00bf1a.
A000 in this sequence is Line-A vector #10 to ROM f882ca; it writes D0.w to stacked SR/CCR, skips A000, and returns after the opcode.
```

## MC68302 / FC68xx current-phase fields

### `$FC6814`

Pending/status candidate.

Observed behavior:

```text
Synthetic/service source uses bit 0x2400.
FC6814 000b -> 240b when source injected.
FC6814 240b -> 000b clears naturally through firmware path.
```

Current interpretation:

```text
FC6814 bit 0x2400 is a pending/status bit for the accepted-looking 68302 service source.
```

Open questions:

```text
What exact MC68302 source does bit 0x2400 represent?
Is it timer-related, service-related, or a board-glue mirrored source?
```

### `$FC6816`

Service/in-service candidate.

Observed behavior:

```text
FC6816 c080 -> e480 at runtime PC 00bf1a.
The changed bit is 0x2400.
```

A gated experiment can clear it back:

```text
FC6816 e480 -> c080
```

but this does not advance boot.

Current interpretation:

```text
FC6816 bit 0x2400 may be an in-service/service-active/EOI latch, but it is not the sole blocker.
```

Current caution:

```text
No proven post-set firmware read/test of FC6816 0x2400 is known in the current service sequence.
Do not keep focusing on FC6816 clear unless a firmware path reads/tests it and gates producer/re-arm behavior.
```

Latest negative result:

```text
Clearing FC6816 0x2400 after:
- 00bf1a setter
- $0d06 set
- FC6814 clear
- later dispatcher RTE

works mechanically, but final state still returns to dispatcher idle at f87f9a.
```

### `$FC6818`

Control/ack/EOI-ish candidate.

Observed behavior:

```text
Vector 0x4e handler writes FC6818=4000.
Vector 0x4f handler writes FC6818=8000.
Runtime later writes FC6818=0080.
```

Current interpretation:

```text
FC6818 participates in accepted-looking IACK/service handler paths.
0x4e and 0x4f differ here, then converge.
```

Open questions:

```text
Is FC6818 an interrupt-control, acknowledge, EOI, mode, or source-select register?
What is the exact difference between 4000 and 8000?
```

### `$FC6884` and `$FC6894`

Timer/control/reload candidates.

Observed behavior before the `00bf1a` service setter:

```text
00bef2 writes FC6894=703b.
00bf00 writes FC6884=703b.
```

Related observed values:

```text
FC6850=003b
FC6852=3f01
FC6884=703b
FC6894=703b
```

Hypothesis:

```text
0x3b may be timer/count/period-related.
0x703b may be mode/control plus count/reload.
```

Open question:

```text
Should FC6884/FC6894 generate a later event or completion signal that re-arms the dispatcher queue?
```

## Important ROM/runtime service paths

### Common clear path

ROM path:

```asm
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

In current accepted IACK runs:

```text
FC6814 240b -> 000b clears naturally.
FC6816 later becomes e480 at 00bf1a.
No firmware-side path clears FC6816 0x2400 afterward.
```

Experimentally clearing FC6816 after the setter is possible but does not advance boot.

### Runtime service set path

Loaded runtime path:

```asm
00bf0e  jsr     $ffff8eca
00bf14  move.w  $0e82.w,D0
00bf18  a000
00bf1a  ori.w   #$2400,$00fc6816.l
00bf22  st      $0d06.w
00bf26  rts
```

This path is currently more important than older media/FDC lowmem state.

## Older boot/media lowmem fields

These fields were important during earlier boot/media/FDC path analysis. Keep them documented, but do not confuse them with the current dispatcher/service blocker.

### `$049D`

ROM status/prompt code.

Observed values:

```text
$049D=05 -> PLEASE INSERT DISK path
$049D=0D -> disk/controller/media error path
```

Important writes:

```text
FB7C9E writes $049D=05 after input/status gate fails
FB81B4 writes $049D=0D after FDC/media result rejection
FB91D4 writes $049D=05 after retry countdown reaches zero
```

### `$04B0`

Countdown/retry counter.

Known behavior:

```text
FB91AC move.b #$08,$04B0
FB91CE subq.b #1,$04B0
FB91D2 bne back while nonzero
FB91D4 writes $049D=05 when countdown expires
```

### `$04C6-$04CC`

FDC result/status storage area.

For command `0x46 Read Data` result interpretation:

```text
$04C6 high = ST0
$04C6 low  = ST1
$04C8 high = ST2
$04C8 low  = C
$04CA high = H
$04CA low  = R
$04CC high = N
```

### `$04AE`

Additional detail/status field.

Observed:

```text
$04AE=2B when FB81B4 writes $049D=0D from FDC/media rejection path
```

### `$04EE`

Input/panel state candidate.

Observed path:

```text
$04EE=FF triggers check at FB7C7A/FB7C84
ROM tests $FC4809 bit 4
if bit clear -> $04EE=00 -> $049D=05
if bit set   -> $04EE=01 -> input gate passed
```

## Notes on old FDC/media blocker

Earlier project focus included raw ASR disk-image recognition and FDC media path.

Those questions are still valid, but they are not the current immediate blocker if the emulator has already reached `LOADING SYSTEM`.

Keep FDC/media facts in:

```text
fdc.md
boot-flow.md
open-questions.md
```

Current immediate blocker belongs mostly to:

```text
dispatcher queue/event system
MC68302 service lifecycle
FC6884/FC6894 timer/control completion
lowmem service flags $0d06/$0e82
```

## Current one-line memory-map takeaway

The current important map is not just the broad MMIO map. It is the relationship between dispatcher lowmem pointers `$00c6/$00c8/$0b6a`, service flags `$0d06/$0e82`, MC68302-like registers `$FC6814/$FC6816/$FC6818`, timer/control candidates `$FC6884/$FC6894`, and the runtime service routine at `00bf1a` that sets `FC6816 |= 0x2400` before the machine returns to dispatcher idle at `f87f9a`.
