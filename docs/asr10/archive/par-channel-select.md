# Kanalvalet är MC68302 PIO port B bit 2:0 — och firmware anger själv vilket värde PAR ska returnera

2026-08-03. Metod: statisk disassemblering av `V350.img` (fristående
Python, ingen MAME), plus `par-loop-state-probe.md`s live-mätning som
kontroll. Noll rader kod ändrade, ingen körning gjord.

`divisor-zero.md` bevisade att den diskresidenta koden ligger byte-
identiskt i `V350.img` med förskjutningen `file_offset = RAM + 0x2600`
(ankare: `$0067F6` -> `0x8DF6`). Hela den felande rutinen kan därför
läsas utan emulator. Det gjordes, och den avgör tre öppna frågor på en
gång.

## 1. `[Verified]` Rutinen i sin helhet, `$0067EC-$006892`

```
0067EC  0039 0007 00FC6829   ori.b   #$07,($00FC6829).l   ; KANAL 7
0067F4  616E                 bsr.s   $006864              ; mät
0067F6  31C2 0DD6            move.w  D2,($0DD6).w
0067FA  203C A3480000        move.l  #$A3480000,D0
006800  80C2                 divu.w  D2,D0                ; <-- ERROR 130
006802  6804                 bvc.s   $006808
006804  303C FFFF            move.w  #$FFFF,D0
006808  31C0 0DF2            move.w  D0,($0DF2).w         ; skalfaktor

00680C  0239 00F8 00FC6829   andi.b  #$F8,($00FC6829).l   ; nolla bit 2:0
006814  0039 0005 00FC6829   ori.b   #$05,($00FC6829).l   ; KANAL 5
00681C  6146                 bsr.s   $006864              ; mät
00681E  347C 0DC2            movea.w #$0DC2,A2            ; instansblock
006822  157C 000A 000D       move.b  #$0A,($0D,A2)
006828  3542 0006            move.w  D2,($06,A2)          ; SÅ filtertillståndet
00682C  267C FFF8DB12        movea.l #$FFF8DB12,A3
006832  4EB9 FFF8DC2E        jsr     $FFF8DC2E
006838  4E75                 rts

00683A  0239 00F8 00FC6829   andi.b  #$F8,($00FC6829).l
006842  0039 0000 00FC6829   ori.b   #$00,($00FC6829).l   ; KANAL 0
00684A  6118                 bsr.s   $006864              ; mät
00684C  C4F8 0DF2            mulu.w  ($0DF2).w,D2         ; * skalfaktorn
006850  4842                 swap    D2                   ; >> 16
006852  D47C 0528            add.w   #$0528,D2
006856  31C2 0DDE            move.w  D2,($0DDE).w         ; centrum + $528
00685A  947C 0A50            sub.w   #$0A50,D2
00685E  31C2 0DE0            move.w  D2,($0DE0).w         ; centrum - $528
006862  4E75                 rts

006864  7E07                 moveq   #7,D7                ; 8 varv
006866  7C00                 moveq   #0,D6
006868  48A7 0300            movem.w D6-D7,-(A7)
00686C  303C 0004            move.w  #$0004,D0
006870  4E48                 trap    #8
006872  7000                 moveq   #0,D0
006874  4E47                 trap    #7
006876  4C9F 00C0            movem.w (A7)+,D6-D7
00687A  207C 00FC2001        movea.l #$00FC2001,A0
006880  4EB9 FFFC60B0        jsr     $FFFC60B0            ; movep.l ($68,A0),D2 = PAR
006886  ED42                 asl.w   #6,D2
006888  E64A                 lsr.w   #3,D2
00688A  DC42                 add.w   D2,D6
00688C  51CF FFDA            dbra    D7,$006868
006890  3406                 move.w  D6,D2
006892  4E75                 rts
```

## 2. `[Verified]` Kanalvalet: `$FC6829` bit 2:0 — MC68302 PIO, inte DUART OPR

Tre `ori.b`/`andi.b`-par mot `($00FC6829).l` sätter bit 2:0 till **7, 5
och 0** omedelbart före var sin `bsr $006864`. `$FC6829` är lågbyten av
PBDAT i MC68302:s interna registerblock (bas `$FC6000`, verifierad via
GIMR på `$FC6812`).

**Muxhypotesen i `par-is-an-adc.md` avsnitt 4 var alltså riktig i sak men
fel i mekanism.** Kanalvalet ligger inte i DUART:ens utgångsport (som i
`esq5505.cpp`) utan i 68302:ns PIO port B. Kortets `MC74HC4051` (U55,
8:1-analogmux) styrs av PB2:0.

`loop840-and-divisor-source.md`s negativa resultat ("loopen skriver inget
kanalval") står kvar och var korrekt: **valet sker utanför loopen, en gång
per mätning.** Loopen är rent 8x översampling av den redan valda kanalen.

### Detta förklarar `par-loop-state-probe.md` exakt

Den mätningen rapporterade `PBDAT` som `$0017`/`$001F` med **bit 2:0
konstant `$7`**, och drog slutsatsen "fast vald PAR-ingång". Rätt
observation, men orsaken är att körningen **aldrig tar sig förbi den
första mätningen**: `ori.b #$07` -> mät kanal 7 -> `divu` -> trap.
Kanal 5 och kanal 0 exekveras aldrig. Att bit 2:0 står stilla på 7 är
alltså ett symptom på ERROR 130, inte en egenskap hos hårdvaran.

DUART OPR (`$D1` internt / `$2E` på stiften) stämmer exakt med den
statiska förutsägelsen i `duart-opr-static.md` (tabellstyrningen på
`$F8E1DA`, index `($0170).w = 0`, tabellpost `$2E` på `$F8E252`,
OPR = `~$2E = $D1`). OPR har alltså **ingenting** med PAR-kanalvalet att
göra. Den frågan är stängd åt båda hållen.

## 3. `[Verified]` Vad de tre kanalerna används till

**Kanal 7 — referensmätning.** Resultatet blir divisor i
`$A3480000 / mätning`, och kvoten lagras på `$0DF2`. Kvoten används
sedan som **multiplikator**: `mulu.w ($0DF2).w,D2` / `swap D2`, alltså
`(värde × faktor) >> 16` — en 0.16 fixpunktsfaktor. Kanal 7 är
kalibreringsreferensen.

**Kanal 5 — ett reglage vars filtertillstånd primas.** `move.w D2,($6,A2)`
med `A2 = $0DC2` skriver direkt in den första mätningen i den filtercell
som ROM-callbacksen på `$F8DB30`/`$F8DB4C` sedan glidande medelvärdar
(`par-is-an-adc.md` avsnitt 2). Man primar ett filter för att det inte ska
rampa upp från noll — vilket bekräftar att `(A2+6)` är just en
filtertillståndscell och att `$0DC2` är ett instansblock.

**Kanal 0 — ett reglages viloläge, kalibrerat.** `(mätning × faktor) >> 16`
lagras som `centrum + $528` på `$0DDE` och `centrum - $528` på `$0DE0`
(`+$528` följt av `-$0A50` = `-2 × $528`). Ett **symmetriskt
toleransfönster ±$528 runt uppmätt viloläge** — en dödzon som kalibreras
vid varje boot.

En andra kalibreringsväg finns på `$0068C8-$0068F2`: `move.b #$07,D0` /
`bsr`, instansblock `$0DD0`, handler `$FFF8DB18`, dispatcher `$FFF8DB4C`
(3/4-1/4-filtret), och en identisk kopia av `$A3480000 / x` -> `$0DF2`.

## 4. `[Verified som aritmetik]` Vad PAR faktiskt ska returnera på kanal 7

Faktorn är `$A3480000 / D2` och används som `(x × faktor) >> 16`. Alltså
`kalibrerat = x × ($A348 / D2)`. **Faktorn är enhet precis när
`D2 = $A348`.** Numeratorns högord är per konstruktion det nominella
värdet — det är inte en tolkning, det faller ut ur aritmetiken.

`D2` är summan av åtta samplingar à `raw << 3`, alltså
`D2 = raw_medel × 64`:

```
raw_medel_nominell = $A348 / 64 = 41800 / 64 = 653,1   av 1023
                   = 63,8 % av fullt utslag
```

`divu.w` overflowar dessutom för `D2 < $A349`, så under detta värde
saturerar faktorn till `$FFFF`. Nominellt ligger alltså precis på
saturationsgränsen och det verkliga viloläget måste ligga **strax över**
653.

| `raw` (kanal 7) | `D2` | Utfall |
|---|---|---|
| 0 | `$0000` | **trap -> ERROR 130** |
| < 653 | < `$A349` | faktorn saturerar till `$FFFF` |
| ~653-655 | ~`$A348` | faktor ~ enhet (konstruktionens nominalpunkt) |
| 1023 | `$FFC0` | faktor `$A375` |

**Detta är det första hårda, firmware-härledda värdet på vad PAR ska
returnera.** Det är ingen gissning och ingen konstant hämtad från en
syskondrivrutin.

Sidonotering, inte belägg: `esq5505.cpp` kallar sin kanal 7
`vRef to check battery`. Kanalindexet sammanfaller. Dess konstant
`0x5540` gör det dock inte — den ligger under saturationsgränsen och är
dessutom skriven i ES5505:s vänsterjusterade skala. Återanvänd den inte.

## 5. `[OPEN]`

1. Kanal 5 och kanal 0 har inga härledda nominalvärden — koden accepterar
   vad som helst där (de kalibreras, de valideras inte). Först när
   kanal 7 passerar går det att observera vad de behöver.
2. `trap #8` (D0=4) och `trap #7` (D0=0) i mätloopen är fortfarande
   ospårade. De kan inte välja kanal (konstanta argument, D7 når dem
   aldrig), men de gör sannolikt en fördröjning så muxen hinner ställa
   sig. Relevant först om en tidsmodell behövs.
3. Wavetable-minnet (`set_addrmap(0, ...)` med `.ram()`) måste lösas
   innan `es5506_device` kan vara permanent instansierad.
