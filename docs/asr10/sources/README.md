# Primärkällor

Filer hämtade utifrån. Redigeras aldrig — citeras.

| Fil | Ursprung | Status |
|---|---|---|
| `es5701.vhd` | http://www.buchty.net/ensoniq/files/es5701.vhd | Rekonstruktion av Rainer Buchty, V1.0 2019-11-23, märkt "syntax test only (ghdl -a)" |
| `ASR10_manual.pdf` | Ray Legnini, "ASR-10 Musician's Manual" (ägarmanual) | Redan i trädet före `scc-board-source-question.md`; 390 sidor, front-panel-drift, inte servicedokumentation |
| `ASR10_service_manual.pdf` | https://www.synthxl.com/wp-content/uploads/2018/03/Ensoniq-ASR-Service-Manual.pdf | Hämtad `scc-board-source-question.md` (uppföljningsrunda). ENSONIQ ASR Service Manual — kommunikationsväg, felkodslistor, kortbeskrivning. `pdftotext`-verifierad, ordagrant citat om "two-line asynchronous interface" bekräftat på egen rad. |
| `ASR10_upper_coil_board_schematic.pdf` | http://www.tauntek.com/ASR10highcoilsch.pdf (länkad från zine.r-massive.com) | Rekonstruerad av R. Grieb (Tauntek), Rev 1.0, 2016-02-21. Visar U3=80C52 (klaviaturens egen scanner-MCU) med SERIN/SEROUT till 20-pin flexkabel — den fysiska källan bakom servicemanualens "two-line asynchronous interface". Egen anmärkning på schemat: "This schematic drawn from through-hole version of pc bd. It may contain errors." |
| `ASR10_lower_coil_board_schematic.pdf` | http://www.tauntek.com/ASR10lowcoilsch.pdf (länkad från zine.r-massive.com) | Samma rekonstruktion, samma varning om möjliga fel. Visar induktionsavkänning (drive/sense-spolar) för tangenterna, ingen egen MCU. |

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
