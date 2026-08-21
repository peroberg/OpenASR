# ASR-10 — dokumentationsmanifest

Normerande förteckning över projektets dokumentation. **Detta manifest, inte något
TGZ-paket eller någon bifogad fil, är svaret på frågan "vilka dokument ingår?"**

Bifogade paket (`asr10-consolidation-v2` … `v9`) är **inte** permanenta projektartefakter.
När innehållet är verifierat och infört i repot ska de kasseras. De får aldrig vara
implicit sanningskälla.

**Konsoliderad 2026-08-04** från statiskt analysmaterial. Mergen är genomförd i
arbetskopian; commit anges här efter att den har granskats och committats.

Utgångsläge: branch `asr10-architecture-cleanup`, HEAD
`1dfaf31b9f3942996f448c67e56463c1301115e5` ("Document ASR10 vector map").
Konsolideringscommit: se git-historiken för den här filen.

## Kategorier

```
current-status          projektets aktuella läge; läses först
canonical-reference     kanonisk kunskap, manuellt vårdad
investigation           frågor som drivs aktivt
generated-static-data   maskingenererad, får ej handredigeras
static-worklog          arbetslogg; provenance, inte sanning
method                  hur resultat producerades och metodens blinda fläckar
archive                 avslutade eller ersatta berättelser
source-material         vendormaterial och externa källor
runtime-tool            instrumentering
```

---

## Rot

| path | kategori | status | källa | typ | beskriver | ersätter | härleds ur |
|---|---|---|---|---|---|---|---|
| `current-status.md` | current-status | **aktuell** | konsolidering 2026-08-04 | handkurerad | aktuellt läge, nästa fas | befintlig 4 652 B | reference/ |
| `DOCUMENTATION-MANIFEST.md` | current-status | **aktuell** | detta dokument | handkurerad | vilka dokument som ingår | — | — |
| `regression-test.sh` | runtime-tool | oförändrad | i trädet | handkurerad | acceptanstest FILE 1 | — | — |

## `reference/` — canonical-reference

| path | status | källa | beskriver | ersätter / härleds ur |
|---|---|---|---|---|
| `boot-sequence.md` | **aktuell** | uppdaterad 2026-08-04 | reset → FILE 1 | steg 1b DPRAM-bryggan tillagt; chip-select-luckan i steg 2 stängd; steg 7 utbyggt med M1–M3 |
| `boot-runtime-timeline.md` | **aktuell** | ny 2026-08-11 | reset → runtime, ansvarsfördelning ROM/RAM | dynamisk PC-profil, IRQ6-källor, regionklassade övergångar |
| `call-graph.md` | **aktuell** | ny 2026-08-04 | kontrollflödesmodell, CSV-schema | ur `call-graph-edges.csv`, `routines.csv` |
| `es5701-wiring.md` | oförändrad | i trädet | ES5701-koppling | — |
| `hardware-map.md` | **aktuell** | uppdaterad 2026-08-04 | fysiska komponenter | chip-select-tabellen tillagd; två rättelserutor markerar föråldrade `$FC6816`- och SCSI-avsnitt som historik |
| `mc68302-status.md` | **aktuell** | ny 2026-08-04 | SIB/CP/PIO/timers per block | ur SIB-censusen |
| `memory-map.md` | **aktuell** | ersatt 2026-08-04 | adresser och avkodning | runtime-delen **flyttad** till `runtime-service-model.md` |
| `methods-static-analysis.md` | **aktuell** | ny 2026-08-04 | metoder och felkällor | — |
| `movep-library.md` | oförändrad | i trädet | MOVEP-thunkar i DPRAM | — |
| `os-code-extraction.md` | **aktuell** | uppdaterad 2026-08-04 | RAM-adress → disk | segmentreglerna tillagda |
| `os-image-layout.md` | **aktuell** | ny 2026-08-04 | diskformat, segmentregler | — |
| `rom-os-abi.md` | **aktuell** | ny 2026-08-04 | arkitektur, bindningstabellen | ur `os-binding-table.csv` |
| `runtime-service-model.md` | **aktuell** | utbruten 2026-08-04 | dispatcher-kö, servicefält, V1.61-observationer | programmatisk split av gamla `memory-map.md`, historiken oförändrad |
| `subroutine-index.md` | **aktuell** | uppdaterad 2026-08-04 | namngivna rutiner | bindningsslots + 6 nya rutiner tillagda |
| `vector-map.md` | **aktuell** | uppdaterad 2026-08-04 | vektormodellen | sammanslagning genomförd; femkategorimodellen tillagd, allt befintligt bevarat |

## `static/` — generated-static-data + static-worklog

**Katalogen är permanent.** Den tidigare formuleringen om att den kan arkiveras efter
konsolidering är återkallad. CSV-filerna ska inte flyttas till `reference/`.

| path | kategori | typ | rader | SHA-256 (16) |
|---|---|---|---|---|
| `README.md` | generated-static-data | handkurerad | — | addendum infällt 2026-08-04 |
| `call-graph-edges.csv` | generated-static-data | **genererad** | 5 243 | `472373eea0fac895` (rättat, `scc-hardware-gap.md` Del 1.1 — tidigare `b8bfb32053274a66` var en stale referens till en äldre 5240-kantersgeneration, aldrig matchad mot `static/README.md`s egen, alltid korrekta rad) |
| `os-binding-table.csv` | generated-static-data | **genererad** | 723 | `600f646943d5a3a7` |
| `rom-abi-entrypoints.csv` | generated-static-data | **genererad** | 1 054 | `6293e5c9cffeb4f4` |
| `routines.csv` | generated-static-data | **handkurerad** | 25 | `8aba2712c91a6a04` |
| `prompts-E1-E4.md` | method | handskriven | — | `49ed64dcbab91b43` |
| `worklog-rom-os-abi.md` | static-worklog | handskriven | — | `bc06c6a45842c323` |
| `worklog-rom-os-abi_2.md` | static-worklog | handskriven | — | `4f2bdc046e3171a3` |

Worklogarna ligger kvar som provenance. De ska märkas som worklog, inte som kanonisk
sanning, och får inte tyst tas bort.

**`README-ADDENDUM.md` existerar inte i repot.** Innehållet är infällt i
`static/README.md` 2026-08-04.

## Analyserade källartefakter

| artefakt | storlek | SHA-256 |
|---|---|---|
| `asr10.bin` | 262 144 | `fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf` |
| `V161.img` | 1 638 400 | `2a5cc161e80001daddf532914e80e854d3079f152197ab3def31055f239182f6` |
| `V350.img` | 1 638 400 | `2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b` |

## `investigations/` — 15 filer, oförändrade

Aktivt drivna frågor. `filesystem-browser-map.md` (273 KB) innehåller historiska
påståenden som är märkta där de avviker från nuvarande bootbeteende.

## `archive/` — 28 filer

Avslutade eller ersatta berättelser, samt projektets handoffdokument enligt etablerad
konvention (`asr10-handoff-*`, `asr10-mame-handoff-*`).

| path | kategori | status | beskriver |
|---|---|---|---|
| `asr10-handoff-2026-08-04.md` | archive | **aktuell** | handoff efter konsolideringen: läsordning, arbetsordning 1–8, öppna frågor, dokumentationsregler |

De 27 tidigare filerna är oförändrade. `sources/` (2 filer) och `lua/asr10_trace.lua`
likaså.

---

## Vad som INTE är en projektartefakt

```
asr10-consolidation-v2.tgz … v9.tgz     kasserade; innehållet är infört i repot
README-ADDENDUM.md                      infällt i static/README.md, finns ej i repot
MERGE.md                                arbetsinstruktion; finns ej i repot
```

Ingen kanonisk fil hänvisar till en fil som bara finns i ett TGZ-paket. Kontrollerat.

## Säkerhetskopior

Sju `archive/*.pre-merge-20260804` skapades under konsolideringen och är **borttagna
före commit**. Samtliga originalversioner finns i HEAD `1dfaf31b9f3`, utom
`static/README.md` som låg i den otrackade `static/`-katalogen — men eftersom den bara
fick material appenderat är dess ursprungliga innehåll de första 11 117 byten av den
nuvarande filen.

`archive/` innehåller därmed sina dokumenterade 27 filer.
