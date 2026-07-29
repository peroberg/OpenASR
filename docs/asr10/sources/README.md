# Primärkällor

Filer hämtade utifrån. Redigeras aldrig — citeras.

| Fil | Ursprung | Status |
|---|---|---|
| `es5701.vhd` | http://www.buchty.net/ensoniq/files/es5701.vhd | Rekonstruktion av Rainer Buchty, V1.0 2019-11-23, märkt "syntax test only (ghdl -a)" |

## Varningar för `es5701.vhd`

* Aldrig simulerad, endast syntaxkontrollerad. Behandla som dokumentation
  av avsikt, inte som gyllene modell.
* Entiteten heter `es5571`, inte `es5701` — sifferomkastning i källan.
  Detta är sannolikt ursprunget till "ES5570" som förekommer i flera
  dokument i `docs/asr10/`; rätt nummer på adressdekodern är ES5700.
* Transkriptionsfel: `lo_r`/`lo_w` respektive `hi_r`/`hi_w` har identiska
  villkor (`rw='1'` i båda). Skrivvarianterna ska rimligen vara `rw='0'`.
  Läs- och skrivvillkor kan inte vara desamma.

Ej hämtade ännu: `es5700.vhd` (GLU/adressdekoder — gäller inte ASR-10 men
dokumenterar de funktioner U5:s PAL måste täcka) och `es5702.vhd`.
