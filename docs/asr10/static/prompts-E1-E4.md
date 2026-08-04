# Promptar för runtime-experimenten E1–E4

Alla körs på din maskin. Gemensamma regler som ska stå kvar i varje prompt:

* **Använd inte `-log`.** Det gav en 19 GB `error.log` förra gången.
* **Ändra inget beteende och committa inget** förrän rapporten är granskad.
* Kör inga git-kommandon parallellt från annat håll under körningen (`.git/index.lock`).
* Referenskörningen är alltid:
  `./mess asr10booth -flop1 floppies/asr10booth/V350.img` utan flaggor.

---

## E2 — Speglingen `$FF0000-$FFFFFF` ↔ `$000000-$00FFFF`  (HÖGSTA PRIORITET)

Detta måste avgöras innan något i `mem_map` rörs.

```
Uppgift: avgör om adressintervallet $FF0000-$FFFFFF pa ASR-10 ar samma fysiska
RAM som $000000-$00FFFF, eller separat minne.

Bakgrund (statiskt underlag, ska INTE tas for givet):
ROM gor 1254 `jsr/jmp abs.w` till 334 distinkta adresser i $FF801E..$FF9FF6.
Noll av malen ar udda, sa det ar riktiga instruktioner - t.ex.
  $F97874:  12 1A        move.b (A2),D1
            4EB8 8030    jsr    $FF8030
            4E75         rts
Statisk analys pekar pa att innehallet pa $FF801E..$FF9FF6 ar OS-bildens
bindningstabell, som enligt en oberoende verifierad laddningsregel ligger pa
RAM $00801E..$009FF6. Hypotesen ar alltsa att $FFxxxx speglar $00xxxx.
Drivrutinen har i dag `map(0xfc5020, 0xffffff).ram()`, dvs separat minne.

Gor foljande, i denna ordning, UTAN att andra mem_map:

1. Lua-tapp: installera lastapp pa $FF8000-$FFFFFF och skrivtapp pa samma
   intervall. Logga `R/W <adress> <varde> <PC>`. Kor referenskorningen till
   "FILE 1 TUTORIAL BNK" och rapportera:
   - hur manga accesser som sker totalt,
   - vilka adresser,
   - vid vilka PC,
   - om nagon access ar en INSTRUKTIONSHAMTNING (PC inom $FF0000-$FFFFFF).

2. Om noll accesser sker under boot: rapportera det och stanna. Da ar fragan
   obesvarad men ofarlig, och vi vet att den vacks forst av nagon funktion
   efter boot (prova da att ga in i en meny, ladda en instrumentfil, spela en
   ton, och kor om).

3. Om accesser sker: gor ett direkt speglingstest i Lua vid machine_reset
   eller vid forsta traff:
   - skriv $DEAD till $008030 (long/word, ange vilket)
   - las $FF8030
   - skriv $BEEF till $FF9000
   - las $009000
   Rapportera de fyra vardena. Lika varden => spegling.

4. Rapportera ocksa vad som faktiskt ligger pa $008030 nar boot ar klar.
   Forvantan enligt hypotesen: `4EF9 FFF97662` (jmp $FFF97662).
   Dumpa 16 byte fran $008030 och 16 byte fran $FF8030 och jamfor.

Andra INGENTING i mem_map i den har omgangen. Rapportera bara.
```

---

## E1 — Vektortabellens installation och ROM→OS-hoppet

E1 och E3 kan köras i samma session; de använder samma tapp.

```
Uppgift: faststall (a) nar OS-vektortabellen skrivs till RAM $000000-$0003FF,
och (b) exakt hur ROM lamnar over kontrollen till OS.

Statiskt underlag:
- OS-bildens forsta 0x400 byte ar en 68000-vektortabell. v0 SSP = $00000300,
  v1 PC = $00000000 i BADE V1.61 och V3.50. Nagon "mjuk reset" ur bilden ar
  alltsa omojlig.
- ROM innehaller INTE ett enda `jsr/jmp abs.l` med mal i RAM (< $400000).
  Overlamningen maste alltsa ske via `jmp/jsr abs.w`, registerindirekt hopp,
  eller `rts` till en stackad adress.
- Kandidat att titta pa forst: slot $801E.w (ROM anropar den 13 ganger och den
  pekar in i OS-koden i bada versionerna: $007144 i V161, $0071C6 i V350).

Gor sa har:

1. Lua-skrivtapp pa $000000-$0003FF. Logga varje skrivning som
   `VEC <adress> <varde> <PC>`. Kor till "FILE 1 TUTORIAL BNK".
   Rapportera: forsta och sista skrivning, vilken PC som gor dem, om det ar
   en loop (kopiering) eller enskilda skrivningar, och om kallan ar $000A00.

2. Lagg till skrivtapp pa $000A00-$000DFF. Om vektortabellen kopieras darifran
   ska den regionen skrivas FORE $000000-omradet.

3. Sparning av overlamningen: logga alla PC-vardena da PC forst gar fran
   ROM-omradet ($F80000-$FBFFFF) till RAM (< $400000) eller till
   $FF0000-$FFFFFF. Rapportera de tio forsta sadana overgangarna med
   fran-PC, till-PC och instruktionen pa fran-PC.

Rapportera i klartext. Andra ingenting.
```

---

## E3 — Laddningskartan (avgör Seg1/Seg2-gränsen)

```
Uppgift: kartlagg exakt var OS-filen hamnar i RAM.

Statiskt underlag:
- OS-filen ligger pa diskoffset 0x3000, 382 block (195 584 byte) i V350.
- Tva laddningsregler ar statistiskt verifierade men gransen mellan dem ar okand:
    Seg1: RAM = OS_offset + 0xA00   (disk = RAM + 0x2600)   - live-verifierad
          pa RAM $0067EC <-> disk 0x08DEC
    Seg2: RAM = OS_offset - 0x5A00  (disk = RAM + 0x8A00)
- Skillnaden ar exakt 0x6400, vilket ar forenligt med att laddaren hoppar over
  0x6400 byte i filen. Var, ar okant.

Gor sa har:

1. Lua-skrivtapp over hela RAM ($000000-$0FFFFF och $FF0000-$FFFFFF) fran
   forsta FDC-lasningen till "KEYBOARD TUNED".
2. Logga INTE varje skrivning. Bygg i stallet ett histogram: for varje
   sammanhangande skrivsekvens, logga
   `LOAD <start-adress> <slut-adress> <antal byte> <PC for skrivinstruktionen>`.
   Sla ihop sekvenser som ar sammanhangande.
3. Rapportera de tio storsta blocken, sorterade efter storlek, med
   start-adress och storlek.
4. For varje block: berakna vilket diskoffset i V350.img som innehaller samma
   byte, genom att jamfora de forsta 32 byten mot filen (sok byte-monstret).
   Rapportera `RAM $xxxxxx <- disk 0x%05X` per block.

Det svarar direkt pa var 0x6400-hoppet ligger och om det finns fler an tva
segment.
```

---

## E4 — CS1-fönstret `$FF6000-$FF7FFF`

Låg prioritet, men billig att köra samtidigt med E2.

```
Uppgift: ta reda pa vad chip select 1 pa MC68302 anvands till.

Statiskt underlag: ROM programmerar BR1=$1FEF, OR1=$FFFE, vilket avkodas till
$FF6000-$FF7FFF (8 KB) med FC-jamforelse pa. Fonstret ligger MEDVETET utanfor
68000:ans kortadresserbara omrade (en teckenutvidgad abs.w kan bara ge
$000000-$007FFF eller $FF8000-$FFFFFF), sa det kan bara nas med 32-bitars
absolut eller registerindirekt adressering. Det talar for periferi eller
expansion, inte kod.

1. Lua-las/skrivtapp pa $FF6000-$FF7FFF, referenskorningen, rapportera antal
   accesser, adresser och PC.
2. Om noll accesser under boot: prova ocksa att ga in i systemmenyn och
   forsoka en SCSI-relaterad funktion, samt en MIDI-funktion.
3. Jamfor med `Asr10Cs3Decoder.cpp/.hpp` i 68302-emulatorprojektet - finns det
   redan en tolkning av CS1 dar?
```

---

## Efter E1–E4

Först när E2 (speglingen), 0x6400-hoppet (E3) och överlämningen (E1) är utredda är det
läge för konsolidering till `reference/`. Fördelningen står i worklogens avsnitt
*Canonical destinations*.

Därefter i din prioritetsordning: bindningstabellens 723 slots mot rutinindexet
(`os-binding-table.csv` är råmaterialet), sedan `$FF8D50` (som enligt L1 är slot
`$8D50.w`), Timer 2-snapshotens anropare och Line-A.
