# ERROR 130: en schemalagd uppgift dividerar med noll — hårdvaruavbrott, inte mjukvarugren

2026-08-01. Läst: `docs/asr10/tick-rate.md`, `docs/asr10/mc68302-irq6-vector.md`,
`CLAUDE.md`. Metod: en watchpoint på lowmem `$C0`/`$C1` (filtrerad till
värdet `0x82` = 130 decimalt), en global instruktionsräknare, en
tickräknare (samma `irq_cb`-tap-teknik som `tick-rate.md`), en
watchpoint på DUART-registret "Start Counter", en watchpoint på
ACR-skrivningen, samt en dump av den levande (RAM-omlokaliserade)
vektortabellen och den anropande RAM-koden runt återvändningsadressen.
Allt gated bakom `ASR10_DIAG_ERROR130_PROBE`, borttaget efter
mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar
netto noll rader efter uppgiften.

Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_ERROR130_PROBE=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

## Sammanfattning

**`ERROR 130` orsakas av en genuin division med noll** i en
RAM-baserad, av schemaläggaren dispatchad uppgift — inte av en
mjukvarugren, inte av en TRAP-instruktion, utan av 68000-kärnans egen
hårdvaruexception (vektor 5, Zero Divide), utlöst när `DIVU.W D2,D0`
körs med `D2=0`. Felkoden `0x82` (130 decimalt) kommer inte från
felberäkningslogik i den dividerande rutinen själv, utan från en
generisk, delad vektor-dispatchtabell i ROM:et som mappar varje
hårdvaruexception till sin egen felkod och hoppar till en gemensam
"maskera+lagra"-rutin.

## 1. Watchpoint på $000000C1: exakt en skrivning, precist lokaliserad

`[Verified]`. Under hela 30-sekunderskörningen skrivs `0x82` till
`$C1` **exakt en gång**:

```
pc=f88284  previous_pc=0067f6  addr=0000c1  previous=01  new=82
instr=24 909 866  tick=52  sr=2708  sr_mask=7
d0=ffffff82  d1=00000001  d2=00000000  d3=00000100
a0=00fc2001  a1=0002a400  a2=0000244e  a3=0002b176
time=0015.086,733,500
```

**`tick=52`** — skrivningen sker mellan den 52:a och den 53:e (sista)
biten i den redan dokumenterade 53-tick-skuren (`tick-rate.md`), bara
~0,9 ms innan skuren tar slut (`tick-rate.md`s sista tick var vid
`t=15.087,619,375`). Instruktionsnummer 24 909 866, mot ACR-skrivningen
(som startar hela tickkedjan) vid instruktion 24 817 220 — bara 92 646
instruktioner, ~65 ms, mellan att räknaren börjar tickas och att felet
skrivs.

## 2. Rutinen: en gemensam, delad exceptionstail — nådd via en riktig hårdvaruexception, inte en TRAP eller vanlig gren

`[Verified]`. `$F88284` föregås direkt av:

```
f88280: ori     #$700, SR      ; maskera alla avbrott (mask=7)
f88284: move.w  D0, $c0.w      ; lagra felkoden (D0 låg byte = 0x82) till $C0/$C1
f88288: jsr     $fff977b0.l
f8828e: jsr     $ffff8d44.l    ; (panelrendering/vidare felhantering, inte spårat vidare)
```

`$F88280` är **inte** unikt för det här felet. Det är en delad
"maskera och lagra felkod"-svans, nådd via en **vektor-dispatchtabell**
strax innan i minnet:

```
f882a0: movea.w #$1600, A7
f882a4: jmp     $fffb8e3e.l     ; (grannvektorns egen, separata hanterare)
f882aa: moveq   #-$80, D0  ;  bra $f88280
f882ae: moveq   #-$7f, D0  ;  bra $f88280
f882b2: moveq   #-$7d, D0  ;  bra $f88280
f882b6: moveq   #-$7e, D0  ;  bra $f88280     <-- HÄR, bekräftat live
f882ba: moveq   #-$7c, D0  ;  bra $f88280
f882be: moveq   #-$7b, D0  ;  bra $f88280
f882c2: moveq   #-$7a, D0  ;  bra $f88280
f882c6: moveq   #-$79, D0  ;  ...
```

`moveq #-$7e,D0` = `0xFFFFFF82` — låg byte `0x82`, exakt värdet som
skrevs. Varje post i den här tabellen hör till EN specifik
CPU-exceptionsvektor och sätter sin egen unika felkod innan den
grenar in i den delade `$F88280`-svansen.

**Bekräftat via den levande (RAM-omlokaliserade) vektortabellen, inte
ROM:ets ursprungliga boot-vektortabell** (som pekar någon helt annanstans
och bara gäller de allra första instruktionerna efter RESET, innan
OS:et flyttar sin egen vektortabell till RAM):

```
lowmem $14/$16 (vektor 5, Zero Divide) = fff8:82b6 -> maskerat till 24 bitar: $00F882B6
```

**`$00F882B6` är alltså den riktiga, aktiva Zero-Divide-hanteraren vid
den här tidpunkten i körningen — och det är exakt stubben som satte
`D0=0xFFFFFF82`.** Slutsats: rutinen nås via 68000-kärnans egen
hårdvaruexception för division med noll (vektor 5), inte via en
TRAP-instruktion, inte via en villkorlig mjukvarugren, inte via normal
`bsr`/`jsr`-anropskedja till en felfunktion.

**Är den en av schemaläggarens callbacks?** `[Verified]`, mycket
sannolikt ja. `A2=0x0000244e` vid felskrivningen är **exakt** en av de
sex platserna i den primära schemaläggartabellen (`$23F6`-`$247A`,
stride `0x16`, dokumenterad i `runtime-cycle.md`/`tick-chain.md`).
`mc68302-irq6-vector.md`s egen ögonblicksbild av samma tabell (från en
tidigare, separat körning) visade just den här platsen (`entry=244e`)
som **dispatchad** (`pending==partner`) under samma sorts tickskur.
Given `runtime-cycle.md`s redan dokumenterade dispatchmekanism
(`f87fa2`: återställer kontext från tabellposten, `rte`:ar in i den)
körs koden som delar med noll som — eller omedelbart efter — just den
uppgiften schemaläggaren dispatchade för den platsen.

## 3. Villkoret: inget program-synligt villkor — CPU:ns egen nolldelningskontroll

`[Verified]`. Anropande RAM-kod (`previous_pc=0067f6`), disassemblerad
från en levande minnesdump (RAM-omlokaliserad kod, inte ROM):

```
0067f6: move.w  D2, $0dd6.w      ; D2 = 0 (bekräftat live, se nedan)
0067fa: move.l  #$a3480000, D0
006800: divu.w  D2, D0            ; D2=0 -> 68000 HÅRDVARUEXCEPTION vektor 5, körs ALDRIG vidare
006802: bvc     $006808           ; -- aldrig nådd den här gången
006804: move.w  #$ffff, D0        ; -- aldrig nådd (mjukvarans egen overflow-sentinel)
006808: move.w  D0, $0df2.w       ; -- aldrig nådd (normal completion-väg)
```

**Det finns inget program-synligt villkor att disassemblera — det ÄR
kontrollflödet.** En riktig 68000 testar divisorn mot noll INNAN
divisionen påbörjas; är den noll genereras exceptionen omedelbart,
före den efterföljande `bvc`-instruktionen (som är till för
kvot-overflow med en GILTIG, nollskild divisor — ett helt annat
felfall som aldrig prövas här).

**`D2=0` bekräftat direkt via en redan existerande, sedan tidigare
byggd diagnostik** (`ASR10_DIVIDER_TASK2`, ovillkorlig, ingen ny kod
behövdes för att se den):

```
event=store_0dd6_rate_param pc=f87dd6 ... d2=00000020   (32, OK, fullbordas normalt)
event=store_0df2_divider_result pc=f87dd6 ...            (matchande resultat lagras)
event=store_0dd6_rate_param pc=fb90dc ... d2=00000020   (32, OK, fullbordas normalt)
event=store_0df2_divider_result pc=fb90dc ...            (matchande resultat lagras)
event=store_0dd6_rate_param pc=0067f6 ... d2=00000000   (NOLL -- inget df2-resultat följer)
```

De två FÖREGÅENDE anropen till samma "lagra hastighetsparameter"-plats
(`$0DD6`, en redan från tidigare session namngiven "rate param"-fält)
fullbordas normalt, med `D2=0x20` (32) och ett matchande
`$0DF2`-resultat. Det TREDJE anropet, från `$0067F6`, är det enda med
`D2=0` — och det enda som saknar ett efterföljande resultat, exakt vad
en hårdvarutrappad division med noll ska se ut som i den här loggen.
**Varför just den här beräkningen ger `D2=0` är inte spårat vidare** —
utanför den här uppgiftens omfattning, men en naturlig fortsättning
(kandidat: den redan kända, separata `ERROR 130`-kopplingen till
ES5506 PAR/host-port som returnerar 0 utan
`ASR10_EXPERIMENT_ES5506_HOST`/`PAR_DIAGNOSTIC`/`PAR_VALUE`,
`mc68302-irq6-vector.md`s citat av `filesystem-browser-map.md` 4.19 —
om ett PAR-härlett värde matar den här divisorn skulle det förklara
varför den blir noll exakt när den riktiga tickkedjan för första
gången kör uppgiften långt nog för att nå den här beräkningen).

## 4. Vad utfärdar "Start Counter", och varför vid t≈15,02 s?

`[Verified]`, men rättar uppgiftens egen premiss. **Ingen explicit
"Start Counter"-läsning (register 14) sker någonsin under hela
30-sekunderskörningen** — en riktad watchpoint på den läsningen gav
noll träffar. Det behövs inte: `mc68681.cpp` rad 967-976 visar att en
skrivning till **ACR** som slår på bit 6 (gå in i timerläge) triggar
`start_ct()` **direkt**, som en sidoeffekt av själva ACR-skrivningen —
ingen separat Start Counter-läsning krävs i timerläge.

```
ASR10_ERROR130_ACR_WRITE pc=f88440 previous_pc=f88416 data=60
instr=24 817 220 tick=0 time=0015.021,619,375
```

**Hela DUART-hårdvaruinitieringssekvensen (MRA, CSRA, CRA, CTUR, CTLR,
ACR — redan disassemblerad i `duart-imr.md`) sker alltså inte förrän
`t≈15,02 s`, inte bara en isolerad "Start Counter"-läsning.** Det är
inte en separat sen-start-bugg utöver den redan kända sena
initieringen — det är samma händelse.

**Är 15 sekunder för sent?** Uppgiftens premiss ("borde ha en tidsbas
långt tidigare") prövades inte oberoende här (skulle kräva en ny,
riktad spårning av hela `LOADING SYSTEM`-fasen, utanför den här
uppgiftens omfattning) men stämmer mot redan etablerad, oberoende
evidens från tidigare uppgifter i den här utredningen: CS3-access-
oraklet (tidigare session) visade FDC-pollning dominera de första
sekunderna av varje körning (över 1,8 miljoner accesser), konsekvent
med att `t=0`-`t≈15 s` går åt till att läsa OS:et från diskett
(`"LOADING SYSTEM"`-fasen) innan schemaläggarfasen — som DUART-
hårdvaran hör till — någonsin nås. En riktig diskettstation är
mekaniskt långsam; 15 sekunder för en full OS-inläsning från 3,5"-
diskett är inte i sig orimligt för riktig ASR-10-hårdvara heller. Det
är `[Hypothesis]`, inte oberoende verifierat här, att det är just
diskladdning och inget annat som fyller de 15 sekunderna — men det är
den enda kandidaten med existerande stödbevis i det här trädet.

## 5. Orsak eller följd? Masken sätts AV felrutinen, inte tvärtom

`[Verified]`, entydigt. `$F88280`s första instruktion är
`ori #$700,SR` — **explicit**, **omedelbart före** felkods-lagringen
vid `$F88284`. Den uppmätta `sr=2708` (mask=7) vid skrivningsögonblicket
är alltså den DIREKTA, omedelbara följden av den instruktionen som
körde ETT ögonblick tidigare — inte ett tillstånd som redan rådde av
någon annan, tidigare anledning.

**Ordningen är: divisionsexception → vektor 5 → `$F882B6` sätter D0 →
`$F88280` maskerar (mask←7) → `$F88284` lagrar felkoden.** Maskeringen
är orsak till att inga fler avbrott tas emot EFTER det här ögonblicket
(vilket `tick-rate.md` redan visade håller i sig, oförändrat, för
resten av 30-sekunderskörningen) — men den är själv en **följd** av
att felrutinen kördes, inte en förutsättning som redan fanns och som
på något vis orsakade felet. Det generiska felhanteringsmönstret
(maskera allt, rapportera, invänta omstart) är rimligt, avsiktligt
beteende för ett äkta fel — inte ett symptom på att något annat redan
hade gått sönder.

## 6. PLAN.md uppdaterad

`[Verified]`. Avsnitt 3 uppdaterat: `X1 = 4,000 MHz` markerad
`[Verified]` med hänvisning till den uppmätta 1,000 ms-perioden mellan
konsekutiva IRQ6-avbrott (`tick-rate.md`). `3,6864 MHz`-antagandet är
struket som en levande fråga — kvar bara som historisk bakgrund till
varför hypotesen restes ursprungligen.

## Städning

All instrumentering (`ASR10_DIAG_ERROR130_PROBE`: C1-watchpointen,
Start Counter- och ACR-watchpointerna, instruktions-/tickräknarna, den
levande vektortabell-dumpen och kod-dumpen kring den anropande
RAM-adressen) borttagen i sin helhet efter mätningen. `git diff --stat
src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader. Ingen
kompensation, ingen ny stub byggd.
