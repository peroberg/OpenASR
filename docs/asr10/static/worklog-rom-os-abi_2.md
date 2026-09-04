# Worklog — ROM↔OS-kontraktet

Statisk analys av `asr10.bin` (256 KB, `$F80000-$FBFFFF`), `V161.img`, `V350.img`.
Inget körts, inget skrivet i MAME-trädet.

Bilagor:
* `rom-abi-entrypoints.csv` — 1054 ROM-adresser, per-version anropsräkning
* `os-binding-table.csv` — 723 slots i bindningstabellen med RAM-adress, mål och anropsräkning

---

## Verified findings

### V1 — Katalogen ligger på diskoffset 0x600

Post = 26 byte: `type.w / namn[12] / size.w (block) / flag.w / startblock.l / reserved.l`.
Block = 512 byte, `filoffset = block * 512`.

| avbild | vol-ID (0x21F) | poster | kedja | högsta block |
|---|---|---|---|---|
| V161.img | `ASR161OID` | 14 | 13/13 sammanhängande | 328 av 3200 |
| V350.img | `OS-V350ID` | **28** | 27/27 sammanhängande | 3175 av 3200 |

Ordet på 0x402 = antal fria block (3200 − högsta block, stämmer i båda).
OS-filen: typ `$0020`, namn `ASR-10 OS`, startblock 24 → diskoffset 0x3000.
V161 173 block (88 576 B), V350 382 block (195 584 B).

**Ersätter** tidigare "V350 har 18 filer, kedja 17/17". Se *Retracted*.

### V2 — Reset-sekvensen använder DPRAM som exekveringsbrygga

```
$F8000C  46FC 2700             move.w  #$2700,SR
$F80010  31FC 0FC6 00F2        move.w  #$0FC6,($00F2).w
$F80016  21FC 0F200000 00F4    move.l  #$0F200000,($00F4).w
$F8001E..$F8005D               OR0/BR0, OR1/BR1, OR2/BR2, OR3/BR3
$F8005E  41FA 0018             lea     ($F80078),A0
$F80062  45FA 0022             lea     ($F80086),A2
$F80066  227C 00FC6200         movea.l #$00FC6200,A1
$F8006C  32D8 / B5C8 / 66FA    kopiera 14 byte till DPRAM
$F80072  4EF9 00FC6200         jmp     $00FC6200      <-- byter exekveringsyta
--- kopian, körs på $FC6200 ---
         33FC 1F01 00FC6830    move.w  #$1F01,(BR0)   <-- ROM $000000 -> $F80000
         4EF9 FFFB8E06         jmp     $FFFB8E06      <-- PIO-init
```

BR0-skrivningen drar undan marken under koden som kör. ROM kopierar därför just de två
instruktionerna till 68302:ns DPRAM och hoppar dit. Första verifierade användningen av
DPRAM som exekveringsyta.

### V3 — Minneskartan härledd ur ROM:s egna BR/OR-skrivningar

Avkodning: adressfält `$1FFC`, skift 11, enable = BR bit 0, FC-jämförelse = OR bit 15.

| CS | BR | OR | fönster | innehåll |
|---|---|---|---|---|
| CS0 (reset) | `$0001` | `$3F82` | `$000000-$03FFFF` | ROM-överlägg vid boot |
| CS0 (efter) | `$1F01` | `$3F82` | `$F80000-$FBFFFF` | ROM 256 KB |
| CS1 | `$1FEF` | `$FFFE` | `$FF6000-$FF7FFF` | 8 KB, **oidentifierat** |
| CS2 | `$1F85` | `$FFFC` | `$FC2000-$FC3FFF` | ES5506 + ES5510 |
| CS3 | `$1F89` | `$7FFC` | `$FC4000-$FC5FFF` | FDC + DUART |

Allt utanför CS0–CS3 och BAR-fönstret måste avkodas av kortlogiken; 68302:an gör det inte.

### V4 — OS-bildens vektortabell är versionsstabil

`os[0x000:0x0C0]` (vektor 0–47) är **byte-identisk** i V161 och V350.
I `0x0C0..0x3FF` skiljer sig **13 byte** — det området är OS-variabler, inte vektorer.

```
v0  SSP = $00000300      v1  PC = $00000000     <-- noll i BÅDA versionerna
v2  $FFF882AA   v3 $FFF882AE   v5 $FFF882B6   v9 $FFF882C6   v10 $FFF882CA
v4  $FFFC6000 (DPRAM)    v11 $FFFC6014 (DPRAM)
v15 $FFF882DA   v24 $FFF882D6   v25-31 $FFF882DA
v32 $FFF88280 (exception-svans)   v33 $FFF87F76 (schemaläggare)
v40 $FFF8812C   v47 $FFF88056
```

62 av vektorerna pekar i ROM, 3 i DPRAM. **`v1 = 0` betyder att ROM inte kan göra en
mjuk reset ur bilden.**

### V5 — Bindningstabellen: 723 JMP-slots på RAM `$00801E-$009FF6`

Detta är kontraktets konkreta form.

* Filområde **OS+0x7600..0x95F6** → RAM `$00801E-$009FF6` under Seg1-regeln (se V6).
* 723 poster, `4EF9 <32-bit>` eller `4EF8 <16-bit>`.
* **ROM anropar 342 av dem, 1254 anropsställen**, uteslutande med `jsr/jmp abs.w`
  (`$801E.w` … `$9FF6.w`, dvs effektiv adress `$FF801E`…`$FF9FF6`).
* Av ROM:s 334 distinkta korta anropsmål landar **313 (93,7 %) exakt på en `JMP`-opcode**
  i tabellen — identisk siffra för V161 och V350, eftersom tabellens *layout* är
  byte-identisk mellan versionerna.
* **567 av 723 slots har identiskt mål** i båda versionerna. 156 skiljer sig.

De mest anropade:

```
$8030.w  ROM x203   -> $F97662   (båda versionerna)
$9818.w  ROM x40    -> $F92478
$8042.w  ROM x35    -> $FB7788
$8C0C.w  ROM x26    -> $F8B2E2
$8036.w  ROM x13    -> $F976CA   (känd: kritisk sektion, återställ SR)
$8644.w  ROM x17    -> $009D60 (V161) / $00B008 (V350)   <-- pekar i OS, versionsberoende
$801E.w  ROM x13    -> $007144 (V161) / $0071C6 (V350)   <-- likaså
```

**Arkitekturen:** ROM är skrivet mot tabellen, inte mot ROM-adresser. OS levererar
tabellen. Därför kan OS:et

1. peka en slot mot ROM → använd ROM:s implementation,
2. peka en slot mot egen kod → **överskugga ROM-rutinen**.

Av de 156 slots som skiljer sig går **74 från ROM-mål i V1.61 till OS-mål i V3.50** —
alltså 74 ROM-rutiner som V3.50 ersätter med egen kod. Exempel:

```
$8978.w  ROM x13   V161 $F8A564 [ROM]  ->  V350 $00D2DE [OS]
$8D3E.w  ROM x4    V161 $F8BE26 [ROM]  ->  V350 $00E17E [OS]
$9490.w  ROM x3    V161 $F900E8 [ROM]  ->  V350 $010EAE [OS]
$969E.w  ROM x4    V161 $F90F0A [ROM]  ->  V350 $011632 [OS]
```

32 slots pekar i OS i **båda** versionerna och anropas ändå av ROM — det är rena
callbacks som ROM alltid förväntar sig att OS levererar.

Detta är patchmekanismen. Det förklarar både varför 92 % av ROM-anropsytan är stabil
och hur Ensoniq kunde ändra beteende utan att byta ROM.

### V6 — OS-bilden laddas i minst två segment

Metod: för varje anropsmål `T` testas kandidatoffset genom att kräva att ordet *före*
`T+K` är en avslutare (`rts/rte/rtr/jmp/nop`) — eller, för tabellslots, att `T+K` självt
är en `JMP`-opcode. `$0000` räknas **inte** som avslutare (se *Retracted* R2).

Live-verifierat ankare: `ori.b #$07,($00FC6829).l` (PAR-kanalvalet, bekräftat i MAME via
exception-ramens `fallande_PC=006802`) ligger på diskoffset **0x08DEC**:

```
RAM $0067EC  <->  disk 0x08DEC  <->  OS+0x05DEC        disk = RAM + 0x2600
```

| segment | regel | ankare V161 | ankare V350 |
|---|---|---|---|
| Seg1 | `RAM = OS_off + 0xA00` (`disk = RAM + 0x2600`) | 10 abs.l + 313 tabellslots | 10 abs.l + 313 tabellslots |
| Seg2 | `RAM = OS_off − 0x5A00` (`disk = RAM + 0x8A00`) | 78 | 259 |

Seg2-regeln är rangordnad **1 av 40 960** kandidater i V350 (261/425 = 61,4 %, tvåan
26 %) och **1 av 21 422** i V161 (78/133 = 58,6 %) — samma konstant i två OS-versioner.

Skillnaden mellan reglerna är exakt **0x6400**. Det är förenligt med att laddaren
hoppar över ett 0x6400 byte stort område i filen, varefter allt efterföljande hamnar
0x6400 lägre i RAM. Var hoppet sker är inte fastställt.

---

## Likely interpretations

### L1 — `$FF0000-$FFFFFF` speglar `$000000-$00FFFF`

Stödet är nu strukturellt, inte bara statistiskt:

* ROM gör 1254 `jsr abs.w` till `$FF801E..$FF9FF6`. Noll av de 334 distinkta målen är
  udda (slumpdata ger ~50 % udda) — det är riktiga instruktioner.
  `$F97874: 12 1A / 4EB8 8030 / 4E75` = `move.b (A2),D1 / jsr $FF8030 / rts`.
* Under Seg1-regeln, som är oberoende verifierad mot ett live-ankare, ligger OS-innehåll
  på RAM `$00801E..$009FF6`. 93,7 % av ROM:s mål landar där **exakt på en `JMP`-opcode**.
* Alltså: `$FF801E` och `$00801E` måste vara samma minne.

Mekanismen är 68000:ans kortadressering — `$8000.w`…`$FFFF.w` teckenutvidgas till
`$FF8000`…`$FFFFFF`. Genom att spegla ger kortadresseringen ett sammanhängande
64 KB-fönster över RAM `$0000-$FFFF`, 2 byte kortare per instruktion. Bindningstabellen
är medvetet placerad i den övre halvan av fönstret.

Det förklarar också `($8D50).w` i SCC-koden: den läser RAM `$008D50`, som är slot
`$8D50.w` i bindningstabellen.

**Konsekvens för drivrutinen:** `asr10_boot.cpp` har i dag `map(0xfc5020, 0xffffff).ram()`
— `$FF8030` är alltså separat RAM från `$008030`. Om speglingen är verklig landar varje
anrop till de 342 slotarna i oinitierat minne. Att maskinen bootar till `FILE 1` betyder
bara att inget av dessa anropsställen har nåtts än. **Ändra ingenting före E2.**

### L2 — CS1 (`$FF6000-$FF7FFF`) ligger medvetet utanför kortfönstret

En teckenutvidgad `abs.w` kan bara producera `$000000-$007FFF` eller `$FF8000-$FFFFFF`.
`$FF6000-$FF7FFF` är alltså **oåtkomligt med kortadressering** och kan bara nås med
32-bitars absolut eller registerindirekt adressering. Det talar för periferi eller
expansionsfönster snarare än het kod. Inga instruktionsfiltrerade referenser hittade i
ROM eller OS ännu — bara okvalificerade långord (160 i ROM, 11/37 i OS), vilka på den
träffnivån är brus.

### L3 — Överlämningsmodellen

```
ROM initierar hårdvara (CS, PIO, DUART, ES5506/ES5510)
→ läser katalog, hittar "ASR-10 OS" på block 24
→ laddar segment till RAM enligt V6
→ bindningstabellen på $00801E-$009FF6 blir därmed giltig
→ installerar/kopierar vektortabellen
→ överlämnar (mekanism okänd)
→ ROM-tjänster fortsätter anropas via tabellen, i båda riktningarna
```

Att OS-bilden saknar PC-fält (`v1 = 0`) och att ROM inte innehåller **ett enda**
`jsr/jmp abs.l` med RAM-mål betyder att överlämningen sker antingen genom en
tabellslot (`jmp $xxxx.w`), genom registerindirekt hopp, eller genom `rts` till en
adress som lagts på stacken. Slot `$801E.w` (ROM x13, pekar i OS i båda versionerna) är
den mest sannolika kandidaten att titta på först.

---

## Retracted findings

**R1.** "V350 har 18 filer, kedja 17/17." Fel. Kom av att V350 har en **byte-identisk
delkopia** av katalogen på 0x41E (`d[0x41E:0x600] == d[0x600:0x7E2]`) som klipps av vid
0x600. Läsning därifrån ger 18 poster och sedan skräp. V161 saknar kopian. Rätt: 28
poster, kedja 27/27, katalogstart 0x600 i båda.

**R2.** Första offsetsvepet räknade `$0000` som giltig instruktionsavslutare. Det gav en
falsk topp på 43–50 % för "wrap-modellen" (hela filen laddad på bas `$FFA600`) enbart
från nollutfyllnad. Efter rättning: **0/7 giltiga träffar** — modellen är **DISPROVEN**.

**R3.** "Preambeln (OS+0..0x5A00) laddas i högminnet `$FF8000`-området." Sveptest över
alla baser `$FF8000-$FFF000` placerade `$FFA600` på plats 10195 av 12424. **DISPROVEN.**
Rätt förklaring är L1 (spegling) + V5 (bindningstabellen).

**R4.** Första offsetsökningen svepte bara `K ≥ 0`, så Seg1 (som kräver negativt `K`)
kunde per konstruktion inte hittas. `0x5A00` togs för hela svaret. Upptäcktes först när
byte-mönstret `0039 0007 00FC6829` söktes och gav disk 0x08DEC i stället för 0x0F1EC.
Båda offseten var riktiga — men för olika segment.

**R5.** "Bindningstabellen ligger på OS+0x07F34..0x09502, 597 slots." För snävt.
Regionen är OS+0x7600..0x95F6, 723 slots, när även `4EF8`-posterna räknas.

---

## Coverage

| påstående | täckning | ankare |
|---|---|---|
| Katalogformat 26 byte | båda avbilderna, alla 42 poster | 42 |
| `filoffset = block × 512` | verifierad för OS-filen i båda | 2 |
| Ordet på 0x402 = fria block | 2 datapunkter — **ej generaliserbart** | 2 |
| CS-avkodning | alla fyra CS, ur ROM:s egna skrivningar | 4 |
| Reset→DPRAM-bryggan | en enda kodväg, fullständigt avkodad | 1 |
| Vektortabell 0–47 identisk | hela `0x000-0x0BF`, båda versionerna | 192 byte |
| Seg1 `RAM = OS_off + 0xA00` | RAM `$002000-$009FF6`; 1 live-ankare + 10 abs.l + 313 tabellslots per version | 324 |
| Seg2 `RAM = OS_off − 0x5A00` | RAM `$010000-$01D6A0` (V350), `$00A000-$00E81E` (V161) | 78 / 259 |
| Gränsen mellan Seg1 och Seg2 | **ej fastställd** — RAM `$00A000-$00FFFF` är tvetydigt | 0 |
| ROM-entrypoint-census | 1054 adresser, båda versionerna, `4EB9/4EF9` + vektorer | fullständig för dessa opkoder |
| Bindningstabell 723 slots | OS+0x7600..0x95F6, båda versionerna | 723 |
| Spegling `$FFxxxx` ≡ `$00xxxx` | **enbart statiskt** — ingen runtime-bekräftelse | 0 |

**Ingenting ovan täcker registerrelativ adressering** `(d,An)` — det blinda hålet som
kostat fem gånger tidigare. Bindningstabellen kan ha anropare som denna analys inte ser.

---

## Open questions

1. **Speglingen `$FFxxxx` ↔ `$00xxxx`** — måste runtime-verifieras innan `mem_map` rörs. (E2)
2. **Det exakta ROM→OS-hoppet** — ROM har noll `jsr/jmp abs.l` med RAM-mål. Slot
   `$801E.w` är förstahandskandidat. (E1/E3)
3. **När installeras vektortabellen på `$000000-$0003FF`?** Under Seg1-regeln hamnar
   bildens tabell på RAM `$000A00`, inte `$000000`. Någon måste kopiera ner den. (E1)
4. **Gränsen mellan Seg1 och Seg2**, och var laddaren hoppar över 0x6400 byte. (E3)
5. **CS1 `$FF6000-$FF7FFF`** — 8 KB med egen chip-select, ingen känd användning. (E4)
6. **Delkopian av katalogen på 0x41E i V350** — varför, och varför på ett offset som
   inte är postjusterat (482 byte = 18,5 poster)?
7. **Volymhuvudets ord på 0x404** (`$0335` / `$013D`) och **0x406** (`$0132` i båda).
8. **`$F95EAA`** — 1 anrop i V1.61, 33 i V350. Största versionsskillnaden i materialet.
9. **`$FF6000`-fönstret vs. `Asr10Cs3Decoder`** i 68302-emulatorprojektet — jämför.

---

## Canonical destinations

```
V2  DPRAM reset-brygga            -> boot-sequence.md (steg 1-2), subroutine-index.md ($F8005E, $FC6200)
V3  Chip-select-avkodning         -> reference/memory-map.md (ny), boot-sequence.md
V4  Vektortabell + korrigeringar  -> reference/vector-map.md
V5  Bindningstabellen             -> reference/rom-os-abi.md (ny), + os-binding-table.csv
V6  Tvasegmentsladdning           -> reference/os-image-layout.md (ny)
V1  Katalogformat + korrigering   -> reference/filesystem.md
L1  Speglingen                    -> reference/memory-map.md, MEN forst efter E2
L2  CS1                           -> reference/memory-map.md, markerad [OPEN]
R1-R5                             -> stannar i worklog.md tills konsolidering
```

Konsolidera först när E2 (speglingen), det okända 0x6400-hoppet och själva
ROM→OS-överlämningen är utredda.
