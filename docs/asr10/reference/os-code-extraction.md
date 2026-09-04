# RAM-adress in, disassemblerad diskresident OS-kod ut

2026-08-01. Destillerad ur `docs/asr10/divisor-zero.md` avsnitt 1 —
första gången den här sessionen behövde disassemblera OS-kod som lästs
in från diskett i klartext (inte ROM, inte handskriven testkod). Metoden
bygger direkt vidare på `docs/asr10/disk-read-path.md`s redan bevisade
diskyta-till-RAM-verifiering; det nya här är att gå **åt andra hållet**
— från en given RAM-adress, hitta rätt ställe på disken, och etablera
att disassemblering av den levande RAM-kopian är giltig utan förbehåll.

**Kräver:** en körande, deterministisk emulation (`-nothrottle`, känt
frö/kommandorad), `unidasm` byggt lokalt, och den redan committade
`ASR10_FDC_CMD46 event=summary`-transaktionsloggen (ovillkorlig,
`asr10_boot.cpp`).

## Steg

1. **Läs N byte live ur den körande maskinen vid måladressen.** Minst
   16-32 byte, mer om möjligt — tillräckligt för att fungera som en
   unik sökterm i steg 2 utan att av misstag träffa en annan,
   oberoende plats på disken. En engångsloggrad räcker
   (`lowmem_word()`/`lowmem_byte()` för adresser inom `0x000000`-
   `0x0fffff`; `read_program_word()` för adresser i drivrutinens
   `mem_map()`-övriga `.ram()`-fönster).

2. **Sök bytesekvensen rakt av i `.img`-filen.** Fristående Python,
   ingen MAME inblandad — `bytes.find()` mot filens råa innehåll.
   Disketten är INTE byte-interlevad (till skillnad från de två
   hi/lo-ROM-filerna) — sök direkt, ingen sammanflätning behövs. Kräv
   **exakt en träff**; flera träffar betyder att sökningen var för kort
   eller för generisk.

3. **Räkna om byteoffset till C/H/R** med `disk-read-path.md`s redan
   bevisade formel (bekräftad där genom oberoende byte-för-byte-test):

   ```
   track_index = C*2 + H
   byte_offset = (track_index * 20 + (R-1)) * 512
   ```

   invers: `sector_index = byte_offset // 512`,
   `track_index = sector_index // 20`, `R = sector_index % 20 + 1`,
   `C = track_index // 2`, `H = track_index % 2`.
   (20 sektorer/spår, 512 byte/sektor — geometrin `disk-read-path.md`
   redan härledde och verifierade för både `V161.img` och `V350.img`.)

4. **Cross-referera mot `ASR10_FDC_CMD46 event=summary`-loggen** (grep
   `decoded_C`/`decoded_H`/`decoded_R`/`decoded_EOT`) för att hitta
   VILKEN transaktion som förde in just den sektorn — bekräftar att en
   verklig, redan loggad diskläsning svarar för den bytesekvensen, inte
   bara att offseten råkar matcha.

5. **Jämför byte för byte.** Identiska → disassemblera den levande
   RAM-kopian fritt, ingen relokerings- eller uppackningstransformation
   finns att ta hänsyn till. Olika → **stanna** — kartlägg
   transformationssteget (komprimering, relokeringsfixup, checksummor)
   innan någon disassemblering tolkas som verklig kod. (Detta villkor
   höll i det enda testade fallet hittills; se `divisor-zero.md` avsnitt
   1 för det fullständiga exemplet.)

6. **Disassemblera.** `unidasm <bin> -arch m68000 -basepc <RAM-adress>`
   fungerar direkt på de levande RAM-bytesen (skriv dem till en liten
   binärfil först) — RAM-koden är, efter steg 5:s bekräftelse, identisk
   med disken, så samma `unidasm`-hantering som används för ROM gäller
   rakt av. Observera: RAM-adresser saknar den hi/lo-sammanflätning
   ROM-filerna kräver; skriv bytesen i den ordning de lästes, ingen
   ombearbetning.

## Gränser — vad metoden INTE visar

* **Bara att KOPIAN i RAM är oförändrad mot disken.** Den säger
  ingenting om VARFÖR just den adressen fick just den koden, eller om
  någon RELOCATION TABLE justerade absoluta adresser INUTI koden efter
  en initial rå kopiering (den enda hittills testade instansen visade
  inga tecken på det, men det är inte generellt uteslutet).
* **Fungerar bara för kod som fortfarande ligger orörd i RAM.** Om
  OS:et senare skriver över eller flyttar den laddade koden krävs en
  ny, tidsmässigt närmare läsning — den här metoden ger en ögonblicksbild,
  inte en garanti om att adressen alltid innehåller samma sak.
* **Kräver att transaktionsloggen faktiskt täcker den relevanta
  disksektorn.** Om en sektor lästs före körningens loggfönster (eller
  med `ASR10_LOG_FDC_ACCESS`/motsvarande avstängt) ger steg 4 inget
  facit — steg 1-3, 5-6 fungerar ändå, men utan transaktions-
  bekräftelsen.

## Var den redan har använts

`docs/asr10/divisor-zero.md` avsnitt 1: `$0067F6` (RAM) ↔ `V350.img`
byteoffset `0x8DF6` ↔ FDC-transaktion 9 (`C=01,H=01,R=01`-`14`,
`R=11` av den transaktionen). Identiska, disassemblerat fritt därefter.

---

## Tillagg 2026-08-04: byteoffset kan nu tolkas direkt

Steg 2:s byteoffset behover inte langre bara jamforas - det finns nu tva belagda
laddningsregler. Se `os-image-layout.md` §3.

```
segment 1 (RAM under $008000):   disk = RAM + 0x2600
segment 2 (RAM over  $008000):   disk = RAM + 0x8A00
```

Det tidigare enda exemplet (`$0067F6` <-> `0x8DF6`) ar en instans av segment 1-regeln.
SCC-initsekvensen i V1.61 (`$00BEF2` <-> `0x148F2`) ar en runtimeverifierad instans av
segment 2-regeln.

**Gransen mellan segmenten ar inte faststalld** - den ligger i intervallet
`($009FF6, $00BEF2]`. Verifiera darfor alltid med bytemonstersokning innan en
offsetregel anvands; se `methods-static-analysis.md` §6. Att titta pa det forutsagda
offsetet bekraftar bara att man kan tolka godtyckliga byte som kod.
