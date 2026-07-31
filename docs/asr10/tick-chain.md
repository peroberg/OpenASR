# Tickkedjan: fyra räknare säger noll — DUART-tick når aldrig $F88300

2026-07-31. Läst: `docs/asr10/runtime-cycle.md`, `docs/mc68302/interrupt-source-map.md`,
`CLAUDE.md`. Metod: fyra exakta räknare på de fyra namngivna PC:na plus
två tabellsnapshot, byggda på samma sätt som `runtime-cycle.md`
(temporär räknare bakom en env-flagga, `ASR10_DIAG_TICK_CHAIN`, borttagen
efter mätningen). `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader efter uppgiften.

Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_TICK_CHAIN=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

## 0. Den statiska kedjan, verifierad instruktion för instruktion

`[Verified]`, disassemblerat med `unidasm -arch m68000` mot den hi/lo-
sammanflätade ROM-avbilden. Uppgiftens egen kedja stämmer, med en liten
rättelse (se nedan):

```
f88300: move.b  $fffc481f.l, D0     ; DUART "Stop Counter"-registret läst = IRQ6-ack
f88306: movea.w $c6.w, A0            ; A0 <- primärtabellens bas
f8830a: moveq   #$0, D0
f8830c: move.w  (A0), D1             ; D1 = post.counter (+0x00)
f8830e: beq     $f8831e              ; counter==0 -> hoppa till nästa post
f88310: subq.w  #1, D1
f88312: move.w  D1, (A0)             ; counter--
f88314: cmp.w   ($14,A0), D1         ; jämför mot tröskel (+0x14)
f88318: bhi     $f8831e              ; counter fortfarande > tröskel -> nästa post
f8831a: bclr    D0, ($2,A0)          ; counter <= tröskel -> rensa bit 0 av (+0x02)
f8831e: adda.w  #$16, A0             ; nästa post (stride 0x16)
f88322: cmpa.w  $c8.w, A0            ; mot tabellslutet
f88326: bcs     $f8830c              ; loopa
f88328: addq.b  #1, $b82.w           ; tickräknare++ (byte, EJ ord)
f8832c: cmpi.b  #$a, $b82.w
f88332: bcs     $f88362              ; <10 -> hoppa förbi sekundärtabellen helt
f88334: clr.b   $b82.w               ; var 10:e tick: nollställ
f88338: movea.w $ca.w, A0            ; A0 <- sekundärtabellens bas
f8833c: move.w  ($14,A0), D1         ; D1 = post.countdown (+0x14)
f88340: beq     $f88358              ; countdown==0 -> nästa post
f88342: subq.w  #1, D1
f88344: move.w  D1, ($14,A0)         ; countdown--
f88348: bne     $f88358              ; !=0 än -> nästa post
f8834a: movea.l ($16,A0), A1         ; countdown nådde exakt 0 -> ladda callback-pekare (+0x16)
f8834e: movem.l A0/A3, -(A7)
f88352: jsr     (A1)                 ; anropa fördröjd callback
f88354: movem.l (A7)+, A0/A3
f88358: adda.w  #$1a, A0             ; nästa post (stride 0x1a)
f8835c: cmpa.w  $cc.w, A0
f88360: bcs     $f8833c              ; loopa
f88362: ...                          ; (annan kod, utanför tickkedjan)
```

**Rättelse av uppgiftens egen beteckning:** `$F8831A` är `bclr D0,($2,A0)`
(D0=0, rensar bit 0), inte `bset`. Semantiskt gör den ändå "skapar
arbete": `runtime-cycle.md` visade att konsumentloopen (`$F87F96`)
dispatchar en post när `byte(post+2) XOR byte(post+3) != 0`; att
rensa bit 0 av `+0x02` kan mycket väl göra byte-paret olika (beroende
på vad partnerbyten `+0x03` innehåller) och därmed skapa dispatchbart
arbete — men mekanismen är en bitrensning, inte en bitsättning.
Räknat vid exakt rätt PC oavsett mnemonic.

## 1. De fyra räknarna: samtliga noll

`[Verified]`, 30 sekunders körning, V350.img, med
`ASR10_DIAG_PANEL_AUTORESPOND=1` (planens föreskrivna djupa körläge):

```
ASR10_TICK_CHAIN_COUNTS f88300=0 f8831a=0 f88328=0 f88352=0
```

**Tolkning enligt uppgiftens egen tabell: `alla fyra 0` ->
avbrottsleverans saknas före handlern -> spår A.** DUART-tick-IRQ6-
hanteraren `$F88300` körs inte en enda gång under hela körningen.

## Varför: SR-masken blockerar nivå 6 varje gång IRQ6 faktiskt reses

`[Verified]`, härlett från den redan existerande (opåverkad av den här
uppgiftens instrumentering) `event=irq6_route`-loggen
(`asr10_boot_state::panel_c_update_irq6`, `ASR10_DIAG_PANEL_AUTORESPOND`).
Denna logg visar att IRQ6 **faktiskt assertas** 160 gånger under
körningen (`m_maincpu->set_input_line(6, ...)` anropas verkligen, driven
av panelens RX-ready-bit `isr&imr&0x20`, `imr=0x2b` konsekvent genom hela
körningen — dvs den delade IRQ6-linjen som både panel-RX och
DUART-räknar-tick enligt uppgiftens egen kedja går via, är verkligen
inkopplad och verkligen skarp). Men vid **samtliga** 160 tillfällen är
CPU:ns eget SR-interruptmask redan på nivå 6 eller 7:

```
SR-mask=6:  97 tillfällen
SR-mask=7:  63 tillfällen
SR-mask<=5:  0 tillfällen
```

En nivå-6-begäran kräver mask < 6 (dvs 0-5) för att tas emot av 68000:an
— mask==6 blockerar den lika säkert som mask==7. **I den här
30-sekunderskörningen sänker CPU:n aldrig sin egen interruptmask under
6 vid något av de ögonblick IRQ6 omvärderas.** Det är inte MAME:s
routing som är trasig (linjen reses verkligen, vektorn `0x56` är
korrekt kopplad i `maincpu_iack_r`) — det är att ROM-koden (eller den
kod-väg boten befinner sig i under `TUNING KBD`) håller processorn med
interrupt disabled på just den nivå som skulle behövas. `[Hypothesis]`:
det är rimligt att just DENNA maskering är avsiktlig under en kritisk
sektion tidigt i initieringen, men eftersom den ALDRIG lättas under
hela det här fönstret (varken för räknar-ticket eller något annat
nivå-6-jobb) är nettoeffekten identisk med att avbrottsleveransen
saknas helt och hållet, sett från tickkedjans perspektiv.

De sex primärpostrena var redan "pending" (se avsnitt 2) från start av
den här mätningen — de sattes alltså av en annan, engångs boot-init-väg
(producenterna redan namngivna i `runtime-cycle.md`:
`f87e82`/`f87f28`/`f87f2c`/`f87fb0`/`f89ac2`/`f880ce`/`f880d2`/`f880fc`/
`f88100`/`f88120`/`f88124`/`f8816e`/`f8ce3a`), inte av `$F88300`. Ingen
av dessa PC:er sammanfaller med tickkedjan. Det bekräftar att de sex
initiala schemaläggningarna och den periodiska DUART-tick-kedjan är två
separata mekanismer, och att den här uppgiften fångar att den andra
(tick-kedjan) aldrig kommer igång alls.

## 2. Tabellsnapshot vid körningens slut

`[Verified]`. Eftersom tick-kedjan aldrig körs (avsnitt 1) är detta
också effektivt en "tidig" snapshot — inget i någon av tabellerna
ändras av `$F88300`/`$F88352` under hela körningen.

**Primärtabell** (`base=23f6 end=247a`, samma sex poster som
`runtime-cycle.md` redan identifierat):

| entry | counter (+00) | pending (+02) | partner (+03) | callback (+06) | threshold (+14) |
|---|---|---|---|---|---|
| `23f6` | `03e8` (1000) | `81` | `81` | `0000c7c8` | `0000` |
| `240c` | `0000` | `80` | `80` | `ffffc8b0` | `0000` |
| `2422` | `0000` | `80` | `80` | `0000738e` | `0000` |
| `2438` | `0000` | `80` | `80` | `fff8f2fa` | `0000` |
| `244e` | `0004` | `01` | `01` | `00006876` | `0000` |
| `2464` | `0064` (100) | `01` | `01` | `0000780c` | `0063` (99) |

Alla sex `pending==partner` (matchar `runtime-cycle.md`s "sent varv" —
redan dispatchade, ingen förändring sedan dess). Notera att `23f6` och
`2464` har ett kvarvarande, icke-noll `counter`-fält (1000 respektive
100) — dessa poster ÄR alltså laddade med en riktig nedräkning och
SKULLE fortsätta räkna ner om tick-kedjan någonsin körde, men gör det
inte eftersom `$F88300` aldrig nås.

**Sekundärtabell** (`base=14c0 end=14f4`, två poster, stride `0x1a`):

| entry | countdown (+14) | callback (+16) |
|---|---|---|
| `14c0` | `0000` | `00000000` (tom plats) |
| `14da` | `01f4` (500) | `0000ba18` |

## 3. Sekundärtabellens callback disassemblerad

`[Verified]` för adressen `$0000BA18` (entry `14da`), levande minnesdump
följt av `unidasm`:

```
0000ba18: move.w  #$1f4,$14ee.w      ; $14ee = ENTRY 14DA:S EGET countdown-fält (14da+0x14)
0000ba1e: move.l  #$0000ba18,$14f0.w ; $14f0 = ENTRY 14DA:S EGEN callback-pekare (14da+0x16)
0000ba26: rts
```

**Svar på punkt 3: callbacken återarmerar sig SJÄLV — den skriver
tillbaka sitt eget ursprungliga `countdown`-värde (500) och sin egen
callback-pekare till sin egen post i SEKUNDÄRTABELLEN.** Den rör
INGEN primärpost, och den gör inget synligt filsystemarbete. Detta är
en ren, självförnyande periodisk timer: var 500:e "var 10:e tick"
(dvs var 5000:e råtick, om tick-kedjan någonsin körde), skulle den
göra … exakt ingenting utåt förutom att räkna om sig själv. Uppgiftens
egen formulering ("callbacken kan återarmera en primär slot") stämmer
alltså bara delvis för den här specifika posten: den återarmerar en
SEKUNDÄR post (sig själv), inte en primär.

`[OPEN]`: de omedelbart efterföljande orden i minnet (`0000ba28`
och framåt, utanför den här callbackens `rts`) råkar innehålla ett
anrop till `$FB7C7A` — samma redan undersökta "semantic reader" av
`lowmem $04ee`/IPCR-bit4 från `docs/asr10/panel-ipcr.md`. Om detta är
en ANNAN, separat callback som bara råkar ligga precis efter i minnet,
eller om min gräns för var callbacken slutar (vid `rts`) är fel, är
inte avgjort här — flaggat som öppen fråga, inte påstått som fakta.

## Sammanfattning mot uppgiftens tolkningstabell

| Utfall | Match | Tolkning |
|---|---|---|
| Alla fyra räknare = 0 | **Ja** | Spår A: avbrottsleverans saknas före handlern |
| `F88328>0, F8831A=0` | Nej (F88328 också 0) | — |
| `F88352=0` (inga sekundära callbacks schemalagda) | Delvis — en post ÄR schemalagd (`countdown=500`, giltig callback), men når aldrig avfyras eftersom kedjan aldrig körs | Nedräknaren är inte 0, leveransen är |
| `F88352>0` men inget återarmeras | Ej tillämpligt (F88352=0) | — |

**Rotorsak, mätt:** IRQ6 reses verkligen (160 gånger, bekräftat via
`m_maincpu->set_input_line`), men CPU:ns SR-interruptmask står på 6
eller 7 vid samtliga tillfällen under hela 30-sekundersfönstret — aldrig
lägre. Det är inte en trasig routing i MAME:t; det är att den emulerade
CPU:n aldrig sänker sin egen mask till en nivå som släpper igenom
nivå 6 under den här körningens observerade tidsfönster. Nästa steg
(inte gjort här): identifiera VILKEN kodväg som sätter/behåller
SR-masken på 6/7 under `TUNING KBD`-fasen, och om det är avsiktligt
(kritisk sektion) eller en artefakt av att något annat (t.ex. en
väntad, aldrig inträffande händelse) förhindrar att masken någonsin
sänks igen.

## Städning

All instrumentering (`ASR10_DIAG_TICK_CHAIN`-räknarna för de fyra
PC:na, samt de två tabellsnapshot-funktionerna) borttagen i sin helhet
efter mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader. Ingen kompensation byggd.
