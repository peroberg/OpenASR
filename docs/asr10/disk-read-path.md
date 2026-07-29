# Diskyta till RAM: transaktionslogg, transportmekanism och byte-för-byte-verifiering

2026-07-29. Rent diagnostiskt arbete — inga kodändringar, inga nya stubbar.
Läst: `running.md`, `CLAUDE.md`, `fdc-map.md`, `filesystem-browser-map.md`
(selektivt: avsnitt 1-3, 4.19-4.20). Körningar mot dagens bygge
(`7bc57b8ab45` + de okommittade fdc-map.md-ändringarna, som är
rent dokumentära och inte rör koden).

**Kommandorad använd** (running.md:44 som mall, `-flop1` i stället för
`-flop` — båda är alias i denna MAME-version, `-flop1` testades och
fungerar identiskt):

```sh
ASR10_EXPERIMENT_ES5510_HOST=1 ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE=1 ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE=1 \
ASR10_DIAG_ROOT_DIRECTORY=1 \
./mess asr10booth -flop1 floppies/asr10booth/{V161,V350}.img \
  -video none -sound none -nothrottle -seconds_to_run 90 -log
```

De två sista flaggorna är redan existerande, av-som-standard diagnostik
i `asr10_boot.cpp` (`ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE`/
`ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE`/`ASR10_DIAG_ROOT_DIRECTORY`) —
inga nya. `ASR10_EXPERIMENT_ES5506_HOST`+`ASR10_EXPERIMENT_PAR_DIAGNOSTIC`
provades också (se avsnitt 5) men gjorde körningen orimligt långsam
(ES5506-enheten instansieras och kör även med `-sound none`) och gav
ingen ny insikt för den här uppgiften; utelämnade ur huvudkörningarna.

---

## 3. AVGÖRANDE TEST (besvaras först, per instruktion)

`[Verified]`. Metod: identifiera vilken 26-byte-post i lågminnestabellen
`$544` (den redan etablerade "katalogtabellen", se avsnitt 4) som
innehåller en unik textsträng, sök samma sträng direkt i `.img`-filen
med ett fristående Python-skript (ingen MAME inblandad), räkna om till
C/H/R med den geometri transaktionsloggen redan avslöjar, och jämför
byte för byte.

**Geometri, avledd och bekräftad:** `V161.img`/`V350.img` är båda
1 638 400 byte. Transaktionsloggen (nedan) visar `density=hd`,
`data_rate=500000`, `decoded_sector_size=512`, `drive_sides=2`. Om
80 cylindrar antas: `1 638 400 / 512 / 80 / 2 = 20` sektorer/spår —
ett jämnt tal, ingen rest. `[Verified]` genom direkt träff (se nedan):
byte-LBA för `(C,H,R)` (R 1-baserat) är

```
track_index = C*2 + H
byte_offset = (track_index * 20 + (R-1)) * 512
```

**Träff:** RAM-posten vid `$544` (index 0) lästes ut av harnessets
befintliga `ASR10_DIAG_ROOT_DIRECTORY`-diagnostik (maskinets slutdump)
som:

```
address=000544 first_word=0020 type_byte=20 name="ASR-10 OS   ."
bytes="00 20 41 53 52 2d 31 30 20 4f 53 20 20 20 00 ad 00 ad 00 00 00 18 00 00 00 00"
```

Strängen `"ASR-10 OS"` söktes direkt i `V161.img` (Python, `.find()`,
ingen tolkning): träff vid byte-offset `0x602` (1538). Enligt
formeln ovan ligger det på `C=0,H=0,R=4` (`(1538/512)+1` ≈ sektor 4,
sektorstart `1536`) — vilket är **exakt** command-fältet i FDC-
transaktion nummer 4 i loggen: `C_byte=00 H_byte=00 R_byte=04 N_byte=02
EOT_byte=05` (läser sektor 4 t.o.m. 5, 1024 byte). Direkt jämförelse,
disk mot RAM, de första 26 byten från sektorstart `0x600`:

```
disk: 00 20 41 53 52 2d 31 30 20 4f 53 20 20 20 00 ad 00 ad 00 00 00 18 00 00 00 00
RAM:  00 20 41 53 52 2d 31 30 20 4f 53 20 20 20 00 ad 00 ad 00 00 00 18 00 00 00 00
```

**Identiska, byte för byte.** Samma jämförelse upprepades för
`V161.img` poster 1 (`44DDL+CHORUS`), 12 (`44ROTO+REVRB`), 13
(`PARALLEL EFX`) och de första två tomma posterna (14, 15 — noll på
båda sidor) — alla identiska. Samma test upprepades oberoende för
`V350.img` (poster 0, 1, 2, 9, 10, 15 — `ASR-10 OS`, `TUTORIAL BNK`,
`JM DIGI SYN`, `TUTORIAL SEQ`, `TUTORIAL SNG`, `ATRK TUT SNG`) — även
dessa identiska, disk mot RAM.

**Svar: Identiska.** Överföringen från diskyta till RAM fungerar
korrekt och förlustfritt för båda avbilderna, för varje post som
testades. Om `"NO INST OR BANK FILES"` visas felaktigt (se avsnitt 5)
är det **inte** ett överföringsfel — data anländer oskadad i RAM. Felet,
om det finns ett, ligger i parsningen/kategoriseringen efter
överföringen, exakt som testet var konstruerat för att skilja på.

---

## 1. FDC-transaktionslogg: kommando, CHRN, byteantal, fas

`[Verified]`, ur `ASR10_FDC_CMD46 event=summary` (befintlig, redan
committad instrumentering, `asr10_boot.cpp:9514`) plus `ASR10PANEL`/
`ASR10PHASE` för fas. Alla transaktioner är kommando `0x46` (READ DATA,
MFM). Två aux-registerskrivningar föregår läsningarna (rad `fc4001`,
inte NEC765-kommandon): `0x88` (datahastighetsval, tvingas 500 kbps av
den redan dokumenterade `ASR10_EXPERIMENT_CMD88_RATE_500K`, se
`fdc-map.md`) och `0xF3` (precompensation).

**`V161.img`, 20 transaktioner, cylinder 0-4:**

| txn | C,H,R,N,EOT | byte | ST0,ST1,ST2 |
|---|---|---|---|
| 1 | 00,00,01,02,01 | 512 | 40,80,00 |
| 2 | 00,00,02,02,02 | 512 | 40,80,00 |
| 3 | 00,00,03,02,03 | 512 | 40,80,00 |
| 4 | 00,00,04,02,05 | 1024 | 40,80,00 |
| 5 | 00,00,06,02,06 | 512 | 40,80,00 |
| 6 | 00,01,05,02,06 | 1024 | 44,80,00 |
| 7 | 00,01,07,02,14 | 7168 | 44,80,00 |
| 8 | 01,00,01,02,14 | 10240 | 40,80,00 |
| 9 | 01,01,01,02,14 | 10240 | 44,80,00 |
| 10 | 02,00,01,02,03 | 1536 | 40,80,00 |
| 11 | 02,00,04,02,14 | 8704 | 40,80,00 |
| 12 | 02,01,01,02,14 | 10240 | 44,80,00 |
| 13 | 03,00,01,02,14 | 10240 | 40,80,00 |
| 14 | 03,01,01,02,07 | 3584 | 44,80,00 |
| 15 | 00,00,07,02,07 | 512 | 40,80,00 |
| 16 | 03,01,08,02,14 | 6656 | 44,80,00 |
| 17 | 04,00,01,02,14 | 10240 | 40,80,00 |
| 18 | 04,01,01,02,10 | 8192 | 44,80,00 |
| 19 | 04,01,11,02,11 | 512 | 44,80,00 |
| 20 | 00,00,06,02,06 | 512 | 40,80,00 |

Identisk med `filesystem-browser-map.md` avsnitt 4.20:s tabell —
**dagens bygge (efter DUART-omskrivningen i `duart.md`) reproducerar
exakt samma 20-transaktionssekvens**, inklusive `ST1.EOC` satt
(bit 7, "End of Cylinder") på varenda transaktion, aldrig något annat
statusbit. Det bekräftar redan-dokumenterade fyndet att EOC är
universellt och ofarligt, inte ett fel.

**`V350.img`, 32 transaktioner, cylinder 0-10** — de första 15
transaktionerna är **byte-för-byte identiska** med `V161.img` (samma
OS-laddarkod läser samma bootsektorer oavsett avbild), sedan divergerar
de vid transaktion 16 eftersom `V350.img`s katalogtabell har fler
poster (28 mot 14, se avsnitt 5) och kräver fler sidoläsningar av
OS-laddaren.

**Fas:** samtliga transaktioner (både 20 och 32 st) slutförs **innan**
panelen visar `"    LOADING SYSTEM    "` — transaktion 1 sker strax
efter `"   ENSONIQ  ASR-10    "`-skärmen visas (rad 3206 mot rad 11014 i
loggen), och sista transaktionen (`txn=20`/`txn=32`) slutförs bara
~200 loggrader innan `"LOADING SYSTEM"` skrivs. **Ingen FDC-transaktion
sker efter det** i någon av de körningar som testades (upp till 300
emulerade sekunder för `V350.img`) — hela diskläsningen är en engångs
OS-laddningsfas, inte en pågående ström. Det matchar
`filesystem-browser-map.md` 4.20 exakt.

---

## 2. Programmerad I/O eller IDMA?

`[Verified]`, **redan avgjort av tidigare statisk disassemblering**
(`docs/asr10/evidence-tree.md` rad 412-433), bekräftat här genom att
räkna om mot dagens transaktionslogg:

* Överföringsloopen (`fb8aa2`-`fb8abe`) pollar MSR vid `$FFFC4001` och
  flyttar en byte via `$FFFC4003` per varv — en **tät CPU-poll-och-
  flytta-loop**, disassemblerad instruktion för instruktion. Ingen
  MC68302-DMA-register (IDMA, PA13-PA15/DREQ/DACK/DONE) förekommer
  någonstans i den disassemblerade räckvidden.
* Bekräftat kvantitativt i dagens logg: transaktion 1 (512 databyte +
  7 resultatbyte) visar `total_fifo_reads=519` — **exakt** en
  CPU-läsning av `fc4003` per överförd byte, plus resultatbytena. Om
  IDMA hade använts hade CPU:n aldrig behövt röra `fc4003` per byte;
  FIFO-räknaren hade varit noll eller irrelevant för dataflödet.
  Samma mönster håller för alla 20/32 transaktioner (t.ex. transaktion
  17: 10240 databyte -> `total_fifo_reads` skalar exakt med
  `data_phase_reads`).
* **Slutsats: `[Verified]` programmerad I/O, inte IDMA.** Eftersom
  ROM:et aldrig programmerar IDMA finns inget att emulera där — det
  finns ingen "IDMA-lucka" som bytes kan försvinna i. Transporten går
  redan helt och hållet via `m_fdc->fifo_r()`/`fifo_w()` (se
  `fdc-map.md` avsnitt 1) rakt in i CPU:ns register/minne genom
  ROM:ets egna `move.b`-instruktioner — samma mekanism som avsnitt 3
  bevisar är byte-exakt.
* PA13-PA15 (DREQ/DACK/DONE): inga träffar i loggen eller i static
  disassembly-materialet av något slag som rör dessa pinnar i
  samband med FDC-läsning. `PLAN.md` avsnitt "Kvarstående SIB-delar"
  listar redan IDMA-motorn som "endast pinnar idag" (dvs. pinnfunktion
  utan motor) — konsekvent med att den aldrig anropas här.

---

## 4. Läses rotkatalogen, i vilken fas, och stämmer det med filesystem-browser-map.md?

`[Verified]`. Ja — och den läses **exakt en gång per körning, i
OS-laddningsfasen**, inte separat vid en senare "bläddra i katalogen"-
händelse. Tabellen vid lågminne `$544` (stride `0x1a`=26 byte, upp till
40 poster) fylls av `fb8ab6`/`fb90dc`-relaterad kod under transaktion 4
(sektor `C0,H0,R4`-`R5`, se avsnitt 3) — **exakt** det
`filesystem-browser-map.md` avsnitt 1-2 redan slog fast: "`$544`
(span `$544`-`$8ff`) — shared sector/table buffer ... fully populated
(all 40 entries) before milestone A by `fb8ab6`". Den enda smärre
avvikelsen: dokumentets angivna övre gräns `$8ff` är för snävt räknat
(40×26=1040 byte ger `$544`+`0x413`=`$957`, inte `$8ff`) — en liten
felräkning i det äldre dokumentet, utan konsekvens för slutsatsen.

**Stämmer det med ASR-10:s diskformat?** Katalogen är en **flat
tabell** (inte ett träd), vilket matchar `filesystem-browser-map.md`
avsnitt 3:s slutsats ("more consistent with a category-filtered,
largely flat structure ... than with a hierarchical/nested-directory
filesystem"). Typbyten (första byten i varje post, `first_word`s
höga byte) korrelerar tydligt med filnamnets eget suffix, vilket
den här körningen ger **nytt, direkt bekräftande belägg** för (inte i
det äldre dokumentet): `0x03`="INST"-typ (`JM DIGI SYN`, `MOOG POP 1`,
`BLUES DRUMS` osv, inget suffix i namnet men tydligt
instrumentklingande), `0x1e`="BNK" (`TUTORIAL BNK`, `ATRK TUT BNK`),
`0x1c`="SEQ" (`TUTORIAL SEQ`), `0x1d`="SNG" (`TUTORIAL SNG`,
`ATRK TUT SNG`), `0x20`=OS-systemfil (`ASR-10 OS`), `0x21`/`0x18`=
effekt-presets (`44DDL+CHORUS`, `PARALLEL EFX`, endast i `V161.img`).

---

## 5. Skiljer sig V350 från V161?

`[Verified]` för katalogens **innehåll**; `[OPEN]` för vad panelen
faktiskt visar (se nedan — kunde inte återskapas i den här sessionen
för någon av avbilderna).

**Katalogen:** `[Verified]`, direkt jämförelse av
`ASR10_ROOT_DIRECTORY_TABLE_SUMMARY`:

| | `V161.img` | `V350.img` |
|---|---|---|
| Giltiga poster | 14 | 28 |
| Första tomma index | 14 | 28 |
| Posttyper som förekommer | OS (`0x20`), effekt-presets (`0x21`,`0x18`) | OS (`0x20`), **INST** (`0x03`, 9 st), **BNK** (`0x1e`, 2 st), SEQ (`0x1c`), SNG (`0x1d`, 2 st) |

`V350.img` innehåller uttryckligen namngivna instrumentposter
(`JM DIGI SYN`, `JM DRUMS`, `DEMO PERCS`, `MOOG POP 1`,
`HIGH STRINGS`, `JM CLAV`, `OB-8*`, `BLUES DRUMS`, `BLUES BASS`,
`BLUES ORGAN` — typbyte `0x03`, instrumentklass) **och** bankposter
(`TUTORIAL BNK`, `ATRK TUT BNK` — typbyte `0x1e`). Detta bekräftades
byte-för-byte mot `.img`-filen (avsnitt 3-metoden), inte bara ur
RAM-loggen. `V161.img` innehåller varken typ `0x03` eller `0x1e` —
bara systemfilen och effektpresets. **Användarens premiss stämmer för
`V350.img`: den innehåller onekligen instrument- och bankfiler**, till
skillnad från `V161.img` som genuint verkar vara ett OS/effekt-media
utan instrument/bank-innehåll (konsekvent med
`filesystem-browser-map.md` 4.20:s tidigare slutsats för just
`V161.img` specifikt — den slutsatsen drogs alltså korrekt, men gäller
bara den avbilden, inte generellt "diskarna saknar innehåll").

**Vad panelen visar:** `[OPEN]`. Med flaggorna denna uppgift angav
(`ASR10_EXPERIMENT_ES5510_HOST=1 ASR10_DIAG_PANEL_AUTORESPOND=1`) plus
de redan existerande FSB/disk-sig/root-directory-flaggorna, **stannade
båda körningarna vid exakt samma punkt: `"    KEYBOARD TUNED"`**, upp
till 300 emulerade sekunder testat för `V350.img` — ingen av dem visade
`"NO INST OR BANK FILES"` eller något annat efterföljande
webbläsarmeddelande alls. Detta skiljer sig från
`filesystem-browser-map.md` 4.19:s historiska resultat (som nådde
`NO INST OR BANK FILES` på ~11,66 emulerade sekunder för `V161.img`)
— men det resultatet krävde en väsentligt bredare flaggmängd
(`ASR10_EXPERIMENT_DUART_COUNTER_TIMER=1`,
`ASR10_EXPERIMENT_ES5506_HOST=1` + `ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1`
+ `ASR10_DIAG_PAR_VALUE=0x200`,
`ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE=1`,
`ASR10_EXPERIMENT_TUNING_STALL_TRACE=1`) än vad den här uppgiften bad
om testas. Ett försök med `ES5506_HOST`+`PAR_DIAGNOSTIC` tillagt
gjordes men fick avbrytas (se ovan, orimlig avmattning + logg på
>20 miljoner rader inom 35 CPU-sekunder — ES5506-diagnostikens egen
loggvolym, inte relaterat till diskvägen).

`[Hypothesis]`: att sex-slots-dispatchen efter `KEYBOARD TUNED`
(dispatchordning 1,3,0,4,5, redan bekräftad i `fdc-map.md` avsnitt 4)
kräver ytterligare en av de flaggorna för att den nod-`89A2`-baserade
fortsättningen (samma öppna fråga som `fdc-map.md` avsnitt 4) ska gå
vidare till webbläsarkoden — **oavsett vilken skiva som är monterad**,
eftersom stoppet inträffar identiskt för båda avbilderna vid samma
logiska punkt (`table_first_word_writes=266` för båda, en kodkonstant,
inte diskberoende). Detta är alltså **inte** en diskinnehålls-fråga
längre på denna punkt i utredningen, utan samma olösta
schemaläggar/nod-fråga som redan är dokumenterad som öppen.

**Sammanfattning:** disk-till-RAM-vägen är bevisligen korrekt och
identisk i mekanism för båda avbilderna (avsnitt 2-3). Katalogernas
**innehåll** skiljer sig verkligt och är korrekt överfört (avsnitt 5,
första halvan). Vad panelen till slut visar för respektive avbild
kunde inte fastställas i den här sessionen med de flaggor uppgiften
efterfrågade — det kräver antingen längre körning med fler av de redan
existerande diagnostikflaggorna, eller att den öppna
schemaläggar/nod-`89A2`-frågan (`fdc-map.md` avsnitt 4) löses först.
