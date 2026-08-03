# Tick-hastigheten: det finns ingen faktor-577-bugg — DUART:en tickar rätt, CPU:n slutar bara lyssna

2026-08-01. Läst: `docs/asr10/mc68302-irq6-vector.md`, `docs/asr10/duart-imr.md`,
`CLAUDE.md`. Metod: en loggande tap på `m_duart->irq_cb()` (ersätter
tillfälligt `set_inputline` rakt av — forwardar ovillkorligt till samma
anrop, så själva kopplingens beteende är oförändrat oavsett flaggan),
periodisk sampling av räknarens levande värde (`m_duart->read(6)`/`read(7)`)
plus SR-mask och ISR var 1 000 000:e instruktion, och en riktad logg för
skrivningar till CTU/CTL/ACR. Allt gated bakom `ASR10_DIAG_TICK_RATE_PROBE`,
borttaget efter mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader efter uppgiften. `docs/asr10/mc68302-irq6-vector.md`
uppdaterad separat (punkt 5).

Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_TICK_RATE_PROBE=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

## Sammanfattning

**Uppgiftens premiss ("52 på 30 s är 1,7 Hz, fel med faktor ~577") är en
missvisande genomsnittssiffra, inte en hastighetsbugg.** DUART:ens
räknare/timer tickar med **exakt rätt hastighet** (~1 ms/tick, matchar
`CTUR:CTLR=2000` @ 4 MHz) under en kort, tät skur på ~65 ms. Sedan slutar
CPU:n för gott att ta emot fler avbrott — inte för att DUART:en slutar
räkna eller begära, utan för att SR-masken permanent fastnar på 7 för
resten av körningen. Genomsnittet 1,7 Hz är vad man får när man delar en
65 ms-skur med korrekt ~1 kHz-takt över ett 30 s-fönster som till 99,8 %
av tiden inte har någon aktiv mottagare alls.

## 1. Vilken källa driver de 52 avbrotten? Bit 3, entydigt, alla gånger

`[Verified]`. Loggat vid varje genuin linje-övergång (105 händelser: 53
assert + 52 clear):

```
53 st state=1 (assert):  isr=08 (bit_counter_ready=1), ALLA övriga bitar 0
52 st state=0 (clear):   isr=00, pc=f88300 i samtliga fall
```

**Samtliga 53 avbrott drivs uteslutande av bit 3 (counter-ready).**
Aldrig bit 0 (TxRDYA), aldrig bit 1 (RxRDYA), aldrig bit 4 (TxRDYB),
aldrig bit 5 (RxRDYB) — trots att `IMR=0x2b` (`0010 1011`) har alla fyra
avmaskerade. RX/TX bidrar noll av de 52 händelserna. Varje `clear`
sker vid `$F88300` (den riktiga IRQ6-handlerns första instruktion, en
läsning av Stop Counter-registret — den ackvitterar/stoppar räknaren
och rensar därmed bit 3, exakt det mönster `duart-imr.md` redan
förutspådde).

## 2. Fyrar den riktiga räknaren? Ja — i en tät, korrekt skur, sedan fastnar CPU:n, inte den

`[Verified]`. Samtliga 53 assert-tidsstämplar, i ordning:

```
15.022,619,375   (start)
15.032,619,375   (+10,000 ms — det första varvet efter start tar längre,
                   se nedan)
15.033,619,375   (+1,000 ms)
15.034,619,375   (+1,000 ms)
...              (mestadels +1,000 ms, någon enstaka +2/+4 ms där
                   CPU:n var upptagen och missade ett fönster)
15.086,637,250   (sista `clear`)
15.087,619,375   (sista `assert`, seq 105/105)
```

**Mellan varven är intervallet nästan uteslutande exakt 1,000 ms** —
precis vad `CTUR:CTLR=0x07D0=2000` @ `X1=4 MHz` (`ACR=0x60`, timerläge,
källa `X1/CLK` odelad — se avsnitt 3) matematiskt ska ge:
`2000/4 000 000 = 500 µs` per halvperiod, avbrott var 1 ms (ISR-bit 3
sätts bara vid `!half_period`, dvs en gång per hel period,
`mc68681.cpp` rad 536-537). **Hastigheten är korrekt, inte fel.**

**Räknaren startar inte förrän ~15,0 s in i körningen.** Periodisk
sampling av det levande räkneverket (`m_duart->read(6)`/`read(7)`,
sidoeffektfritt) visar `current_count=0000` i alla 24 samples från
`t=0,6 s` till `t=14,5 s` — konsekvent med att "Start Counter" (register
14) helt enkelt inte har lästs än. Det matchar att ~15 sekunder går åt
till diskladdning/`LOADING SYSTEM` innan schemaläggarfasen (som
tick-kedjan driver) någonsin nås.

**Efter skuren: CPU:n slutar svara, DUART:en slutar inte begära.**
Sampling från `t≈17,6 s` till `t≈29,4 s` (24 samples, var 625:e ms):

```
sr_mask=7   i samtliga 24 samples, utan undantag
isr=08      i samtliga 24 samples — bit 3 fortfarande genuint satt
irq6_pending=1  i samtliga 24 samples — CPU:ns egen IRQ6-linje fortfarande asserterad
```

**Den tidigare, oprecisa "200 Hz"-siffran jag först räknade fram ur
råvärdena var ett samplingsalias, inte en verklig andra klocka.** Med
625 ms mellan sampeltagningarna och en verklig cykeltid på ~1-2 ms
träffar varje sampel en godtycklig punkt i en redan pågående,
självförnyande nedräkning (`duart_timer_callback` anropar `start_ct()`
på nytt varje gång den fyrar, `mc68681.cpp` rad 540) — det ser ut som en
långsam, oregelbunden klocka om man bara tittar på enskilda
stickprovsvärden, men den underliggande takten är alltid densamma
~1 ms-cykeln. Ingen ny, långsammare klocka konfigureras om — CTUR/CTLR/
ACR skrivs bara en gång var, i boten, aldrig igen (avsnitt 3).

**Slutsats: DUART-modellen har inget fel.** Den startar sent (väntar på
mjukvaran), tickar därefter exakt rätt, och fortsätter genuint begära
avbrott för resten av körningen utan att någonsin ge upp. Det är
`asr10booth`s egen CPU-tillstånd (SR-mask fast på 7) som slutar
lyssna, inte DUART-modellen som slutar prata.

**Varför fastnar masken på 7?** `[Likely]`, inte fullständigt
instrumenterat i den här uppgiften (skulle kräva en ny, riktad
undersökning): skuren av 53 avbrott inträffar rad-för-rad EXAKT före
`"ERROR 130 - REBOOT ?"` visas på panelen (loggrad 819914-820616 för
skuren, loggrad 821017 för felmeddelandet, i den körningen). Ett
felläge som väntar på en omstartsknapp som aldrig kommer i headless
läge är ett rimligt ställe för ROM:en att medvetet höja
interruptmasken och sluta schemalägga bakgrundsarbete — men det är en
rimlig förklaring, inte en verifierad kodväg. Ingen ny undersökning av
`ERROR 130`-hanterarens egen kod gjordes här.

## 3. CTUR, CTLR och ACR: bekräftade, en skrivning var, ingen loop

`[Verified]`, riktad logg vid skrivning till `$FC480D` (CTU), `$FC480F`
(CTL) och `$FC4809` (ACR):

```
seq=1 pc=f8843c addr=fc480d data=07 a0=fc4801 register=CTU
seq=2 pc=f8843c addr=fc480f data=d0 a0=fc4801 register=CTL
seq=3 pc=f88440 addr=fc4809 data=60 a0=fc4801 register=ACR
```

**Exakt en skrivning var, aldrig fler under hela 30-sekunderskörningen.**
`CTUR:CTLR = 0x07D0 = 2000` decimalt (bekräftar `PLAN.md` avsnitt 3:s
hypotes, nu mätt live snarare än teoretiserad). `ACR=0x60` = `0110 0000`
— bit 6 satt (timerläge, `mc68681.cpp` rad 446: `ACR & 0x40`), bitarna
5-4 `= 10` (`case 2: // X1/CLK`, rad 456-458) — full, odelad `X1`-klocka
som källa, inte `/16`. Det här är **inte** en tabellstyrd loop i
ROM-datamening — det är en rak sekvens av `move.b #imm,(offset,A0)`-
instruktioner mot ett delat basregister `A0=$FFFC4801` (redan
lokaliserad och disassemblerad i `docs/asr10/duart-imr.md`s
motsvarande avsnitt; den här uppgiften bekräftar samma tre värden på
nytt, oberoende, via en annan mätmetod). Ingen ytterligare
"init-loop" att lokalisera utöver det redan dokumenterade.

`m_maincpu`-oraklets tidigare "två träffar var på fc480c/fc480e" (en
ORD-nivå-räkning från en tidigare uppgifts SIB-orakel) räknar
troligen en läsning och en skrivning, eller hög/lågbyte-åtkomst,
separat från den här bytenivå-riktade taggen som bara räknar
skrivningar till den udda bytelanen — inte utrett vidare här, marginell
avvikelse utan betydelse för svaret.

## 4. Visas "TUNING KBD - HANDS OFF"? Nej — verifierat, inte gissat

`[Verified]`, `grep` mot hela råloggen (823 000+ rader), inte bara den
sista distinkta texten:

```
grep -c "TUNING KBD"      -> 0
grep -c "KEYBOARD TUNED"  -> 0
grep -c "HANDS OFF"       -> 0 (som panelrad; två träffar totalt i hela
                                 loggen är bara flagg-NAMN som
                                 "ASR10_EXPERIMENT_POST_TUNING_..."-
                                 raderna, inte panelinnehåll)
```

Full panelsekvens denna körning:

```
"q" -> "ENSONIQ ASR-10" -> "LOADING SYSTEM" -> "q" -> "q" -> "q"
-> "ERROR 130 - REBOOT ?"
```

**Rättelse av `mc68302-irq6-vector.md`s gissning:** den dokumentet
gissade att tuning "slutförs för snabbt för att panelen ska hinna
visa sin text". Det är inte vad som händer, och det går att avfärda
direkt: panelloggen (`ASR10PANEL text=...`) skriver en rad **varje
gång den ackumulerade texten ändras**, oavsett hur kort tid den
existerar — den skulle ha fångat en enda-instruktions-blipp lika bra
som en sekundlång visning. Att strängen aldrig förekommer alls,
någonstans i loggen, betyder att ROM:ens kontrollflöde **aldrig
passerar den kod som bygger den texten**, inte att det passerar den
för snabbt för att synas. Den gamla baslinjen (dokumenterad upprepade
gånger i `runtime-cycle.md`, `tick-chain.md`, `duart-imr.md`) visar
`"TUNING KBD - HANDS OFF"` som ett stabilt, upprepat tillstånd — boten
satt och visade den texten om och om igen i den gamla, trasiga
avbrottskopplingen. Med den riktiga kopplingen tar ROM:et en annan väg
som aldrig alls går via den koden. **Varför** kontrollflödet skiljer
sig åt (villkorlig gren beroende på avbrottstajmning, olika
kodvägsval efter att schemaläggaren faktiskt gör framsteg, etc.) är
inte utrett här — det är en öppen fråga för en framtida uppgift, inte
besvarad av den här.

## 5. `mc68302-irq6-vector.md` uppdaterad

`[Verified]`. Ett nytt stycke tillagt direkt efter `irq6_ack_vector()`s
kodblock: `irq6_ack_vector()` är hårdkodad mot `GIMR=0x8040` och läser
inte det faktiska registret (som inte är modellerat i den här enheten
ännu); varning tillagd om att detta måste åtgärdas — läsa det riktiga
GIMR-registret — innan `mc68302_device` används av någon annan
drivrutin eller om ASR-10-drivrutinen börjar skriva ett annat värde än
det ROM:en råkar sätta idag.

## Städning

All instrumentering (`ASR10_DIAG_TICK_RATE_PROBE`: IRQ6-tappen,
räknar-/SR-/ISR-samplingen, CTU/CTL/ACR-skrivloggen) borttagen i sin
helhet efter mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader. Ingen kompensation, ingen ny stub byggd.
