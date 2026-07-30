# ASR-10 i MAME — plan

Kanonisk plan per 2026-07-29. Ersätter `roadmap.md` och de daterade
handoff-dokumenten som riktningsgivare. De får ligga kvar som historik,
men det är den här filen som gäller.

Premiss: målet är en ASR-10 som låter och känns som en ASR-10, med
ES5506 och ES5510. Det gör MAME till destination, inte referens.

## 0. Faktiska namn i det här trädet

Korrigerat mot baslinjen 2026-07-29 (`docs/asr10/baseline.md`).

* Binären heter **`mess`**, inte `mame`.
* Maskinen som utvecklas heter **`asr10booth`** och ligger i
  `src/mame/ensoniq/asr10_boot.cpp`.
* `esqasr.cpp` med upstream-maskinen `asr10` **byggs inte** med nuvarande
  `SOURCES=`-filtrering.

**Två körlägen. Blanda dem aldrig.** Se `docs/asr10/running.md`.

```sh
# no-media: "PLEASE INSERT DISK" är FÖRVÄNTAT, inte ett fel
./mess asr10booth -video none -sound none -nothrottle -seconds_to_run 30 -log

# med media: den djupa bootkedjan
./mess asr10booth -flop floppies/asr10booth/V161.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

Med media når booten (enligt tidigare sessioner):
`ENSONIQ ASR-10` → `LOADING SYSTEM` → `TUNING KBD HANDS-OFF` →
`KEYBOARD TUNED` → `NO INST OR BANK FILES`.

**Varje ändring ska verifieras mot BÅDA lägena.** `docs/asr10/baseline.md`
är endast no-media och är därmed en otillräcklig referens.

**Varning för raderingslistan:** tidigare handoff-dokument kör med
`ASR10_DIAG_IRQ6_DUART_PANEL=1`, `ASR10_EXPERIMENT_ROUTE_DUART_INTRN_TO_IRQ6=1`
och `ASR10_EXPERIMENT_DUART_CHB_RX_FIFO=1`. Några `ASR10_EXPERIMENT_*` kan
alltså vara bärande för den djupa vägen. Kontrollera mot med-media-körning
innan något av dem tas bort.

**Konsekvens som ändrar planen:** `asr10_boot.cpp` är inte ett harness
bredvid en drivrutin — det *är* drivrutinen, en parallell fork av
maskinen. Den kan alltså inte raderas, bara konvergera. Slutmålet är att
`asr10booth` gradvis blir det som `esqasr.cpp`s `asr10`-maskin borde vara:
MC68302 + `mc68681` + `upd72069` + `es5506` + `es5510`, utan
instrumentering, och därmed möjlig att lyfta tillbaka till `esqasr.cpp`
och skicka upstream. Raderingslistan gäller instrumenteringen och de
handmodellerade enheterna, inte filen.

**Öppen fråga som fas 2 och 4 vilar på:** Lua når `bpset`/`wpset` bara med
debuggern aktiverad, men `-debug` startar den interaktiva debuggern som
hänger headless (kräver `kill -9` enligt baslinjen). Verifiera att
`-debug -debugger none` ger Lua-åtkomst utan interaktivt UI **innan**
något byggs på Lua-spåret. Faller det behöver fas 4 en annan väg in.

---

## 1. Vad som faktiskt saknas

Verifierat i det här trädet 2026-07-29.

```
src/mame/ensoniq/esqasr.cpp:125
    M68000(config, m_maincpu, XTAL(16'000'000)); // actually MC68302
```

ASR-10-maskinen kör en naken 68000 utan en enda SIB-funktion. Det är
hela hålet. `src/devices/machine/` innehåller 68153bim, 6821pia,
68230pit, 68307, 68340 — ingen 68302.

Allt annat finns färdigt:

| Funktion | Enhet i MAME | Rader |
|---|---|---|
| DUART (SCN2681, U20) | `machine/mc68681.cpp` | 1 895 |
| FDC (uPD72069, U34) | `machine/upd765.h` → `upd72069_device` | i upd765 |
| OTTO / ES5506 (U29) | `sound/es5506.cpp` | 2 134 |
| ESP / ES5510 (U43) | `cpu/es5510/es5510.cpp` | 1 288 |
| Panel / VFD | `ensoniq/esqpanel.cpp`, `esqvfd.cpp` | — |

### U41 (ES5701 SuperGLU) och U5 (custom PAL)

Enligt `superglu-investigation-2026-06-29.md` och `es5701-wiring.md`,
med evidenstaggning i original:

* **ES5701 SuperGLU (U41)** — `[Verified]` ingen minnesdekoder, inga
  chip selects, känner inte till FDC:n. Gör bussöversättning CPU↔ESP och
  CPU↔OTIS/OTTO, latchar sampleadresser, maskar databussen för
  8/12/13/16-bitars samples, delar klocka, ger DTACK till ESP.
  **Inte på bootens kritiska väg.** Ligger på ljudets kritiska väg, men
  MAME fördelar dess funktioner över kretsar som redan finns:
  `esqpump.h` / `esq_5505_5510_pump_device` (bunden som
  `device_sound_interface`, aldrig i adressrymden) och sampleformat­
  hanteringen inuti `es5506_device`. **U41 behöver ingen egen
  MAME-enhet.** Frågan är stängd; återupptas först när ljud förväntas.
  Primärkälla nu i trädet: `docs/asr10/sources/es5701.vhd` med varningar
  i `sources/README.md` (aldrig simulerad, entiteten felstavad `es5571`,
  identiska villkor för läs och skriv).
  Två noteringar att bära med sig till dess: ES5701 maskar **låga byten**
  vid CPU-läsning från OTTO (i 8-bitsläge läses den som noll), och
  `esp_dtack <= '0' when esp_cs='0'` — DTACK till ESP asserteras omedelbart
  via open collector, med källans egen anmärkning att pulsförlängning kan
  behövas. Om ROM:en väntar på ESP-DTACK och MAME:s `es5510` kvitterar
  annorlunda blir det en hängning som inte ser ut som ett timingproblem.
* **ES5570 GLU** — VFX-SD/TS-familjens adressdekoder. `[DISPROVEN]` som
  ASR-10:s karta; ASR-10 har egen dekod via U5 + 74HC138/139.
* **U5, custom PAL "ASR-10 V1.1 6457"** — `[OPEN]`. Detta är ASR-10:s
  faktiska adressdekoder och det verkliga hålet vid sidan av 68302:an.
  Citat ur `es5701-wiring.md`: "that PAL has not been reverse engineered
  in any doc in this tree". De observerade FC-prefixade fönstren
  (FC2001, FC2D40-FC2D7F, FC3000-FC31FF) hör hit, inte till ES5701.

## 2. Mätningar som motiverar prioriteringen

`asr10_boot.cpp` 10 832 rader, `asr10_boot_codex.cpp` 5 878 rader,
mot ett upstream-skelett `esqasr.cpp` på 228 rader.
Hela 68307-familjen i MAME — device, SIM, buss, timers, headers — är
**1 327 rader**. Det är storleksordningen en komplett MC68302-klassad
SIM ska ha.

`asr10_boot.cpp` handmodellerar 2681:ans counter/timer parallellt med
att `mc68681.cpp` finns i trädet:

```
m_duart_counter_timer, m_duart_ctu_preload, m_duart_ctl_preload,
m_duart_acr, m_duart_counter_start_count, m_duart_counter_fire_count,
m_duart_panel_asr_shadow[0x10], m_duart_vfx_shadow[0x10]
ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 = true
```

Sidoprojektet `~/develop/mc68302` (Moira): 15 117 rader src+tests,
varav `tests/Asr10RomIntegrationTest.cpp` ensam är 2 987. Byggt som
`CMAKE_BUILD_TYPE=Debug` utan optimering — allt som upplevts som
långsamt där har mätts i en ooptimerad build av misstag.

Benchmark mot det repot, 100 000 instruktioner, Debug -O0:

```
ren exekvering                        367 000 instr/s
full TraceRecorder::executeStep()     243 000 instr/s   1,5x
 + ExecutionBlockBuilder              191 000 instr/s   1,9x
```

Slutsats: instrumenteringen är **inte** flaskhalsen. Testsvitens
instruktionsbudget är 50 410 000, varav två testfall står för
50 000 000 (99,2 %). Ett av dem är taggat `[.]` (dolt), det andra inte:

```
rad 1811  "[asr10-rom][integration][sib-guard][long][.]"   25M, dold
rad 2383  "[asr10-rom][integration][duart-timer]"          25M, körs
```

## 3. Hypotes: DUART X1 är 4,000 MHz, inte 3,6864

Nuvarande modell antar `X1 = 3.6864 MHz`, markerat i README som
obekräftat. Komponentlistan från fysisk ASR-10 har tre kristaller:

```
Y1  16.000000 MHz   systemklocka (MC68302FC16C, U28)
Y2  30.476180 MHz   OTTO / ES5506
Y3  33.868800 MHz   ljudklocka, 768 x 44,1 kHz
```

Ingen 3,6864 MHz-kristall finns på kortet. Däremot fyra binärräknare:
U16, U17, U21 (MC74HC161AN) och U47 (SN74F161AN). X1 är alltså härledd.
16,000 / 4 = 4,000 MHz ligger på SCN2681:ans övre gräns.

Med CTUR:CTLR = 0x07D0 = 2000:

```
2 x 2000 / 3 686 400 = 1,0851 ms    nuvarande antagande
2 x 2000 / 4 000 000 = 1,0000 ms    vid X1 = 4 MHz
```

En OS-schemaläggare tickar rimligare på jämn millisekund. Testbart och
gratis att prova i fas 1.

**Mekanism, belagd i primärkälla.** `docs/asr10/sources/es5701.vhd`
(Buchtys rekonstruktion) visar att ES5701 tar in 16 MHz och delar med två:

```vhdl
process(clk16) begin
    if falling_edge(clk16) then c16 <= not(c16); end if;
end process;
clk8 <= c16;
```

Kortet har Y1 = 16,000 MHz och gott om vippor för ytterligare en delning
(74HC74 på U13/U38/U39, 74F74 på U50/U64). Kedjan 16 → 8 → 4 MHz till
SCN2681:ans X1 har därmed en trovärdig mekanism, inte bara aritmetik.
Fortfarande `[Hypothesis]` tills den mätts.

Övrigt ur komponentlistan: **U5 "ASR-10 V1.1 6457"**, 20 pinnar, är
nästan säkert PAL:en för CS3:ans sekundäravkodning (FDC 0xFC4000,
DUART 0xFC4800, SCSI-kandidat 0xFC5000). **U42, 6N138**, är
MIDI-in-optokopplare — se fas 2.

---

## 4. Faser

### Fas 0 — miljö (timmar)

* Sätt `CMAKE_BUILD_TYPE` explicit i `~/develop/mc68302/CMakeLists.txt`.
* Tagga 25M-testet på rad 2383 med `[.]`.
* Verifiera headless-körning:
  `mame asr10 -video none -sound none -nothrottle -seconds_to_run 10`

Utgångskriterium: headless går, och inga mätningar sker längre i en
ooptimerad build av misstag.

### Fas 1 — riktiga enheter i stället för stubbar (dagar)

* Mappa in `mc68681`/`scn2681` på 0xFC4800 i `asr_map`.
* Mappa in `upd72069_device` på 0xFC4000.
* Riv ut motsvarande handmodellering ur `asr10_boot.cpp`.
* Prova X1 = 4,000 MHz enligt avsnitt 3.

**Baslinjen ändrar tyngdpunkten.** Enligt `baseline.md` visar displayen
redan `"q"` → `"ENSONIQ ASR-10"` → upprepat `"PLEASE I..."`. Panelvägen
fungerar alltså i huvudsak, och booten når fram till diskprompten. Det gör
**FDC-halvan viktigare än DUART-halvan** — blockeraren ligger i diskvägen,
inte i displayen.

Gör ändå DUART:en först, i egen commit: den är mindre, den testar
X1-hypotesen, och en felaktig tick kan i sig störa disktiming. Därefter
FDC:n i egen commit, så att regressioner går att härleda.

Utgångskriterium: booten når minst lika långt som baslinjen med riktiga
enheter i stället för handmodellerade, och det går att avgöra om
"NO INST FILES" är ett riktigt problem eller en artefakt av den egna
FDC-approximationen.

Detta är den största vinsten per nedlagd timme i hela planen. Städa
inte i harnesset först — den här fasen gör merparten av det överflödigt.

### Fas 2 — scoping av 68302 (en dag)

Fråga som ska besvaras innan fas 3 påbörjas: **använder ASR-10
MC68302:ans kommunikationsprocessor?**

ASR-10 har en extern SCN2681 med två kanaler. Kanal B är panelen.
U42 (6N138) är MIDI-in. Om MIDI ligger på DUART:ens kanal A används
68302:ans SCC:er sannolikt inte alls — och då försvinner SCC:erna, det
dubbelportade RAM:et och buffertdeskriptorerna ur fas 3.

Utgångskriterium: skriftligt svar ja/nej med belägg. Halverar
potentiellt fas 3.

### Fas 3 — MC68302 som MAME-device

Mot 68307 som strukturell mall — samma filuppdelning, samma sätt att
linda 68000-kärnan, samma interruptcontroller-till-IPL-mönster.
Registervärden och bitbetydelser kommer från MC68302:s datablad och
`~/develop/mc68302/docs/mc68302/`, inte från 68307.

```
mc68302.cpp      device, ärver m68000_device, BAR-relokerbart registerfönster
mc68302sim.cpp   SCR, BR0-3/OR0-3 chip selects, port A/B PIO
mc68302tmu.cpp   Timer 1, Timer 2, watchdog
mc68302int.cpp   GIMR/IPR/IMR/ISR, IPL-generering, vektor vid IACK
mc68302cp.cpp    kommunikationsprocessor — endast om fas 2 kräver det
```

Storleksordning utan CP: 1 500–2 000 rader. Semantiken finns redan
verifierad i `~/develop/mc68302`; det är implementationen som flyttar,
inte utredningen.

**Inbyggt krav:** varje access mot det interna registerfönstret klassas
som `known`, `known-unimplemented` eller `unknown`. Okända räknas per
körning och räkningen går att läsa ut. Ca 50 rader. Detta är projektets
orakel — en boot som kommer längre ska komma med en räkning på hur många
gånger den gissade. `Mc68302SemanticGuard` i sidoprojektet är fröet.

#### Vad som flyttas in från `~/develop/mc68302`

Redan modellerat och verifierat där. Detta är repots hela transferlast —
det är kunskapen som flyttar, inte koden:

* BAR och SCR som riktiga bootstrap-register; ROM:ens första instruktioner
  konfigurerar den interna adressrymden som på hårdvara
* Intern adressrymd: dual-port RAM och SIB-register routade via BAR,
  med bootstrap-aliasen för BAR/SCR separerade från normal intern rymd
* CS0-CS3 med BR/OR-par; ROM:en flyttar om minneskartan själv
* PIO med faktisk semantik: PACNT, PBCNT, PADDR, PADAT, PBDDR, PBDAT
* Port A IDMA-pinnar PA13-PA15 (DREQ, DACK, DONE) — pinfunktioner, ej motor
* Timer 2-konfiguration med interruptvektor och maskning
* Interruptcontroller: interna vektorer kan levereras

Två mekanismer ska **flyttas, inte byggas om**:

* **Semantic Guard** — verifierar registerskrivningens *betydelse*, inte
  bara värdet: "ROM skrev 0xE000 till PACNT → PA13-15 aktiverar sina
  dedikerade funktioner → de funktionerna är modellerade → skrivningen är
  tillåten". Detta *är* projektets orakel och det finns redan byggt.
* **SIB Observer** — skiljer initiering från normal drift, vet vilka
  register som får ändras senare, varnar vid oväntad omkonfiguration.

#### Acceptanskriterium

Enheten är klar när ROM:en tar sig genom hela den verifierade
bootstrap-sekvensen med **noll okända accesser**:

```
1. BAR
2. SCR
3. CS0-CS3
4. Port A
5. Port B
6. Timer 2
7. interruptrelaterade register
8. vidare mot panel-, DUART- och övrig hårdvaruinitiering
```

Och maskinkonfigurationen säger `MC68302(...)`. Observera att detta gäller
`asr10booth` i `asr10_boot.cpp` — det är den maskin som byggs. Raden
`M68000(config, m_maincpu, XTAL(16'000'000)); // actually MC68302` i
`esqasr.cpp:125` är upstreams skelett och byggs inte idag; den rättas när
`asr10booth` konvergerat tillräckligt för att lyftas tillbaka dit.

#### Kvarstående SIB-delar — fas 2 avgör vilka som behövs

`[Verified]` **IDMA-motorn behövs inte.** `docs/asr10/disk-read-path.md`
visar att disköverföringen är programmerad I/O: antalet FIFO-läsningar
matchar bytesantalet exakt, en CPU-access per byte, och PA13-PA15 samt
IDMA-registren rörs aldrig. Pinfunktionerna räcker. Detta stryker den
största enskilda posten ur fas 3.

Kvar att avgöra: IACK med full hårdvarusemantik, interruptcontroller med
prioritering/pending/nesting, timers med komplett runtime-beteende,
watchdog, SCC/SMC/kommunikationsprocessor, externa signalers elektriska
beteende.

`[Verified]` **Diskvägen är korrekt och ska strykas ur misstänktlistan.**
Byte-för-byte-verifierad mot .img-filen: "ASR-10 OS" i RAM-katalogen på
$544 motsvarar diskoffset 0x602 = cyl 0 / head 0 / sektor 4 = FDC-
transaktion #4, geometri 80 x 2 x 20 x 512 = 1 638 400 byte. Sju poster
per diskett, inklusive tomma, identiska. Rotkatalogen läses exakt en gång
under OS-laddningsfasen.

`[Verified]` **"NO INST OR BANK FILES" är rätt svar för V161.** V161
innehåller OS och effektpresets, inga instrument. V350 innehåller
verkliga instrument (typ 0x03: JM DIGI SYN, MOOG POP 1, BLUES DRUMS) och
banker (typ 0x1e: TUTORIAL BNK, ATRK TUT BNK). Meddelandet var alltså
aldrig en bugg på V161 — maskinen hade rätt.

`[Verified]` **Den faktiska blockeraren:** booten stannar efter
`KEYBOARD TUNED` och är **diskoberoende** — identisk stall för båda
avbilderna upp till 300 emulerade sekunder. Matchar den sedan 2026-06-29
öppna node-89A2-schemaläggarfrågan. Den historiska djupare booten krävde
en betydligt större uppsättning `ASR10_EXPERIMENT_*`-flaggor.

#### Flaggorna är kravlistan

Varje `ASR10_EXPERIMENT_*`/`ASR10_DIAG_*`-flagga som kompenserar för en
saknad MC68302-funktion (klass (a) i `docs/asr10/experiment-flags.md`)
är en bekännelse: maskinen gör inte X rätt, så vi fejkar X. Full
inventering — alla 45 flaggor, klassificerade, med döda kontra aktiva
markerade — finns i `docs/asr10/experiment-flags.md`. Sammanfattat här:

**Aktiv klass (a)** — kompenserar idag, i varje körning
(`static constexpr ... = true`, ingen väg att stänga av utan
källkodsändring):

* `ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3` — Port B PIO: syntetiserar
  en växlande klockbit vid `0xfc6828`-läsning som ingenting på kortet
  faktiskt driver.
* `ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE` — internt
  register `0xfc6860`: låtsas att en busy-bit självrensar efter en kort
  läsfördröjning.

**Död klass (a)** — tolv `static constexpr ... = false`-konstanter,
grupperade i två familjer, ingen väg att slå på utan källkodsändring:

* Tre kring interruptcontrollerns ack/service-clear:
  `ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B`,
  `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480`,
  `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER`.
* Nio kring timer/IACK-syntes:
  `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ` + `_IRQ_LEVEL`,
  `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR` + `_IRQ_LEVEL` +
  `_VECTOR_BYTE` + `_SOURCE_MASK` + `_ONESHOT` +
  `_WAIT_FOR_SERVICE_CLEAR` + `_MIN_CALLBACK_GAP`.

Tolv övergivna försök att fejka interrupt-acknowledge är ett indicium,
inte en slump: interruptcontrollern går inte att fejka med punktvisa
patchar. Varje försök byggdes, testades, och lämnades avstängd — ingen
av dem tog booten längre än vad den redan var utan dem.

**De döda klass-(a)-konstanterna ska INTE raderas före fas 3.** De är
specifikationen, uttryckt som en lista över vad som misslyckades: en
riktig `mc68302int.cpp` (GIMR/IPR/IMR/ISR, IPL-generering, vektor vid
IACK) gör exakt det dessa tolv konstanter gissade sig fram till, fast
på riktigt. Radera dem när den enheten finns och gör dem överflödiga,
inte innan — se raderingslistan i `experiment-flags.md` för vad som
redan kan tas bort oberoende av fas 3 (klass (d) och de döda
klass-(b)-stubbarna).

**IDMA, kommunikationsprocessorn och watchdogen står inte på
kravlistan.** Ingen `ASR10_EXPERIMENT_*`-flagga någonsin byggd
kompenserar för någon av dem. Det är inte bevis för att de är
onödiga — det är frånvaro av bevis, och kan lika gärna betyda att
ROM-koden som skulle ha behövt dem aldrig nåtts (booten stannar efter
`KEYBOARD TUNED`, se nedan). IDMA är separat avskriven ovan på
starkare grund (`disk-read-path.md`, disassemblerad programmerad I/O).
Kommunikationsprocessorn avgörs av fas 2:s egen SCC/MIDI-fråga, inte av
den här flagginventeringen. Watchdogen har inget eget spår i någon
riktning.

Rör ASR-10 lite av detta blir fas 3 klart mindre än takgränsen 2 000 rader.

#### Evidensdisciplin

`docs/asr10/` taggar konsekvent `[Verified]` / `[Likely]` /
`[Hypothesis]` / `[OPEN]`. Statusbeskrivningarna av mc68302-repot gör det
inte — där står "fungerar nu" och "verifieras" utan kvalificering. Den
strängare disciplinen gäller. Skriv om statusen till **specifikationsform
per register** före implementation: vad ROM:en skriver, i vilket steg, vad
värdet betyder, vad som är modellerat och vad som inte är det, med taggar.
Det dokumentet är vad som ska läsas när `mc68302sim.cpp` skrivs.

### Fas 4 — flight recorder

Den enda komponenten på hela listan som inte redan finns i MAME.
`luaengine_debug.cpp` (533 rader) exponerar `bpset`, `wpset`, `step`,
`go`, symboltabeller och uttryck — **ingen per-instruktions-hook**.
Inspelningen måste vara C++.

Grunden finns redan, sju rader:

```
commit d36b79addd6  m68000: add optional instruction execution callback
 src/devices/cpu/m68000/m68000.cpp | 6 +++++-
 src/devices/cpu/m68000/m68000.h   | 2 ++
```

Design — tre lager med tre olika tillväxtlagar:

1. **Blockgraf.** Primitiven är kanten, inte instruktionen. Jämför PC
   med förväntad nästa PC; lika betyder inuti block, skriv ingenting.
   Olika betyder kant: `(från, till) -> räknare` i en hashmap. En loop
   på 50 000 varv är *en post med räknaren 50000*. Växer med programmets
   storlek, inte med körtiden. Loopdetektering är gratis: en loop är en
   bakåtkant med hög räknare.
2. **Ringbuffert.** Fast array, 65 536 kanter à 8 byte = 512 KB, skriver
   över det äldsta. Ger de senaste 65 536 kontrollflödesövergångarna
   exakt. Konstant minne, ingen fil, aldrig en GB-logg.
3. **Händelselogg.** Exceptions, interrupts, vektorhämtningar,
   SIB-skrivningar, okända registeraccesser, chip select-byten. Sällsynt
   nog att spara i sin helhet. Varje händelse stämplas med index i
   ringbufferten och aktuellt block-id.

Stämpeln ger kontexten: "hur hamnade vi i vektor 0x56" = hitta
händelsen, gå bakåt i ringen. "Vilken rutin skrev först till PACNT" =
skrivningen bär sitt block-id. Och gratis på köpet: **vilken kod har
aldrig körts** = ROM minus grafens nodmängd.

Ingenting formateras på den heta vägen. Lua får ett litet API — dumpa
graf, dumpa ring, dumpa händelser, nollställ — och äger policyn för när
och vad. Storleksordning 300–400 rader.

Prototypa datamodellen i `~/develop/mc68302` där bygget är sekunder,
porta sedan in bakom callbacken.

---

## 5. Överföring mc68302 → mame-upstream

Principen: **kunskapen flyttar, koden mestadels inte.** Koden i
`~/develop/mc68302` är Moira-formad — `Mc68302Cpu` överlagrar Moiras
`read8`/`read16`, `Mc68302Bus` äger en 16 MB-vektor. MAME är `address_map`,
`device_t` och minnesutrymmen. En rad-för-rad-översättning bär med sig
femtontusen raders struktur in i något som ska vara femtonhundra.

Men det betyder inte att något kastas. Det mesta flyttar — som semantik,
som data eller som dokument.

### Disposition per källfil

| Fil i mc68302 | Går till | Form |
|---|---|---|
| `Mc68302SystemIntegration` | `mc68302sim.cpp` | semantik, omskriven i MAME-idiom |
| `Mc68302ChipSelect` | `mc68302sim.cpp` | **semantik — MAME:s 68307 har bara registerstubbar, denna är funktionell och ligger före** |
| `Mc68302Pins` | `mc68302sim.cpp` | semantik, inkl. PA13-15 DREQ/DACK/DONE |
| `Mc68302Timer` | `mc68302tmu.cpp` | semantik, schemaläggning görs om till `emu_timer` |
| `Mc68302Watchdog` | `mc68302tmu.cpp` | semantik |
| `Mc68302Interrupts`, `Mc68302InterruptTrace` | `mc68302int.cpp` | semantik, IPL matas MAME-vägen |
| `Mc68302SemanticGuard` | enhetens access-klassificerare | **regelverket flyttar i sak — detta är oraklet** |
| `Mc68302SemanticCoverage` | enhetens räknare + körrapport | begreppsmodell |
| `Mc68302SibObserver` | enheten eller ett Lua-skript | init-kontra-driftskillnaden är det värdefulla |
| `Asr10Cs3Decoder` | **drivrutinens `address_map`**, inte enheten | ASR-10:s kortnivådekod (U5:s PAL) — FDC 0xFC4000, DUART 0xFC4800, SCSI-kandidat 0xFC5000 |
| `tests/*.cpp` | förväntansfil, se nedan | data |
| `docs/mc68302/*.md` | **flyttas fysiskt** till `docs/mc68302/` här | oförändrat |
| `Mc68302Cpu`, `Mc68302Bus` | — | Moira-formade, ersätts av `device_t` + `address_map` |
| `Scn2681Duart` | — | ersätts av MAME:s `mc68681` |
| `RomImage`, `RomTarget`, `InterleavedRomLoader` | — | ersätts av MAME:s ROM-regioner |
| `ExternalBusTarget` | — | ersätts av `devcb` / `address_map` |
| `TraceStep`, `TraceNode`, `TraceRecorder`, `ExecutionBlockBuilder` | — | ersätts av flight recordern, fas 4 |

### Förväntansfil — det som gör acceptanskriteriet mekaniskt

Testerna kodar inte logik, de kodar fakta: i bootstrap-steg N skriver
ROM:en värdet V till register R. Bryt ut den tabellen till en genererad,
maskinläsbar fil som **båda** sidor läser:

```
# docs/asr10/expectations/asr10-sib-bootstrap.txt   (genererad)
seq  kind      address     value   meaning
1    write16   0x000000F2  0x0FC6  BAR
2    write16   0x00FC6832  0x3F82  OR0
3    write16   0x00FC6830  0x1F01  BR0
...
```

* mc68302 genererar den ur en boot — `BusAccess` bär redan type, size,
  address, value, target och functionCode. Endast `subtarget`-strängen
  utesluts; den är presentation, inte fakta.
* mc68302:s Catch2-tester assertar mot filen i stället för mot inbakade
  konstanter.
* Ett Lua-skript i MAME sätter watchpoints över det interna fönstret och
  assertar mot samma fil.
* Filen **committas här**, i `mame-upstream`. Destinationen äger sanningen,
  labben producerar den.

**Avgörande regel: jämför på bussnivå, aldrig på instruktionsnivå.** Moira
och MAME:s m68000 skiljer sig i prefetch, cykelräkning och kantfall och
kommer aldrig ge identiska instruktionstracer. Men båda måste producera
samma sekvens av skrivningar till BAR, SCR, BR/OR, PACNT och Timer 2. Det
är invarianten. Normaliserad form: ordning, kind, adress, värde. Cykeltal
och instruktionsnummer kastas.

Jämföraren är trivial — läs två sekvenser, diffa, rapportera första
avvikelsen med kontext. Femtio rader i valfritt språk.

### När labben ska läggas ner

Riktningen på jämförelsen kommer att vända. Idag är mc68302 referensen,
eftersom SIB-semantiken bara finns där. När MAME-enheten är korrekt går
booten längre än mc68302 någonsin kommer — den har riktig DUART, riktig
FDC och riktigt ljud. Då är MAME referensen.

mc68302 kan läggas ner när fyra saker är sanna:

1. Förväntansfilen är genererad och committad här.
2. `docs/mc68302/` har flyttat hit.
3. Flight recorderns datamodell är fastlagd (fas 4 prototypas där).
4. MAME bootar längre än labben gör.

Fram till dess är den en bänk, och en bänk ska krympa.

## 6. Raderingslista

Följer i huvudsak av fas 1 och 4, inte av städning:

* DUART-modellen i `asr10_boot.cpp` — counter/timer, ACR, preloads,
  skuggregisterfilerna `m_duart_panel_asr_shadow`, `m_duart_vfx_shadow`
* FDC-approximationen
* Alla `*_EXPERIMENT_*`-konstanter och -flaggor
* All observationskod som flyttar till Lua
* `asr10_boot_codex.cpp` i sin helhet
* ES5570-kandidatstubbarna, `[DISPROVEN]` i `es5701-wiring.md` som
  ASR-10:s karta och aldrig träffade vid körning i någon session:
  `es550x_vfx_candidate_r/w`@0x200000, `es5510_vfx_candidate`@0x260000,
  `duart_vfx_candidate`@0x280000, `fdc_vfx_candidate`@0x2c0000,
  `es5506_ts_candidate`@0x300000, `es5510_ts_candidate`@0x380000 —
  sex handlerpar plus deras adresskarteposter
* I sidoprojektet: de tretton handrullade triggerloopar som gör
  `Asr10RomIntegrationTest.cpp` 2 987 rader lång

Mål: `asr10_boot.cpp` under 1 000 rader.

---

## 7. Stående regler

Dessa hör hemma i `CLAUDE.md` i reporoten och gäller varje session.

1. **Harnesset får inte vara större än det det instrumenterar.**
   Överskridande är ett stopp, inte en observation.
2. **Instrumentering raderas när utredningen är klar.** Fyndet går in i
   dokumentationen; koden som producerade det försvinner. Argumentet
   "den kan behövas igen" gällde när det var dyrt att skriva om den.
3. **C++ endast för det som ändrar maskinens beteende.** Allt som bara
   tittar hör hemma i Lua.
4. **Innan något nytt byggs: färdigställ eller radera det halvfärdiga
   som redan gör samma sak.**
5. **Ett prestandapåstående kräver en siffra. Ett arkitekturpåstående
   kräver ett fall som går sönder.**

---

## 8. Prompter för Claude Code

Varje session startar kall. Prompterna är därför fristående.

### Fas 0

> I `~/develop/mc68302`: sätt `CMAKE_BUILD_TYPE` explicit i
> `CMakeLists.txt` (Release som default, Debug via flagga). I
> `tests/Asr10RomIntegrationTest.cpp`, lägg till taggen `[.]` på
> TEST_CASE runt rad 2383 ("ASR-10 ROM receives periodic DUART counter
> timer IRQ") — den kör 25 000 000 instruktioner och står för i stort
> sett hela svitens körtid. Kör sedan sviten och rapportera väggtiden
> före och efter. Lägg inte till någon ny kod utöver detta.

### Fas 1

> I `~/develop/mame-upstream`, läs först `docs/asr10/PLAN.md`.
> Ersätt den handskrivna DUART-modellen i
> `src/mame/ensoniq/asr10_boot.cpp` med MAME:s riktiga enhet:
> mappa in `mc68681`/`scn2681` på 0xFC4800 och `upd72069_device` på
> 0xFC4000. Ta bort motsvarande handmodellering — counter/timer, ACR,
> preloads, `m_duart_panel_asr_shadow`, `m_duart_vfx_shadow` och alla
> `*_EXPERIMENT_*`-konstanter. Sätt DUART:ens X1 till 4.000 MHz
> (härledd 16.000/4; se avsnitt 3 i planen) och rapportera vad det gör
> med tick-perioden. Kör headless:
> `-video none -sound none -nothrottle -seconds_to_run 30`.
> Rapportera vad displayen skriver och hur många rader som togs bort.
> Lägg inte till ny instrumentering i den här uppgiften.

### Fas 2

> Avgör om ASR-10 använder MC68302:ans kommunikationsprocessor (SCC:er,
> dubbelportat RAM, buffertdeskriptorer) eller inte. Utgå från att U42
> (6N138) är MIDI-in och att SCN2681:ans kanal B är panelen. Sök i
> ROM-avbilderna efter accesser mot 68302:ans interna fönster i
> CP-området och mot DUART:ens kanal A. Svara ja eller nej med belägg,
> och skriv in svaret i `docs/asr10/PLAN.md` avsnitt 4, fas 2.

### Fas 3

> Skriv en MC68302-device för MAME i `src/devices/machine/`, med
> `src/devices/machine/68307*.{cpp,h}` som strukturell mall — samma
> filuppdelning och samma mönster för att linda 68000-kärnan och för att
> mata IPL. Filer: `mc68302.cpp`, `mc68302sim.cpp`, `mc68302tmu.cpp`,
> `mc68302int.cpp`. Registervärden och bitbetydelser hämtas från
> `~/develop/mc68302/docs/mc68302/` och `~/develop/mc68302/src/`, inte
> från 68307. Bygg in klassificering av varje access mot det interna
> registerfönstret som known / known-unimplemented / unknown, med en
> räknare per körning som går att läsa ut. Byt sedan
> `M68000(config, m_maincpu, ...)` mot `MC68302(...)` i `esqasr.cpp`.
> Flytta in Semantic Guard och SIB Observer från det repot i stället för
> att bygga nya — de är redan färdiga och de utgör projektets orakel.
> Acceptanskriterium: ROM:en tar sig genom hela bootstrap-sekvensen
> (BAR, SCR, CS0-3, port A, port B, Timer 2, interruptregister) med noll
> okända accesser. Riktmärke: hela 68307-familjen är 1 327 rader;
> överskrid inte 2 000.

### Fas 4

> Implementera flight recordern enligt `docs/asr10/PLAN.md` avsnitt 4,
> fas 4 — blockgraf med kanträknare, ringbuffert på 65 536 kanter,
> händelselogg med stämpel mot ringbufferten. Installera den bakom den
> befintliga instruktions-callbacken (commit d36b79addd6). Ingen
> formatering på den heta vägen; exponera i stället dump-funktioner till
> Lua. Riktmärke 300–400 rader. Prototypa gärna datamodellen i
> `~/develop/mc68302` först där bygget tar sekunder.
