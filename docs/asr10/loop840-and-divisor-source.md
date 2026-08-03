# $006868-loopen väljer ingen kanal, och DIVU:s divisor kommer inte formellt från $0DD6 — men PAR-kedjan ligger ändå på den kritiska vägen

2026-08-03. Läst: `docs/asr10/par-is-an-adc.md`, `docs/asr10/
divisor-zero.md`, `docs/asr10/error-130.md`, `docs/asr10/
os-code-extraction.md`, `CLAUDE.md`. Ren mätning: ingen enhet
kopplades in permanent, ingen `read_port_cb` bands i standardbygget,
inget värde gissades som "rätt". All instrumentering (ett nytt,
minimalt one-shot-hook `ASR10_DIAG_LOOP840_PROBE`, 16 rader) gated
bakom en egen flagga och borttagen i sin helhet efter mätningen.
Punkt 3 använde uteslutande den redan existerande
`ASR10_EXPERIMENT_PAR_DIAGNOSTIC`/`ASR10_DIAG_PAR_VALUE`-mekanismen
— noll nya rader för den delen. `git diff --stat
src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader efter
uppgiften.

## Svar i korthet

**a) Skriver `$006868`-loopen ett kanalval? NEJ.** Ingen skrivning
till DUART OPR, MC68302 PIO port B eller ES5506 PAGE förekommer i
loopkroppen. Mux-hypotesen i `par-is-an-adc.md` avsnitt 4, **som den
är formulerad** (en direkt skrivning mellan varven), faller. Se
avsnitt 1 för den obesvarade kvarvarande möjligheten (två TRAP-anrop
per varv, ospårade).

**b) Kommer divisorn från `$0DD6`? NEJ, formellt — men hela
dataflödet visar att den ändå är PAR-kedjans värde.** `DIVU.W`:s
källoperand är register-direkt (`D2`), inte en minnesåtergäsning av
`$0DD6`. Men `D2` är overörd mellan lagringen till `$0DD6` och
divisionen, så värdet är identiskt — och dess enda producent är
`$006868`-loopens `MOVEP.L`-ackumulering från `$FC2069` (PAR). PAR
ligger alltså kvar på den kritiska vägen mot `ERROR 130`, bara via
registerkontinuitet, inte via en `$0DD6`-omläsning. Se avsnitt 2.

**Punkt 3, mekanismtest:** en godtycklig, uttryckligen icke-
auktoritativ testkonstant (`0x155`) injicerad via den redan
existerande `ASR10_DIAG_PAR_VALUE`-mekanismen visar att kedjan
`PAR → MOVEP.L → D2` leder korrekt: `D2` blir och förblir noll-skilt
så fort en enda `MOVEP.L`-läsning sker mot ett icke-noll PAR. Se
avsnitt 3 — med en viktig bieffekt (avsnitt 4) som förhindrade att
just den specifika `$0067F6`-instansen nåddes i den här körningen.

---

## 1. `[Verified]` `$006840-$006894` disassemblerat — loopen skriver ingen kanal

Metod: `os-code-extraction.md`. Ett nytt, minimalt one-shot-hook
(`ASR10_DIAG_LOOP840_PROBE`, 16 rader — en flaggparsning i
`machine_reset()`, en medlemsvariabel för engångsspärr, och ett anrop
till den REDAN EXISTERANDE `dump_loaded_code_range()`-hjälparen vid
`pc==0x0067f6`, samma redan bevisade avtryckare som `divisor-zero.md`
använde) dumpade 84 byte levande RAM. Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_LOOP840_PROBE=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

`unidasm -arch m68000 -basepc 0x006840` mot de 84 dumpade byten gav
en **självkonsistent** instruktionskedja hela vägen (varje
instruktions längd landar exakt på nästa instruktions adress, inga
luckor) — och kedjan från `$006872` och framåt är **byte-för-byte
identisk** med `divisor-zero.md`s redan disk-verifierade capture,
vilket bekräftar att disassembleringen är i fas, inte förskjuten:

```
006868: movem.w D6-D7,-(A7)
00686c: move.w  #$4, D0
006870: trap    #$8
006872: moveq   #$0, D0          ; <- redan diskverifierad i divisor-zero.md
006874: trap    #$7              ; <- redan diskverifierad
006876: movem.w (A7)+, D6-D7     ; <- redan diskverifierad
00687a: movea.l #$fc2001, A0     ; <- redan diskverifierad, A0 OFÖRÄNDRAD varje varv
006880: jsr     $fffc60b0.l      ; <- redan diskverifierad, samma displacement (+0x68=PAR) varje varv
006886: asl.w   #6, D2
006888: lsr.w   #3, D2
00688a: add.w   D2, D6
00688c: dbra    D7, $006868      ; <- redan diskverifierad
006890: move.w  D6, D2           ; <- redan diskverifierad
006892: rts                      ; <- redan diskverifierad
```

(`006840-006866` disassemblerar också rent, men `divisor-zero.md`
flaggar redan att `$006872` föregås av "svansen på en annan rutin" —
d.v.s. `006840-006866` hör sannolikt till en helt annan, tidigare
rutin, inte till loopen. Det påverkar inte svaret nedan: loopens EGEN
kropp, oavsett var den nås ifrån, är `$006868-$00688c`.)

**Negativt resultat, som begärt:** ingen `MOVE`/`MOVEP`-instruktion
med mål `$FC480E`/`$FC480F` (DUART OPR), MC68302 PIO port B, eller
ES5506 PAGE (`$FC2079`/`7B`/`7D`/`7F`) förekommer någonstans i
loopkroppen. `A0` sätts en gång (`$fc2001`) och rörs aldrig om;
`$FC60B0`-thunken läses alltid med samma fasta displacement (`+0x68`
= PAR). De åtta varven är **översampling av EN kanal**, inte ett
kanalval mellan åtta. **`par-is-an-adc.md` avsnitt 4:s mux-hypotes,
formulerad som en direkt skrivning mellan varven, faller.**

**Vad som INTE är uteslutet:** loopen anropar `TRAP #8` (med
`D0=4`) och `TRAP #7` (med `D0=0`) en gång per varv, mellan
`movem`-paret. Dessa OS-vektorer är **inte spårade** i den här
uppgiften — om ett av dem indirekt gör en kanalväljande skrivning
(t.ex. om `TRAP #8` är ett generellt "board I/O"-anrop) skulle det
vara en indirekt mux ändå. Det är utanför den här uppgiftens
uttryckliga omfattning (tre namngivna register, ingen trap-
vektorspårning begärd) och rapporteras som en öppen lucka, inte som
uteslutet.

---

## 2. `[Verified]` Divisorn: register-direkt `D2`, inte minnesdirekt `$0DD6` — men samma värde, oförändrat

`error-130.md` avsnitt 3 hade redan disassemblerat den fällande
sekvensen; den här uppgiften verifierar den oberoende igen mot
`divisor-zero.md`s redan diskverifierade 34-bytecapture via
`unidasm`, och läser ut den exakta adresseringsmoden explicit:

```sh
$ unidasm -arch m68000 -basepc 0x0067f6 div0067f6.bin
67f6: 31c2 0dd6            move.w  D2, $dd6.w
67fa: 203c a348 0000       move.l  #$a3480000, D0
6800: 80c2                 divu.w  D2, D0
6802: 6804                 bvc     $6808
6804: 303c ffff            move.w  #$ffff, D0
6808: 31c0 0df2            move.w  D0, $df2.w
```

**Opkoden vid `$006800` är `80C2`.** 68000-kodning för DIVU.W:
`1000 ddd 011 mmm rrr` (bit15-12=DIVU/OR-klass, bit11-9=destination
`Dn`, bit8-6=`011`=DIVU.W-opmode, bit5-3=källans EA-mode, bit2-0=
källans register/mode-fält). `80C2` = `1000 000 011 000 010`:
destination `D0`, källans EA-mode = `000` (**Data Register Direct**),
register = `010` (`D2`). **Ingen extension-word konsumeras** för
källan — nästa ord (`6804`) är omedelbart nästa instruktion
(`BVC`), vilket bara är möjligt om källan är registerdirekt (en
absolut-adress-källa som `$0dd6.w` skulle kräva en egen
extension-word, exakt som grannraderna `move.w D2,$dd6.w` och
`move.w D0,$df2.w` visar i disassemblerings-syntaxen — notera att
`unidasm` själv skriver ut adressoperander med `$adr.w`-syntax men
registeroperander utan, vilket är den läsbara bekräftelsen på samma
sak).

**Formellt svar: NEJ — divisorn läses inte från `$0DD6` vid
`divu`-instruktionen.** Det är `D2`-registret, inte ett omladdat
minnesord.

**Men hela dataflödet:** mellan lagringen (`$0067F6:
move.w D2,$0dd6.w`) och divisionen (`$006800: divu.w D2,D0`) finns
**exakt en instruktion** (`$0067FA: move.l #$a3480000,D0`), som
uteslutande rör `D0`. **`D2` är alltså bevisligen oförändrat mellan
lagringen och divisionen** — samma värde som skrevs till `$0DD6` är
det värde som divideras med, bara läst direkt ur registret i stället
för omladdat från minnet. Och det värdets enda producent (avsnitt 1,
samt `divisor-zero.md` avsnitt 2, oförändrat av den här uppgiften)
är `$006890: move.w D6,D2` — utfallet av `$006868`-loopens
`MOVEP.L`/PAR-ackumulering.

**Slutsats:** svaret på "kommer divisorn från $0DD6" är nej i strikt
adresseringsmodemening, men **PAR-spåret är INTE en återvändsgränd**
— det ligger fortsatt obrutet på den kritiska vägen till `ERROR 130`,
via registerkontinuitet snarare än en minnesomläsning. Det här är en
precisering av `error-130.md`/`divisor-zero.md`s formulering, inte en
motsägelse av deras slutsats.

---

## 3. `[Verified]` Mekanismtest: PAR → MOVEP.L → D2 leder, med ett godtyckligt testvärde

Eftersom avsnitt 2 visar att PAR-kedjan är den enda producenten av
divisorns värde (om än via registret, inte `$0DD6`), kördes punkt 3:
den redan existerande `ASR10_EXPERIMENT_PAR_DIAGNOSTIC`/
`ASR10_DIAG_PAR_VALUE`-mekanismen (ingen ny kod), avläst på `$FC206D`/
`$FC206F` (inte `$FC2069`, per `par-is-an-adc.md` avsnitt 1:s redan
bevisade bytelane-tabell). Testvärdet **`0x155`** valdes godtyckligt
— ett lättigenkänt bitmönster (`01 0101 0101`), **inget
hårdvarupåstående**, uttryckligen dokumenterat som sådant här.

```sh
ASR10_EXPERIMENT_ES5506_HOST=1 ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1 \
  ASR10_DIAG_PAR_VALUE=0x155 ASR10_DIAG_PANEL_AUTORESPOND=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 200 -log
```

Den redan existerande `ASR10_ES5506_HOST event=host_read`-
diagnostiken (oförändrad, samma som i `es5506-hostport.md`) visar,
för en av de faktiska `$FC60B0`-anroparna (samma schemaläggarplats
`a2=0x00244e` som `error-130.md` identifierade för den felande
uppgiften; `sp=000300`, samma låga stackadress som tidigare
capturer):

```
access_count=6013  address=fc2069  data=00  d2=00000007  (D2 = gammalt, oförändrat värde)
access_count=6014  address=fc206b  data=00  d2=00000007
access_count=6015  address=fc206d  data=01  d2=00000007
access_count=6016  address=fc206f  data=55  d2=00000007
access_count=6017  address=fc2069  data=00  d2=00000aa8  <- D2 HAR ÄNDRATS
access_count=6018  address=fc206b  data=00  d2=00000aa8
access_count=6019  address=fc206d  data=01  d2=00000aa8
access_count=6020  address=fc206f  data=55  d2=00000aa8
... (identiskt, stabilt, för ytterligare 20+ observerade MOVEP.L-anrop)
```

**Byten på `$FC206D`/`$FC206F` (`01`, `55`) matchar exakt de två
lägsta byten av det injicerade `0x0155`**, precis i de bytelane-
positioner `par-is-an-adc.md` avsnitt 1 förutspådde. **`D2` går från
sitt gamla värde (`0x00000007`) till ett nytt, ANNAT, noll-skilt
värde (`0x00000aa8`) omedelbart efter att den första `MOVEP.L`-
läsningen med det injicerade värdet fullbordas, och förblir stabilt
noll-skilt genom alla efterföljande anrop** (konstant `read_port_cb`
→ samma bytes varje gång → samma `D2` varje gång, som väntat).
Jämfört med baslinjen (`es5506-hostport.md` avsnitt 4b), där `D2` var
**exakt `0x00000000` i varenda observerad instans utan undantag**,
är kontrasten entydig: **kedjan leder.** En icke-noll PAR-läsning
propagerar mätbart och reproducerbart till ett noll-skilt `D2` via
exakt samma `MOVEP.L`-mekanism.

**Vad detta INTE visar:** att `D2=0x00000aa8` skulle vara "rätt".
`0xAA8` är resultatet av godtyckliga `0x155` plus den anropande
kodens egen (ospårade i den här uppgiften) efterbehandling av den
råa `MOVEP.L`-summan — ingen gissning görs om vad ett korrekt PAR-
värde är eller borde ge.

---

## 4. `[Verified]` Bieffekt: konstant, globalt injicerad PAR hindrade körningen från att nå `$0067F6` specifikt

`ASR10_DIAG_PAR_VALUE` är en **global konstant** — den gäller för
VARJE anrop av `read_port_cb()` under hela körningen, inte bara
`$006868`-loopens. Med `0x155` injicerat globalt fastnade körningen
permanent i `LOADING SYSTEM`-fasen (bekräftat vid både 30 och 200
sekunders `-seconds_to_run`; loggen växte till **372 miljoner
rader** utan att någonsin nå `ERROR 130` eller ens lämna
`LOADING SYSTEM`) — sannolikt för att en TIDIGARE, orelaterad
PAR-konsument (samma tidiga `PAGE`/`PAR`-svepningsrutin vid
`pc=f8cf1e`/`f8cf3e` som redan observerats i `es5506-hostport.md`)
förväntar sig ett annat värde eller mönster för att avsluta sin egen
sväp/villkorstest, och med en konstant, orörlig `0x155` aldrig gör
det.

**Den specifika `pc==0x0067f6`-instansen nåddes alltså aldrig i den
här körningen.** Mätningen i avsnitt 3 kommer från en TIDIGARE
`$FC60B0`-anropare (samma schemaläggarplats, en annan
återvändoadress) — inte den exakta divisionsrutinen. Det är en
begränsning, rapporterad rakt av: att verifiera mekanismen vid EXAKT
`$0067F6` skulle kräva antingen ett tidsvillkorat (inte konstant)
injektionsvärde, eller att lösa `LOADING SYSTEM`-svepningsrutinens
eget villkor först — båda utanför den här uppgiftens omfattning
("Detta testar bara att kedjan leder, inte vad rätt värde är").
**Loggfilen från den 200-sekunders körningen (28 GB) raderades efter
att relevanta rader extraherats**; ingen del av den bevarades.

---

## Städning

`ASR10_DIAG_LOOP840_PROBE` (16 rader: en flaggparsning, en
engångsspärr-medlemsvariabel, ett hook-anrop till den redan
existerande `dump_loaded_code_range()`) borttagen i sin helhet.
`ASR10_EXPERIMENT_PAR_DIAGNOSTIC`/`ASR10_DIAG_PAR_VALUE`/
`ASR10_EXPERIMENT_ES5506_HOST` användes uteslutande via redan
existerande, redan committade flaggor — noll ny kod för punkt 3.
`git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar netto noll
rader efter uppgiften. Binären ombyggd från den reverterade källan.
Ingen enhet kopplades in permanent, ingen `read_port_cb` bands i
standardbygget, inget PAR-värde föreslås som korrekt.
