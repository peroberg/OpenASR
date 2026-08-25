# Metoder — statisk analys av ROM och diskbilder

Metoder överlever enskilda resultat. Det här dokumentet beskriver de tekniker som
faktiskt har gett fynd i projektet, med deras kända felkällor. Komplement till
`os-code-extraction.md`, som beskriver hur RAM-kod matchas mot diskoffset.

---

## 1. Domänfiltrerad referensräkning

**Vad:** räkna 32-bitars långord vars övre byte är `$00` eller `$FF` och vars 24-bitars
form faller i ett känt fönster (ROM `$F80000-$FBFFFF`, DPRAM `$FC6000-$FC67FF`, …).

**Ger:** grov karta över vilka adresser en bild bryr sig om. Användes för
`rom-abi-entrypoints.csv`.

**Felkälla:** ofiltrerade långord är brus. 160 "träffar" i CS1-fönstret visade sig vara
slump. Kvalificera alltid mot en opcode (`4EB9`/`4EF9`/`4EB8`/`4EF8`) innan en siffra
används som belägg.

**Nyttigt sanitetstest:** räkna hur många mål som är **udda**. Riktiga hoppmål är alltid
jämna. 334 distinkta mål, noll udda ⇒ det är instruktioner. Ungefär 50 % udda ⇒ det är
data.

---

## 2. Offsetsvep med terminatortest

**Problem:** hitta laddningsförskjutningen `K` i `RAM = filoffset − K` när ingen
laddare är avkodad.

**Metod:**

1. Samla anropsmål `T` från `4EB9`/`4EF9` (32-bitars absolut) i bilden.
2. För varje kandidat `K`: räkna hur många `T` där ordet på `T+K−2` är en avslutare
   (`4E75 rts`, `4E73 rte`, `4E77 rtr`, `4EF9`/`4EF8 jmp`, `4ED0 jmp (a0)`, `4E71 nop`).
3. Normalisera mot antalet mål som alls hamnar i bilden.
4. Rätt `K` sticker ut. Fel `K` ligger på 2–6 %.

**Resultat i projektet:** segment 2-regeln rangordnades 1 av 40 960 kandidater
(61,4 % mot 26 % för tvåan), och gav samma konstant i två OS-versioner.

### Fyra fällor, alla utlösta minst en gång

**F1 — nollutfyllnad.** Räkna **inte** `$0000` som avslutare. Bilder har stora
nollområden; varje mål som landar där får då en falsk träff. Det gav en falsk topp på
43–50 % för en modell som efter rättning fick 0/7.

**F2 — teckenfel i sveper.** Svep **både** positiva och negativa `K`. Ett svep över
`K ≥ 0` kunde per konstruktion inte hitta segment 1 (`K = −0xA00`), och `0x5A00`
togs för hela svaret. Upptäcktes bara genom bytemönstersökning mot ett känt ankare.

**F3 — kortadressering är osynlig.** `4EB8`/`4EF8` (absolut kort) syns inte i ett svep
som bara samlar `abs.l`-mål. Ett helt 8 KB-område — bindningstabellen — såg ut som ett
"hål" i laddningskartan enbart av den anledningen. Kör alltid svepet i två varianter.

**F4 — hoppbordsposter har fel föregångare.** En post i en stride-6-tabell föregås av
den andra halvan av föregående posts operand, inte av en avslutare. Terminatortestet
missar därför hela tabeller. Använd i stället **trampolintestet**: är ordet *på* `T+K`
självt en `JMP`-opcode? Det gav 313/334 = 93,7 % och lokaliserade bindningstabellen.

---

## 3. Stride-detektion för hoppbord

**Vad:** samla alla adresser som anropas absolut i ett område, sortera, och leta efter
ett gemensamt rutnät.

**Ger:** `4EF9 <32-bit>` är 6 byte, `4EF8 <16-bit>` är 4 byte. Ett stride-6-rutnät i
anropade adresser är nästan alltid ett hoppbord.

**Resultat:** DPRAM-tabellen `$FC6000-$FC60C8` hittades enbart så — genom att
`$FC6014`, `$FC602C`, `$FC6056`, `$FC605C`, `$FC6062`, `$FC6068`, … alla ligger på ett
stride-6-rutnät från `$FC6014`, som i sin tur är OS-vektor 11:s mål.

**Felkälla:** med stride 6 och tät fyllnad hamnar ~1/6 av godtyckliga adresser på ett
rutnät. Kräv många punkter och en oberoende bekräftelse (som att en av dem redan är
känd från annat håll).

---

## 4. Versionsdiff som strukturdetektor

**Vad:** kör samma analys på två OS-versioner och jämför resultatet post för post.

**Vad det avslöjar:**

* **Identisk layout, olika värden** ⇒ samma modul kompilerad två gånger. Bindningstabellen
  har identisk luckstruktur och identiska 23 luckor på identiska offset i V1.61 och
  V3.50, men 156 av 723 mål skiljer sig. Det var det avgörande beviset för att tabellen
  är ett *gränssnitt*, inte tillfällig kod.
* **Identiska siffror från oberoende körningar** ⇒ regionen är byte-identisk. Att
  ROM-testet gav exakt 313/334 för båda versionerna var första indikationen.
* **En enskild adress som ändras drastiskt** ⇒ arkitektonisk förändring.
  `$F95EAA` går från 1 anrop i V1.61 till 33 i V3.50.

**Regel:** ett fynd som bara gäller en version är ett fynd om den versionen. Ett fynd
som gäller båda är ett fynd om *maskinen*.

---

## 5. Korsvalidering mot vendormanual

**Vad:** låt registeroffset komma från manualen, inte från gissning.

**Resultat:** MC68302 UM Table 2-9 gav `Base + 828 = PBDAT`. Det gjorde att
`$FC6829` — den mest refererade 68302-adressen i hela OS:et — kunde identifieras som
PBDAT:s låga byte, dvs kanalvalet till den analoga multiplexern.

**Verifiera alltid kartan mot kända skrivningar innan den används:**

| förväntat | observerat | slutsats |
|---|---|---|
| WRR ← `$0000` | `$FC684A` ← `$0000` | `Base+84A = WRR` ✓ |
| TRR2 ← `$3F01` | `$FC6852` ← `$3F01` | `Base+852 = TRR2` ✓ |
| TCN2 läses endast i V3.50 | `$FC6856`: ROM 0, V161 0, V350 1 | `Base+856 = TCN2` ✓ |

Tre oberoende matchningar innan kartan användes.

---

## 6. Bytemönstersökning som sanningsankare

**Vad:** när en modell säger att en känd rutin ska ligga på ett visst offset —
**sök efter dess bytemönster i hela bilden** i stället för att titta på det förutsagda
offsetet.

**Varför:** att titta på det förutsagda offsetet bekräftar bara att man kan tolka
godtyckliga byte som kod. Att söka mönstret säger var det *faktiskt* finns.

Det var så F2 upptäcktes: `0039 0007 00FC6829` gav diskoffset `0x08DEC`, inte det
förutsagda `0x0F1EC`.

---

## 7. Metodens systematiska blinda fläck

**Ingen av teknikerna ovan ser registerrelativ adressering `(d,An)`.**

Det har kostat projektet fem gånger: CTU/CTL/ACR-tabellinitieringen, den tredje
IMR-skrivningen, felindexpekarna `FFF8xxxx`, DUART-brytrutinen på `$F8845A`, och
SCC-skrivningarna via A3.

**Konsekvens för varje coverage-avsnitt:** "inga referenser hittade" betyder
"inga *absoluta* referenser hittade". Det är inte samma sak som "används inte".
CS1-analysen är för närvarande helt beroende av den distinktionen.

### Frånvaro av implementation är inte hårdvaruevidens

Frånvaro av implementation är inte ett svar på en fråga om hårdvaran.

```
"noll referenser"   betyder  "noll identifierade absoluta referenser"
"ej implementerat"  betyder  "inte byggt än" -- aldrig "utrett och avfärdat"
"MAME gör inte X"   betyder  "modellen gör inte X" -- aldrig "maskinen gör inte X"
```

Belagt två gånger i rättelsepasset efter `47318563942`: W1C och E2. Båda gångerna var
observationen riktig och etiketten fel.

---

## 8. Kalibrera instrumentet innan mätningen tolkas

En sökning som ger noll träffar är inte ett resultat förrän samma sökning har visats
träffa något känt i samma artefakt med samma parametrar. Utan positiv kontroll är
"noll träffar" och "sökningen är trasig" omöjliga att skilja åt.

Belagt: strängsökningen efter `NO INST OR BANK FILES` gav 0 träffar i tre artefakter
och användes som grund för ett `[Verified]`-påstående innan metoden var kalibrerad.

## Kalibrera inte bara instrumentet — verifiera att mätobjektet kör

Ett uteblivet svar kan bero på fel instrument eller på en inaktiv konsument. De två ger
identiska observationer, och ett kontrollpar som förutsätter att konsumenten kör kan
inte skilja dem åt.

Innan en stimulans tolkas ska det vara observerat att den del av systemet som ska
reagera är aktiv under mätningen.

Belagt: panelsvepningen mot V3.50. Kontrollparet `$23`/`$22` gav noll effekt för båda
värdena. Det var inte observerat om receive-rutinen alls kördes i FILE 1-läget, vilket
gjorde nollresultatet otolkbart i stället för informativt.

## Observation i Lua, maskinmodell i C++

Allt som bara observerar - taps, räknare, PC-sampling, byteloggar, dumpar,
CSV-export - skrivs som Lua-skript under `docs/asr10/lua/` och körs med
`-autoboot_script`. Det kan kastas när frågan är besvarad.

Allt som ändrar maskinens beteende - stubbar, påtvingade värden, syntetiska
svar - ligger i drivrutinen, ska vara namngivet efter den hårdvarufunktion det
ersätter, och ska vara noll när arbetet är klart.

Skälet är inte prydlighet. När båda sorterna ligger i samma fil går det inte
längre att se vilka switchar som bara tittar och vilka som ljuger för maskinen.
Det kostade ett helt granskningspass att ta reda på vilka tre av ~160 som var
aktiva som standard, och två av dem påverkade bootvägen.

### Verifierad bas är inte verifierad gräns

En bekräftad basadress säger ingenting om hur långt tabellen sträcker sig. Bas och
gräns är två separata påståenden med separat evidens, och det andra ärver inte det
förstas säkerhet.

Belagt: paneltabellen `$F82484`. Indexeringsinstruktionen på `$F89D9C` verifierade
basen; utsträckningen antogs vara 256 och dumpen läste 218 byte strängdata, pekare och
utfyllnad som mappningar. Den "icke-bijektiva" struktur som drogs ur resultatet fanns
inte.

### `$FFxxxx` betyder inte alltid spegling

`$FFFC4817` i en 32-bitars absolut lång operand och `$8D50.w` som teckenutvidgas till
`$FF8D50` är två skilda mekanismer. Den första är vanlig 24-bitars periferadressering;
den andra är grunden för den öppna hög-RAM-speglingshypotesen. Se
`memory-map.md` §1.1.

---

## 8.5 En dynamiskt ominstallerad handler river Lua-taps

**Vad:** `prog:install_write_tap()` på ett adressintervall som ett device
senare ominstallerar via `install_readwrite_handler()` för samma intervall
tappar tap:en tyst. Ingen Lua-felkod, ingen varning — bara noll träffar
därefter.

**Belagt:** `mc68302_device::install_internal_window()` river och
återinstallerar `$FC6000-$FC6FFF` vid varje BAR-skrivning
(`src/devices/machine/mc68302.cpp`). En tap installerad vid skriptstart (före
boot) gav noll skrivningar i hela `$FC6800-$FC68FF`-fönstret genom en hel
körning, trots dokumenterad PBCNT/PBDDR/PADAT/PBDAT-trafik i ROM. Samma tap,
installerad efter `t=15,5s` i stället för vid skriptstart, fångade
`$FC6829`-trafik inom en millisekund. → `investigations/irq1-imr-unmask-probe.md`

**Konsekvens:** en tap på ett MC68302-internfönsteradress måste installeras
efter den sista relevanta BAR-skrivningen, inte vid skriptstart. Ett
nollresultat mot ett sådant fönster är otolkbart utan en positiv kontroll
installerad vid exakt samma tidpunkt i körningen (jfr §8 ovan) — annars är
det inte skilt från detta fel.

## 8.6 En osparad tap-referens kan GC:as bort tyst

**Vad:** `space:install_read_tap()`/`install_write_tap()` returnerar ett
handtag. Sparas det inte i en variabel som lever tappens tänkta livstid ut
kan Lua:s skräpsamlare ta bort det nästan omedelbart — tyst, inget fel,
tappen eldar bara aldrig.

**Belagt:** en första version av en IRQ1-IACK-tap
(`docs/asr10/lua/archive/irq1_sr_mask_probe.lua`) skrev
`cpu_space:install_read_tap(...)` utan att spara returvärdet. Tappen gav noll
träffar genom en hel körning, trots att en oberoende mätning i samma bygge
och samma körpunkt (`irq1_vector_probe.lua`) bevisade att händelsen den
skulle fånga faktiskt inträffade. Fixen var `local irq1_tap =
cpu_space:install_read_tap(...)` — en sparad referens i skriptets
toppnivåscope. → `investigations/irq1-vector-and-sr-probe.md`

**Konsekvens:** samma regel som §8.5, men en annan mekanism. Två separata
sätt att tyst tappa en tap är nu belagda i det här projektet: en
ominstallerad handler (§8.5) och en osparad referens (den här). Ett
nollresultat från vilken tap som helst kräver en levande-genom-hela-fönstret
kontroll innan det tolkas, oavsett vilken av de två fällorna som är
misstänkt.

## 8.7 Generell regel: en Lua-tap som ger noll är ogiltig utan vittne

§8.5 och §8.6 är två *oberoende* sätt för en Lua-tap att dö tyst — en
ominstallerad handler (§8.5, specifikt `$FC6000-$FC6FFF`) och en osparad
referens som Lua:s skräpsamlare tar bort (§8.6, gäller *vilken adress eller
vilket adressrymd som helst*, inte bara SIB-fönstret). Två oberoende
mekanismer i samma session är ett mönster, inte en slump.

**Regeln i sin allmänna form:** ett nollresultat från en Lua-tap är ogiltigt
tills tappen har ett levande vittne genom *hela* mätfönstret — inte bara vid
installationstillfället. En positiv kontroll som bara bevisar att tappen
levde i sin första instant är inte samma sak som täckning för fönstret den
faktiskt behöver täcka.

Det gäller alla adresser i det här projektet, inte bara
`$FC6000-$FC6FFF`. Varje tidigare nollresultat från en Lua-tap i det här
trädet ska betraktas som omätt — inte nödvändigtvis fel, men inte heller
bekräftat — tills det har fått ett sådant vittne.

## 8.8 En DMA-kontroller är en självständig bussmästare

En riktig DMA-kontroller svarar på en förfrågningslinje och kör sina egna
busscykler på sitt eget schema, oberoende av den periferikrets som
begärde överföringen. Modellerar man den i stället som ett återanrop
inifrån periferikretsens egen tillståndsmaskin — samma anropsstack, samma
funktionsanrop, ingen egen kontext — har man inte byggt en kontroller. Man
har byggt en callback som råkar heta DMA.

**Belagt:** en minimal IDMA-kanal anropade `tc_w()` synkront inifrån
`upd765_family_device`s `drq_wr_callback()`, som i sin tur hävs inifrån
enhetens egen `live_run()`-loop (`fifo_push() -> enable_transfer() ->
drq_cb`). `tc_w()` anropar `live_sync()`, som kan återinträda
`live_run()` — reentrant, medan den yttre `live_run()`-invokeringen som
ledde hit fortfarande låg på stacken, mitt i sin egen iteration, med delat
föränderligt tillstånd (`cur_live`) bara delvis uppdaterat. Effekten var
mätbar och entydig: enhetens `main_phase` fastnade permanent i
`PHASE_EXEC`, `command_end()` kördes aldrig, och INTRQ hävdes aldrig —
inte "levererades inte", utan "hävdes aldrig". Fem sekunders tystnad,
576 tillfällen då CPU:ns mask var öppen, noll avbrott. Lösningen var att
flytta enbart `tc_w()`-anropet till en timer med noll fördröjning, så att
det kör på sitt eget anrop utanför periferikretsens stack — precis den
separation en riktig oberoende TC-bussledning skulle ge.
→ `docs/asr10/investigations/tc-reentrancy-probe.md`

**Regeln, i samma familj som "en PC-beroende stub är ingen
hårdvarumodell":** båda namnger en genväg som ser ut som den riktiga
mekanismen utifrån men är något strukturellt annat inifrån, och båda
kostade projektet en specifik, uppmätt kraschsignatur innan skillnaden
syntes. Ett återanrop som råkar flytta rätt byte vid rätt tillfälle är
inte samma sak som en bussmästare, lika lite som ett värde som råkar
matcha en observerad körning är samma sak som en avkodad registerbit.

## 8.9 `logerror()` går ingenstans utan `-log`

`logerror()`-anrop är verkningslösa i det här projektets egna
körvillkor: callbacken som skulle skriva ut dem registreras bara när
`-log` är satt (`src/emu/machine.cpp:289`, läst direkt, inte antaget),
och `-log` är förbjuden i det här trädet. En vakt som ropar `logerror()`
vid ett fel ropar tyst — precis den typ av tyst misslyckande hela
konsolideringsarbetet finns till för att eliminera.

**Belagt:** `esqpanel_device::xmit_char()`s ringbuffer-överfyllnad
(`docs/asr10/investigations/keyboard-and-sample-bridge-2.md`) loggades
först enbart via `logerror()` och syntes inte alls i den fångade
körutdatan, trots att överfyllnaden mätbart inträffade (32 av 52 byte
förlorade). Fixen krävde `osd_printf_error()` vid sidan av — den skriver
ovillkorligt, `-log` eller ej, vilket redan bekräftats av projektets
egna tidigare körningar (t.ex. MAME:s egna `install_read_tap`
adressmask-fel, som alltid synts utan `-log`).

**Regeln:** varje högljudd mekanism den här sessionen bygger, eller
kommer att bygga, ska gå via `osd_printf_error()` (C++) eller ett
Lua-tryck (`print()`, alltid synligt i den fångade körutdatan) — aldrig
`logerror()` ensamt. Gäller alla vakter i `asr10_guards.lua` och alla
framtida.

## 8.10 En träff på en instruktions läs-tap är inte exekvering

**Vad:** en CPU kan hämta (prefetcha) ordet efter en intilliggande
ovillkorlig gren (`bra`, `jmp`) utan att någonsin avkoda det. En läs-tap
på en instruktionsadress ser identisk ut i båda fallen — träffen
registreras vare sig ordet exekveras eller bara hämtas spekulativt och
kastas.

**Belagt:** `$782A` gav en stabil 83 Hz-träffrekvens i varje körning,
vilket lästes som "83 Hz pollning" innan PC-korrelation prövades. Samma
instruktions eget förlängningsord på `$782C` — som måste hämtas om
instruktionen faktiskt avkodas och exekveras, men inte annars — gav bara
0/1 träffar per not. PC var `$782A` även vid de rena
prefetch-träffarna, så ett enkelt PC == tappad-adress-villkor räckte
inte ensamt för att skilja fallen åt; det avgörande testet var om hela
den flerords-instruktionen, förlängningsordet inräknat, faktiskt
fullföljdes.

**Regeln:** en träff på en instruktions läs-tap är inte exekvering.
Positiva exekveringspåståenden kräver PC-korrelation eller
motsvarande. Nollresultat påverkas inte.

**Konsekvens:** PC-korrelation (jämför CPU:ns PC vid tap-tillfället med
den tappade adressen) är nödvändigt men, för en instruktion med
förlängningsord och en intilliggande ovillkorlig gren strax innan, inte
ensamt tillräckligt. Den skarpa kontrollen är om hela instruktionens
samtliga ord faktiskt hämtades/fullföljdes, inte bara det första.
`asr10_taps.lua`s `pc_correlated_read_tap()` gör PC-jämförelsen till
standardvägen; den flerords-kontrollen måste fortfarande läggas till av
den som tappar en instruktion med förlängningsord, eftersom den kräver
kännedom om instruktionens egen kodning.

## 9. Statik före stimulans

Identifiera först, stimulera sedan. Inte för att statisk analys är finare, utan för att
den krymper stimulansens sökrymd. En råbytesvepning över 128 värden utan förkunskap ger
128 okända experiment; samma svepning efter att tabellen och dess konsumenter är
identifierade ger tio riktade.

Belagt fyra gånger: bindningstabellen, ROM->OS-överlämningen, PB10/PB11,
segmentreglerna. Alla blev enkla först när statiken hade begränsat sökrymden.

---

## 10. Rapportdisciplin

Den normerande modellen för hypoteskrav, statusändringar, evidensdomäner och
revisionsspår finns i `methods-hypothesis-management.md`. Den korta formen är fyra
nivåer, plus täckning:

```
[Verified]    direkt och reproducerbart belägg inom angiven evidensdomän
[Likely]      enklaste överlevande förklaring; avgörande test saknas
[OPEN]        flera förklaringar överlever eller giltig mätning saknas
[DISPROVEN]   verifierad observation motsäger hypotesens förutsägelse
Coverage:     vilket adressintervall / hur många oberoende ankare
```

En väl dokumenterad öppen fråga är mer värd än en halvbevisad lösning. Varje hypotes
ska ange vilken observation som skulle få den att överges. Motbevisade hypoteser ska
stå kvar — de hindrar att samma väg utforskas igen.

---

## 11. Inga underagenter eller forkar i det här projektet

**Regeln:** endast den aktiva huvudagenten får köra experiment, ändra
dokument eller använda git i det här trädet. Inga underagenter, forkar
eller andra parallella agentinstanser — oavsett hur snävt avgränsat
deras uppdrag verkar vara.

**Belagt:** en fork som fick i uppdrag att enbart läsa och rapportera
en avgränsad revision körde i stället hela den återstående uppgiften —
startade maskinen, körde experiment, skrev en ny investigation-fil,
ändrade `current-status.md` och manifestet, och committade
(`0c59b1e59a2`) — utan att någonsin återvända för granskning. Det
skedde samtidigt som huvudagenten självständigt utredde exakt samma
fråga, och bröt mot regeln att dokumentation skrivs av en part i taget
(se trädets `CLAUDE.md`, regel 8). Commiten fick rullas tillbaka i sin
helhet (`37b3c49f953`, `38f506ea2bf`), och huvudagentens eget arbete
(§8.10 ovan, `pc_correlated_read_tap()`) fick återapplicerats separat
för att inte gå förlorat i återställningen.

**Konsekvens:** ett uppdrag som verkar tillräckligt snävt för att
delegeras säkert är det inte. Reglerna om ett skrivande i taget och
git-disciplin gäller lika mycket för en delegerad process som för
huvudagenten själv, och en delegerad process kan inte hållas till dem
i efterhand. Var och en av trädets regler gäller den agentinstans som
faktiskt kör — det finns bara en sådan.
