# DUART OPR: kanalvalstabellen existerar och stämmer exakt — men körs bara en gång, till index 0, och rör aldrig ERROR 130-fönstret

2026-08-03. Läst: `docs/asr10/loop840-and-divisor-source.md`,
`docs/asr10/par-is-an-adc.md`, `CLAUDE.md`. **Föregående dokument
`docs/asr10/duart-opr-static.md`, som uppgiften hänvisade till som
redan skriven, existerar INTE i det här trädet** — varken committad
eller ospårad. Adresserna i uppgiftens rättelse ($FFFC481B/1D/1F,
de fyra PC:na) verifierades i stället direkt mot ROM-avbilden i den
här uppgiften (se avsnitt 1) innan någon körning gjordes; de visade
sig stämma exakt. Metod därefter: statisk disassemblering
(`unidasm` mot den rekonstruerade `asr10_rom.bin`, ingen körning) för
punkt 1/2/4:s förarbete, sedan en riktad, observationsendast
utökning av det redan existerande `outport_cb()`-tapet
(`ASR10_DIAG_DUART_OPR_PROBE`, 82 rader: en `outport_cb()`-bindning,
en kompletterande skrivtap på MC68302:s PIO-block, plus tillägg till
en redan existerande korrelationslogg) för punkt 1/2/3:s dynamiska
del. Allt gated bakom `ASR10_DIAG_DUART_OPR_PROBE`, borttaget i sin
helhet efter mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader efter uppgiften.

Körkommando (den körning siffrorna nedan kommer från):

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_DUART_OPR_PROBE=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

En andra körning (samma flaggor + `ASR10_EXPERIMENT_ES5506_HOST=1`,
utan någon PAR-konstant) gjordes för att slutgiltigt reda ut punkt 3
— se avsnitt 5, som **rättar ett fel** i `loop840-and-divisor-
source.md` avsnitt 4.

**Miljöanteckning, inte en kodfråga:** två körningar i den här
uppgiften avbröts av `SIGKILL (Code Signature Invalid)` /
`Taskgated Invalid Signature` — macOS underkände den nybyggda
binärens ad-hoc-signatur. Löst med `codesign -s - --force ./mess
./mame`. Orelaterat till instrumenteringen; nämns bara för att spara
nästa session tid om det återkommer.

## Sammanfattning

**Tabellen finns, är läst och skriven exakt som förutspått** — index
0 ger `$D1`, precis den första av de fyra förutspådda tabellvärdena.
Men **`$0170` (tabellindexet) observerades ALDRIG anta något annat
värde än `0` under en hel 30-sekundersstart till `ERROR 130`.** OPR
ändras totalt **sex gånger** under hela körningen, inte upprepade
gånger i en kanalcykel: en fast konstant vid boot-start, en fast
konstant vid t≈15,007s, och tabellvärdet (index 0) omedelbart
därefter — sedan **aldrig igen**. `$006868`-loopens samtliga åtta
PAR-läsningar sker EFTER att OPR redan slutgiltigt lagt sig vid
`$D1`, och ser exakt samma värde i alla åtta varv.

**Detta är, mätt i den här specifika bootscenariot, muxhypotesen
"tekniskt levande men praktiskt död"**: mekanismen (en riktig,
4-post-kapabel indexerad tabell som slår igenom till OPR) existerar
och fungerar bevisligen, men den EXERCERAS aldrig till mer än sitt
första index under den här körningen — så OPR bidrar ingenting till
divisionen-med-noll-kedjan (`error-130.md`/`divisor-zero.md`/
`loop840-and-divisor-source.md`) utöver att vara en STATISK,
redan-avgjord bakgrund vid den tidpunkt PAR faktiskt läses.

## 1. `[Verified]` De fyra kända skrivställena: tre exekverar exakt en gång var, ett exekverar aldrig

Statisk disassemblering (`unidasm -arch m68000 -basepc <adr> -skip
<adr-0xf80000> asr10_rom.bin`) bekräftade uppgiftens korrigerade
adresser exakt:

```
f87ece: moveq   #-$1, D0
f87ed0: move.b  D0, $fffc481d.l   ; SET  = $FF
f87ed6: not.b   D0
f87ed8: move.b  D0, $fffc481f.l   ; RESET= $00   -> rå OPR = $FF

f8e1da: movea.l #$fff8e252, A0    ; tabellbas
f8e1e0: moveq   #$0, D0
f8e1e2: move.b  $170.w, D0        ; D0 = index (BYTE, inte word -- ".w" är bara adresseringsmodens storlek)
f8e1e6: move.b  (A0,D0.w), D0     ; D0 = tabell[index]
f8e1ea: not.b   D0
f8e1ec: move.b  D0, $fffc481d.l   ; SET  = NOT(tabell[index])
f8e1f2: not.b   D0
f8e1f4: move.b  D0, $fffc481f.l   ; RESET= tabell[index]        -> rå OPR = NOT(tabell[index])
f8e1fa: bclr    #$7, $fc6823.l    ; MC68302 PIO, ovillkorligt varje gång

f8e240: move.b  #$9f, $fffc481d.l ; SET  = $9F
f8e248: move.b  #$60, $fffc481f.l ; RESET= $60   -> rå OPR = $9F (disjunkta masker: $9F|$60=$FF)

f97bd2: move.b  #$d9, $fffc481d.l ; SET  = $D9
f97bda: move.b  #$26, $fffc481f.l ; RESET= $26   -> rå OPR = $D9 (disjunkta masker: $D9|$26=$FF)
f97be2: bset    #$7, $fc6823.l    ; MC68302 PIO
```

Tabellen vid `$F8E252` (rå bytes, dumpade direkt ur ROM-avbilden):
`2e 57 36 00 20 03 c0 bc ...` — `NOT()` av de fyra första ger
`D1 A8 C9 FF`, **exakt de fyra värden uppgiften förutspådde.**

**Dynamiskt, via det ovillkorliga `outport_cb()`-tapet (fångar VARJE
verklig OPR-ändring, inte ett stickprov):**

| Ordning | PC | Rå OPR (härledd) | Fysisk nivå (loggat `opr_level`) | `$0170` | Cykel | Ungefärlig tid |
|---|---|---|---|---|---|---|
| 1 (reset) | — | $00 | $FF | — | 0 | t=0 |
| 2 | `$F8E240` | $9F | $60 | 00 | 708 | t≈0,00004s |
| 3 | `$F8E248` | $9F (oförändrad) | $60 | 00 | 728 | t≈0,00005s |
| 4 | `$F87ED0` | $FF | $00 | 00 | 240 182 146 | t≈15,011s |
| 5 | `$F87ED8` | $FF (oförändrad) | $00 | 00 | 240 182 166 | t≈15,011s |
| 6 | `$F8E1EC` | $FF (oförändrad, SET|$D1 mot redan-$FF) | $00 | 00 | 240 742 266 | t≈15,048s |
| 7 | `$F8E1F4` | **$D1** | $2E | 00 | 240 742 286 | t≈15,048s |

En redan existerande, oberoende diagnostik (`ASR10TRACE`,
ovillkorlig, inte skriven i den här uppgiften) bekräftar rad 6/7
byte-för-byte: `pc=f8e1ec addr=fc481d rw=W data=d1d1` (SET-argumentet
var bokstavligen `$D1` — `NOT($D1)=$2E`, vilket är exakt vad koden
skriver eftersom den skriver `NOT(tabell[index])` till SET-registret,
inte tabellvärdet direkt) och `pc=f8e1f4 addr=fc481f rw=W
data=2e2e` (RESET-argumentet var `$2E` = `tabell[0]`, exakt). Två
oberoende loggmekanismer, samma siffror.

**`$F97BD2` (det fjärde stället) skrev ALDRIG till OPR under hela
körningen** — `outport_cb()`-tapet, som fångar 100% av verkliga
OPR-ändringar (inte en bevakningslista över specifika PC:n), visar
noll händelser med det stället som källa. Detta är ett direkt,
ovillkorligt negativt resultat, inte en avsaknad av bevakning.

**`$0170` (tabellindexet) var `00` vid samtliga observationstillfällen
(rad 6/7 ovan) — aldrig något annat värde under hela 30-sekunders-
körningen.** Tabellrutinen (`$F8E1DA`) exekverar **exakt en gång**.

## 2. `[Verified med förbehåll]` MC68302 PIO: `bclr #7` exekverar (indirekt bevisat), men tapet för att mäta VÄRDET är trasigt

`f8e1fa: bclr #$7,$fc6823.l` ligger direkt efter den redan bekräftat
exekverande OPR-tabellskrivningen (rad 6/7 ovan, samma basic block,
ovillkorlig kodväg) — den måste alltså också ha exekverat exakt en
gång, vid samma tillfälle. Statisk sökning genom hela ROM:et hittade
totalt fem bit-instruktioner mot `$FC6823`: `bchg #3` (`$F8B94A`,
oöverspårad exekvering), `bclr #7` (`$F8E1FA`, bekräftat
exekverande), `bclr #4`/`bset #4` (`$F977B0`/`$F977C6`, oöverspårade),
`bset #7` (`$F97BE2`, del av det ALDRIG exekverande fjärde
OPR-stället — se avsnitt 1 — så den här specifika instruktionen körs
inte heller).

**Den nya skrivtapen på `$FC6820-$FC682B` gav noll träffar under hela
körningen — ett verktygsfel, inte ett negativt resultat.** MC68302:s
egna interna 4KB-fönster (`$FC6000-$FC6FFF`) installeras dynamiskt
när ROM:et programmerar BAR (redan dokumenterat i `mem_map()`s egen
kommentar: "installed dynamically on BAR write... shadows its own
slice of it"). Den dynamiska ominstallationen sker mycket tidigt
(före `$F8E240`s skrivning vid cykel 708, sannolikt) och **tar
bort/ersätter en tidigare installerad passthrough-tap i samma
intervall** — ett känt MAME-beteende när en enhet installerar sitt
eget adressutrymme ovanpå en redan-installerad tap. Tapet observerade
alltså bara ett kort fönster INNAN BAR programmerades, inte
händelsen vid `$F8E1FA` (som sker vid t≈15s, långt efter BAR).
**Vad `bclr #7` faktiskt sätter/nollställer i det verkliga PIO-
registret kunde alltså INTE mätas den här omgången** — det kräver
antingen ett PC-riktat hook (läsa `$FC6823` direkt via
`read_program_byte` när `pc==0x00f8e1fa` körs, snarare än en
adressrymds-tap) eller att installera tapet EFTER BAR-skrivningen,
ingetdera gjort här.

**Vad som ÄR säkert:** en enda bit (bit 7) rensas eller sätts, aldrig
ett flervärt kanalval i sig själv. Bit 7 kan vara en
"mux-strobe"/"latch"-signal snarare än en kanaladress — konsekvent
med att OPR (inte PIO) bär den 3-bitars kanalinformationen tabellen
kodar, medan PIO-biten möjligen bara talar om NÄR den ska läsas.
`[Hypothesis]` — inte verifierat här.

## 3-5. `[Verified]` Korrelation, och en rättelse av `loop840-and-divisor-source.md`

**Rättelse:** `loop840-and-divisor-source.md` avsnitt 4 påstod att
det fanns en TIDIGARE, separat `$FC60B0`-PAR-anropare (samma
schemaläggarplats `a2=0x00244e`, en annan återvändoadress) som
exekverade FÖRE `LOADING SYSTEM`, och att den specifika `$0067F6`-
instansen aldrig nåddes i den PAR-konstant-injicerande körningen. En
ny, ostörd körning (samma flaggor, INGEN PAR-konstant injicerad)
visar att detta var en feltolkning: **`access_count=6013` (samma
räknarvärde som förra uppgiftens rapport citerade) inträffar på
EXAKT samma ställe i en ostörd körning** — direkt efter att OPR
lagt sig vid `$D1` (t≈15,048s), inte tidigare. Den tidigare
körningens `ASR10PANEL`-text fastnade vid `"LOADING SYSTEM"` (aldrig
`ERROR 130`), men CPU:n/schemaläggaren fortsatte tydligen köra i
bakgrunden ändå — panelens textuppdatering, inte schemaläggaren
själv, var vad som fastnade av den injicerade konstanten. **Det
finns alltså ingen separat "tidigare PAR-konsument" i en ostörd
körning — bara EN, den redan kända `$006868`-loopen, vid t≈15,048s.**

**Punkt 3:s faktiska svar, med den rättelsen:** eftersom det bara
finns EN konsument, är frågan "samma eller olika OPR-värde vid de två
konsumenterna" **inte tillämplig** — men i en STARKARE mening än
uppgiftens egen "fullgott resultat om samma värde": det finns bara
en enda mätpunkt att jämföra med sig själv. Samtliga 32 loggade
`fc2xxx_correlate`-händelser (åtta varv × fyra MOVEP-byte) under
`$006868`-loopen visar identiskt `opr_level=2e` (rå OPR `$D1`),
`index_0170=00`, hela vägen igenom — helt konsekvent med att OPR
redan är permanent stilla vid den tidpunkten.

## Slutsats för muxhypotesen

Tabellen (`$F8E1DA`/`$F8E252`) är en riktig, fungerande, fyrvägs
kanalvalsmekanism — statiskt bevisad och dynamiskt bekräftad
byte-för-byte. Men **den exerceras bara en gång, till index 0, lång
tid innan `$006868`-loopen någonsin läser PAR, och rör sig sedan
aldrig igen genom hela körningen till `ERROR 130`.** OPR:s slutgiltiga,
permanenta värde (`$D1`) föregår PAR-läsningarna med god marginal och
kan därför inte förklara VARFÖR PAR läser noll — det kan bara
fastställa VILKEN fysisk ingång (om någon) som var vald när den
gjorde det. Vilken av de fyra tabellindexen (motsvarande vilken
fysisk kanal) som faktiskt är "kanal 0" är inte känt och gissas inte
här.

## Städning

`ASR10_DIAG_DUART_OPR_PROBE` (82 rader: `outport_cb()`-bindning,
loggfunktion, PIO-skrivtap, tillägg till den redan existerande
`fc2xxx_correlate`-loggen) borttagen i sin helhet. `git diff --stat
src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader. Ingen
enhet kopplades in permanent (`ASR10_EXPERIMENT_ES5506_HOST` användes
bara för den andra, rent observerande körningen i avsnitt 3-5, för
att komma åt den redan existerande PAR-läsningstappen -- ingen ny
kod för det), ingen analogmodell byggd, inget kanalvärde gissat.

## Konsolidering efter läsning mot aktuell kod

2026-08-03, samma arbetsläge. Det här avsnittet lägger inte till ny
modell och bygger inte någon ADC. Syftet är bara att låsa vad OPR/PAR-
utredningen faktiskt säger, med evidensstatus.

### `[Verified]` Statisk OPR/PADAT-kod

ROM-filerna i `roms/asr10booth/` interleavades enligt `ROM_LOAD16_BYTE`
och disassemblerades med repoets `./unidasm`. De fyra OPR-ställena och
tabellen matchar avsnitt 1:

* `$F87ED0/$F87ED8`: SET `$FF`, RESET `$00` -> intern OPR-latch `$FF`.
* `$F8E240/$F8E248`: SET `$9F`, RESET `$60` -> intern OPR-latch `$9F`.
* `$F8E1DA-$F8E1FA`: läser byte `$0170`, tabellindexerar `$F8E252`,
  skriver `NOT(tabell[index])` till SET och `tabell[index]` till RESET,
  följt av `bclr #7,$FC6823`.
* `$F97BD2/$F97BDA/$F97BE2`: SET `$D9`, RESET `$26`, följt av
  `bset #7,$FC6823`.

Tabellens första bytes är `2e 57 36 00 ...`; `NOT()` av dem är
`d1 a8 c9 ff`. Dokumentets dynamiska observation, index `$0170=00`,
ger därför intern OPR-latch `$D1`.

`$FC6823` är låg byte av `PADAT` (`$FC6822`), inte Port B. Instruktionen
vid `$F8E1FA` rensar alltså bit 7 i Port A-data-latchens låga byte.

### `[Verified]` Tillstånd vid den observerade PAR-loopen

I den dokumenterade V350-körningen är sista observerade OPR-ändring före
`$006868`-loopen tabellrutinen vid `$F8E1F4`. Därmed gäller:

* intern DUART OPR-latch: `$D1`;
* fysiskt DUART-utvärde enligt `mc68681_device` (`write_outport(OPR ^ 0xff)`):
  `$2E`;
* tabellindex `$0170`: `$00`;
* senaste OPR-sättare: `$F8E1DA-$F8E1F4`;
* senaste statiskt bevisade PIO-sättare i samma block: `$F8E1FA`,
  `bclr #7,$FC6823` (Port A `PADAT` low byte).

Port B-läget från MC68302-initsekvensen är separat: `PBCNT=0x0080`,
`PBDDR=0xF097`, `PBDAT=0x0007` enligt `docs/mc68302/observed-access-coverage.md`.
Det finns ingen ny dynamisk mätning i den här filen som visar att
Port B ändras kring PAR-loopen.

### `[Verified]` Konsumenterna i den ostörda vägen

Den här filens rättelse av `loop840-and-divisor-source.md` står kvar:
i den ostörda körningen finns bara den kända `$006868`-loopen som
PAR-konsument på ERROR-130-vägen. Frågan om "samma eller olika
OPR-värde för flera konsumenter" är därför inte tillämplig för den
observerade bootvägen; alla åtta PAR-varv ser samma OPR-latch `$D1`
och fysiska OPR-nivå `$2E`.

### `[Likely]` Vad bootvägen stödjer

Den observerade vägen stödjer starkast "mux vald långt tidigare" eller
"fast vald PAR-ingång vid lästillfället": OPR-tabellen är en riktig
kanalvalsmekanism, men den körs bara en gång till index 0 och ligger
stilla innan PAR läses. Detta kan fastställa vilket statiskt läge som
gällde; det förklarar inte varför PAR-värdet blir noll.

### `[Hypothesis]` Fortfarande öppet

OPR plus MC68302 PIO kan fortfarande vara en kombination av mode/latch/
strobe på kortnivå, men den observerade ERROR-130-vägen bevisar inte
signalidentiteten. Särskilt är `$FC6823`-skrivningen bara statiskt
korrelerad med OPR-tabellblocket här; den dynamiska skrivtapen mot
MC68302-fönstret var ogiltig eftersom BAR-installationen ersatte tapet.
Ingen `read_port_cb` ska bindas och ingen PAR-konstant ska injiceras
förrän den fysiska signalen eller firmwarekravet är bättre belagt.
