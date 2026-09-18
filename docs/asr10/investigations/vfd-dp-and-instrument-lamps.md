# VFD Integrated Decimal Point and Instrument Selection LEDs

## 1. Problembeskrivning

Två relaterade panel- och displayproblem har identifierats i OpenASR:
1. **Integrerad decimalpunkt / punkt i VFD (BAR=001.01):**
   Under sequencer-visning (t.ex. vid locate/play) visades `BAR=00B01` istället för `BAR=001.01`.
   Displayen har 22 teckenpositioner och texten "BLUES INTRO  BAR=001.01" är 23 tecken lång om punkten kräver en egen cell. Den fysiska VFD-modulen (1x22 vakuumfluorescerande display) har integrerade decimalpunktssegment (DP) i varje teckencell.
2. **Instrument-väljardioder 1–8 saknades:**
   Endast instrument 1 (`asr10_instlamp0`) var ansluten i layouten och drivrutinen, och den var felaktigt kopplad till annunciator-register `$77 bit 0`. När andra instrument valdes tändes lampan för instrument 1.

---

## 2. Analys & Firmware-reversering

### 2.1 Teckenavkodning av siffror med integrerad punkt (ROM $F824BE & $F8A790)

Disassemblering av ASR-10 ROM avslöjade formatrutinen vid `$F8A790`:
- När firmware formaterar tal med integrerad decimalpunkt sätts flaggan vid `$0C94`.
- När `$0C94 != 0` ersätts standard-ASCII siffror `'0'..'9'` (`0x30..0x39`) med en lookup i tabellen vid ROM `$F824BE`:
  ```
  $F824BE: 21 23 25 28 29 3A 3B 5B 5C 5D
  ```
- Denna tabell mappar siffrorna `0..9` med tänd decimalpunkt till teckenkoderna:
  - `0x21` = `'0.'` (istället för ASCII `!`)
  - `0x23` = `'1.'` (istället för ASCII `#`)
  - `0x25` = `'2.'` (istället för ASCII `%`)
  - `0x28` = `'3.'` (istället för ASCII `(`)
  - `0x29` = `'4.'` (istället för ASCII `)`)
  - `0x3A` = `'5.'` (istället för ASCII `:`)
  - `0x3B` = `'6.'` (istället för ASCII `;`)
  - `0x5B` = `'7.'` (istället för ASCII `[`)
  - `0x5C` = `'8.'` (istället för ASCII `\`)
  - `0x5D` = `'9.'` (istället för ASCII `]`)

Tidigare använde `font[]` i `src/mame/ensoniq/esqvfd.cpp` standard BFM fruit-machine segment. För `0x23` (ASCII `#`) fanns font-definitionen `0xc62a`. Via `conv_segments` konverterades detta till `0x03ce`, vilket visuellt på en 14-segmentsdisplay liknar ett 'B' eller '#', och DP-segmentet (bit 14, `0x4000`) förblev släckt.
Därför visades `BAR=00B01` istället för `BAR=001.01`.

Lösning:
I `font[]` i `esqvfd.cpp` sattes bit 12 (`0x1000`, som av `conv_segments` translateras till MAMEs `led14segsc` DP-bit 14 / `0x4000`) för dessa 10 specialkoder, i kombination med respektive siffersegment:
```c
0x32b7, // 0. (Ensoniq VFD 0x21) -> led14segsc 0x4ffc
0x1408, // 1. (Ensoniq VFD 0x23) -> led14segsc 0x4300
0xf206, // 2. (Ensoniq VFD 0x25) -> led14segsc 0x4b6c
0x5226, // 3. (Ensoniq VFD 0x28) -> led14segsc 0x4be8
0xd023, // 4. (Ensoniq VFD 0x29) -> led14segsc 0x4718
0xd225, // 5. (Ensoniq VFD 0x3A) -> led14segsc 0x4ee8
0xf225, // 6. (Ensoniq VFD 0x3B) -> led14segsc 0x4eec
0x1026, // 7. (Ensoniq VFD 0x5B) -> led14segsc 0x4308
0xf227, // 8. (Ensoniq VFD 0x5C) -> led14segsc 0x4fec
0xd227, // 9. (Ensoniq VFD 0x5D) -> led14segsc 0x4fe8
```
Nu tänds DP-segmentet korrekt i cell 19 och siffran '1' visas som `1.` utan att flytta markören eller ta upp en extra cell.

---

### 2.2 Instrument-väljardioder (Nivå 2 Indicator Commands $74, $75, $76)

Traces av serietrafiken från DUART kanal B under laddning och val av instrument gav:
- När instrument laddas eller aktiveras skickar firmware Nivå 2 Indikatorkommandon:
  - `$74 <id>`: Stäng av indikator `<id>` (Off)
  - `$75 <id>`: Tänd indikator `<id>` fast (Solid On)
  - `$76 <id>`: Blinka indikator `<id>` (Blink)
- Indikator-ID:n för instrument:
  - ID `0..7`: Instrument 1..8 Loaded/Present (gul kanal på tvåfärgade LED:er)
  - ID `8..15`: Instrument 1..8 Selected/Active (röd kanal på tvåfärgade LED:er)
- Annunciator-registret `$77 bit 0`:
  Detta register sätts till 1 så fort *något* instrument är valt, och rensas till 0 när inget instrument är valt. Att tidigare binda `m_instrument_lamps[0]` till `$77 bit 0` gjorde att lampa 1 tändes oavsett vilket instrument som valdes.

Lösning:
1. `asr10panel_device::rcv_complete()` tar emot `$74..$76` och lagrar följande byte (`id`).
2. För `id >= 8 && id <= 15` uppdateras `m_instrument_lamps[id - 8] = (state == 0) ? 0 : 1`.
3. Felaktig koppling till `$77 bit 0` togs bort.
4. I `src/mame/layout/asr10_panel.lay` lades 8 instrumentlampor till (`asr10_instlamp0`..`7`), dels i annunciator-fältet och dels direkt på knapparna `#1` (`BTN_02`), `#2` (`BTN_08`), `#3` (`BTN_0E`), `#4` (`BTN_14`), `#5` (`BTN_04`), `#6` (`BTN_22`), `#7` (`BTN_1C`), `#8` (`BTN_16`).

---

## 3. Regression & Verifiering

Nytt regressionstest: `docs/asr10/lua/vfd_dp_and_inst_lamps.lua`.
Testet verifierar:
1. Laddning av fil 2 till slot 1 och fil 3 till slot 2.
2. Initialt släckt läge (`lamps=[0 0 0 0 0 0 0 0]`).
3. Val av slot 1 tänder `asr10_instlamp0` (lamp 1 = 1, lamp 2 = 0).
4. Direkt byte till slot 2 släcker slot 1 och tänder `asr10_instlamp1` (lamp 1 = 0, lamp 2 = 1).
5. Avval av slot 2 släcker alla lampor.
6. Laddning av sequence och navigering till BAR-vyn.
7. Avläsning av segmentvärde i kolumn 19:
   - DP-segment (`0x4000`) är tänt.
   - Det gamla korrupta '#' / 'B'-värdet (`0x03CE`) förekommer inte.
   - Exakt värde är `0x4300` ('1' + DP).
