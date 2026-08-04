# Worklog — ROM↔OS-kontraktet (statisk analys)

Källor: `asr10.bin` (256 KB, `$F80000-$FBFFFF`), `floppies/asr10booth/V161.img`,
`floppies/asr10booth/V350.img`. Endast statisk analys — inget körts, inget skrivet i trädet.

Bilaga: `rom-abi-entrypoints.csv` (1054 ROM-adresser, per-version anropsräkning).

---

## F1 — Filsystemet: katalogen ligger på 0x600, inte 0x41E  [Verifierat]

Katalogposten är 26 byte:

```
+0  type.w      +2  namn[12]      +14 size.w (block)
+16 flag.w      +18 startblock.l  +22 reserved.l
```

Katalogen börjar på **diskoffset 0x600** i *båda* avbilderna. Filsystemets block är
512 byte och `filoffset = block * 512` rakt av.

| avbild | vol-ID (0x21F) | poster | kedja | högsta block |
|---|---|---|---|---|
| V161.img | `ASR161OID` | 14 | 13/13 sammanhängande | 328 av 3200 |
| V350.img | `OS-V350ID` | **28** | 27/27 sammanhängande | 3175 av 3200 |

Volymhuvudet på 0x400: ordet på **0x402 = antal fria block** (V350: 25 = 3200−3175,
V161: 2872 = 3200−328). [Verifierat, 2 datapunkter]
Orden på 0x404 (`$0335` / `$013D`) och 0x406 (`$0132` i båda) är **oidentifierade**. [OPEN]

**Korrigering av tidigare underlag:** V350 beskrevs som "18 filer, kedja 17/17".
Det kom av att V350 har en **byte-identisk delkopia av katalogen på 0x41E**
(`d[0x41E:0x600] == d[0x600:0x7E2]`, verifierat) som klipps av vid 0x600. Att läsa
därifrån ger 18 poster och sedan skräp. V161 har ingen sådan kopia — 0x41E är där
`$0000`. Varför V350 har kopian, och varför den ligger på ett offset som inte är
postjusterat mot 0x600 (skillnad 482 byte = 18,5 poster): [OPEN].

OS-filen i båda: **typ `$0020`, namn `ASR-10 OS`, startblock 24 → diskoffset 0x3000**.

* V161: 173 block → 0x3000..0x18A00 (88 576 byte)
* V350: 382 block → 0x3000..0x32C00 (195 584 byte)

---

## F2 — ROM:s överlämning till sig själv: BR0-omprogrammeringen körs ur DPRAM  [Verifierat]

Hela sekvensen från reset, avkodad ur `asr10.bin`:

```
$F80000  00000300 0000000C     SSP / PC (ROM ligger överlagrat på $000000 vid reset)
$F8000C  46FC 2700             move.w  #$2700,SR
$F80010  31FC 0FC6 00F2        move.w  #$0FC6,($00F2).w
$F80016  21FC 0F200000 00F4    move.l  #$0F200000,($00F4).w
$F8001E  33FC 3F82 00FC6832    move.w  #$3F82,(OR0)
$F80026  33FC 0001 00FC6830    move.w  #$0001,(BR0)
$F8002E  33FC FFFE 00FC6836    move.w  #$FFFE,(OR1)
$F80036  33FC 1FEF 00FC6834    move.w  #$1FEF,(BR1)
$F8003E  33FC FFFC 00FC683A    move.w  #$FFFC,(OR2)
$F80046  33FC 1F85 00FC6838    move.w  #$1F85,(BR2)
$F8004E  33FC 7FFC 00FC683E    move.w  #$7FFC,(OR3)
$F80056  33FC 1F89 00FC683C    move.w  #$1F89,(BR3)
$F8005E  41FA 0018             lea     ($F80078),A0
$F80062  45FA 0022             lea     ($F80086),A2
$F80066  227C 00FC6200         movea.l #$00FC6200,A1
$F8006C  32D8                  move.w  (A0)+,(A1)+     \
$F8006E  B5C8                  cmpa.l  A0,A2            |  kopiera 14 byte till DPRAM
$F80070  66FA                  bne.s   $F8006C         /
$F80072  4EF9 00FC6200         jmp     $00FC6200        <-- kör vidare ur DPRAM
--- kopian, exekveras på $FC6200 ---
$F80078  33FC 1F01 00FC6830    move.w  #$1F01,(BR0)     <-- flyttar ROM från $000000 till $F80000
$F80080  4EF9 FFFB8E06         jmp     $FFFB8E06        <-- PIO-init
```

Poängen: **BR0-skrivningen drar undan marken under den kod som kör**, så ROM kopierar
just de två instruktionerna till 68302:ns DPRAM och hoppar dit. Detta är den första
verifierade användningen av DPRAM som exekveringsyta och stärker hypotesen "DPRAM är
ett firmware-lager" (vektor 4 → `$FFFC6000`, vektor 11 → `$FFFC6014`).

### Chip-selects avkodade

Avkodning enligt BRx/ORx-formatet (adressfält `$1FFC`, skift 11, enable = bit 0,
FC-jämförelse = OR bit 15):

| CS | BR | OR | fönster | innehåll |
|---|---|---|---|---|
| CS0 (reset) | `$0001` | `$3F82` | `$000000-$03FFFF` | ROM-överlägg vid boot |
| CS0 (efter) | `$1F01` | `$3F82` | **`$F80000-$FBFFFF`** | ROM 256 KB |
| CS1 | `$1FEF` | `$FFFE` | **`$FF6000-$FF7FFF`** | 8 KB, **oidentifierat** [OPEN] |
| CS2 | `$1F85` | `$FFFC` | **`$FC2000-$FC3FFF`** | ES5506 (`$FC2000`) + ES5510 (`$FC3000`) |
| CS3 | `$1F89` | `$7FFC` | **`$FC4000-$FC5FFF`** | FDC (`$FC4000`) + DUART (`$FC4801`) |

Detta är första gången minneskartan är härledd ur ROM:s egna skrivningar i stället
för ur drivrutinens `mem_map`. Allt utanför CS0–CS3 och BAR-fönstret (`$FC6000`) måste
avkodas av kortlogiken — 68302:an gör det inte.

`$FF6000-$FF7FFF` ligger direkt under `$FF8000` och är den enda chip-select som ännu
inte har någon känd användning. Värt en egen undersökning.

---

## F3 — OS-bilden laddas i (minst) två segment  [Verifierat metod, gränser Sannolika]

### Metod

För varje `jsr/jmp abs.l` med RAM-mål `T` i OS-filen testas kandidatoffset `K`
(RAM = OS_off − K) genom att kräva att ordet *före* `T+K` är en avslutare
(`rts/rte/rtr/jmp/nop`). `$0000` räknas **inte** som avslutare — det gav i en tidigare
körning en falsk topp på 43–50 % enbart från nollutfyllnad, vilket jag först tog för
ett resultat. Baslinjen ligger på 2–6 %.

### Verifierat ankare

`ori.b #$07,($00FC6829).l` (PAR-kanalvalet, live-verifierat i MAME via
exception-ramens `fallande_PC=006802`) ligger på **diskoffset 0x08DEC**.

```
RAM $0067EC  <->  disk 0x08DEC  <->  OS+0x05DEC        disk = RAM + 0x2600
```

Detta bekräftar den gamla `+0x2600`-regeln — men bara för sitt segment.

### Två segment

Fördelning av validerade ankare per 4 KB RAM (V350; V161 identisk form):

```
RAM $002000-$007FFF   Seg1 dominerar   (Seg2: 1 träff, Seg1: 10)
RAM $008000-$008FFF   inga mål alls i någon version   <-- ren gräns
RAM $009000-$01D6A0   Seg2 dominerar   (Seg2: 259, Seg1: 8)
```

| segment | regel | ankare V161 | ankare V350 | filområde (ankarnas span) |
|---|---|---|---|---|
| Seg1 | `RAM = OS_off + 0xA00` (`disk = RAM + 0x2600`) | 10 | 10 | OS+0x01B1C..0x072C4 |
| Seg2 | `RAM = OS_off − 0x5A00` (`disk = RAM + 0x8A00`) | 78 | 259 | OS+0x0F788..0x230A0 |

Seg2-regeln är rangordnad **1 av 40 960** kandidater i V350 (261/425 = 61,4 %; tvåan
26 %) och **1 av 21 422** i V161 (78/133 = 58,6 %) — och det är *samma* konstant i två
OS-versioner. Detta är alltså inte längre "två ankare": Seg1 har 10 oberoende ankare
per version plus ett live-verifierat, Seg2 har 78 respektive 259.

Stickprov, RAM `$009DA0` (Seg2 → OS+0x0F7A0):
`... 00 0A 4E75 | 9EFC 000C 48A7 8000 48E7 00C0 61B2 ...`
= `rts` följt av `suba.w #$0C,A7 / movem.w D0,-(A7) / movem.l D0-D1,-(A7)` — läroboks-
prolog. Samma adress under Seg1-regeln ger `3FB6 3FBA 3FB6 9884 ...`, dvs data.

### Det som INTE går ihop

* **Filhålet.** Mellan segmentens ankarspann ligger OS+0x072C4..0x0F7A4 —
  **34 016 byte i V161, 33 944 i V350**. Nästan identisk storlek i båda versionerna,
  alltså strukturellt, inte slumpmässigt. Vart det området laddas: **[OPEN]**.
* **Vektortabellen.** OS+0x0000..0x03FF är en 68000-vektortabell (se F4), men under
  Seg1-regeln hamnar den på RAM `$000A00`, inte `$000000`. Någon måste kopiera ner den.
  **[OPEN]** — se experiment E1.
* Bilden laddas alltså inte som en platt bild till en enda bas. "Bootbar" var, som du
  sa, fel ord.

### Två hypoteser jag testade och som föll

1. **Wrap-modellen** (hela filen laddad på bas `$FFA600` så att OS+0x5A00 hamnar på
   `$000000`): testad mot 7 rena `abs.l`-mål i `$FFA600-$FFFFFF` per version →
   **0/7 giltig föregångare i båda**. [DISPROVEN]
2. **Preambel i högminne** (OS+0..0x5A00 → `$FF8000`-området): sveptest över alla
   baser `$FF8000-$FFF000`, `$FFA600` hamnade på plats 10195 av 12424. [DISPROVEN]

---

## F4 — OS-bildens vektortabell är identisk mellan V1.61 och V3.50  [Verifierat]

`os[0x000:0x0C0]` (vektor 0–47) är **byte-för-byte identisk** i V161 och V350.
I `0x0C0..0x3FF` skiljer sig endast **13 byte** (offset 199, 201, 212, 213, 215, 217,
219, 224, 225, 418, 419, 470, 471). Det området är alltså inte vektorer utan
OS-variabler, precis som tidigare antaget, och de är nästan helt versionsstabila.

```
v0  SSP = $00000300
v1  PC  = $00000000          <-- noll i BÅDA versionerna
v2  $FFF882AA   v3  $FFF882AE   v5  $FFF882B6   v9  $FFF882C6
v4  $FFFC6000  (DPRAM)       v11 $FFFC6014  (DPRAM)
v10 $FFF882CA   v15 $FFF882DA   v24 $FFF882D6   v25-31 $FFF882DA
v32 $FFF88280  (gemensam exception-svans)      v33 $FFF87F76 (schemaläggare)
v40 $FFF8812C   v47 $FFF88056
```

Av vektor 0–47: 62 pekar i ROM, 3 i DPRAM, resten i RAM/odefinierat.

Att **v1 (PC) = 0** i båda versionerna betyder att ROM **inte** kan göra en
"mjuk reset" som hämtar SSP/PC ur bilden — svaret på din fråga 4 är nej.
Det finns ingen startadress i bildens huvud. [Verifierat]

---

## F5 — Den mätbara ABI-ytan: 1054 ROM-adresser, 895 gemensamma  [Verifierat]

Räknat på `4EB9`/`4EF9` med mål i `$F80000-$FBFFFF` plus vektortabellens ROM-pekare.

| | V161 | V350 |
|---|---|---|
| distinkta ROM-adresser | 975 | 975 |
| gemensamma båda versioner | **895** | |
| endast V161 | 79 | |
| endast V350 | 80 | |

**92 % av ROM-anropsytan är oförändrad mellan V1.61 och V3.50.** Det är den bästa
enskilda indikationen på att ROM↔OS-gränssnittet är ett *avsiktligt kontrakt* och inte
bara sammanlänkad kod.

Mest anropade (fullständig lista i CSV:n):

```
$F95EAA   V161 1   V350 33     <-- största versionsskillnaden i hela materialet
$FB84DA   V161 8   V350 11
$FBA0D6   V161 6   V350  9
$F8CAB4   V161 7   V350  7
$F97662   V161 5+2 V350 5+2    (jsr+jmp)
$F976CA   V161 3+3 V350 3+3    kritisk sektion (återställ SR) — känd
$F87FD2   V161 5   V350  5     schemaläggarområdet — känd
$FBA4FE   V161 2   V350  9
$FBA5A2   V161 6   V350  5
$FB813C   V161 9   V350  0     <-- borttagen i V350
```

`$F95EAA` går från 1 till 33 anrop mellan versionerna. Om någon rutin ska
identifieras först är det den.

`$F882DA` förekommer 34 gånger i båda — det är catch-all-stubben i
exception-tabellen, alltså vektorer, inte anrop.

### Länkregion i OS-bilden

`OS+0x07F34..0x09502` innehåller **597 `4EF9 <ROM-adress>`-slots** i *båda* versionerna,
med **exakt samma 23 luckor på exakt samma offset**. Det är samma modul kompilerad två
gånger med olika måladresser — en explicit bindningsyta mot ROM. 505 av 596 slots har
dessutom *samma* mål i båda versionerna.

Blandat i regionen finns `4EF8 xxxx` (`jmp abs.w`), dvs hopp till RAM via kort
adressering.

---

## F6 — `$FF8000-$FFFFFF` är samma minne som `$008000-$00FFFF`  [Sannolikt — måste verifieras]

Detta är turens viktigaste öppna fråga, och den påverkar drivrutinen.

**Observation A.** ROM gör **1362 `jsr/jmp abs.w`-anrop till 374 distinkta adresser i
`$FF801E..$FFA26E`**. Noll av de 374 målen är udda — vid slumpdata vore ~50 % udda, så
detta är riktiga instruktioner, inte falska träffar. Kontroll av enskilda platser
bekräftar: `$F97874: 12 1A / 4EB8 8030 / 4E75` = `move.b (A2),D1 / jsr $FF8030 / rts`.
200 av anropen går till `$FF8030` ensamt.

**Observation B.** OS-bilden anropar samma område, upp till `$FFFF8C`.

**Observation C.** Sveptest över baser i `$FF0000-$FFFFFF` för OS:ets högminnesmål
ger en entydig topp på **`$FF09FE` ≈ `$FF0A00`** (V350 355/623 = 57 %, rang 1 av 29 217;
V161 222/492 = 45 %, rang 1). Seg1-regeln säger att OS+0 hamnar på RAM `$000A00`.

`$FF0A00 − $000A00 = $FF0000`. Med andra ord: **innehållet på `$FFxxxx` är samma
OS-innehåll som på `$00xxxx`.**

**Tolkning.** Kortlogiken speglar sannolikt RAM så att `$FF0000-$FFFFFF` är samma
celler som `$000000-$00FFFF`. Det ger 68000:ans korta absoluta adressering
(`$8000-$FFFF` → `$FF8000-$FFFFFF`) ett sammanhängande 64 KB-fönster över RAM
`$0000-$FFFF` — 2 byte kortare per instruktion. Det förklarar också varför både ROM
och OS lägger sina hetaste rutiner just där, och varför `($8D50).w` (= `$FF8D50`) i
SCC-koden aldrig gick att placera: den läser RAM `$008D50`.

**Konsekvens för drivrutinen.** `asr10_boot.cpp` har i dag
`map(0xfc5020, 0xffffff).ram()` — `$FF8030` är alltså *separat* RAM från `$008030`.
Om speglingen är verklig kommer varje anrop till de 374 ROM-rutinerna i `$FF8xxx` att
landa i oinitierat minne. Att maskinen ändå bootar till `FILE 1` betyder bara att inget
av dessa anropsställen har nåtts än. **Ändra ingenting förrän E2 nedan är körd.**

---

## Svar på dina fem frågor

**1. Var laddas OS?**
Inte på en enda bas. Minst två segment: `RAM = OS_off + 0xA00` för RAM `$000A00-$007FFF`
och `RAM = OS_off − 0x5A00` för RAM `$008000` och uppåt. Ett filområde på ~34 000 byte
mellan dem saknar placering. [Seg1 verifierat mot live-ankare + 10 statiska ankare per
version; Seg2 verifierat mot 78/259 ankare; hålet OPEN]

**2. När skrivs OS-vektortabellen till RAM?**
Bilden bär sin vektortabell på OS+0x0000, och den är identisk mellan versionerna.
Under Seg1-regeln landar den på RAM `$000A00`, inte på `$000000`. Alltså måste något
kopiera `$000A00-$000DFF` → `$000000-$0003FF`, eller så installerar laddaren tabellen
separat. **När detta sker är inte fastställt.** [OPEN — se E1]

**3. När börjar CPU:n använda OS-vektorer i stället för ROM-vektorer?**
Frågan är delvis felställd: OS:ets vektorer *pekar in i ROM*. 62 av de 48 första
vektorerna (räknat med dubbletter) går till ROM:s stubbtabell `$F882AA-$F882DA` och
till `$F88280`/`$F87F76`. Övergången är alltså inte "ROM-vektorer → OS-vektorer" utan
"ROM:s egen tabell → OS:ets tabell som fortfarande dirigerar till ROM, plus tre
DPRAM-poster (v4, v11) och OS-egna poster". Exakt ögonblick: samma som fråga 2. [OPEN]

**4. Hoppar ROM till OS:ets PC, eller görs en mjuk reset?**
**Nej till mjuk reset.** `v1 (PC) = $00000000` i både V1.61 och V3.50. Det finns ingen
startadress i bilden. ROM måste hoppa till en adress den känner till på annat sätt —
antingen hårdkodad eller ur ett fält jag ännu inte hittat. [Verifierat att bilden inte
innehåller den; var ROM tar den ifrån är OPEN]

**5. Vilka ROM-rutiner fortsätter OS att anropa?**
**895 gemensamma ROM-entrypoints**, 92 % av ytan oförändrad mellan V1.61 och V3.50,
plus en explicit `4EF9`-bindningstabell på OS+0x07F34..0x09502 med 597 slots och
identisk luckstruktur i båda versionerna. Fullständig lista i
`rom-abi-entrypoints.csv`. [Verifierat]

---

## Föreslagna experiment (kräver körning — din sida)

**E1 — vektortabellens installation.** Lua-skrivtapp på `$000000-$0003FF` och på
`$000A00-$000DFF` under boot, logga `PC` vid varje skrivning. Svarar på fråga 2 och 3
i en körning, och visar samtidigt om laddaren skriver `$000A00` först eller `$000000`
först.

**E2 — speglingen `$FFxxxx` ↔ `$00xxxx` (viktigast).** Lässtapp på `$FF8000-$FFFFFF`.
Två utfall:
* Om ROM/OS någonsin *exekverar* eller läser där under boot och drivrutinen ger nollor,
  är speglingen verklig och `mem_map` måste ändras.
* Om området aldrig rörs under boot är F6 fortfarande obesvarad, men ofarlig tills
  vidare — och då vet vi vilken funktion (t.ex. första `jsr $FF8030`) som väcker den.

**E3 — laddningskartan.** Skrivtapp med adressintervall-histogram över hela boot-
laddningen (från första FDC-läsning till `KEYBOARD TUNED`). Ger den faktiska
RAM-placeringen för hela OS-filen på en körning och avgör hålet på 34 000 byte direkt.
Detta ersätter allt gissande ovan.

**E4 — `$FF6000-$FF7FFF` (CS1).** Läs/skriv-tapp. 8 KB med egen chip-select som ingen
ännu förklarat.

---

## Fel jag gjorde i den här omgången, för protokollet

1. Läste V350:s katalog från 0x41E (delkopian) och fick 18 poster i stället för 28.
   Det var *också* källan till den gamla siffran i underlaget.
2. Räknade `$0000` som giltig instruktionsavslutare i det första sveptestet, vilket gav
   en falsk 43–50 %-topp för wrap-modellen. Rättat; modellen föll till 0/7.
3. Svepte bara `K ≥ 0` i första offsetsökningen, så det verifierade `+0xA00`-segmentet
   (som kräver negativt `K`) kunde per konstruktion inte hittas. Hittade `0x5A00`,
   trodde det var hela svaret, och upptäckte felet först när jag sökte
   `0039 0007 00FC6829` som byte-mönster och fick disk 0x08DEC i stället för 0x0F1EC.
   Båda offseten var riktiga — men för olika segment.
