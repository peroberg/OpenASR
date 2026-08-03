# ASR-10 bootkedja

Hela vägen från reset till `FILE 1  TUTORIAL BNK`, i ordning. Varje steg
länkar till `subroutine-index.md` för detaljer.

**Ungefär hälften av övergångarna har en identifierad rutin. Resten är
markerade `RUTIN EJ IDENTIFIERAD` och utgör arbetslistan.** Ett steg utan
rutin betyder att vi vet att det händer, inte hur.

Referenskörning från riktig ASR-10 (användarens maskin):

```
ENSONIQ ASR-10 -> SCSI INSTALLED -> SEARCHING FOR SCSI DEV
-> PLEASE INSERT DISK -> LOADING SYSTEM -> TUNING KBD - HANDS OFF
-> KEYBOARD TUNED -> FILE 1...
```

Emulerad körning saknar de två SCSI-stegen (ingen SCSI-option). Övrigt
matchar.

---

## 1. Reset `[V]`

`$F8000C` `move.w #$2700,sr`. SSP `$00000300` och PC `$0000000C` ur
ROM-avbildens första åtta byte. Endast tre `move.w #imm,sr` finns i hela
ROM:en; `$F87FCC` är den enda som öppnar avbrott helt.

## 2. Hårdvaruinit `[V delvis]`

`$FB8E7E` sätter MC68302:s avbrottskontroller och Timer 2: GIMR←`$8040`,
IMR←0, ISR←`$FFFF`, IPR←`$FFFF`, TRR2←`$3F01`, TMR2←`$003B`.

DUART:en initieras med CTUR/CTLR←`$07D0` och ACR←`$60` (timerläge,
X1/CLK). Det ger `2 × 2000 / 4 MHz` = **1,000 ms tick**, vilket är
uppmätt. Räknaren startar implicit vid ACR:s bit 6-övergång — inget
Start Counter Command utfärdas någonsin.

`$F97BC6` ser ut att vara en tabellstyrd DUART-init i (offset, värde)-par
`[L]` — aldrig bekräftad vid körning.

**RUTIN EJ IDENTIFIERAD:** MC68302:s BAR/SCR- och chip-select-uppsättning.
Vi vet att BAR placerar det interna fönstret på `$FC6000`, men inte var
det skrivs.

## 3. Panelhälsning `[V]`

`   ENSONIQ  ASR-10    ` skrivs via THRB `$FFFC4817` av paneldrivrutinen
`$F89AA2`-`$F89D22`. Källtext ur 22-teckenstabellen `$FB8F2E`-`$FB9094`.

## 4. SCSI-avsökning `[V som text, ej i emulering]`

`SCSI INSTALLED` och `SEARCHING FOR SCSI DEV.` finns i samma tabell.
Visas på användarens maskin, inte i emuleringen.

**RUTIN EJ IDENTIFIERAD.**

## 5. Diskdetektering `[V delvis]`

`PLEASE INSERT DISK` ur tabellen. FDC-väntningarna `$FB8D1E` (MSR bit 4
CB), `$FB8D40` (RQM=1 och DIO=0) och `$FB8D78` hör hit; timeout `80000`
sätts på `$FB7BD6`. Alla tre löser ut på första pollningen i den
verifierade V350-körningen.

**RUTIN EJ IDENTIFIERAD:** vad som avgör att en diskett finns och startar
laddningen.

## 6. OS-laddning `[V som dataflöde]`

`    LOADING SYSTEM    ` visas. FDC läser diskens spår sekventiellt.
**Transaktion 9** läser C=01, H=01, R=01..20 och för in överlagringen som
innehåller den diskresidenta koden på `$0067xx`-`$0078xx`. Byte-för-byte
verifierad mot `V350.img`.

**RUTIN EJ IDENTIFIERAD:** transportrutinen som driver transaktionerna.

## 7. ROM lämnar över till diskresident kod `[EJ IDENTIFIERAD]`

**Den enskilt viktigaste luckan i kedjan.** Vi vet att koden hamnar i RAM
och exekveras, men inte var överlämningen sker eller hur kontrollen ges
till den.

## 8. Analog kalibrering `[V]`

Kör som schemalagd uppgift, alltså efter första ticken.

```
0067EC  valj kanal 7 (PBDAT bit 2:0)  ->  006864  mat 8 ganger
006800  divu.w D2  ->  faktor pa $0DF2
00680C  valj kanal 5  ->  006864  ->  primar filtercell ($0DC2+6)
00683A  valj kanal 0  ->  006864  ->  centrum +/- $528 pa $0DDE/$0DE0
0068C8  andra vagen, kanal 7 igen, instans $0DD0
```

Detta är där `ERROR 130` uppstod när PAR svarade noll. Endast noll är
dödligt; koden klampar overflow avsiktligt.

## 9. Klaviaturtuning `[V som text]`

`TUNING KBD - HANDS OFF` byggs ur fragmentvokabulären `$F81003`-`$F81F87`.

**RUTIN EJ IDENTIFIERAD:** själva tuningen.

## 10. `KEYBOARD TUNED` `[V som text]`

Fragment ` TUNED` på `$F812CC`.

**RUTIN EJ IDENTIFIERAD:** rutinen som avslutar tuningen och postar nästa
arbete. Spår finns i `investigations/filesystem-browser-map.md` §4.15:
en nod med `+2=0x89a2` postas till slot 0 via `trap #9` i samma ögonblick
tuningen slutförs.

## 11. Effektnedladdning `[V delvis]`

ES5510-värdfönstret `$FC3000`-`$FC31FF`. Selektorerna ligger på `$FC3101`
(`$80`), `$FC3141` (`$A0`), `$FC3181` (`$C0`) och `$FC31C1` (`$E0`).
`$E0` = "Write select GPR + INSTR" är den som commitar.

Misslyckas den blir det `EFFECT DOWNLOAD FAILED` och `ERROR 032` — enligt
servicehandboken "bad download".

**RUTIN DELVIS IDENTIFIERAD:** överföringsloopen anges som `$F97574` i
`investigations/filesystem-browser-map.md` §4.22, ej oberoende bekräftad.

## 12. Root directory läses `[V som dataflöde]`

FDC läser cylinder 0, huvud 0, block 2-3 = filoffset `$400`-`$7FF`.
Katalogen börjar på `$41E`, 26 byte per post.

**RUTIN EJ IDENTIFIERAD:** parsern.

## 13. Typfilter `[EJ IDENTIFIERAD]`

Bläddraren visar instrument (`$03`) och banker (`$1E`). Hittar den inga
skrivs `NO INST OR BANK FILES`. V350 innehåller nio instrument och två
banker, så meddelandet är alltid ett fel när det uppträder.

## 14. `FILE 1  TUTORIAL BNK` `[V som utfall]`

Post 2 i katalogen, typ `$1E` = BANK. På riktig hårdvara lyser `INST` och
`STOP` fast, `LOAD` blinkar snabbare än 1 s, och `BANK` tänds för
bankfiler.

**RUTIN EJ IDENTIFIERAD:** formateringen av raden.

---

## Genomgående: schemaläggaren

Från första ticken drivs allt av `$F88300` (IRQ6-producent, dekrementerar
slotarnas räknare och sätter pending) och `$F87F92` (dispatchskanning).
Sex slots, stride `$16`, tabellgränser `($00C6).w`/`($00C8).w`.

En slot vars räknare är noll blir **aldrig** redo. `trap #8` beväpnar
räknaren; faktisk väntan är `counter − threshold`.

## Genomgående: panelvägen

```
panelsvar -> mc68681 kanal B RX-FIFO -> SRB RxRDY -> ISR bit 5
          -> irq_cb -> IRQ6 -> $F884BE -> handler via ($00DE) -> RHRB pop
```

Drivrutinen använder `m_chanB->rx_fifo_push()`, en direkt FIFO-poke.
Enheten äger FIFO, RxRDY, ISR och avbrott — men seriell ramning och
timing är förbikopplade. Korrekt i verkan, inte elektriskt modellerad.

## Genomgående: analogporten

`es5506_host_read_par_diag()` (`asr10_boot.cpp:2528`) returnerar
`PAR_DIAGNOSTIC_VALUE = 0x200` **för samtliga kanaler** — den läser inte
PBDAT och skiljer inte på kanal. Bunden på rad 8895 via
`read_port_cb()`. Ingen användarinmatning kan åsidosätta den; det är en
kompileringstidskonstant.

Det är alltså **en syntetisk analog vilonivå**, inte en
board-defaulttabell. Den finns för att OS:ets kalibrering ska kunna
slutföras: kanal 7 mäts åtta gånger, summan blir D2, D2 är divisor i
kalibreringsfaktorn, och endast noll orsakar undantag. Övriga kanaler
primar filter, centrum och trösklar.

Värdet är valt, inte uppmätt. Servicehandbokens `REFERENCE >= 190` går
inte att översätta till PAR:s råvärde förrän servicetestets skalningskod
är spårad, och den finns varken i boot-ROM:en eller på V350/V161.

**Hygiennotering:** funktionen heter fortfarande `_diag` och gör
`logerror` vid varje läsning, trots att den nu är den permanenta
produktionsvägen. Bör döpas om och tystas.

---

## Arbetslista

Stegen utan identifierad rutin, i den ordning de sannolikt ger mest:

1. Steg 7 — ROM till diskresident kod. Störst lucka, förklarar mest.
2. Steg 13 — typfiltret. Litet, väl avgränsat, och `NO INST OR BANK FILES`
   är en känd bugg när det uppträder.
3. Steg 12 — katalogparsern. Behövs för en framtida native-motor.
4. Steg 10 — vad som avslutar tuningen och postar browserarbetet.
5. Steg 14 — radformateringen.
6. Steg 6 — FDC-transportrutinen.
7. Steg 5, 9, 4, 2 — mindre brådskande.
