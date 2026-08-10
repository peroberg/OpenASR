# ASR-10 — arkitekturen mellan ROM och OS

Detta dokument beskriver *modellen*. Adresser och tabeller finns i
`memory-map.md`, `os-image-layout.md`, `call-graph.md` och i
`static/os-binding-table.csv`.

Läs det här först om du är ny i projektet.

---

## 1. Grundmodellen

ASR-10 är **inte** en maskin där ROM startar upp, laddar ett operativsystem och sedan
lämnar över. ROM är permanent infrastruktur som körs hela maskinens livstid.

```
ROM  = bootstrap + hårdvarulager + permanent tjänstebibliotek + schemaläggare
OS   = diskladdad programvara som bygger ovanpå ROM:s fasta ABI
       ...OCH som får överskugga enskilda ROM-tjänster
```

De tre viktigaste konsekvenserna:

**Kontrollflödet är dubbelriktat.** ROM anropar in i OS och OS anropar in i ROM. Det är
inte ett lager-under-lager. Arbetsfördelningen är inte symmetrisk — ROM äger
lågnivådrivrutinerna (panelens seriella protokoll, FDC-transaktioner, schemaläggaren),
OS äger den högre logiken (filbläddrare, UI-tillstånd, sequencer) — men **båda
riktningarna är belagda**.

**OS-filen är inte en fristående körbar bild.** Den saknar startadress
(vektor 1 = `$00000000` i både V1.61 och V3.50), och av de 48 verkliga vektorerna
(0–47) pekar **42 in i ROM och 2 i DPRAM**. Den kan inte köras utan ROM.
(De 208 långorden på vektorplats 48–255 är OS-variabler, inte vektorer — se
`vector-map.md` §5.)

**Kontraktet är versionsstabilt.** Av 1054 ROM-adresser som OS-bilderna refererar är
895 (85 %) gemensamma för V1.61 och V3.50 — två versioner som ligger flera år isär.

---

## 2. Bindningstabellen — den tydligaste identifierade ABI-strukturen

Bindningstabellen är den mest omfattande och tydligast avgränsade ABI-struktur som
hittills identifierats. Den är **inte** bevisat den enda — indirekta
kontrollöverföringar är okartlagda, se `call-graph.md` §2.5.

### Vad den är

En array av hoppinstruktioner som **OS-filen levererar** och som **ROM anropar**.

```
Placering i RAM      $00801E - $009FF6      (723 poster)
Kortadresserad som   $801E.w  - $9FF6.w     ⇒ effektiv adress $FF801E-$FF9FF6
Källa i OS-filen     OS+0x7600 - OS+0x95F6
Postformat           4EF9 <32-bitars adress>   (6 byte)
                     4EF8 <16-bitars adress>   (4 byte)
```

ROM anropar **342 av de 723 slotarna, från 1254 anropsställen**, uteslutande med
`jsr/jmp abs.w`. Det är hela mekanismen: ROM känner aldrig till en ROM-adress för en
tjänst — den känner till en slot.

### Vad varje slot kan peka på

| slotmål | betydelse | antal (V3.50) |
|---|---|---|
| en ROM-adress | använd ROM:s implementation | majoriteten |
| en OS-adress | OS levererar tjänsten (callback eller override) | 107 |
| en annan slot | kedjad omdirigering | 36 |

### Varför det spelar roll: patchmekanismen

567 av 723 slots pekar likadant i båda versionerna. De 156 som skiljer sig är de
intressanta:

| förändring V1.61 → V3.50 | antal |
|---|---|
| ROM-mål → **OS-mål** (V3.50 ersätter en ROM-rutin med egen kod) | **74** |
| OS-mål → OS-mål (samma tjänst, ny implementation) | 59 |
| slot-mål → slot-mål | 21 |
| ROM-mål → annan ROM-adress | 1 |
| slot-mål → OS-mål | 1 |

Ensoniq kunde alltså ändra beteendet hos 74 ROM-rutiner **utan att byta ROM**, bara
genom att peka om en post i en tabell som ligger på disken. Det är designens
huvudpoäng, och det förklarar både varför ROM-ytan är så stabil och varför OS-filen
växer från 88 KB till 195 KB mellan versionerna.

Exempel:

```
$8978.w   ROM anropar x13   V1.61 → $F8A564 [ROM]   V3.50 → $00D2DE [OS]
$8D3E.w   ROM anropar x4    V1.61 → $F8BE26 [ROM]   V3.50 → $00E17E [OS]
$9490.w   ROM anropar x3    V1.61 → $F900E8 [ROM]   V3.50 → $010EAE [OS]
$969E.w   ROM anropar x4    V1.61 → $F90F0A [ROM]   V3.50 → $011632 [OS]
```

32 slots pekar in i OS i **båda** versionerna och anropas ändå av ROM. Det är rena
callbacks — tjänster som ROM aldrig har implementerat och alltid förväntar sig att OS
levererar:

```
$801E.w   ROM x13   V1.61 $007144   V3.50 $0071C6
$8644.w   ROM x17   V1.61 $009D60   V3.50 $00B008
$864A.w   ROM x7    V1.61 $009DA4   V3.50 $00B04C
$87F2.w   ROM x5    V1.61 $002B14   V3.50 $002B5A
```

### De hetaste slotarna

```
$8030.w   ROM x203   → $F97662   båda versionerna
$9818.w   ROM x40    → $F92478
$8042.w   ROM x35    → $FB7788
$8C0C.w   ROM x26    → $F8B2E2
$9380.w   ROM x18    → $F8FAC6
$8036.w   ROM x13    → $F976CA   känd: kritisk sektion, återställ SR
```

`$8030.w` med 203 anropsställen är den mest anropade adressen i hela ROM. Att
identifiera `$F97662` är den enskilt högst prioriterade rutinanalysen i projektet.

### Det andra hoppbordet: DPRAM

Samma konstruktion finns i miniatyr i 68302:ans DPRAM, `$FC6000-$FC60C8`, cirka 30
poster på stride 6. OS-bildens vektor 4 och vektor 11 pekar in i det
(`$FFFC6000`, `$FFFC6014`), och PAR-kalibreringen anropar `$FFFC60B0`.

Innehållet går inte att läsa statiskt. Se `memory-map.md` §2.1.

---

## 3. Vad ROM ansvarar för

Verifierat ur ROM:s egen kod:

* **Reset och minneskarta.** Chip selects, ROM-flytten från `$000000` till `$F80000`
  via DPRAM-bryggan.
* **Hårdvaruinitiering.** PIO (PACNT/PADDR/PADAT, PBCNT/PBDDR/PBDAT), avbrottsstyrning
  (GIMR/IPR/IMR/ISR), Timer 2, watchdog, kommunikationsprocessorn, SCC1–3.
* **Exception-hantering.** Stubbtabellen `$F882AA-$F882DA` och den gemensamma svansen
  `$F88280` som producerar `ERROR nnn`.
* **Schemaläggaren.** `$F87F80` (spara), `$F87F92` (skanna), `$F87FCC` (idle).
  TCB-stride `$16`. OS-vektor 33 pekar hit.
* **Kritiska sektioner.** `$F976EC` / `$F976FA`, exponerade som slot `$8030.w`/`$8036.w`.
* **Diskaccess.** FDC-rutinerna och filsystemsläsningen som hittar `ASR-10 OS`.
* **Tjänstebiblioteket.** 895 entrypoints som OS anropar, listade i
  `static/rom-abi-entrypoints.csv`.

## 4. Vad OS ansvarar för

* **Bindningstabellen** — utan den kan de 342 slots ROM anropar inte nå sina mål.
* **Vektortabellen** för `$000000-$0003FF`, inklusive OS-variabler från `$0000C0`.
* **Callbacks** som ROM anropar men aldrig implementerar (32 slots).
* **Overrides** av ROM-rutiner (74 slots i V3.50).
* **Applikationslagret** — filbläddrare, sequencer, effektnedladdning, panelhantering.

## 5. Vad som fortfarande är okänt

| fråga | status | experiment |
|---|---|---|
| Exakt hur ROM lämnar över kontrollen | [OPEN] — se §6 | E1 |
| När vektortabellen installeras på `$000000` | [OPEN] | E1 |
| Var det 0x6400 byte stora hoppet i filen ligger | [OPEN] | E3 |
| Om `$FFxxxx` verkligen speglar `$00xxxx` | [DISPROVEN] för nuvarande MAME V3.50 data reads; opcode-fetch coverage [OPEN] | E2 2026-08-10 |
| DPRAM-hoppbordets innehåll | [Likely struktur, innehåll OPEN] | dumpa DPRAM efter boot |
| CS1 `$FF6000-$FF7FFF` | [OPEN] | E4 |
| `$F95EAA` — 1 anrop i V1.61, 33 i V3.50 | [OPEN] | statisk |

## 6. Överlämningen ROM → OS

Det här är projektets största arkitekturfråga. Nedan är allt som faktiskt är känt,
strikt separerat.

### Verified

* OS-filen ligger på diskblock 24 (offset `0x3000`), typ `$0020`, namn `ASR-10 OS`.
* OS-bildens vektor 0 (SSP) = `$00000300`, **vektor 1 (PC) = `$00000000`** i båda
  versionerna. Det finns ingen startadress i bilden.
* **ROM innehåller inte ett enda `jsr`/`jmp` med 32-bitars absolut RAM-mål.** Noll.
* ROM-kod finns i RAM efter boot: RAM-adresser har matchats mot diskoffset,
  verifierats mot FDC-transaktioner och disassemblerats. Se `os-code-extraction.md`.
* Minst två laddningsregler gäller, se `os-image-layout.md`.

### Likely

* Överlämningen sker via **en bindningsslot** (`jmp $xxxx.w`), via registerindirekt
  hopp, eller via `rts` till en stackad adress. Det är de enda mekanismer som återstår
  när 32-bitars absoluta RAM-hopp är uteslutna.
* Förstahandskandidat **bland de identifierade slotarna**: `$801E.w`. Den är den lägsta
  slotten i tabellen, ROM anropar den 13 gånger, och den pekar in i OS-kod i båda
  versionerna (`$007144` / `$0071C6`). Detta är en rangordning av kandidater, **inte en
  bevisad överlämning** — och överlämningen behöver inte alls gå via tabellen, se
  `call-graph.md` §2.5.
* Vektortabellen måste kopieras eller installeras separat, eftersom OS-bildens tabell
  under segment 1-regeln hamnar på RAM `$000A00`, inte `$000000`.

### Open

* Det exakta ögonblick då PC lämnar `$F80000-$FBFFFF` för första gången.
* Ordningen mellan (a) laddning, (b) vektorinstallation, (c) bindningstabellens
  giltighet, (d) första OS-anropet.
* Om ROM validerar bilden (checksumma, magiskt tal) innan överlämning.

### Modeller som kvarstår

**M1 — Slotöverlämning.** ROM laddar bilden, kopierar vektorer, och gör sitt vanliga
`jsr $801E.w`. Slotten pekar nu på OS-kod i stället för på ROM-kod. Överlämningen är
osynlig i ROM:s kod eftersom den *inte finns* i ROM — den finns i tabellen på disken.
Elegant, förenlig med patchmekanismen, och skulle förklara varför ingen explicit
överlämning hittas. **Starkaste kandidaten.**

**M2 — Registerindirekt hopp.** ROM beräknar en adress ur en laddningsdeskriptor och
gör `jmp (A0)`. Kräver att deskriptorn hittas i bilden; ingen är funnen.

**M3 — `rts` till stackad adress.** Laddaren lägger en OS-adress på stacken och
returnerar dit. Svår att skilja från M1 statiskt.

Experiment E1 skiljer M1 från M2/M3 i en körning: logga varje övergång där PC går från
ROM-området till RAM, med från-PC och instruktionen på från-PC. Om från-instruktionen
är `4EB8 801E` är det M1.

### Varför frågan är viktig bortom sig själv

Om den faller på plats faller sannolikt flera andra samtidigt: när vektorerna
installeras, när bindningstabellen blir giltig, och hur ansvaret delas under resten av
körningen. Det är rimligen projektets viktigaste enskilda upptäckt kvar att göra.
