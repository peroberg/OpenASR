# FDC MSR-väntningarna vid $FB8D1E/$FB8D40/$FB8D78: statisk disassemblering + körd verifiering

2026-07-31. Rent diagnostiskt uppdrag — ingen bestående kodändring.
Läst: `PLAN.md`, `CLAUDE.md`, `docs/asr10/fdc-map.md`. All disassemblering
gjord med `unidasm -arch m68000 -basepc <adr> -skip <byte-offset>` mot
den hi/lo-sammanflätade ROM-avbilden (`asr-65e0-hi-1.5b.bin`/
`asr-648c-lo-1.5b.bin`), inte handdekodning. Körd verifiering med dagens
`./mess asr10booth` mot både `V161.img` och `V350.img`.

**Metodnot om `unidasm`:** `-basepc` sätter INTE automatiskt adressen vid
`-skip`-punkten — den måste anges explicit (`-basepc <mål-adr> -skip
<mål-adr - 0xf80000>`), annars visas fel adress för varje rad trots att
rätt byte faktiskt disassembleras.

## 1. De tre väntningarna, exakt disassemblerade

```
fb8d1e: btst #4,$fffc4001.l      ; MSR bit4 = CB (command busy)
fb8d26: beq  $fb8d3e              ; CB==0 -> rts (klart, ingen väntan kvar)
fb8d28: moveq #4,D3
fb8d2a: bsr  $fb8d6c               ; kort fast fördröjning (4 iterationer)
fb8d2c: subq.l #1,$476.w          ; LONG räknare, satt till 0x13880=80000 vid $FB7BD6
fb8d30: bpl  $fb8d1e               ; räknare >=0 -> loopa
fb8d32: move.b #$d,$49d.w          ; timeout: sätt felflagga
fb8d38: move.b #$20,$4ae.w         ; underkod 0x20
fb8d3e: rts

fb8d40: btst #7,$fffc4001.l       ; MSR bit7 = RQM
fb8d48: beq  $fb8d58               ; RQM==0 -> hoppa direkt till nedräkning
fb8d4a: moveq #4,D3
fb8d4c: bsr  $fb8d6c
fb8d4e: btst #6,$fffc4001.l       ; MSR bit6 = DIO
fb8d56: beq  $fb8d6a               ; RQM=1 OCH DIO=0 -> rts (klart)
fb8d58: subq.l #1,$476.w
fb8d5c: bpl  $fb8d40
fb8d5e: move.b #$d,$49d.w
fb8d64: move.b #$21,$4ae.w         ; underkod 0x21
fb8d6a: rts

fb8d78: movea.w #$4c6,A0            ; A0 = resultatbuffert (lowmem $4c6-)
fb8d7c: clr.b $4e7.w                ; byteräknare = 0
fb8d80: tst.b $49d.w               ; om felflagga redan satt: avbryt tyst
fb8d84: bne  $fb8dcc
fb8d86: subq.l #1,$476.w            ; LOOP TOP
fb8d8a: bpl  $fb8d9a
fb8d8c: move.b #$d,$49d.w
fb8d92: move.b #$22,$4ae.w         ; underkod 0x22
fb8d98: bra  $fb8dcc
fb8d9a: btst #7,$fffc4001.l        ; MSR bit7 = RQM
fb8da2: beq  $fb8d86                ; RQM==0 -> loopa (ingen ny fördröjning)
fb8da4: moveq #4,D3
fb8da6: bsr  $fb8d6c
fb8da8: btst #6,$fffc4001.l        ; MSR bit6 = DIO
fb8db0: beq  $fb8dcc                ; RQM=1 OCH DIO=0 -> KLART (normal end-of-result)
fb8db2: move.b $fffc4003.l,(A0)+   ; läs resultatbyte
fb8db8: addq.b #1,$4e7.w
fb8dbc: cmpi.b #$a,$4e7.w           ; 10 byte-tak
fb8dc2: bne  $fb8d86
fb8dc4: move.b #$23,D3              ; 10 byte utan naturligt slut -> D3=0x23
fb8dc8: bsr  $fb81ae                ; delad felsättare (se nedan)
fb8dcc: rts
```

`[Verified]`. Detta bekräftar exakt uppgiftens egen statiska avkodning
(`$FB8D1E`→bit4/CB, `$FB8D40`→bit7+bit6/RQM+DIO, `$FB8D78`→samma bitar i
en läsloop) och underkoderna 0x20/0x21/0x22 ordagrant. En fjärde,
tidigare inte namngiven underkod hittades på köpet: **0x23**, satt om
resultatfasen läser 10 byte utan att MSR någonsin visar
"RQM=1,DIO=0"-övergången (den riktiga uPD765-familjens resultatfas är
som mest 7 byte — 10 är ett säkerhetstak, inte ett förväntat utfall).

**Delad felsättare `$FB81AE`** (skriver 0x49d/0x4ae i OMVÄND ordning
mot de tre direkta timeout-fallen ovan):

```
fb81ae: moveq #$d,D2
fb81b0: move.b D3,$4ae.w    ; 04ae skrivs FÖRST
fb81b4: move.b D2,$49d.w    ; 049d skrivs SIST (redan instrumenterad i
                             ; koden: "if (pc == 0x00fb81b4) log_fb81b4_path(...)")
```

De tre inline-timeouten (fb8d32/fb8d5e/fb8d8c) skriver i motsatt
ordning (049d först, 04ae sist) — den redan existerande
`ASR10_049D_WRITE`-loggen (körs ovillkorligt vid varje skrivning till
$49d, se `log_lowmem_049d` i `asr10_boot.cpp`) fångar ändå entydigt
VILKEN väntning som misslyckades, eftersom PC för 049d-skrivningen
ensam är 1:1 mot underkoden (fb8d32→0x20, fb8d5e→0x21, fb8d8c→0x22,
fb81b4-vägen→variabel D3). Ingen ny loggning krävdes för att svara på
fråga 1 — den fanns redan.

## 2. Körd verifiering: ingen av de tre väntningarna misslyckas i praktiken

`[Verified]`, två oberoende 150-sekunders körningar (emulerad tid,
`-nothrottle`), båda med `ASR10_DIAG_PANEL_AUTORESPOND=1`:

```
./mess asr10booth -flop1 floppies/asr10booth/V350.img ... -seconds_to_run 150
./mess asr10booth -flop1 floppies/asr10booth/V161.img ASR10_EXPERIMENT_ES5510_HOST=1 ... -seconds_to_run 150
```

Den redan existerande, ovillkorliga `ASR10_049D_WRITE`-loggen (fyller
`field_04ae` = aktuellt 0x4ae-värde, men se ordningsanmärkningen ovan)
visar, över **BÅDA** körningarna tillsammans (95 skrivningar till
$49d.w totalt):

```
50+37  current=00   (felflagga rensad — normal drift)
1+1    current=01
1+1    current=11
1+1    current=fe
1+1    current=ff
0+0    current=0d   <-- ALDRIG skrivet
```

**$049D blir aldrig 0x0d i någon av körningarna.** Ingen av de tre
väntningarna (`$FB8D1E`/`$FB8D40`/`$FB8D78`) tar slut på sin
80 000-varvsbudget i praktiken. (`current=01/11/fe/ff` är $49d
återanvänt som scratch av annan kod, inte relaterat till FDC-felvägen —
matchar `fdc-map.md`s redan dokumenterade lågminnesåteranvändning.)

**Boten stannar vid `"TUNING KBD - HANDS OFF"`** i båda körningarna,
når aldrig `"KEYBOARD TUNED"` inom 150 s (avviker från en tidigare
sessions rapporterade "~114,5 s" med en annan flaggkombination —
`fdc-map.md` avsnitt 3 flaggar redan den skillnaden som obekräftad).
Maskinens avslutssammanfattning (`ASR10_MC68302_ACCESS_TOP`) är
**byte-identisk** mellan en 30 s- och en 150 s-körning med samma
avbild — inget SIB-fönsteraccess sker alls efter en tidig punkt,
konsekvent med den redan dokumenterade slutsatsen i `PLAN.md`
("2026-07-30, mekanismen identifierad") att den faktiska
efterföljande stallningen är en panel-/schemaläggningsfråga, inte en
FDC-väntning.

**FDC:n används bara under disklastningen, inte efteråt.** Direkt
loggbevis (linjenummer i en 20 s-körning, V161): `"LOADING SYSTEM"`
visas vid rad 1 409 275 av 1 411 463 totalt. Alla ~1,02 miljoner
`ASR10FDC`-rader (se avsnitt 3) ligger FÖRE den raden. Efter
`"LOADING SYSTEM"` rör ROM:en aldrig `$FC4001`/`$FC4003` igen inom
testfönstret — bara DUART-kanal A/B och panelschemaläggning. De tre
väntningarna prövas alltså bara under den inledande OS-inläsningen,
inte under `TUNING KBD`-fasen där boten faktiskt fastnar.

## 3. Kommandoväg (AUX kontra FIFO) och MSR-mönster — mätt, inte antaget

`[Verified]`, temporär aktivering av den redan skrivna men
kompileringstidsavstängda `ASR10_LOG_FDC_ACCESS`-flaggan (rad 101,
`false`→`true`, återställd till `false` efter mätningen — `git diff
--stat` bekräftar netto noll rader i `asr10_boot.cpp` efteråt). En 20 s
körning (V161, autorespond) genererade ~1,02 miljoner `ASR10FDC`-rader,
alla före `"LOADING SYSTEM"`.

**Konsekvent tvåvägs-mönster**, exempel ur den sista transaktionen
före `"LOADING SYSTEM"` (kommando 0xf3, SPECIFY-familjen, sedan
kommando 0x0e, "enable motors"):

```
detail=status_bit4_busy_must_clear        pc=fb8d1e addr=fc4001 data=0080
detail=status_bit7_write_ready_must_set   pc=fb8d40 addr=fc4001 data=0080
detail=status_bit6_write_direction_must_clear pc=fb8d4e addr=fc4001 data=0080
detail=upd72069_aux_command               pc=fb8d10 addr=fc4001 rw=W data=0e0e   <- AUX-väg ($FFFC4001)
detail=status_bit7_receive_ready_must_set pc=fb8d9a addr=fc4001 data=00d0
detail=status_bit6_receive_data_present   pc=fb8da8 addr=fc4001 data=00d0
detail=upd72069_fifo_read                 pc=fb8db2 addr=fc4003 rw=R data=0080   <- FIFO-väg ($FFFC4003)
```

`upd72069_aux_command`/`upd72069_fifo`-taggarna (redan i koden, rad
4565/4678) skiljer entydigt AUX-kommandoskrivning ($FFFC4001, `fb8d10`)
från FIFO-databyte ($FFFC4003, `fb8cee`), exakt som `fdc-map.md` redan
dokumenterat.

**Avgörande mätning: ingen av de tre väntningarna loopar mer än en
gång, någonsin, i hela den ~1 miljon-rader långa körningen.** Räknat
konsekutiva upprepningar av samma PC:

```
pc=fb8d1e, fb8d40, fb8d4e, fb8d9a, fb8da8   ->  repeat_count alltid 1
```

(Jämför `pc=fb8aa2`, den separata data-fas-bytepollningsloopen inuti
CMD46/45 — den loopar legitimt tiotusentals gånger per sektor, men det
är en annan loop än de tre under utredning.) Varje gång ROM:en frågar
"är CB=0?"/"är RQM=1,DIO=0?"/"är RQM=1,DIO=1?" är svaret redan ja vid
FÖRSTA kontrollen. $476-räknaren (80000/8000 varv) kommer alltså
aldrig ens till sitt andra varv i en verklig körning — retry-grenen är
i praktiken död kod för alla observerade transaktioner.

## 4. Jämfört med `upd765.cpp` rad 568-601 (`msr_r()`)

`[Verified]`. Bitarna (`upd765.h` rad 76-80): `MSR_DB=0x0f`,
`MSR_CB=0x10` (bit4), `MSR_EXM=0x20` (bit5), `MSR_DIO=0x40` (bit6),
`MSR_RQM=0x80` (bit7) — matchar ROM:ens bittest exakt.

```cpp
switch(main_phase) {
case PHASE_CMD:
    msr |= MSR_RQM;
    if(command_pos) msr |= MSR_CB;
    break;
case PHASE_EXEC:
    msr |= MSR_CB;
    if(spec & SPEC_ND) msr |= MSR_EXM;
    if(internal_drq) {
        msr |= MSR_RQM;
        if(!fifo_write) msr |= MSR_DIO;
    }
    break;
case PHASE_RESULT:
    msr |= MSR_RQM|MSR_DIO|MSR_CB;
    break;
}
```

Mappning mot de tre väntningarna:

| Väntning | Väntar på | Uppfylld i `main_phase` |
|---|---|---|
| `$FB8D1E` (CB=0) | Kontrollern inte upptagen | `PHASE_CMD` med `command_pos==0` |
| `$FB8D40` (RQM=1,DIO=0) | Redo för kommando/parameterbyte | `PHASE_CMD`, eller `PHASE_EXEC` med `internal_drq && fifo_write` |
| `$FB8D78`/`$FB8D9A` (RQM=1,DIO=1 — annars klar) | Resultat-/databyte redo att läsas | `PHASE_EXEC` med `internal_drq && !fifo_write`, eller `PHASE_RESULT` |

Alla tre stämmer exakt med den dokumenterade uPD765-familjeprotokollet
och med hur `upd72069_device` (samma bas­klass) implementerar det.
**Detta är den arkitektoniska förklaringen till avsnitt 2 och 3:s
mätning.** `main_phase` byter tillstånd synkront, inom samma
registeraccess som utlöser övergången — det finns ingen modellerad
fördröjning i MAME:s uPD765-familj mellan att en kommandobyte/databyte
tas emot och att nästa `msr_r()` reflekterar den nya fasen. Därför ser
ROM:ens pollning alltid rätt bitmönster på första försöket: det finns
inget "mellanläge" att fastna i. De tre väntningarna existerar i
ROM:en som en generell säkerhetsmekanism mot en långsammare eller
felaktig kontroller — mot MAME:s nuvarande, odelayade modell är de
strukturellt overksamma.

## Sammanfattning

| Fråga | Svar | Evidens |
|---|---|---|
| 1. Vilken underkod skrivs vid $049D=$0D? | **Ingen — 0x0d skrivs aldrig till $49D i två fulla 150 s-körningar.** Statiskt: 0x20 (fb8d1e/CB), 0x21 (fb8d40/RQM+DIO-skriv), 0x22 (fb8d78/RQM+DIO-läs), plus en fjärde, tidigare onämnd 0x23 (10-byte-tak i fb8d78) | `[Verified]`, disassemblering + två körda 150s-körningar |
| 2. MSR-historik över sista 100 pollningarna | Inget timeout-fönster existerar att visa — men mätt över hela körningen (~1M rader): var och en av de tre väntningarna löser sig på FÖRSTA kontrollen, alltid | `[Verified]`, temporär `ASR10_LOG_FDC_ACCESS=true`-mätning, återställd |
| 3. Vilket kommando föregick? | AUX-väg ($FFFC4001, `fb8d10`) och FIFO-väg ($FFFC4003, `fb8cee`) är två strukturellt skilda kodvägar, båda observerade (t.ex. kommando 0xf3 SPECIFY följt av 0x0e enable-motors) | `[Verified]`, live-loggat |
| 4. `main_phase` vid uppgivande, matchar MSR-bitarna fasen? | Frågan har inget observerat fall (ingen uppgivning sker) — men de tre väntningarnas bitmönster mappar exakt mot `PHASE_CMD`/`PHASE_EXEC`/`PHASE_RESULT` i `upd765.cpp:568-601`, vilket förklarar VARFÖR ingen uppgivning sker: inga mellanlägen finns i modellen | `[Verified]`, källäsning `upd765.cpp` |

**Ingen kompensation byggd.** `ASR10_LOG_FDC_ACCESS` återställd till
`false` (rad 101), `git diff --stat src/mame/ensoniq/asr10_boot.cpp`
visar netto noll rader efter uppgiften. Ingen ny loggning lämnades
kvar i koden — allt som behövdes för avsnitt 1-3 fanns redan
ovillkorligt (`log_lowmem_049d`/`ASR10_049D_WRITE`,
`ASR10_POST_LOADING_FDC`-gaten, `upd72069_aux_command`/
`upd72069_fifo`-detaljtaggarna); avsnitt 3:s MSR-historik krävde en
temporär flip av en redan skriven, avstängd flagga, inte ny kod.
