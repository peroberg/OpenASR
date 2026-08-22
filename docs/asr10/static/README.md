# ASR-10 static-analysis workspace

Detta är en temporär arbetsyta för reproducerbar statisk analys av
ASR-10-ROM, OS-avbildningar och kontraktet mellan ROM och diskresident OS.

Materialet här är inte automatiskt kanonisk projektdokumentation.
Verifierade och stabiliserade fynd ska över tid införlivas i:

- `../reference/boot-sequence.md`
- `../reference/vector-map.md`
- `../reference/subroutine-index.md`
- `../reference/os-code-extraction.md`
- framtida `../reference/os-image-layout.md`
- framtida `../reference/rom-os-abi.md`
- framtida eller reviderad `../reference/memory-map.md`

Arbetsloggarna bevarar metod, rättelser, täckning och negativa resultat.
Referensdokumenten ska beskriva projektets aktuella kunskap utan historiskt
brus.

## Innehåll

### `worklog-rom-os-abi.md`

Första statiska analysomgången av ROM/OS-kontraktet.

Innehåller bland annat:

- OS-filernas katalogposition och storlek
- resetsekvensens exekveringsbrygga genom MC68302 DPRAM
- chip-select-konfigurationen
- OS-vektortabellen
- de första laddningsmodellerna
- ROM-entrypoint-census
- metodfel och retraktioner

### `worklog-rom-os-abi_2.md`

Fortsatt och korrigerad analys.

Innehåller bland annat:

- bindningstabellens fulla utbredning
- 723 fasta JMP-slots
- versionsskillnader mellan V1.61 och V3.50
- ROM-rutiner som OS kan ersätta
- förbättrade segmentmodeller
- täckningsgränser
- öppna runtimeexperiment

### `rom-abi-entrypoints.csv`

Census över ROM-adresser som refereras av OS-versionerna.

Kolumnerna skiljer bland annat:

- `jsr`
- `jmp`
- vektorpost
- V1.61
- V3.50
- gemensamt eller versionsspecifikt mål

Tabellen är ett råmaterial för att stegvis namnge och dokumentera ROM-rutiner
i `reference/subroutine-index.md`.

### `os-binding-table.csv`

Bindningstabellens 723 slots.

Varje slot innehåller ett `JMP` mot antingen:

- en permanent ROM-rutin
- OS-kod
- en versionsspecifik ersättning
- en callback som ROM förväntar sig från OS

Tabellen är den hittills tydligaste konkreta representationen av
ROM/OS-kontraktet.

### `prompts-E1-E4.md`

Förslag till runtimeexperiment för:

- installation av vektortabellen och ROM→OS-överlämningen
- möjlig låg-RAM-aliasning i `$FF8000-$FFFFFF`
- OS-laddarens segmentmodell
- CS1-fönstret `$FF6000-$FF7FFF`

Prompterna är arbetsunderlag och ska granskas innan körning. Inga
minnesmappsändringar ska göras enbart utifrån den statiska analysen.

### `panel-raw-map.csv`

Genererad dump av ROM-bytena `$F82484-$F82583`.

Kolumner:

- `raw`: rå byte som firmware läser från DUART kanal B RHRB
- `mapped`: byte som finns på `$F82484 + raw`
- `status`: `mapping` för raw `$00-$25`, `beyond-verified-bound` därefter
- `note`: objektidentifiering för dumpade byte bortom den verifierade mappningsprefixen

Lookup-basen är verifierad genom indexeringskod vid `$F89D9C`. Den verifierade
mappningsprefixen slutar före `$F824AA`, som är separat belagd av
`$F89D4A 247c fff8 24aa` (`movea.l #$FFF824AA,A2`) som `ERROR `-strängbas.
Lookupkoden saknar samtidigt en övre gränskontroll; se
`../investigations/panel-input-model.md`.

---

# Sammanfattning av uppnådda resultat

## 1. ROM och OS är inte separata världar

OS:et ersätter inte ROM.

ROM fungerar som:

- reset- och bootstraplager
- hårdvaruinitialisering
- permanent tjänstebibliotek
- exception- och trapplager
- scheduler- och avbrottsinfrastruktur
- en samling fasta entrypoints

OS laddas från disk och bygger vidare på detta.

V1.61 och V3.50 refererar ett mycket stort gemensamt ROM-gränssnitt.
Analysen har identifierat över tusen ROM-adresser i det sammanlagda
entrypoint-materialet och en mycket stor stabil gemensam yta mellan
versionerna.

## 2. Bindningstabellen är ROM/OS-kontraktets konkreta form

OS+`$7600-$95F6` motsvarar under den kända första segmentregeln RAM
`$00801E-$009FF6`.

Området innehåller 723 slots med `JMP`-instruktioner.

ROM använder kort absolut adressering mot slots som `$801E.w`, `$8030.w`
och `$8D50.w`, i stället för att alltid anropa implementationerna direkt.

OS kan därför för varje tjänst välja att:

1. peka sloten mot ROM:s implementation
2. peka sloten mot egen diskresident kod

V3.50 ersätter minst 74 tjänster som V1.61 lät ligga kvar i ROM.
Ytterligare callbacks pekar in i OS i båda versionerna.

Detta är en verklig patch- och kompatibilitetsmekanism.

## 3. Resetsekvensen använder DPRAM som kod

ROM börjar efter reset i ett tillfälligt lågminnesoverlay.

Innan BR0 flyttar ROM till `$F80000-$FBFFFF` kopierar firmware två
instruktioner till MC68302 DPRAM på `$FC6200` och hoppar dit.

DPRAM-koden:

1. programmerar om BR0
2. flyttar ROM ur resetområdet
3. hoppar vidare till ROM:s PIO-init på dess slutliga adress

DPRAM är därför verifierat exekveringsminne, inte bara register-, parameter-
eller thunkområde.

## 4. Chip-select-kartan kommer ur firmware

ROM programmerar samtliga fyra BR/OR-par.

Hittills härledd karta:

| CS | Område | Känd funktion |
|---|---:|---|
| CS0, reset | `$000000-$03FFFF` | ROM-overlay |
| CS0, slutlig | `$F80000-$FBFFFF` | 256 KB ROM |
| CS1 | `$FF6000-$FF7FFF` | okänd, 8 KB |
| CS2 | `$FC2000-$FC3FFF` | ES5506 och ES5510 |
| CS3 | `$FC4000-$FC5FFF` | FDC och DUART |

CS1 är fortfarande en öppen hårdvarufråga.

## 5. OS-filen har minst två laddningsregler

OS-filen ligger från diskoffset `$3000`.

Två starkt stödda relationer har identifierats:

```text
Seg1:
RAM = OS_offset + $0A00
disk = RAM + $2600

Seg2:
RAM = OS_offset - $5A00
disk = RAM + $8A00
```

Seg1 innehåller bland annat den verifierade PAR-kalibreringsrutinen och
bindningstabellen.

Seg2 stöds av många oberoende mål i både V1.61 och V3.50.

Den exakta segmentgränsen är ännu inte känd. Skillnaden mellan reglerna är
`$6400`, vilket tyder på att ett område i filen inte laddas enligt samma
linjära modell.

Ingen generell regel får användas utanför dokumenterad coverage.

## 6. OS-bildens början innehåller vektorer och lågminnesdata

De första vektorerna är versionsstabila.

Bland annat:

- CPU-exceptions pekar till ROM-stubbar
- illegal-instruction och line-F pekar på DPRAM-kod
- TRAP- och schedulervektorer pekar mot ROM
- senare lågminnesområde återanvänds som globala variabler

OS-bildens PC-fält är noll. ROM gör därför inte en traditionell mjuk reset
genom att ladda OS-bildens SSP och PC.

Den exakta ROM→OS-överlämningen är ännu okänd.

## 7. Kataloganalysen har korrigerats

V350 innehåller 28 katalogposter, inte 18.

Den tidigare räkningen utgick från en partiell kopia av katalogen vid
`$41E`. Den kan inte användas som katalogstart.

Den riktiga katalogen börjar på `$600`.

OS-posten:

- typ `$20`
- namn `ASR-10 OS`
- startblock 24
- diskoffset `$3000`

## 8. Timer 2 är aktiv som intern periodisk räknare

ROM skriver:

```text
TMR2 = $003B
TRR2 = $3F01
```

Mot MC68302-manualen innebär detta:

- timern enabled
- master clock
- restart vid referens
- prescaler 1
- periodisk modulo `$3F01`
- ungefär 1,008 ms per cykel vid 16 MHz

Timer 2-interruptet är inte avmaskerat i hittills identifierad ROM-kod.

TOUT2 är inte routad ut eftersom PB6 används som GPIO.

V3.50 läser TCN2 och lagrar räknarvärdet i en struktur.
V1.61 har ingen motsvarande känd Timer 2-access.

Timer 2 används därför sannolikt som en internt avläst periodisk tidsbas i
V3.50. Den exakta konsumenten och semantiken är ännu okänd.

## 9. SCC- och CP-kod finns, men aktivering är inte fullständigt kartlagd

ROM konfigurerar:

- SIMODE
- SCC1
- SCC2
- SCC3
- DSR-register
- parameter-RAM
- CP Command Register

Kommandona:

```text
$21 = ENTER HUNT MODE, SCC1
$23 = ENTER HUNT MODE, SCC2
```

är verifierade mot MC68302-manualen.

De hittade absoluta SCM-skrivningarna har ENR och ENT avstängda.
Det är ännu inte bevisat att SCC:erna förblir avstängda, eftersom
registerrelativ kod via bindningsslot `$8D50.w` återstår att analysera.

## 10. Port B har fått en betydligt tydligare roll

PBCNT och PBDDR ger preliminärt:

- PB0–PB2: utgångar för ADC-kanalval
- PB3: LRCLK-ingång och källa till ERROR 009-testet
- PB6: GPIO, inte TOUT2
- PB7: multiplexad WDOG-utgång
- PB9–PB11: ingångar och de enda hittills identifierade internt avmaskerade
  MC68302-portavbrotten

Watchdogens faktiska drift och service är ännu inte utredd.

---

# Öppna frågor

## ROM→OS-överlämningen

Följande är ännu okänt:

- den första exekverade OS-rutinen
- instruktionen som överlämnar kontrollen
- om överlämningen sker genom slot, indirekt hopp, stackad retur eller
  schedulerdispatch
- när OS-vektortabellen installeras i lågminnet
- när bindningstabellen blir aktiv

## Adressalias i högminnet

ROM:s korta absoluta anrop bildar effektiva adresser i
`$FF8000-$FFFFFF`, medan bindningstabellens kända RAM-position är
`$008000-$009FFF`.

Det finns starkt statiskt stöd för att extern adressdekodning aliasar dessa
områden, men ingen runtimeverifiering ännu.

Ingen ändring av `mem_map` får göras innan detta testats.

## OS-segmentgränsen

Seg1 och Seg2 är starkt stödda, men följande är öppet:

- exakt gräns
- vad det återstående ungefär 24 KB stora området innehåller
- om ytterligare segment eller overlays finns
- om laddaren hoppar över ett exakt `$6400` byte stort område

## CS1

`$FF6000-$FF7FFF` har egen chip select men ingen verifierad användning.

Möjliga roller måste härledas från runtimeaccesser, schema och befintlig
68302-emulatorkod.

## MC68302

Följande är fortfarande ofullständigt:

- Timer 2-snapshotens callers
- Timer 2:s exakta funktion i V3.50
- watchdogkonfiguration och service
- PB9–PB11:s fysiska signalkällor
- SCC1–SCC3:s faktiska enablekedjor
- buffer descriptors och parameter-RAM
- SMC/SCP
- IDMA/SDMA
- interna vektorer `$40-$4F` och verkliga handlers
- eventuell latent aktivitet från externa pins utan senare SIB-skrivningar

## Vektorer

Kanoniska luckor:

- levande verifiering av exception- och TRAP-vektorer
- handlersemantik för flera TRAP-nummer
- line-A-dispatch och faktisk användning av `$A000`
- line-F-koden i DPRAM
- MC68302 interna vektorer och installerade handlers
- externa IRQ1 och IRQ7
- PB8–PB11-källkedjor
- indirekta DUART-handlerpekare

## Rutinindex

CSV-materialet identifierar många entrypoints men namnger inte deras
semantik.

`reference/subroutine-index.md` ska byggas ut stegvis, inte ersättas av en
automatisk lista.

Varje ny post bör innehålla:

- runtimeadress
- image och filoffset
- namn
- ansvar
- callers
- callees
- data/register som läses och skrivs
- versionsstatus
- evidensnivå
- coverage
- kända osäkerheter

---

# Metodregler

1. Skilj alltid mellan:
   - refererad
   - konfigurerad
   - enabled
   - interrupt avmaskerat
   - exekverad
   - observerat aktiv

2. Två ankare bevisar inte ett helt segment.

3. Absolut adressökning hittar inte registerrelativa accesser.

4. Rå bytecensus kan inte skilja kod från data.

5. Runtimeexperiment får inte ändra minneskartan innan observationen är
   granskad.

6. Alla felaktiga slutsatser ska bevaras som retraktioner i worklog för att
   förhindra återfall.

7. Varje stabilt fynd ska ange sin framtida kanoniska destination.

---

# Livscykel för denna katalog

`static/` är inte tänkt som permanent slutstruktur.

Den kan tas bort eller arkiveras när:

- råtabellerna har en reproducerbar generator eller bevarats där de behövs
- stabila fynd har flyttats till `reference/`
- worklogens öppna frågor har egna ärenden eller undersökningsdokument
- retraktionerna har bevarats i ett lämpligt arkiv
- CSV-filerna har fått en definierad roll som genererade analysartefakter

Fram till dess är detta projektets stagingområde för statisk kunskap.

---

# Tillagg 2026-08-04

## Katalogens roll ar permanent

`static/` ar **inte** langre en temporar stagingyta som ska tommas. Den tidigare
formuleringen om att katalogen kan tas bort efter konsolidering ar aterkallad.

| katalog | innehall |
|---|---|
| `reference/` | slutsatser, modeller, manuellt vardad kunskap |
| `static/` | reproducerbara analysresultat, tabeller, arbetsloggar |
| `investigations/` | fragor som drivs aktivt just nu |
| `archive/` | avslutade eller ersatta berattelser |

CSV-filerna hor hemma har aven langsiktigt och ska **inte** flyttas till `reference/`.

## Analyserade kallartefakter

| artefakt | storlek | SHA-256 |
|---|---|---|
| `asr10.bin` | 262 144 | `fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf` |
| `V161.img` | 1 638 400 | `2a5cc161e80001daddf532914e80e854d3079f152197ab3def31055f239182f6` |
| `V350.img` | 1 638 400 | `2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b` |

OS-filen i respektive avbild: block 24, diskoffset `0x3000`. V1.61 173 block
(88 576 B), V3.50 382 block (195 584 B).

## Genererade kontra handkurerade filer

| fil | rader | typ | SHA-256 (16) |
|---|---|---|---|
| `call-graph-edges.csv` | 5 243 + rubrik | **genererad bas** | `472373eea0fac895` |
| `call-graph-observations-v350-rx22.csv` | 5 + rubrik | **runtime-observationer** | `aa0ee0423c32f1ab` |
| `os-binding-table.csv` | 723 + rubrik | **genererad** | `600f646943d5a3a7` |
| `panel-file1-tx-window-v350.csv` | 11 + rubrik | **genererad** | `e8deb6af538aef62` |
| `panel-03c0-block-trace-v350-file1.csv` | 475 + rubrik | **genererad** | `39410814e2bbb5d8` |
| `panel-button-sweep-v350.csv` | 64 + rubrik | **genererad, oberoende körningar** | `29583d8c6f3ff27` |
| `panel-button-sweep-v350-cumulative.csv` | 64 + rubrik | **genererad, kumulativ historik** | `8216a603bcf205b8` |
| `panel-frame-blockdiff-v350-4040.csv` | 1 + rubrik | **genererad** | `bddf8c2f54b131fa` |
| `panel-frame-injection-v350-4001.csv` | 5 + rubrik | **genererad** | `f94f851dc31e8bbf` |
| `panel-frame-injection-v350-4040-es5506.csv` | 3 + rubrik | **genererad** | `7c6adea901116f51` |
| `panel-frontpanel-branch-sweep-v350-003f.csv` | 64 + rubrik | **genererad** | `373cb59ed83720e8` |
| `panel-frontpanel-branch-sweep-v350-407f.csv` | 64 + rubrik | **genererad** | `667d7173527105ce` |
| `panel-frontpanel-branch-sweep-v350-80bf.csv` | 64 + rubrik | **genererad** | `68690a0477e2e690` |
| `panel-raw-byte-sweep-v350.csv` | 256 + rubrik | **genererad** | `8a575265c39f66fa` |
| `panel-reply-substitution-v350.csv` | 11 + rubrik | **genererad, partiell** | `17bfe2306e3f2ba4` |
| `rom-abi-entrypoints.csv` | 1 054 + rubrik | **genererad** | `6293e5c9cffeb4f4` |
| `routines.csv` | 25 + rubrik | **handkurerad** | `3aee116585356b06` |

Genererade filer far **inte** redigeras for hand - de skrivs over vid nasta korning.
Runtime-observationer hor hemma i en separat fil som slas ihop vid generering.
`call-graph-observations-v350-rx22.csv` innehaller de fem rx22-kanter som forst
lades direkt i `call-graph-edges.csv`; regenerering ska appenda dessa rader
efter den genererade basen, med oforandrade `edge_id`, `execution_source` och
tidsstamplar.
`routines.csv` ar motsatsen: den vaxer en rad i taget, med explicit `boundary_evidence`
och `source_document` per post.

## Vad filerna innehaller - och inte

```
os-binding-table.csv
    Statiskt extraherade bindningsslots och versionsberoende mal.
    723 slots pa RAM $00801E-$009FF6.

rom-abi-entrypoints.csv
    Statiskt identifierade direkta referenser fran OS till ROM.
    1054 adresser, per-version rakning.

call-graph-edges.csv
    Kontrollflodeskanter pa ANROPSSTALLENIVA, inte rutinniva.
    executed=unknown betyder INTE att kanten ar falsk eller oanvand -
    det betyder att ingen namngiven observation annu ar knuten till den.
    Genererad bas: observed 7, inferred_from_trace 3,
    not_reached_in_bounded_run 4, unknown 5229.
    Sammanslagen vy med runtime-observationer: observed 12,
    inferred_from_trace 3, not_reached_in_bounded_run 4, unknown 5229.
    Evidence-fordelning i sammanslagen vy: static-abs.l 2285,
    static-table 1490, static-abs.w 1362, static-vectortable 92,
    runtime-PC 8, observed 5, static 4, static+arch 2.

routines.csv
    Rutinidentiteter med explicit gransevidens och proveniens.
    confidence:       11 verified, 13 start-verified, 1 likely
    canonical_status: 20 resolved, 2 partially_resolved,
                      3 unresolved_high_priority
    routine_id identifierar en konkret adress i en konkret version.
    Versionsberoende rutiner har separata rader som delar logical_name.
```

## Kantantalet: 5240 -> 5243

En tidigare generation av `call-graph-edges.csv` hade 5240 rader. Skillnaden ar **inte**
ett raknefel och inte tva blandade generationer:

```
statisk bas          5229 kanter   ofoerandrad mellan generationerna
runtime/arkitektur     11 -> 14    tre rader tillagda
                     ----
                     5240 -> 5243
```

Jamfor alltid `evidence`-fordelningen mellan generationer, aldrig totalen.
`edge_id` ar verifierat globalt unikt (5243 av 5243), och alla 745 versionsberoende
slotmal har separata identifierare per version.

## Metodens tre begransningar

Metoderna ar dokumenterade i `../reference/methods-static-analysis.md`.

1. **Registerrelativ adressering `(d,An)` syns inte.** Varje "noll referenser" i
   materialet betyder "noll *identifierade absoluta* referenser". Den blinda flacken ar
   reell och inte teoretisk: SCC-avbrottshanterarna laddar `movea.l #$00FC6880,A1` och
   adresserar hela registerblocket relativt A1.
2. **1404 kanter hanger pa speglingshypotesen** `$FFxxxx <-> $00xxxx`. De ar markta
   `to_space=HIGH-RAM-ALIAS-CANDIDATE` och `mapping_basis=mirror-hypothesis`.
   Experiment E2 avgor den. **Ingen text far beskriva speglingen som runtimeverifierad.**
3. **833 kanter saknar RAM-adress** eftersom segmentgransen mellan segment 1 och 2 inte
   ar faststalld. De har `from_address_status=unmapped-segment` med koordinaten kvar i
   `from_file_offset`. Experiment E3 avgor den.

## Vilka referensdokument som harleder kunskap harifran

| referensdokument | harleder ur |
|---|---|
| `reference/rom-os-abi.md` | `os-binding-table.csv`, `rom-abi-entrypoints.csv` |
| `reference/call-graph.md` | `call-graph-edges.csv`, `routines.csv` |
| `reference/os-image-layout.md` | worklogarna, segmentanalysen |
| `reference/mc68302-status.md` | SIB-registercensusen i worklog 2 |
| `reference/memory-map.md` | chip-select-avkodningen, DPRAM-analysen |
| `reference/vector-map.md` | vektortabellsjamforelsen V1.61/V3.50 |
| `reference/methods-static-analysis.md` | metoderna och deras felkallor |

## Worklogarna

`worklog-rom-os-abi.md` och `worklog-rom-os-abi_2.md` ligger kvar som **provenance**,
inte som kanonisk sanning. De far inte tyst tas bort. Deras retraktioner (R1-R5) ar
speglade i respektive referensdokument.
