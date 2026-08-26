# ASR-10 — minneskarta

Härledd ur ROM:s egna BR/OR-skrivningar och ur MC68302 User's Manual, inte ur
drivrutinens `mem_map`. Där drivrutinen avviker anges det uttryckligen.

## 1. Chip selects (MC68302 SIB)

ROM programmerar alla fyra chip selects i en följd på `$F8001E-$F8005D`, före allt annat.

### BR/OR-formatet (MC68302 UM §3.6.2.1, §3.6.2.2)

```
BR:  15-13 FC2-FC0 │ 12-2 BASE ADDRESS (A23-A13) │ 1 RW  │ 0 EN
OR:  15-13 DTACK   │ 12-2 BASE ADDRESS MASK      │ 1 MRW │ 0 CFC
```

* `EN` = BR bit 0, `RW` = BR bit 1 (0 = endast läsning, 1 = endast skrivning)
* `CFC` = **OR bit 0** — 0 betyder att funktionskoderna ignoreras
* `MRW` = OR bit 1 — **0 = RW maskad** (både läs och skriv), 1 = RW gäller
* `DTACK` = OR bit 15–13; `111` = extern DTACK, `000`-`110` = 0–6 väntetillstånd
* adressfältet är bit 12–2 → basadress = `(BR & $1FFC) << 11`

**CFC är OR bit 0, inte bit 15.** Bit 15 tillhör DTACK-fältet. För samtliga fyra
programmerade chip selects är CFC = 0 — **ingen FC-jämförelse är påslagen någonstans**.

### ASR-10:s fyra chip selects

| CS | BR | OR | fönster | riktning | DTACK | innehåll | status |
|---|---|---|---|---|---|---|---|
| CS0 (reset) | `$0001` | `$3F82` | `$000000-$03FFFF` | endast läsning | 1 WS | ROM-överlägg vid boot | [Verified] |
| CS0 (efter) | `$1F01` | `$3F82` | `$F80000-$FBFFFF` | endast läsning | 1 WS | ROM, 256 KB | [Verified] |
| CS1 | `$1FEF` | `$FFFE` | `$FF6000-$FF7FFF` | **endast skrivning** | **extern** | **oidentifierat**, 8 KB | [OPEN] — se §5 |
| CS2 | `$1F85` | `$FFFC` | `$FC2000-$FC3FFF` | läs + skriv | extern | ES5506 `$FC2000`, ES5510 `$FC3000` | [Verified] |
| CS3 | `$1F89` | `$7FFC` | `$FC4000-$FC5FFF` | läs + skriv | 3 WS | FDC `$FC4000`, DUART `$FC4801` | [Verified] |

Att CS0 är läs-endast är korrekt för ROM. Att CS2 och CS3 är läs+skriv är korrekt för
ljudkretsarna respektive FDC/DUART. **CS1 är den enda som är skrivbar-endast**, och den
enda tillsammans med CS2 som använder extern DTACK.

Adressfönstren är oförändrade jämfört med tidigare avkodning — det var bara CFC-,
MRW- och DTACK-tolkningen som var fel.

BR0 skrivs två gånger. Första skrivningen (`$0001`) håller kvar ROM-överlägget på
`$000000` så att reset-vektorn fungerar. Andra skrivningen (`$1F01`) flyttar ROM till
`$F80000` — och **den instruktionen kan inte köras ur ROM**, eftersom den drar undan
marken under sig själv. Se `boot-sequence.md` §DPRAM-bryggan.

Allt som inte täcks av CS0–CS3 eller BAR-fönstret måste avkodas av kortlogiken.
68302:an gör det inte.

### 1.1 Två orelaterade `$FFxxxx`-mekanismer

Två helt olika mekanismer producerar `$FFxxxx` i disassemblern.

**A. 32-bitars absolut lång adress med `$FF`-fyllnad**

```text
move.b $FFFC4817.l,D1
```

På 68000/68302-systemets 24-bitars adressbuss maskas detta till `$FC4817`. Detta är ren
periferadressering, ingen spegling inblandad. ROM använder denna form för bland annat
DUART-fönstret `$FC4801-$FC481F`.

**B. 16-bitars absolut kort adress**

```text
jsr $8D50.w
```

CPU:n teckenutvidgar absolut kort adress till `$FF8D50`. För att bindningstabellen på
RAM `$008D50` ska kunna nås den vägen krävs att hårdvaran aliasar
`$FF8000-$FFFFFF` mot `$008000-$00FFFF`. Det är speglingshypotesen, och den är
fortfarande [OPEN] (E2).

De två fallen ser nästan identiska ut i en hexdump. De är arkitektoniskt orelaterade.
Ett fynd av typ A är aldrig evidens för eller emot typ B.

## 2. MC68302 internt fönster (BAR)

BAR pekar fönstret till **`$FC6000-$FC6FFF`**, 4 KB:

| område | innehåll |
|---|---|
| `$FC6000-$FC67FF` | DPRAM, 2 KB (parameter-RAM + användbart RAM) |
| `$FC6800-$FC6FFF` | SIB-register, `Base = $FC6800` |

### 2.1 DPRAM används som exekveringsyta och som hoppbord

Två oberoende observationer:

1. **Reset-bryggan.** ROM kopierar 14 byte till `$FC6200` och hoppar dit för att
   kunna programmera om BR0. [Verified]
2. **Vektorer och anrop pekar in i DPRAM.** OS-bildens vektor 4 → `$FFFC6000`,
   vektor 11 → `$FFFC6014`. Absoluta anrop från ROM och OS träffar `$FC6014`,
   `$FC602C`, `$FC6056`, `$FC605C`, `$FC6062`, `$FC6068`, `$FC606E`, `$FC6074`,
   `$FC6080`, `$FC608C`, `$FC6098`, `$FC60A4`, `$FC60B0`, `$FC60B6`, `$FC60BC`,
   `$FC60C2` — alla på ett **stride-6-rutnät från `$FC6014`**. En andra serie
   (`$FC6028`, `$FC602E`, `$FC603A`, `$FC6046`) ligger på ett parallellt
   stride-6-rutnät. [Verified att adresserna ligger på rutnäten]

   Stride 6 = `4EF9 <32-bitars adress>`. Det är samma konstruktion som
   bindningstabellen i RAM (se `rom-os-abi.md`). Slutsats: **`$FC6000-$FC60C8` är
   ett litet hoppbord i DPRAM**, cirka 30 poster. [Likely]

### 2.1b Vad som redan är känt om DPRAM-innehållet

DPRAM är **inte** en enda okänd yta. Fem klasser ska hållas isär:

| område | innehåll | status |
|---|---|---|
| `$FC6000-$FC60C8` | hoppbord, stride 6; vektor 4 → `$FC6000`, vektor 11 → `$FC6014` | struktur [Likely], mål [OPEN] |
| `$FC60B0`, `$FC60B6`, `$FC60BC` | verifierade MOVEP-thunkar (se `movep-library.md`); `$FC60B0` anropas av PAR-kalibreringen på RAM `$006880` | [Verified] |
| `$FC6200` | reset-bryggan — de 14 byte ROM kopierar dit vid boot, innehållet känt byte för byte | [Verified] |
| `$FC6400` / `$FC6500` | SCC1:s och SCC2:s parameter-RAM. Laddas som pekare av ROM (`$F8C174`, `$F8C190`) **och** av båda OS-versionerna, och används i varje SCC-mottagning | adressering [Verified], innehåll [OPEN] |
| `$FC6600`, `$FC6608`, `$FC6700`, `$FC6704`, `$FC6710` | SCC3:s parameterområde plus ett block till | [OPEN] |
| övrigt `$FC6000-$FC67FF` | odumpat | [OPEN] |

Att dumpa DPRAM efter boot ger hoppbordens mål **och** buffer descriptor-innehållet i en
enda körning. Det är den billigaste öppna åtgärden i projektet.

### 2.2 SIB-registerkarta med referensräkning

Offset enligt MC68302 UM, Table 2-9 (`Base = $FC6800`). Räkningen är antalet
32-bitars absoluta referenser i respektive avbild.

| adress | reg | ROM | V161 | V350 | anmärkning |
|---|---|---|---|---|---|
| `$FC680E` | (BAR-blocket) | 0 | 1 | 1 | offset ej bekräftat |
| `$FC6812` | GIMR | 1 | 0 | 0 | ROM skriver `$8040` |
| `$FC6814` | IPR | 2 | 0 | 0 | ROM skriver `$FFFF` (rensa) |
| `$FC6816` | IMR | 3 | 1 | 1 | ROM skriver `$0000` |
| `$FC6818` | ISR | 6 | 4 | 4 | ROM skriver `$FFFF` (rensa) |
| `$FC681E` | PACNT | 1 | 0 | 0 | `$E000` |
| `$FC6820` | PADDR | 1 | 0 | 0 | `$FFFF` |
| `$FC6822/23` | PADAT | 5+7 | 0 | 1 | `$18FC` vid init |
| `$FC6824` | PBCNT | 1 | 0 | 0 | `$0080` |
| `$FC6826` | PBDDR | 1 | 0 | 1 | `$F097` |
| `$FC6828` | PBDAT | 3 | 0 | 0 | `$0007` vid init |
| **`$FC6829`** | **PBDAT (låg byte)** | 2 | **12** | **14** | **firmwarets analoga kanalval; fysisk muxkoppling `[Likely]`** |
| `$FC6830-$FC683E` | BR0/OR0…BR3/OR3 | 1 var | 0 | 0 | endast boot |
| `$FC684A` | WRR | 1 | 0 | 0 | `$0000` |
| `$FC6850` | TMR2 | 1 | 0 | 0 | `$003B` |
| `$FC6852` | TRR2 | 1 | 0 | 0 | `$3F01` |
| `$FC6856` | TCN2 | 0 | **0** | **1** | endast V3.50 läser den |
| `$FC6860` | CR (Command Register) | 3 | 0 | 0 | CP-kommandon |
| `$FC6880` | SCC1 registerbas | 1 | 1 | 1 | laddas i A1/A3 → **registerrelativ** åtkomst osynlig här |
| `$FC6882` | SCON1 | 1 | 1 | 1 | `$7000` från både ROM och OS |
| `$FC6884` | SCM1 | 1 | 1 | 1 | ROM `$7033`, OS `$703B` (ENR) |
| `$FC6886` | DSR1 | 0 | 0 | 0 | inga identifierade absoluta ref.; registerrelativt ej uteslutet |
| `$FC6890/92/94/96` | SCC2 bas/SCON/SCM/DSR | 1/1/1/**0** | 1/1/1/**0** | 1/1/1/**0** | samma mönster |
| `$FC68A2/A4/A8/AA` | SCC3 SCON/SCM/SCCE/SCCM | 1 var | 0 | 0 | endast ROM; SCM3 `$0038` = **HDLC** |
| `$FC68A6` | DSR3 | 0 | 0 | 0 | inga identifierade absoluta ref. |
| `$FC68B2` | **SIMASK** | 0 | 0 | 0 | inga identifierade absoluta referenser |
| `$FC68B4` | **SIMODE** | 1 (`$4189`) | 1 (`$4189`) | 1 (`$4189`) | samma värde från ROM och OS; se `mc68302-status.md` §0 |

Tre oberoende korsvalideringar av offsetkartan:

* `$FC684A` skrivs med `$0000` — matchar den kända WRR-skrivningen.
* `$FC6852` skrivs med `$3F01` — matchar den kända TRR2-skrivningen.
* `$FC6856` läses **en gång i V3.50 och aldrig i V1.61** — matchar exakt det tidigare
  Timer 2-fyndet ("V350 läser TCN2 en gång, V161 har ingen Timer 2-åtkomst alls").

**Korrigering:** `$FC6829` har tidigare beskrivits utan registeridentitet. Det är
PBDAT:s låga byte. `ori.b #$07,($00FC6829).l` i PAR-kalibreringen sätter alltså
PB2–PB0, dvs **firmwarets analoga kanalval**, och `andi.b #$F8` nollställer
fältet först. V3.50-runtime parar den cykliska 0/2/5/3/4(+7)-sekvensen med
ES5506 PAR-läsningar; att den fysiskt driver just U55 är `[Likely]` tills
pinrouting verifierats. Det är den mest använda enskilda 68302-registeradressen
i hela OS:et (12 respektive 14 referenser).

Den fullständiga avgränsade V3.50-domänen är 0,2,5,3,4 plus periodisk 7 och
villkorlig 1. Selector 1 kräver ASR-88-modellflagga samt key-event-countdown och
är `[Likely]` mono/channel pressure. Selector 6 saknar generator i den
analyserade acquisitionvägen och är `[Verified unreachable]` där; detta bevisar
inte dess fysiska mux-pinidentitet. Se
`../investigations/analog-selector-control-map-v350.md`.

**Blind fläck:** räkningen ovan täcker bara 32-bitars absolut adressering.
Registerrelativa åtkomster `(d,An)` syns inte. Det felet har kostat projektet fem gånger.

## 3. RAM

Externt avkodat. Drivrutinen mappar i dag:

```
map(0x000000, 0x0fffff)  handlers (low_rom_or_lowmem)
map(0x100000, 0x1fffff)  .ram()      samplings-RAM-kandidat
map(0xf00000, 0xf7ffff)  .ram()
map(0xfc5020, 0xffffff)  .ram()      catch-all
```

Verifierat RAM-innehåll efter boot:

| RAM | innehåll | belägg |
|---|---|---|
| `$000000-$0003FF` | 68000-vektortabell + OS-variabler från `$0000C0` | `vector-map.md` |
| `$000A00-$007FFF` | OS-kod, segment 1 | `os-image-layout.md` |
| `$00801E-$009FF6` | **ROM↔OS-bindningstabellen**, 723 slots | `rom-os-abi.md` |
| `$00A000-$01D6A0` | OS-kod, segment 2 (V3.50) | `os-image-layout.md` |

## 4. Speglingen `$FF0000-$FFFFFF` ↔ `$000000-$00FFFF`  [OPEN]

ROM gör **1254 `jsr/jmp abs.w`-anrop till 334 distinkta adresser i `$FF801E-$FF9FF6`**.
Inget av de 334 målen är udda; slumpdata skulle ge omkring hälften udda. Enskilda
platser bekräftar att det är instruktioner:

```
$F97874:  12 1A        move.b (A2),D1
          4EB8 8030    jsr    $FF8030
          4E75         rts
```

Under den oberoende verifierade laddningsregeln för segment 1 ligger bindningstabellen
på RAM `$00801E-$009FF6`, och **313 av 334 mål (93,7 %) landar där exakt på en
`JMP`-opcode**. Samma siffra för V1.61 och V3.50.

Mekanismen är 68000:ans korta absoluta adressering: `$8000.w`…`$FFFF.w` teckenutvidgas
till `$FF8000`…`$FFFFFF`. En spegling gör att kortadresseringen täcker ett
sammanhängande 64 KB-fönster över RAM `$0000-$FFFF`, till priset av 2 byte mindre per
instruktion. Bindningstabellen är medvetet lagd i fönstrets övre halva.

Det förklarar också `($8D50).w` i SCC-koden: det är slot `$8D50.w` i bindningstabellen,
inte en variabel.

**E2-resultat 2026-08-10.** [OPEN] Spegling `$FFxxxx` <-> `$00xxxx`.

Observerat:

* 566229 dataläsningar i `$FF8000-$FFFFFF` under V3.50-körning.
* `$FF8D44 = $F9` och `$008D44 = $00` vid samma körning.

Ej fastställt:

* om fönstret är avkodat i `mem_map`
* om `$F9` är minnesinnehåll eller open bus
* om opcode-hämtning speglar; Lua saknade åtkomst till opcode-space
* samplingstidpunkt relativt OS-laddning

Ej utfört: det ursprungliga V1.61-testfallet där `$00BF0E` kör
`4EB9 FFFF8ECA` och returnerade rent till `$00BF14` i den historiska körningen.
V3.50 har motsvarande sekvens på `$00E48A`, inte `$00BEE2`.

De 1404 kanterna med `mapping_basis=mirror-hypothesis` står kvar. Den statiska
bindningstabellkopplingen är fortfarande verklig som kodmönster, men MAME-datareaden
avgör inte hårdvaruspeglingen och får inte höjas till `direct` utan ny evidens.

**Runtime-fönsterobservation 2026-08-11.** [OPEN] V3.50 körde kod vid `$FFB0BC/$FFB0D4`
i `FILE 1  TUTORIAL BNK`-läget. Samma körning dumpade 64 byte från de höga adresserna
och motsvarande låga adresser:

```text
$FFB0BC:
1239fffc4813c23c0050670661c86000020a7000720074001239fffc4817307803c04ed04a38ccd1677031fcb39203c04a016654610000eeb43c00806746b43c

$00B0BC:
fffba626661e0838000304c167244eb9000133c64a38049d6600018a11fc000404b060644a38016a67120838000304c1660a11fc001c049d6000016a1038049a

$FFB0D4:
1239fffc4817307803c04ed04a38ccd1677031fcb39203c04a016654610000eeb43c00806746b43c003a6518b43c0040650876024eb88912600876004eb9fff8

$00B0D4:
6600018a11fc000404b060644a38016a67120838000304c1660a11fc001c049d6000016a1038049ab03c00006716b03c000767104a3804b2660a11fc0009049d
```

Bytejämförelse: `$FFB0BC`/`$00B0BC` skiljer sig i 60 av 64 byte, första skillnad
`+00 = $12/$FF`; `$FFB0D4`/`$00B0D4` skiljer sig i 61 av 64 byte, första skillnad
`+00 = $12/$66`. Detta är en observation av dessa två adresser i den nuvarande
MAME-modellen, inte en omklassificering av E2 eller av de 1404
`mirror-hypothesis`-kanterna.

Disassemblering av det exekverade höga fönstret:

```text
$FFB0B0  8FCC             dc.w    $8fcc
$FFB0B2  0C40 3838        cmpi.w  #$3838,D0
$FFB0B6  57F8 CCD1        seq     ($ccd1).w
$FFB0BA  4E75             rts
$FFB0BC  1239 FFFC 4813   move.b  $fffc4813.l,D1
$FFB0C2  C23C 0050        and.b   #$50,D1
$FFB0C6  6706             beq     $ffb0ce
$FFB0C8  61C8             bsr     $ffb092
$FFB0CA  6000 020A        bra     $ffb2d6
$FFB0CE  7000             moveq   #$0,D0
$FFB0D0  7200             moveq   #$0,D1
$FFB0D2  7400             moveq   #$0,D2
$FFB0D4  1239 FFFC 4817   move.b  $fffc4817.l,D1
$FFB0DA  3078 03C0        movea.w ($03c0).w,A0
$FFB0DE  4ED0             jmp     (A0)
```

## 5. CS1 — `$FF6000-$FF7FFF`  [OPEN]

Projektets största öppna hårdvarufråga. Allt känt samlat.

### Vad som är fastställt

* BR1 = `$1FEF`, OR1 = `$FFFE`, skrivet på `$F80036` respektive `$F8002E` — **före**
  PIO-init, DUART-init och allt annat. [Verified]
* Fönstret är 8 KB, `$FF6000-$FF7FFF`.
* Enable-biten är satt och ändras aldrig. Fönstret är aktivt genom hela körningen.
* **Riktning: endast skrivning** (BR bit 1 = 1, OR MRW = 1 → RW inte maskad).
* **Extern DTACK** (OR bit 15–13 = `111`) — enheten kvitterar själv, variabel timing.
* **Ingen FC-jämförelse** (OR bit 0 = 0), som för alla fyra chip selects.

### Det strukturella argumentet

En teckenutvidgad `abs.w` kan bara producera `$000000-$007FFF` eller `$FF8000-$FFFFFF`.
**`$FF6000-$FF7FFF` är därmed oåtkomligt med kortadressering** och kan bara nås med
32-bitars absolut eller registerindirekt adressering.

Det är inte en slump. Kortfönstret `$FF8000-$FFFFFF` är fullt av bindningstabell och
het OS-kod. CS1 är lagt precis under, i den första adressen som *inte* kan nås billigt.
Det talar starkt för att området inte är kod.

### Referensräkning

Instruktionsfiltrerade referenser: **inga hittade** i vare sig ROM eller OS.
Ofiltrerade 32-bitars långord som råkar falla i intervallet: 160 i ROM, 11 i V161,
37 i V350 — på den träffnivån är det brus, inte belägg.

Slutsatsen som får dras är smalare: **inga direkta absoluta eller identifierade
immediate-basreferenser har hittats.** Eventuell användning kan ske genom indirekt eller
dynamiskt härledd adressering (aritmetiskt beräknad adress, pekare hämtad ur en tabell
eller ur RAM, runtimegenererad kod), ligga i en kodväg som ännu inte klassificerats eller
ännu inte exekverats — **eller saknas helt**. Metoden kan inte skilja dem åt.

### Runtimeobservation 2026-08-27

E4:s första riktade körning är nu gjord. En Lua-skrivtapp över hela CS1,
retained genom hela mätningen och med levande low-RAM-witness, fångade **133**
firmware-skrivningar i V3.50 genom:

```
FILE 1 -> FILE LOADED -> Instrument 1 -> MIDI note-on
```

Skrivningarna ligger i `$FF7F00-$FF7FF6`. Boot skriver den regelbundna
stride-8-serien; note-on ger tre ord från den redan kartlagda
per-voice-helperkedjan:

```
$F8E278 -> $FF7F00 = $0006
$F8E27C -> $FF7F02 = $0007
$F8E280 -> $FF7F04 = $0008
```

`$F8E270` laddar `A0` från röstpostens `+$2A`, vars initiering utgår från
`$FF7F00`. CS1 är därmed **[Verified runtime used]**, medan den fysiska
mottagaren och registersemantiken fortsatt är `[OPEN]`. Den observerade
per-voice-trafiken är inte belägg för clock/rate-control. Se
`../investigations/audio-clock-and-rate-architecture-v350.md`.

### Hypoteser

| # | hypotes | talar för | talar emot |
|---|---|---|---|
| H1 | ~~SCSI-kontroller~~ | — | **[DISPROVEN]** — SCSI-kretsen ligger på `$FC5001`/`$FC5003` i **CS3**. ROM `$FBB5C0` gör `movea.l #$00FC5001,A4` / `movea.l #$00FC5003,A3`, och både ROM och båda OS-versionerna skriver `move.b #$18` (WD33C93 Command) följt av `move.b #$00` (Reset). Två register på udda lane med stride 2 är AM33C93A:s programmeringsmodell. |
| H2 | Expansions-/tillvalsfönster (minne, SP-kort) | 8 KB, aktivt hela tiden, aldrig refererat absolut | inget positivt belägg |
| H3 | NVRAM / kalibreringsdata | 8 KB är rimlig storlek; skulle nås via pekare | inget positivt belägg |
| H4 | Diagnostik-/testfönster | förklarar varför normal boot aldrig rör det | inget positivt belägg |
| H5 | Oanvänt — konfigurerat "för säkerhets skull" | firmware initierar ofta hela adressrymden vid boot | RW-, MRW- och DTACK-fälten är **inte** defaultvärden; någon har medvetet programmerat CS1 som en skrivport |

### Vad som är känt och vad som inte är det

```
CS1   $FF6000-$FF7FFF
      enabled
      write-selected / write-only
      external DTACK
      no function-code comparison
      function unknown
```

Inga identifierade direkta absoluta eller immediate-basreferenser i ROM eller i någon
OS-version; runtime använder i stället åtminstone en indirekt/per-röstväg i
`$FF7Fxx`.

Elektriskt är fönstret alltså kartlagt. Funktionellt är det helt öppet.

### Rekommendation — E4

Att CS1 är oförklarat betyder **inte** att analysen är fel. "Tidigt initierat" är inte
samma sak som "tidigt använt".

För framtida funktionsklassning: skrivtappa `$FF6000-$FF7FFF`, logga
**adress, bredd, värde, PC och körfas**. Normal boot och en note-on är nu
verifierade; kör därefter igenom:

```
filbläddring · instrumentladdning · spela ljud · sampling
effektladdning · hårdvarutest · optionsdetektering
```

ES5701/ljud-glue är fortfarande en rimlig hypotes men saknar positiv
**funktions- eller kopplingsevidens** från CS1-trafiken.

Jämför också mot `Asr10Cs3Decoder` i 68302-emulatorprojektet innan nya hypoteser läggs till.
