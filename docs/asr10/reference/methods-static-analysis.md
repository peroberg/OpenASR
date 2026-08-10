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

---

## 9. Statik före stimulans

Identifiera först, stimulera sedan. Inte för att statisk analys är finare, utan för att
den krymper stimulansens sökrymd. En råbytesvepning över 128 värden utan förkunskap ger
128 okända experiment; samma svepning efter att tabellen och dess konsumenter är
identifierade ger tio riktade.

Belagt fyra gånger: bindningstabellen, ROM->OS-överlämningen, PB10/PB11,
segmentreglerna. Alla blev enkla först när statiken hade begränsat sökrymden.

---

## 10. Rapportdisciplin

Fyra nivåer, plus täckning:

```
[Verified]    reproducerbart belägg anges, inklusive metod
[Likely]      stark indikation, alternativ förklaring finns kvar
[OPEN]        ställd fråga utan svar
[DISPROVEN]   testad hypotes som föll — behåll den och skriv hur den föll
Coverage:     vilket adressintervall / hur många oberoende ankare
```

En väl dokumenterad öppen fråga är mer värd än en halvbevisad lösning. Motbevisade
hypoteser ska stå kvar — de hindrar att samma väg utforskas igen.
