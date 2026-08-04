# ASR-10 — diskformat och OS-bildens layout

## 1. Filsystemet

Block = 512 byte. `filoffset = block × 512`, rakt av, ingen interleave.

### Volymhuvud, diskoffset `0x400`

| offset | V161 | V350 | tolkning |
|---|---|---|---|
| `0x400` | `$0000` | `$0000` | [OPEN] |
| `0x402` | `$0B38` (2872) | `$0019` (25) | **antal fria block** = 3200 − högsta använda block [Verified, 2 datapunkter] |
| `0x404` | `$013D` | `$0335` | [OPEN] |
| `0x406` | `$0132` | `$0132` | [OPEN] — identiskt i båda |
| `0x21F` | `ASR161OID` | `OS-V350ID` | volym-ID, ASCII |

### Katalog, diskoffset `0x600`

Post = 26 byte:

```
+0   type.w
+2   namn[12]      ASCII, blankutfyllt
+14  size.w        antal block
+16  flag.w
+18  startblock.l
+22  reserved.l
```

Posterna är sammanhängande; slut = `type == 0 && namn == 0`.

| avbild | poster | kedja | högsta block |
|---|---|---|---|
| V161.img | 14 | 13/13 sammanhängande | 328 av 3200 |
| V350.img | 28 | 27/27 sammanhängande | 3175 av 3200 |

Kända typer: `$0020` OS, `$0003` instrument, `$001C` sequence, `$001D` song,
`$001E` bank, `$0021` effekt, `$0018` (V161: `PARALLEL EFX`).

### Fallgrop: delkopian på `0x41E` i V350

V350 innehåller en **byte-identisk delkopia** av katalogen på diskoffset `0x41E`
(`d[0x41E:0x600] == d[0x600:0x7E2]`), som klipps av vid `0x600`. Läser man därifrån får
man 18 poster och sedan skräp. V161 saknar kopian helt — `0x41E` är där `$0000`.

**Läs alltid katalogen från `0x600`.** Varför kopian finns, och varför den ligger på ett
offset som inte är postjusterat mot `0x600` (skillnad 482 byte = 18,5 poster), är [OPEN].

### OS-filen

| | V161 | V350 |
|---|---|---|
| namn / typ | `ASR-10 OS` / `$0020` | `ASR-10 OS` / `$0020` |
| startblock | 24 | 24 |
| diskoffset | `0x3000` | `0x3000` |
| storlek | 173 block = 88 576 B | 382 block = 195 584 B |
| slut | `0x18A00` | `0x32C00` |

---

## 2. OS-bildens interna layout

```
OS+0x00000 - 0x000BF   68000-vektortabell, vektor 0-47   [identisk i båda versionerna]
OS+0x000C0 - 0x003FF   OS-variabler (återanvänd vektorplats), 13 byte skiljer
OS+0x00400 - 0x01BFF   glest, mestadels nollor
OS+0x01C00 - 0x072F0   tät 68000-kod   ← segment 1
OS+0x07600 - 0x095F6   BINDNINGSTABELLEN, 723 slots  ← segment 1
OS+0x095F6 - 0x0F788   [OPEN] ~24 KB utan fastställd placering
OS+0x0F788 - 0x230A0   tät 68000-kod   ← segment 2
OS+0x230A0 - slut      [OPEN]
```

---

## 3. Laddningsreglerna

### Metod

För varje anropsmål `T` i bilden testas en kandidatförskjutning genom att kräva att
ordet *före* `T+K` är en avslutare (`rts/rte/rtr/jmp/nop`), eller — för hoppbordsposter
— att `T+K` självt är en `JMP`-opcode. Se `methods-static-analysis.md` §2.

### Segment 1 — `RAM = OS_offset + 0xA00`   ⇔   `disk = RAM + 0x2600`

**Live-verifierat ankare:** `ori.b #$07,($00FC6829).l` i PAR-kalibreringen ligger på
diskoffset `0x08DEC`. Rutinen körs på RAM `$0067EC`; `divu`-felet på `$006800`
bekräftades i MAME via exception-ramens `fallande_PC=006802`.

```
RAM $0067EC   ↔   OS+0x05DEC   ↔   disk 0x08DEC
```

Täckning: RAM `$002000-$009FF6`. Ankare: 1 live + 10 `abs.l`-mål per version
+ 313 bindningsslots per version.

### Segment 2 — `RAM = OS_offset − 0x5A00`   ⇔   `disk = RAM + 0x8A00`

Rangordnad **1 av 40 960** kandidater i V350 (261/425 = 61,4 %, tvåan 26 %) och
**1 av 21 422** i V161 (78/133 = 58,6 %). Samma konstant i två OS-versioner.

Täckning: RAM `$010000-$01D6A0` (V350), `$00A000-$00E81E` (V161).

**Runtimeankare.** SCC-initsekvensen i V1.61 ligger på diskoffset `0x148E0`-`0x14926`.
Under segment 2-regeln ger det RAM `$00BEE0`-`$00BF26`, vilket matchar de PC-värden en
tidigare MAME-körning loggade (`$00BEF2`, `$00BF00`, `$00BF1A`, `$00BF22`) **byte för
byte**. Segment 2-regeln har därmed för första gången ett runtimeverifierat ankare och
vilar inte längre enbart på statistik.

Samma kod ligger i V3.50 på RAM `$00E49A`-`$00E4D4` — de loggade adresserna tillhör
alltså en V1.61-körning.

Stickprov, RAM `$009DA0`:
```
... 00 0A 4E75 │ 9EFC 000C 48A7 8000 48E7 00C0 61B2 ...
                 suba.w #$0C,A7 / movem.w D0,-(A7) / movem.l D0-D1,-(A7)
```
Samma adress under segment 1-regeln ger `3FB6 3FBA 3FB6 9884` — data, inte kod.

### Skillnaden är exakt `0x6400`

Det är förenligt med att laddaren **hoppar över 0x6400 byte** i filen, varefter allt
efterföljande hamnar `0x6400` lägre i RAM. Var hoppet ligger är [OPEN] — någonstans i
`OS+0x095F6 - 0x0F788`. Experiment E3 avgör det på en körning.

### Gränsen

Bindningstabellen på RAM `$008000-$009FF6` tillhör segment 1. SCC-initsekvensen på
`$00BEF2` tillhör segment 2. Gränsen ligger alltså i intervallet:

```
X ∈ ($009FF6, $00BEF2]
det överhoppade filområdet på 0x6400 byte börjar i [OS+0x095F6, OS+0x0B4F2)
```

Inom intervallet ger båda reglerna validerande träffar, så exakt gräns är fortfarande
[OPEN]. Experiment E3 avgör den.

---

## 4. Coverage

| påstående | täckning | ankare |
|---|---|---|
| Katalogformat 26 byte | båda avbilderna, alla 42 poster | 42 |
| `filoffset = block × 512` | OS-filen i båda | 2 |
| `0x402` = fria block | 2 datapunkter, **ej generaliserbart** | 2 |
| Segment 1-regeln | RAM `$002000-$009FF6` | 324 |
| Segment 2-regeln | RAM `$010000-$01D6A0` | 78 / 259 |
| Segmentgränsen | **ej fastställd** | 0 |
| `OS+0x095F6-0x0F788` | **ej placerad** | 0 |
| `OS+0x230A0`-slut | **ej placerad** | 0 |

Ingen av mätningarna ser registerrelativ adressering `(d,An)`. Delar av bilden kan
alltså vara kod med anropare som metoden inte upptäcker.

---

## 5. Motbevisade modeller

**Wrap-modellen** — hela filen laddad på bas `$FFA600` så att `OS+0x5A00` hamnar på
`$000000`. Testad mot 7 rena `abs.l`-mål i `$FFA600-$FFFFFF` per version:
**0/7 giltiga träffar i båda**. [DISPROVEN]

**Preambel i högminne** — `OS+0..0x5A00` laddad i `$FF8000`-området. Sveptest över alla
baser `$FF8000-$FFF000` placerade `$FFA600` på plats 10195 av 12424. [DISPROVEN]
Rätt förklaring är speglingen (`memory-map.md` §4) plus bindningstabellen.
