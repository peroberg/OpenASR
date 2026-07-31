# DUART IMR: den riktiga räknar-tick-avbrottet är genuint pending — bron som skulle koppla den till IRQ6 finns bara för RX

2026-07-31. Läst: `docs/asr10/tick-chain.md`, `docs/asr10/runtime-cycle.md`,
`CLAUDE.md`. Metod: sju exakta PC-räknare (med första/sista
instruktionsnummer), en IRQ6-övergångslogg som jämför det redan
existerande handmodellerade skuggregistret mot den RIKTIGA
`mc68681_device`-instansens eget ISR (`m_duart->read(5)`, ett
sidoeffektfritt register per `mc68681.cpp:700-701`), en SR-efter-
$F87FCC-logg, och en riktad logg för skrivningar till CTU/CTL/ACR.
Allt gated bakom `ASR10_DIAG_DUART_IMR_PROBE`, borttaget efter
mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar
netto noll rader efter uppgiften.

Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_DUART_IMR_PROBE=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

## Rättelse av uppgiftens eget statiska underlag

`[Verified]`. Uppgiftens LÄGE säger "$F88400 och $F8A048 är de ENDA två
skrivningarna till DUART:ens IMR i hela ROM:en." Det stämmer inte —
det finns en **tredje**, som en textsökning efter den bokstavliga
adressen `480b` missar eftersom den skriver via ett adressregister:

```
f883fe: bsr     $f88450
f88400: move.b  #$0, $fffc480b.l        ; IMR = 0x00 (litteral adress)
f88408: bsr     $f88458
f8840a: move.b  #$10,($4,A0)            ; A0 = $FFFC4801 (bekräftat live)
f88410: move.b  #$13,($0,A0)            ; MRA (MR1A)
f88416: move.b  #$07,($0,A0)            ; MRA (MR2A)
f8841c: move.b  #$ee,($2,A0)            ; CSRA (baud-val kanal A)
f88422: move.b  #$01,($4,A0)            ; CRA
f88428: move.b  #$30,($4,A0)            ; CRA
f8842e: move.b  #$08,($4,A0)            ; CRA
f88434: jmp     $863e.w
f88438: move.w  #$7d0,D0
f8843c: movep.w D0,($c,A0)              ; CTU=$07, CTL=$D0  (CTUR:CTLR=0x07D0=2000 -- matchar PLAN.md avsnitt 3, nu mätt live)
f88440: move.b  #$60,($8,A0)            ; ACR = 0x60
f88446: bsr     $f88450
f88448: move.b  #$2b,($a,A0)            ; IMR = 0x2b  (register-relativ, missad av textsökning på "480b")
```

Det här ÄR det "tabellstyrda"-liknande init som uppgiftens punkt 5
efterfrågade — inte en dataTABELL i ROM utan en rak sekvens av
`move.b #imm,(offset,A0)` mot ett delat basregister (`A0=$FFFC4801`,
bekräftat direkt ur en levande register-dump). `movep.w` (peripheral
data move, alternerande byte) skriver CTU och CTL i en enda
instruktion — det förklarar varför oraklet ser exakt två träffar på
`fc480c` och `fc480e` var trots noll litterala referenser.

## 1. De sju räknarna: ordning och instruktionsnummer

`[Verified]`, 30 s, V350.img, `ASR10_DIAG_PANEL_AUTORESPOND=1`:

| PC | Antal | Första instr# | Sista instr# |
|---|---:|---:|---:|
| `$F88400` | 2 | 24 817 195 | 25 037 557 |
| `$F8A048` | 1 | 24 844 336 | 24 844 336 |
| `$F87FCC` | 494 828 | 25 055 157 | 47 322 372 |
| `$F88300` | 0 | — | — |
| `$F8831A` | 0 | — | — |
| `$F88328` | 0 | — | — |
| `$F88352` | 0 | — | — |

Kompletterat med den redan existerande `event=imr_write`-loggen
(ovillkorlig, opåverkad av den här uppgiftens instrumentering) ger
detta den fullständiga, tidsordnade IMR-historien för hela körningen —
bara **åtta** skrivningar totalt, i den här exakta ordningen:

```
1. 00b014        imr 00 -> 00   (RAM-kod, no-op)
2. f88400 (1:a)  imr 00 -> 00   (no-op)
3. f88448 (1:a)  imr 00 -> 2b   (bit0,1,3,5: TxRDYA+RxRDYA+CounterReady+RxRDYB)
4. f8a048        imr 2b -> 09   (SMALNAR AV: bara TxRDYA+CounterReady kvar)
5. 00b07a        imr 09 -> 00   (RAM-kod, maskerar allt igen)
6. 00b014        imr 00 -> 00   (RAM-kod, andra varvet)
7. f88400 (2:a)  imr 00 -> 00   (no-op, andra varvet)
8. f88448 (2:a)  imr 00 -> 2b   (SISTA skrivningen i hela 30 s-körningen)
```

**Den stabila, bestående IMR-nivån för resten av körningen (från
instruktion ~25 miljoner och framåt, dvs praktiskt taget hela
30-sekundersfönstret) är `0x2b` — vilket INKLUDERAR bit 3
(counter/timer ready).** `$F8A048`:s smalare `0x09` är transient
(existerar bara mellan steg 4 och 5, långt innan schemaläggarens
idle-scan ens börjar). `$F87FCC` (maskoppning) körs allra första gången
vid instruktion 25 055 157 — EFTER att IMR redan stabiliserats på
`0x2b` vid steg 8 (instruktion strax under 25 055 157, mätt i
loggordning). **Uppgiftens tolkning "F8A048 körd, F88400 senare ->
timer-IRQ maskeras igen" träffar alltså delvis rätt riktning, men
missar att en TREDJE skrivning (`f88448`, upptäckt här) återställer
bit 3 till aktiv igen innan idle-scanen ens startar.**

## 2. IRQ6-övergångar: riktig ISR mot skugg-ISR

`[Verified]`. Den redan existerande `event=irq6_route`-loggen (skugg-
`isr`/`imr`, alltid `isr=20`/`imr=2b` i den här körningen — dvs
källan som drar linjen är alltid RX ready, aldrig counter/timer, exakt
som föregående uppgift visade). Den här uppgiften lade till en parallell
logg av det RIKTIGA `m_duart`-objektets eget ISR (`m_duart->read(5)`,
sidoeffektfritt) vid samma 160 tillfällen:

```
real_isr=08   159 av 160 gånger
real_isr=00     1 av 160 gånger (allra första, innan räknaren hunnit gå ett varv)
```

**Den RIKTIGA DUART-enhetens interna räknare/timer har alltså redan
löpt ut och satt sin egen ISR-bit 3 i praktiskt taget hela körningen.**
Det är inte en modellbrist i `mc68681_device` — chippet gör exakt vad
det ska.

## 3. Blir ISR & IMR & 0x08 sant efter $F87FCC? Ja.

`[Verified]`. Med den stabila IMR=`0x2b` (bit 3 satt, se avsnitt 1) och
riktig ISR=`0x08` i 159/160 mätningar: `0x08 & 0x2b = 0x08`, nollskilt.
**Villkoret är sant i praktiskt taget hela körningen efter att
`$F87FCC` börjat exekvera.** Uppgiftens egen förgrening 3 ("IMR bit 3
aktiv, ISR bit 3 nej -> jaga ACR/CTUR/CTLR/start-stopp") träffar
alltså INTE här — båda bitarna är satta. Den init-sekvens den
förgreningen efterfrågade hittades och dumpades ändå (avsnitt ovan),
som bekräftande bifynd, inte som huvudspår.

## 4. SR-värdet direkt efter $F87FCC: masken öppnas verkligen, varje gång

`[Verified]`. Loggat vid `$F87FD0` (nästa instruktion efter `$F87FCC`s
`move.w #$2000,SR`), **alla 494 828 gånger**:

```
sr=2000  sr_mask=0   (494 828 av 494 828 -- 100%)
```

Masken öppnas fullständigt, utan undantag, varje gång idle-scanen
passerar den punkten.

## Slutsats: bron mellan den riktiga enheten och IRQ6-linjen saknar counter/timer-vägen

`[Verified]`, sammanfogning av avsnitt 1-4 plus källäsning av
`asr10_boot.cpp` och `mc68681.h`:

1. Den riktiga `m_duart`-enhetens IRQ-utgång (`irq_cb()`/`write_irq`,
   `mc68681.h` rad 113) är **aldrig kopplad** i maskinkonfigurationen —
   `SCN2681(config, m_duart, ...)` saknar en `.irq_handler().set(...)`-
   rad, och en kommentar precis intill bekräftar det uttryckligen:
   "Channel A (MIDI) and channel B (front panel) are not wired to
   anything yet." Det är alltså inte specifikt för counter/timer — HELA
   den riktiga enhetens avbrottslinje är frikopplad från CPU:n.
2. IRQ6 drivs istället av ett handmodellerat substitut,
   `panel_c_update_irq6()`, som beräknar `active` från ett eget
   skuggregister `m_panel_c_isr`. Det skuggregistret får **bara någonsin**
   bit 0x20 (RX ready) satt någonstans i hela filen — ingen kodväg sätter
   någonsin dess bit 3.
3. Följden: även om den riktiga enheten genuint och kontinuerligt vill
   avbryta (bekräftat, avsnitt 2-3) och CPU:ns mask genuint öppnas om
   och om igen (bekräftat, avsnitt 4), **anropas `set_input_line(6,...)`
   aldrig av det skälet** — koden som skulle behöva göra det tittar
   aldrig på den riktiga enhetens tillstånd för just den bit-en.
4. Detta förklarar `$F88300`/`$F8831A`/`$F88328`/`$F88352` = 0 mer
   precist än "SR-masken blockerar": masken blockerar de 160 RX-drivna
   händelserna (som `tick-chain.md` redan visat), men counter/timer-
   händelsen *begärs aldrig ens* vid CPU-linjenivå, oavsett mask.

Detta är varken ett `mc68302int.cpp`-problem (den kretsen finns inte i
det här steget och är inte inblandad — IRQ6 är extern, per
`interrupt-source-map.md`) eller ett ACR/CTUR/CTLR-initieringsproblem
(det initieras korrekt, se ovan). Det är en lucka specifikt i
`asr10_boot.cpp`s egen handmodellerade IRQ6-bro: den bygger
`m_panel_c_isr` för RX-fallet men glömde motsvarande gren för
counter/timer-fallet när den byggdes.

`[Hypothesis]`, inte åtgärdat här (uppgiften förbjuder kompensation):
den enkla, korrekta fixen är sannolikt att antingen (a) binda
`m_duart->irq_cb()` till `m_maincpu`'s IRQ6-linje på riktigt i
`device_add_mconfig`, vilket skulle göra hela `panel_c_update_irq6`-
skuggningen överflödig för BÅDA källorna (RX och counter/timer), eller
(b) om den handmodellerade bron ska leva kvar ett steg till, lägga till
en motsvarande `m_panel_c_isr |= 0x08`-gren driven av den riktiga
enhetens faktiska `irq_pending()`/tick-callback. Väg (a) är den som
`CLAUDE.md`s regel 5 ("använd MAME:s enheter") pekar mot.

## Städning

All instrumentering (`ASR10_DIAG_DUART_IMR_PROBE`: de sju PC-räknarna,
IRQ6-realISR-loggen, SR-efter-$F87FCC-loggen, CTU/CTL/ACR-skriv-loggen)
borttagen i sin helhet efter mätningen. `git diff --stat
src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader. Ingen
kompensation, ingen ny stub byggd.
