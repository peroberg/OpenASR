# MC68302 — statusöversikt per funktionsblock

Underlag: ROM:s och båda OS-versionernas **identifierade absoluta referenser** till
`$FC6000-$FC6FFF`, korsvaliderade mot MC68302 User's Manual (Table 2-9, §3.2.5, §4.5.3).

`Base = $FC6800`. **Varje räkning i det här dokumentet avser identifierade 32-bitars
absoluta referenser.** Registerrelativ adressering `(d,An)` syns inte — se
`methods-static-analysis.md` §7. Att den blinda fläcken är reell och inte teoretisk är
nu belagt: SCC-avbrottshanterarna laddar `movea.l #$00FC6880,A1` respektive
`#$00FC6890,A1` — SCC1:s och SCC2:s registerbaser — och adresserar sedan hela
registerblocket via A1. Ingen av de åtkomsterna finns i någon census nedan.

---

## 0. Registeridentiteter — rättelser

Adresser som tidigare dokumenterats som gissningar har nu namn ur manualen.
Rättelserna ändrar tolkningen av **en tidigare V1.61-baserad runtimeutredning** — inte
av nuvarande HEAD, som når `FILE 1  TUTORIAL BNK`. Se §3 "Vad detta INTE visar".

| adress | tidigare beskrivning | **faktiskt** | konsekvens |
|---|---|---|---|
| `$FC6814` | "pending/status candidate" | **IPR** — Interrupt Pending Register | gissningen stämde |
| `$FC6816` | "service/in-service candidate" | **IMR** — Interrupt Mask Register | **fel** — se §1 |
| `$FC6818` | "control/ack/EOI-ish candidate" | **ISR** — In-Service Register | delvis rätt; EOI sker hit |
| `$FC6884` | "timer/control/reload candidate" | **SCM1** — SCC1 Mode Register | **fel** — se §3 |
| `$FC6894` | "timer/control/reload candidate" | **SCM2** — SCC2 Mode Register | **fel** — se §3 |
| `$FC6829` | oidentifierad | **PBDAT låg byte** | analog mux-kanalval |
| `$FC68B2` | (jag skrev SIMODE) | **SIMASK** — Serial Interface Mask Register | **min egen felattribuering, rättad** |
| `$FC68B4` | (jag skrev SIMASK) | **SIMODE** — Serial Interface Mode Register | den tidigare projektanalysen hade rätt |

### SIMASK/SIMODE — låst mot manualen

Samma behandling som SCM-konflikten fick, för att inte en andra felattribuering ska
överleva. MC68302 UM, Table 2-9, kolumnvis:

```
Base + 8B2   SIMASK   16 bit   SI   Serial Interface Mask Register   reset $FFFF
Base + 8B4   SIMODE   16 bit   SI   Serial Interface Mode Register   reset $0000
```

Instruktionen som skriver `$4189`:

```
ROM  $F8C0CE   33FC 4189 00FC68B4   move.w #$4189,($00FC68B4).l    ; SIMODE, 16 bit
V161 OS+0x11884                     samma instruktion, samma värde
V350 OS+0x13E0C                     samma instruktion, samma värde
```

`$4189` går alltså till **SIMODE**, inte SIMASK. Den tidigare projektanalysen som kallade
`$4189` för SIMODE var korrekt; det var min tabell i förra omgången som var omvänd.
**SIMASK (`$FC68B2`) har noll identifierade absoluta referenser.**

Offsetkartan verifierades mot tre kända skrivningar innan den användes:
WRR ← `$0000` på `$FC684A`, TRR2 ← `$3F01` på `$FC6852`, och TCN2 (`$FC6856`) som
läses en gång i V3.50 och aldrig i V1.61 — exakt vad Timer 2-utredningen redan visat.

---

## 1. Avbrottsstyrning (GIMR / IPR / IMR / ISR)

### Bitkarta (identisk för IPR, IMR och ISR)

```
15 PB11   14 PB10   13 SCC1   12 SDMA   11 IDMA   10 SCC2
 9 TIMER1  8 SCC3    7 PB9     6 TIMER2   5 SCP     4 TIMER3
 3 SMC1    2 SMC2    1 PB8     0 ERR
```

### Verified

* ROM skriver GIMR ← `$8040`, IMR ← `$0000`, ISR ← `$FFFF`, IPR ← `$FFFF` på `$FB8E7E`.
* IMR = 0 vid boot betyder **inte** att källorna är avstängda — bara att de inte når kärnan.
* **`$2400` = bit 13 + bit 10 = SCC1 + SCC2.**

### Vad `ori.w #$2400,($FC6816)` faktiskt gör

Runtime-rutinen på `00BF1A`:

```asm
00bf0e  jsr     $FFFF8ECA          ; abs.l; se noten under blocket
00bf14  move.w  $0e82.w,D0
00bf18  a000                       ; Line-A: set_sr(D0)
00bf1a  ori.w   #$2400,$00fc6816.l ; IMR |= SCC1|SCC2   -> AVMASKERA
00bf22  st      $0d06.w
00bf26  rts
```

Detta är **inte** en in-service-latch. Det är OS:et som **avmaskerar
SCC1- och SCC2-avbrott**. Motsvarande i ROM:s rensningsväg på `$F8C0FC`:

```asm
f8c0fc  andi.w #$dbff,$00fc6816.l  ; IMR &= ~(SCC1|SCC2)  -> MASKERA
f8c104  andi.w #$dbff,$00fc6814.l  ; IPR
```

`$DBFF = ~$2400`.

### Semantisk varning: IPR och ISR är write-1-to-clear

Manualen, §3.2.5.2 och §3.2.5.4: *"the user should clear the corresponding bit by
writing a one to that bit ... bits that are written as zeros will not be affected."*

`andi.w #$dbff,(IPR)` är en read-modify-write som skriver **nollor** i bit 13 och 10 och
**ettor** i alla andra bitar som var satta. Under write-1-to-clear rensar den alltså
allt **utom** SCC1/SCC2 — motsatsen till den naiva läsningen.

Att `$FC6814` observerats gå `240b → 000b` i harnesset betyder därför antingen att
(a) rensningen sker via IACK, vilket manualen anger som normalvägen i en vektoriserad
miljö, eller (b) att emulerade IPR inte implementerar write-1-to-clear.
**Detta bör kontrolleras mot `mc68302.cpp` innan fler slutsatser dras om
service-livscykeln.**

Samma fråga gäller ISR (`$FC6818`), där EOI ska ske genom att skriva en etta.

### Open

* Vilken av bitarna 4/6/9 (TIMER3/TIMER2/TIMER1) som någonsin avmaskeras.
* Om `mc68302.cpp` implementerar W1C för IPR och ISR.
* GIMR `$8040` bitvis betydelse.

---

## 2. Timers och watchdog

| register | adress | ROM | V161 | V350 | status |
|---|---|---|---|---|---|
| TMR1 | `$FC6840` | 0 | 0 | 0 | inga absoluta referenser |
| TRR1 | `$FC6842` | 0 | 0 | 0 | |
| WRR | `$FC684A` | 1 | 0 | 0 | ROM skriver `$0000` |
| TMR2 | `$FC6850` | 1 | 0 | 0 | `$003B` |
| TRR2 | `$FC6852` | 1 | 0 | 0 | `$3F01` |
| TCN2 | `$FC6856` | 0 | 0 | **1** | endast V3.50 läser den |

### Verified

* Timer 2 är aktiverad (`RST=1`, `ICLK=01` masterklocka), periodisk modulo `$3F01`
  (~1,0081 ms vid 16 MHz), ORI=1.
* IMR bit 6 (TIMER2) avmaskeras aldrig av någon identifierad ROM-skrivning.
* TOUT2 är **inte** routad: `PBCNT = $0080` lämnar bit 6 klar, så PB6 förblir GPIO.
* V3.50 läser TCN2 exakt en gång och lagrar till `(A2+$10)`. V1.61 rör inte Timer 2.

### Open

* Watchdog: vad `WRR = $0000` innebär, vad som startar räknaren, om WCN någonsin läses,
  WDOG-utgångens polaritet och open-drain-status, och vart på kortet den går.
  Ingen av dessa frågor är besvarad. `WRR` skrivs en gång och rörs sedan aldrig.
* Vad V3.50 använder TCN2-avläsningen till — vilken rutin, vilken anropare.

---

## 3. Kommunikationsprocessorn och SCC1–3

Detta block var centralt i en tidigare V1.61-baserad blockerutredning och är fortfarande
arkitektoniskt viktigt. Se "Vad detta INTE visar" nedan innan slutsatser dras om
nuvarande boot.

### SCM-bitkarta (manual §4.5.3)

```
15..6  protokollspecifika bitar
 5..4  DIAG1-DIAG0    00 = normal drift (CTS/CD under automatisk kontroll)
 3     ENR            Enable Receiver
 2     ENT            Enable Transmitter
 1..0  MODE1-MODE0    00=HDLC  01=Asynkron (UART/DDCMP)
                      10=Synkron DDCMP/V.110  11=BISYNC / Promiscuous Transparent
```

### Observerade skrivningar

| plats | register | värde | avkodat |
|---|---|---|---|
| `$F8C0EC` (ROM) | SCM1 `$FC6884` | `$7033` | MODE=11, ENT=0, **ENR=0**, DIAG=11 |
| `$F8C0F4` (ROM) | SCM2 `$FC6894` | `$7033` | samma |
| `$00BF00` (OS runtime) | SCM1 `$FC6884` | `$703B` | MODE=11, ENT=0, **ENR=1**, DIAG=11 |
| `$00BEF2` (OS runtime) | SCM2 `$FC6894` | `$703B` | samma |

**Skillnaden mellan `$7033` och `$703B` är exakt bit 3 = ENR.**

Det besvarar den länge öppna frågan om ENR/ENT sätts efter de absoluta SCM-skrivningarna:
vid körning sätts **ENR men inte ENT** på både SCC1 och SCC2. Maskinen slår alltså på
två mottagare men ingen sändare.

### Sekvensen blir sammanhängande

```
00bef2  SCM2 ← $703B          ENR på SCC2
00bf00  SCM1 ← $703B          ENR på SCC1
00bf0e  jsr $FFFF8ECA         abs.l — se noten under blocket
00bf14  D0 ← ($0E82).w
00bf18  a000                  set_sr(D0)
00bf1a  IMR |= $2400          avmaskera SCC1 + SCC2
00bf22  ($0D06).w ← $FF
00bf26  rts
```

Först aktiveras mottagarna, sedan avmaskeras deras avbrott. Det är en lärobokssekvens
för att sätta igång seriell mottagning.

**Notation för `$00BF0E`.** Instruktionen är `4EB9 FFFF8ECA`, alltså `jsr` med **lång
absolut** operand. Skriv genomgående:

```
instruktionsoperand:          $FFFF8ECA
24-bitars effektiv adress:    $FF8ECA
```

Adressen förväntas nå bindningsslot `$008ECA` **endast om speglingen
`$FFxxxx ↔ $00xxxx` gäller** — vilket ännu inte är verifierat. Det är precis därför den
här instruktionen är E2:s testfall.

### Avbrottshanterarna finns och är kompletta

På identisk OS-offset i **båda** versionerna (`OS+0x08356`, `OS+0x08392`), alltså
RAM `$008D56` och `$008D92` under segment 1-regeln:

```
SCC1 ISR $008D56   movem.l ... / movea.l ($12D8).w,A2      ; kontrollblock
                   movea.l #$00FC6400,A0                   ; SCC1 parameter-RAM
                   movea.l #$00FC6880,A1                   ; SCC1 registerbas
                   jsr $00643C                             ; gemensam mottagningsrutin
                   move.w #$2000,(ISR)                     ; EOI bit 13
                   ... tst ($016F).w / ($0D04).w ... rte

SCC2 ISR $008D92   samma form, ($1320).w, $FC6500, $FC6890,
                   jsr $00643C, move.w #$0400,(ISR)        ; EOI bit 10
```

Det verifierar oberoende av manualen att `$FC6818` är ISR, att bit 13 är SCC1 och bit 10
är SCC2, att firmware använder **vektoriserade** SCC-avbrott (inget IPR-pollande behövs),
och att parameter-RAM ingår i mottagningsvägen.

### Initieringen fasas mot PB3/LRCLK

Sekvensen inleds med en pollande vänteloop:

```
btst #3,($00FC6829).l     ; PBDAT bit 3 = PB3
beq.s <tillbaka>          ; vänta tills PB3 = 1
dbra  (D5 varv)           ; fördröjning
SCM2 ← $703B              ; ENR på SCC2
dbra  (D6 varv)           ; andra fördröjningen
SCM1 ← $703B              ; ENR på SCC1
```

PB3 är sedan tidigare identifierad som LRCLK-ingången (ROM `$F8C142`, flankdetektor med
`$7FFF`-timeout = ERROR 009). `PBDDR = $F097` lämnar bit 3 som ingång och
`PBCNT = $0080` lämnar den som GPIO.

Att mottagarna aktiveras efter en LRCLK-flank, med två separata registerhållna
fördröjningar, ger **positiv evidens** — inte bara uteslutning — för att SCC1/SCC2 hör
till ljudsektionens seriella dataflöde.

### Vad detta INTE visar  [viktigt]

Den observerade runtime-körningen bakom `$00BEF2`-`$00BF26` är en **V1.61-körning** och
hör till en historisk harness-/blockerutredning. Nuvarande flagglösa HEAD med V3.50 når
`KEYBOARD TUNED` och `FILE 1  TUTORIAL BNK`.

Skriv därför inte att SCC1/SCC2 är den nuvarande blockeraren. Korrekt formulering:

> I den dokumenterade V1.61-runtimekörningen aktiverades SCC1/SCC2 och deras avbrott
> avmaskerades. Firmwarekedjan fram till kompletta SCC-handlers är statiskt verifierad.
> Det är ännu inte dynamiskt visat att ett förväntat SCC-avbrott uteblev, eller att det
> blockerade just den körningen.

En komplett statisk firmwareväg bevisar inte att den fysiska eller emulerade producenten
levererar signalen. `Verified` gäller firmware; `Open` gäller hårdvarusidan.

Däremot gäller: **att jaga rensning av `$FC6816` är fel spår.** Registret är IMR — att
rensa det maskerar avbrottet, det kvitterar ingenting. EOI sker till ISR, och
hanterarna gör det redan korrekt.

### SCC3 körs i ett annat läge, och bara av ROM

```
ROM $F8C118  SCON3 ← $0020
ROM $F8C128  SCCE3 ← $FF        (move.b, rensa alla event)
ROM $F8C130  SCCM3 andi.b #$40
ROM $F8C138  SCM3  ← $0038      MODE=00 HDLC, ENT=0, ENR=1, DIAG=11
```

**SCC3 konfigureras för HDLC**, till skillnad från SCC1/SCC2 som körs i
BISYNC/Promiscuous Transparent. Ingen OS-version rör SCC3 absolut. Vad SCC3 används till
är [OPEN].

### CP-kommandona är avkodade  [Verified]

CR ligger på `Base+860`, är **8 bitar på D15–D8** (jämn byte-lane) och har formatet:

```
7 RST │ 6 GCI │ 5-4 OPCODE │ 3 res │ 2-1 CH.NUM │ 0 FLG
OPCODE  00 STOP TRANSMIT · 01 RESTART TRANSMIT · 10 ENTER HUNT MODE
        11 Reset receiver BCS generator (endast BISYNC)
CH.NUM  00 SCC1 · 01 SCC2 · 10 SCC3
```

Handskakningsrutinen och dess två ROM-anropare:

```
$F8C16C  2478 12D8            movea.l ($12D8).w,A2      ; SCC1 kontrollblock
$F8C170  207C 00FC6400        movea.l #$00FC6400,A0     ; SCC1 parameter-RAM
$F8C176  267C 00FC6880        movea.l #$00FC6880,A3     ; SCC1 registerbas
$F8C17C  4EB8 8D50            jsr    $FFFF8D50          ; bindningsslot -> $F8C1BE
$F8C180  7021                 moveq  #$21,D0
$F8C182  6100 001E            bsr.w  $F8C1A2
$F8C186  4E75                 rts

$F8C188  2478 1320            movea.l ($1320).w,A2      ; SCC2 kontrollblock
$F8C18C  207C 00FC6500        movea.l #$00FC6500,A0     ; SCC2 parameter-RAM
$F8C192  267C 00FC6890        movea.l #$00FC6890,A3     ; SCC2 registerbas
$F8C198  4EB8 8D50            jsr    $FFFF8D50          ; samma slot
$F8C19C  7023                 moveq  #$23,D0
$F8C19E  6102                 bsr.s  $F8C1A2
$F8C1A0  4E75                 rts

$F8C1A2  0839 0000 00FC6860   btst #0,(CR)              ; FLG
$F8C1AA  66F6                 bne.s  $F8C1A2            ; vänta tills CP är klar
$F8C1AC  13C0 00FC6860        move.b D0,(CR)            ; skriv kommandot
$F8C1B2  0839 0000 00FC6860   btst #0,(CR)
$F8C1BA  66F6                 bne.s  $F8C1B2            ; vänta på kvittens
$F8C1BC  4E75                 rts
```

Avkodning:

```
$21 = 0010 0001   OPCODE=10 ENTER HUNT MODE, CH.NUM=00 SCC1, FLG=1
$23 = 0010 0011   OPCODE=10 ENTER HUNT MODE, CH.NUM=01 SCC2, FLG=1
```

**ROM utfärdar ENTER HUNT MODE till SCC1 och SCC2.** Det är exakt vad manualen kräver
före att ENR sätts: *"To restart reception, the ENTER HUNT MODE command should be issued
before ENR is set again."* Kedjan ENTER HUNT MODE → ENR → avmaskera i IMR är därmed
komplett och i rätt ordning.

Båda OS-versionerna har **en** anropare, och den skickar ett annat kommando:

```
V161 OS+0x1187A / V350 OS+0x13E02
   4EB9 FFF8C0E6        jsr $FFF8C0E6
   303C 0081            move.w #$0081,D0
   4EB9 FFF8C1A2        jsr $FFF8C1A2

$81 = 1000 0001   RST=1, FLG=1  ->  CP SOFTWARE RESET
```

OS:et gör alltså en **CP-mjukvarureset** innan det skriver SIMODE och SCON.

### `($8D50).w` är stängd

Den länge öppna frågan om `($8D50).w` i SCC-koden har sitt svar här: `$F8C17C` och
`$F8C198` gör `jsr $FFFF8D50`, alltså bindningsslot `$8D50.w`, som enligt tabellen pekar
på ROM `$F8C1BE` i båda versionerna — instruktionen direkt efter handskakningens `rts`.
Det är en SCC-konfigurationshjälprutin som ROM når via tabellen i stället för direkt.

### Open

* Vad SCC1 och SCC2 är anslutna till fysiskt, och vem sändaren är. MIDI och panel går via
  DUART:en. LRCLK-fasningen pekar mot ljudsektionen — **[Likely]**, ej verifierat.
* Om ett SCC-avbrott faktiskt uteblir i den relevanta körningen.
* Vad `$00643C` producerar, och vad `($016F).w` / `($0D04).w` grindar.
* Buffer descriptor-innehåll och CP-state — DPRAM kan inte läsas statiskt.
* Vad DIAG=11 innebär (00 = normal drift).
* Vad SCC3:s HDLC-kanal används till.

---

## 4. PIO (port A och B)

### Verified

ROM:s init på `$FB8E06`:

```
PACNT ($FC681E) ← $E000
PADDR ($FC6820) ← $FFFF
PBCNT ($FC6824) ← $0080
PBDDR ($FC6826) ← $F097
PADAT ($FC6822) ← $18FC
PBDAT ($FC6828) ← $0007
```

**`$FC6829` (PBDAT låg byte) är den mest refererade 68302-adressen i OS:et** — 12
referenser i V1.61, 14 i V3.50, mot 2 i ROM. PAR-kalibreringen gör
`andi.b #$F8` följt av `ori.b #<kanal>` — dvs **PB2–PB0 är kanalvalet till den analoga
multiplexern** framför ES5506:ans PAR-ingång.

`PBCNT = $0080` lämnar bit 6 klar ⇒ PB6 är GPIO ⇒ TOUT2 är inte routad.

### Open

* Vad de övriga bitarna i `PBDDR = $F097` och `PADAT = $18FC` styr.
* PB8–PB11 är avbrottskällor (IPR/IMR bit 1, 7, 14, 15). **ROM avmaskar PB9, PB10 och
  PB11 genom `IMR |= $C080` på `$F87F0A`.** Endast PB8 (bit 1) förblir maskerad i alla
  identifierade absoluta ROM-skrivningar. Vad de tre avmaskade stiften är fysiskt
  anslutna till är [OPEN] — de är de enda avbrottskällor ROM självt släpper fram.

### PB9/PB10/PB11 statisk uppföljning 2026-08-10

[Verified] ROM:s PB10- och PB11-handlers ligger kvar vid de äldre
handlerkandidaterna:

```
PB10 $F88F06  tst.b  $0C3A.w
              move.b #$0C,$0C3A.w
              move.b #$01,$0C36.w
              move.w #$4000,$FC6818.l
              rte

PB11 $F88F22  tst.b  $0C3B.w
              move.b #$0C,$0C3B.w
              move.b #$01,$0C37.w
              move.w #$8000,$FC6818.l
              rte
```

Den strukturella likheten mellan `$0C3A/$0C36` och `$0C3B/$0C37` är [Likely]
ett gemensamt debounce- eller periodiskt service-mönster, men konsumenten är
inte verifierad.

En absolut och kort-absolut sökning i ROM samt V1.61/V3.50 identifierade ingen
direkt dekrementerare för `$0C3A/$0C3B` och ingen direkt konsument för
`$0C36/$0C37` utanför handlerns egna skrivningar. Det betyder endast: noll
identifierade referenser inom den använda metoden. `(d,An)` och andra
registerrelativa former täcks inte av den sökningen.

[Verified] PB9 skiljer sig från PB10/PB11. ISR-bit `$0080` kvitteras från flera
platser, bland annat ROM `$F8CFE0`, `$F8CFF0`, `$F8D076` samt OS-kod kring
V350 RAM `$014862` / V161 RAM `$014A18` enligt segment-2-mappningen. Den
omgivande koden pekar mot audio/ES5506-service, men PB9:s fysiska källa och
eventuella flaggkonsument är fortfarande [OPEN].

---

## 5. Block utan identifierade absoluta referenser

Rubriken är avsiktligt formulerad så. **"Inga identifierade absoluta referenser" är inte
samma sak som "används inte"** — SCC-hanterarna visar konkret att block nås
registerrelativt via en laddad bas.

| block | register | identifierade absoluta ref. | status |
|---|---|---|---|
| IDMA | CMR/SAPR/DAPR/BCR/CSR/FCR | 0 | [OPEN] |
| SCP | SPMODE `$FC68B0` | 0 | [OPEN] |
| SMC1/SMC2 | — | 0 | [OPEN] |
| SDMA | — | 0 | [OPEN] |
| SIMASK | `$FC68B2` | 0 | [OPEN] |
| SIMODE | `$FC68B4` | 1 i vardera, värde `$4189` | delvis känt |
| **Parameter-RAM** | `$FC6400` SCC1, `$FC6500` SCC2 | **ROM 1+1, V161 1+1, V350 1+1** | **belagd i mottagningsvägen** |
| Buffer descriptors | i DPRAM | — | [OPEN], innehållet kan inte läsas statiskt |

**Parameter-RAM är inte längre utan belägg.** ROM (`$F8C174`, `$F8C190`) och båda
OS-versionerna (`OS+0x08362`, `OS+0x0839E`) laddar `$00FC6400` respektive `$00FC6500`
som pekare, och SCC-avbrottshanterarna använder dem i varje mottagning. Ytterligare
absoluta referenser finns till `$FC6404`, `$FC6600`, `$FC6608`, `$FC6700`, `$FC6704`,
`$FC6710` — alltså även SCC3:s parameterområde och ett block till.

**SIMODE (`$FC68B4`) skrivs `$4189` från både ROM och OS**, i båda fall omedelbart efter
ett anrop till CP-handskakningen `$FFF8C1A2` — i OS:ets fall med kommandot `$81`,
CP software reset. Den tidigare noteringen att "SIMODE/SIMASK skrivs före den kritiska
sektionen" gäller alltså bara **SIMODE**. SIMASK (`$FC68B2`) har noll identifierade
absoluta referenser och kan skrivas registerrelativt, eller inte alls.

---

## 6. Prioriterad ordning härifrån

1. **Ta reda på vad SCC1/SCC2 är kopplade till.** LRCLK-fasningen ger riktningen; det
   som saknas är fysisk verifiering och en identifierad sändare.
2. **[Verified] W1C-semantiken för IPR och ISR är inte implementerad i HEAD.**
   `src/devices/machine/mc68302.h` anger att detta steg saknar interruptcontroller,
   timer, IDMA och kommunikationsprocessor. `mc68302.cpp::classify_offset()` klassar
   `$FC6812-$FC6819` (`GIMR/IPR/IMR/ISR`) som `known_unimplemented`; värdena är därför
   shadow storage, inte MC68302-registersemantik.

   ```
   0x0400-0x07ff  SCC/SMC parameter RAM      known_unimplemented
   0x0800-0x0811  IDMA                       known_unimplemented
   0x0812-0x0819  GIMR/IPR/IMR/ISR           known_unimplemented
   0x081e-0x0823  Port A                     known_unimplemented
   0x0840-0x084d  Timer 1 + watchdog         known_unimplemented
   0x0850-0x085a  Timer 2                    known_unimplemented
   0x0880-0x08b5  SCC1-3 / SMC / SCP         known_unimplemented
   ```

   Slutsats: samtliga interna interruptkällor saknar i dag fungerande modell. Den enda
   fungerande avbrottsvägen vid HEAD är extern IRQ6 via `irq6_ack_vector()`. Det ger en
   gemensam förklaring till att panelvägen fungerar medan SCC, Timer 2 och PB9-PB11
   framstår som inaktiva.
3. **PB9, PB10 och PB11 — vad de är anslutna till.** `IMR |= $C080` på `$F87F0A` gör
   dem till de **enda interna 68302-källor ROM självt avmaskar**, och det sker före
   hela SCC-vägen. Ingen av de tre är identifierad. Detta är inte en restfråga.
4. **Följ CP-reset- och HUNT-sekvenserna till descriptorinitialiseringen** och
   dokumentera buffer descriptors samt mottagningsstate. Kommandoavkodningen är klar;
   nästa steg är vad kommandona verkar på.
5. Parameter-RAM-innehåll och buffer descriptors — kräver DPRAM-dump vid körning.
6. Watchdog: WRR/WCN/WDOG-kedjan, som fortfarande är helt outredd.
7. SCC3:s HDLC-kanal — konfigureras av ROM, används av ingen identifierad kod.
