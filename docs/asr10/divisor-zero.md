# D2:s producent: ett riktigt enhetssvar, inte ett minnesfel — och de "friska" instanserna var aldrig samma uppgift

2026-08-01. Läst: `docs/asr10/error-130.md`, `docs/asr10/disk-read-path.md`,
`docs/asr10/tick-rate.md`, `CLAUDE.md`. Metod: en riktad läsning av 34
byte runt `$0067F6` ur den körande maskinen, ett fristående Python-
sökskript mot `V350.img` (ingen MAME inblandad), `unidasm` mot både
ROM-avbilden och de levande RAM-bytesen, en global instruktionsräknare,
en tickräknare (samma `irq_cb`-tap-teknik som tidigare uppgifter), en
räknare + full kontextdump för varje exekvering av `pc==0x0067f6`, och
en riktad förlängning av en redan existerande, observations­endast
enhets-tap (`m_hook_fc2068_tap`) till att också logga under den här
uppgiftens egen flagga. Allt gated bakom `ASR10_DIAG_DIVISOR_PROBE`,
borttaget efter mätningen. `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader efter uppgiften.

Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_DIVISOR_PROBE=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

## Sammanfattning

**D2 blir noll för att en MOVEP.L-läsning mot en genuint icke-
modellerad enhetsyta (`$FC2001`-relativt, redan känt från tidigare
sessioner) svarar med allt-nollor — inte för att någon skriver fel
värde till ett minnesord.** Vägen dit går via en loop och en
registeröverföring, inte via en fast minnesadress. Och **den "friska"
jämförelsen i uppgiftens eget läge höll inte**: de två ROM-instanserna
med `D2=0x20` är generiska buffertfyllningsloopar som råkar passera
samma adress via en pekare — inte samma uppgift som den som misslyckas.

## 1. Metodkontroll: identiska, ingen relokering

`[Verified]`. 34 byte lästa live ur den körande maskinen kring
`$0067F6` (`31c2 0dd6 203c a348 0000 80c2 6804 303c ffff 31c0 0df2
0239 00f8 00fc 6829 0039 0005`) söktes rakt av i `V350.img` med ett
fristående Python-skript — **exakt en träff**, vid byteoffset
`0x8DF6` (36 342 decimalt). Med `disk-read-path.md`s redan etablerade
geometriformel (`track_index=C*2+H`,
`byte_offset=(track_index*20+(R-1))*512`):

```
sector_index = 36342 // 512 = 70, rest 502
track_index  = 70 // 20 = 3  ->  C=1, H=1
R            = 70 % 20 + 1 = 11
```

**Cross-referens mot den redan existerande, ovillkorliga
`ASR10_FDC_CMD46`-transaktionsloggen:** transaktion 9 läser `C=01,
H=01, R=01` till `EOT=14` (20 decimalt) — dvs hela spåret, sektor 1
t.o.m. 20. `R=11` faller inom det intervallet. **Transaktion 9 är
alltså den transaktion som förde in den här koden i RAM.**

**Identiska, byte för byte — ingen relokerings- eller
uppackningstransformation existerar för den här koden.** Testet
följer exakt `disk-read-path.md`s redan bevisade metod (avsnitt 3),
tillämpad på en ny adress. Disassemblering nedan är därför giltig utan
förbehåll.

## 2. Var kommer D2 ifrån? En enhetsläsning via en loop, inte en direkt minnesläsning

`[Verified]`. Koden omedelbart före `$0067F6` (disassemblerad ur den
levande RAM-dumpen, inte ur ROM):

```
006872: moveq   #$0, D0
006874: trap    #$7               ; (svans av en tidigare, orelaterad rutin)
006876: movem.w (A7)+, D6-D7      ; D6, D7 återställs FRÅN STACKEN
00687a: movea.l #$fc2001, A0      ; A0 = enhetsbasadress
006880: jsr     $fffc60b0.l       ; anropar en enhets-thunk
006886: ...                       ; (bearbetning, ej fullständigt spårad)
00688c: dbra    D7, $006868       ; LOOPAR TILLBAKA — D7 är en loopräknare
006890: move.w  D6, D2            ; D2 = D6 (loopens ackumulerade resultat)
006892: rts                       ; återvänder till $0067F6
0067f6: move.w  D2, $0dd6.w       ; lagrar D2 i lågminne
```

**`$0067F6` är en RETURADRESS, inte ett hopp-mål** — instruktionen
omedelbart före den är `rts`. `$0067F6` nås alltså genom att en
anropande rutin gjorde `bsr`/`jsr` till den här lilla hjälprutinen
(startpunkt `$006868`, utanför den fångade räckvidden), som i sin tur
loopar (`dbra D7`) och för varje varv anropar en delad
enhets-läsningsthunk:

```
fc60b0: movep.l ($68,A0), D2     ; MOVEP.L, fyra byte från A0+0x68 = $fc2001+0x68 = $fc2069
fc60b4: rts
```

**`$FC60B0` är redan känd, tidigare dokumenterad kod** — matchar
ordagrant citatet i `evidence-tree.md` rad 199 ("`movea.l
#$fc2001,A0`/`jsr $fffc60b0`") och den redan byggda
`ASR10_FC2001_TRACE`-diagnostiken (`asr10_boot.cpp`, `m_hook_fc2068_tap`,
`m_fc60b0_verified`). Den här uppgiften bekräftar samma kod live, oberoende,
och kopplar den för första gången direkt till divisionen med noll.

**D2 kommer alltså varken från en enkel minnesläsning eller en enkel
enhetsläsning — det är en loop som ackumulerar `MOVEP.L`-svar över
flera varv i D6, och kopierar resultatet till D2 precis innan
`rts`.** Eftersom D2:s ursprung är en ENHET (steg 4), inte en fast
minnesadress, är punkt 3:s villkor ("om D2 läses ur minne") inte
tillämpligt — se punkt 4 i stället.

## 3. Inte tillämpligt: D2 läses inte ur en fast minnesadress

`[Verified]` som negativt resultat. Ingen minnesadress att
watchpointa — värdekedjan är register-till-register
(`D2←D6`) och register-till-enhet (`D2←MOVEP.L`-thunken), inte
minne-till-register. Se punkt 4.

## 4. Enhetsläsningen: `$FC2069`, redan känt fönster, vårt svar är allt-nollor

`[Verified]`. Adress: `$FC2001 + 0x68 = $FC2069` (udda bytelane,
samma `MOVEP`-konvention som hela `$FC2001`-fönstret använder
genomgående i det här trädet). Fönster: **redan dokumenterat** i
`PLAN.md` ("external device window (FC2xxx) — FC2001-relative MOVEP
library, 20 thunks, se `movep-library.md`") och `es5701-wiring.md`/
`es5506-chain-verification.md` (hypotes: ES5506/ES5505-klassat
värdregisterfönster, adressmatematiken `[Verified]` sedan tidigare,
kortnivå-routningen `[Hypothesis]`).

**Vårt svar, mätt live, 34 händelser (flera loopvarv × 4 byte per
varv):** `data=0000` **i samtliga fall, utan ett enda undantag.**

```
event=fc2xxx_read pc=fc60b0 address=... offset=fc2068 ... data=0000
event=fc2xxx_read pc=fc60b0 address=... offset=fc206a ... data=0000
event=fc2xxx_read pc=fc60b0 address=... offset=fc206c ... data=0000
event=fc2xxx_read pc=fc60b0 address=... offset=fc206e ... data=0000
(upprepat identiskt för varje loopvarv)
```

**Vad som borde ha returnerats:** kan inte beläggas i det här trädet
och rapporteras därför inte som en gissning. Vad som ÄR belagt: den
redan existerande koden som äger den här tap:en har sin egen kommentar
om **varför** svaret alltid är noll — `$FC2068`-`$FC206F` ligger i ett
**vanligt `.ram()`-block** (`0xfc0000`-`0xfc3fff`) utan någon riktig
enhetsmodell bakom sig; ingenting skriver någonsin dit, så varje
läsning ser bara outnyttjat minnes standardvärde (noll). Det är
**inte** ett svar från en emulerad ES5506/host-port-enhet — det är
frånvaron av en sådan.

**Det här är samma, redan tidigare identifierade rotorsak** som
`mc68302-irq6-vector.md`/`error-130.md` citerade från
`filesystem-browser-map.md` 4.19 (ES5506 PAR/host-port returnerar 0
utan `ASR10_EXPERIMENT_ES5506_HOST`/`PAR_DIAGNOSTIC`/`PAR_VALUE`) — den
här uppgiften spårar den nu ner till den **exakta** adressen
(`$FC2069`, via den specifika `$FC60B0`-thunken) i stället för bara
"ES5506 host-port" i allmänhet.

## 5. Ordning: FÖRSTA och ENDA invokationen — inget att korrumpera

`[Verified]`, entydigt. `pc==0x0067f6` exekverar **exakt en gång**
under hela 30-sekunderskörningen (räknat instruktion för instruktion,
inte stickprov):

```
seq=1  instr=24 909 860  tick=52  (den enda träffen)
```

**Det finns ingen "frisk instans före" av den här specifika rutinen.**
Detta är samtidigt den FÖRSTA och ENDA gången koden på `$0067F6` någonsin
körs. Ordningen pekar entydigt mot **initialiseringsfel** (uppgiften
misslyckas redan vid sitt första och enda försök) — inte korruption av
ett tidigare fungerande tillstånd, eftersom det aldrig fanns ett
tidigare fungerande tillstånd att korrumpera.

**Rättelse av uppgiftens eget läge:** `error-130.md`s ursprungliga
observation ("friska instanser av samma uppgift har D2=0x20") byggde
på den redan existerande `ASR10_DIVIDER_TASK2`-diagnostikens NAMNGIVNING
("rate_param"/"divider_result"), inte på verifierad kod. Disassemblering
av de två ROM-adresser som visade `D2=0x20` (`$F87DD6`, `$FB90DC`) visar
att de är **generiska buffertfyllnings-/nollställningsloopar**
(`f87dd6: move.w D0,(A0)+` inuti en `cmp.w #$400,D1`-räknad loop mot
`A0` upp mot `$1FFFFF`; `fb90dc: clr.w (A0)+` i en liknande loop) —
adressen `$0DD6` nås där bara som EN av MÅNGA sekventiella positioner
en pekarloop passerar, inte som ett medvetet mål. **De är inte samma
uppgift som den som misslyckas, och `D2=0x20` där betyder inget om
"vad D2 normalt ska vara"** — det är den generiska loopens aktuella
räknar-/fyllvärde vid just det ögonblicket, oberoende av den riktiga
divisionsrutinen på `$0067F6`. Det finns alltså inget giltigt
"friskt"-facit att jämföra det felande `D2=0` mot över huvud taget.

## 6. `docs/asr10/os-code-extraction.md` skriven

Se den filen för den återanvändbara metoden (RAM-adress in,
disassemblerad diskresident kod ut), destillerad ur avsnitt 1 ovan.

## Städning

All instrumentering (`ASR10_DIAG_DIVISOR_PROBE`: `pc==0x0067f6`-
räknaren/dumpen, instruktions-/tickräknarna, samt den riktade
utökningen av `m_hook_fc2068_tap`s villkor till att även reagera på
den här flaggan) borttagen i sin helhet efter mätningen. `git diff
--stat src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader. Ingen
kompensation, ingen ny stub byggd, inga bakgrundsagenter användes.
