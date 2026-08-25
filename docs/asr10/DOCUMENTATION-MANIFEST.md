# ASR-10 — dokumentationsmanifest

Normerande förteckning över projektets dokumentation. **Detta manifest, inte något
TGZ-paket eller någon bifogad fil, är svaret på frågan "vilka dokument ingår?"**

Bifogade paket (`asr10-consolidation-v2` … `v9`) är **inte** permanenta projektartefakter.
När innehållet är verifierat och infört i repot ska de kasseras. De får aldrig vara
implicit sanningskälla.

**Konsoliderad 2026-08-04** från statiskt analysmaterial. Mergen är genomförd i
arbetskopian; commit anges här efter att den har granskats och committats.

**Reviderad 2026-08-23.** Manifestet är en levande förteckning. Antal och
referenstabell nedan inventerades mot arbetskopian; äldre konsolideringssiffror
får inte läsas som aktuell täckning.

**Statusstädat 2026-08-25 efter transport/A-B-rundan.** Transport och
sequencer execution är `[Verified]` (`$1D` Play, `$17` Stop/Continue i aktiv
playback, hörbart ljud), men musikalisk/audio-korrekt sequencer-playback är
`[OPEN]` och projektets högst prioriterade funktionella problem: TUTORIAL-
flödet fungerar betydligt bättre utan att vara originalvaliderat, medan en
fresh-boot `ATRK TUT BNK`-laddning främst ger klickigt/felaktigt ljud.
`$001098`-populationen motsvarar bankinnehållet och är inte i sig en loaderbugg.
UI-modellen är inte komplett trots verifierad displaymekanik. Den
PC-relativa 68000-static-analysis-uppgraderingen är ett separat verktygsarbete
under playback- och UI-prioriteterna.

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
| `current-status.md` | current-status | **aktuell** | löpande handoff, statusstädad 2026-08-25 | handkurerad | aktuellt läge, nästa experiment | tidigare kumulativ status | reference/ + investigations/ |
| `DOCUMENTATION-MANIFEST.md` | current-status | **aktuell** | detta dokument | handkurerad | vilka dokument som ingår | — | — |
| `regression-test.sh` | runtime-tool | **aktuell** | i trädet | handkurerad | boot, panel, load, guards och torrt note-audio | äldre FILE 1-only-baseline | lua/ |

## `reference/` — canonical-reference

| path | status | källa | beskriver | ersätter / härleds ur |
|---|---|---|---|---|
| `handoff-2026-08-23.md` | **historisk freeze med aktuell post-freeze-rättelse** | repo- och evidensaudit 2026-08-23, statusnot 2026-08-25 | femminutersläge, falsifierade hypoteser, öppna frågor och arbetskopiegräns | läses för freeze-provenance; aktuell runtime och prioritet i `current-status.md` |
| `architecture-handoff.md` | **historiskt checkpoint** | pre-IDMA-arkitektur, märkt 2026-08-22 | servicekärna och den passerade storage-blockeraren | runtime-status ersatt av `current-status.md`; arkitekturdelar fortsatt relevanta |
| `audio-storage-architecture.md` | **aktuell** | uppdaterad 2026-08-22 | gränsen storage / ES5701 / ES5506 / ES5510 | senare load- och note-audio-evidens införd |
| `boot-sequence.md` | **aktuell** | uppdaterad 2026-08-04 | reset → FILE 1 | steg 1b DPRAM-bryggan tillagt; chip-select-luckan i steg 2 stängd; steg 7 utbyggt med M1–M3 |
| `boot-runtime-timeline.md` | **aktuell** | ny 2026-08-11 | reset → runtime, ansvarsfördelning ROM/RAM | dynamisk PC-profil, IRQ6-källor, regionklassade övergångar |
| `call-graph.md` | **aktuell, coverage-kvalificerad** | ny 2026-08-04, rättelse 2026-08-25 | kontrollflödesmodell och CSV-schema; inte komplett firmware-callgraph | ur `call-graph-edges.csv`, `routines.csv`; BSR/BRA/Bcc/PC-relative/indirekt/TRAP saknas i extractionen |
| `display-protocol.md` | **aktuell, funktionellt avgränsad** | runtimeinventering 2026-08-24, statusnot 2026-08-25 | DUART/displaybyteprotokoll, cursor, attribut och öppna UI-gränser | verifierade mekanismer är inte en komplett UI-modell |
| `front-panel-model.md` | **aktuell** | manual-, runtime- och källkodsinventering 2026-08-25 | layoutoberoende semantisk panelmodell, kontrollgrupper, verifierade råkoder, host-keymap och displaygräns | normerande underlag för framtida display-, racklayout- och host-control-faser |
| `e2-address-model.md` | **aktuell** | riktad adressrevision | `$FFxxxx`-mekanismer och `$FC5803` | skiljer verifierad decode från speglingshypotes |
| `es5701-wiring.md` | oförändrad | i trädet | ES5701-koppling | — |
| `hardware-map.md` | **aktuell** | uppdaterad 2026-08-04 | fysiska komponenter | chip-select-tabellen tillagd; två rättelserutor markerar föråldrade `$FC6816`- och SCSI-avsnitt som historik |
| `instrument-to-otto-runtime.md` | **aktuell** | runtime-/firmwareanalys | voice table till ES5506 | runtime voice boundary |
| `interrupt-topology-gaps.md` | **aktuell med historisk runtime-tabell** | dynamisk vektor/IACK-inventering | SCC/PB/IDMA/Timer2-källor och modellluckor | pre-engine SCC/IDMA-rader kvalificeras av freeze-noten; PB-fynden består |
| `mc68302-status.md` | **aktuell med historiska modellgränser** | ny 2026-08-04, freeze-not 2026-08-23 | SIB/CP/PIO/timers per block | statisk census består; äldre `[OPEN]` för SCC/IDMA ersätts av smal current-model-verifiering |
| `memory-map.md` | **aktuell** | ersatt 2026-08-04 | adresser och avkodning | runtime-delen **flyttad** till `runtime-service-model.md` |
| `methods-hypothesis-management.md` | **aktuell** | ny 2026-08-22 | normerande hypoteshantering, evidensstatus och revisionsspår | formaliserar projektets etablerade mät- och rapportdisciplin |
| `methods-static-analysis.md` | **aktuell** | ny 2026-08-04, uppdaterad 2026-08-25 | metoder, 68000-prefetch och callgraph-coverage | PC-korrelation är stark evidens men tvetydiga flerords-/branchfall kräver extra witness |
| `movep-library.md` | oförändrad | i trädet | MOVEP-thunkar i DPRAM | — |
| `os-code-extraction.md` | **aktuell** | uppdaterad 2026-08-04 | RAM-adress → disk | segmentreglerna tillagda |
| `os-image-layout.md` | **aktuell** | ny 2026-08-04 | diskformat, segmentregler | — |
| `panel-manual.md` | **aktuell** | panelprotokollanalys | panelens manuella och emulerade gränssnitt | — |
| `rom-os-abi.md` | **aktuell** | ny 2026-08-04 | arkitektur, bindningstabellen | ur `os-binding-table.csv` |
| `runtime-object-model.md` | **aktuell med historiska gränser** | firmwareobjektanalys | storage-, sample-, instrument- och voice-ägarskap | vissa pre-IDMA runtimegränser läses via `current-status.md` |
| `runtime-service-model.md` | **aktuell** | utbruten 2026-08-04 | dispatcher-kö, servicefält, V1.61-observationer | programmatisk split av gamla `memory-map.md`, historiken oförändrad |
| `scc-board-source-question.md` | **aktuell med historiska no-input-pass** | servicehandbok, schema och sample-mode-test | fysisk keyboardlänk och konkurrerande SCC-hypoteser | samplingens PCM-konsument är verifierad; direkt pin/glue och separat keyboardanvändning `[OPEN]` |
| `scc-hardware-gap.md` | **aktuell med historisk pre-engine-gräns** | statisk + dynamisk SCC-analys, addendum 2026-08-23 | descriptorer, register, handlers, IDMA-konsument och modellomfång | firmware-/descriptorvägen består; current-model-engine i senare investigations |
| `scsi-operation-example.md` | **aktuell** | firmwareanalys | konkret SCSI-operation och `$0402` | — |
| `storage-completion-dispatch.md` | **aktuell med historiska runtimegränser** | firmwareanalys | vektor `$4B`/`$51` och `$0402` | aktuell implementation i `current-status.md` |
| `subroutine-index.md` | **aktuell** | uppdaterad 2026-08-25 | namngivna rutiner och försiktigt statusmärkta runtimeadresser | `$007830`/`$007CA8` PC-korrelerade; `$007E24` execution verifierad men semantik öppen; `$007C7C` inte entry |
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
| `routines.csv` | generated-static-data | **handkurerad** | 25 | `3aee116585356b06` |
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

## `investigations/` — 85 Markdown-filer (räknat om 2026-08-25)

Experimenthistorik och aktivt drivna frågor. Antalet är inventerat från trädet,
inte en permanent invariant. Filerna kan innehålla ersatta claims; deras README
pekar uttryckligen på `current-status.md` och `reference/` som aktuell sanning.
Senaste tillägget är `slot5-pc-correlation-and-atrk-slot-table.md`: kontrollerar
Slot 5-fyndet mot §8.10 sedan `$007C7C` visade sig aldrig PC-matcha —
kopplingen står kvar, PC-korrelerad denna gång, men den verkliga
måladressen är `$007CA8`, inte `$007C7C` (44 byte in i samma block);
rättar alla tidigare slutsatser som citerade fel adress. Förklarar
`$001098`-tabellens cykling i B som normalt beteende (3 riktiga
instrument mot bankfilens egen skivinnehåll, 8 fasta
instrument/spår-knappar enligt manualen) i stället för en bugg.
Effektpresettabellens skrivare undflyr adressbaserad write-tapping
(en riktig metodlucka); kontrollkörningen `ATRK`→`TUTORIAL BNK`→`ATRK`
försöktes seriöst men blockeras fortfarande av en bankladdning som
permanent låser filbläddraren till Seq/Song-läge, beskrivet exakt i
stället för löst. Röstlivslängder mätta som siffror i båda fallen men
inte urskiljande utan en `CR`-bitavkodning denna uppgift inte hann med.
Föregående tillägg var `transport-ab-test-play-stop-continue.md`: `$1D`=Play,
`$17`=Stop/Continue bekräftade dynamiskt via ES5506-registeraktivitet
(stopp till noll, återupptagning med nyprogrammerade röster); `$17`s
"CREATE NEW SEQUENCE" står kvar som ett eget, giltigt sammanhang —
kontextberoende, inte en felaktig attribution. Detta verifierar transport,
sequencer execution och hörbart ljud, **inte** musikalisk/audio-korrekt
playback; ATRK-fallets klickiga/felaktiga ljud är projektets högst
prioriterade funktionella `[OPEN]`. `$007C7C` visar sig inte
PC-bekräftat exekvera under uppspelning (374 träffar, noll `matched`);
den verkliga upprepade läsaren är `$007E24`. En platstabell vid `$001098`
skiljer fallen exakt: A har en distinkt referens per plats och tomma
oanvända platser, B har alla platser fyllda men cirkulerande genom bara
3 distinkta referenser — deterministiskt, inte historieberoende. B:s
aktiva röster delar bara 4 sampelregioner över ~18 röster (9-faldig/
3-faldig dubblering); A:s är olika. En effektpresettabell finns efter
första `ATRK`-laddningen och är nollställd efter en andra — en verklig
ledtråd, ofullständigt kontrollerad denna uppgift. Enskild
instrumentladdning bekräftad: `"PICK IN5TRUMENT BUTT0N"`, samma
filbläddrare som bankladdning, ett extra platsval. Föregående tillägg
var `call-graph-intersection-and-rom-string-search.md`:
använder `call-graph-edges.csv` som mängdoperation (panel-nåbart ∩
sekvenserar-nående) — snittet är tomt, med och utan de 1404
spegelkanterna, förklarat av att panelklustret enbart använder
`bsr`/`bra`/`jmp (An)` internt och att `$F8F2FA` nås via `TRAP #9`, inget
som grafens egen byggmetod (absoluta jsr/jmp-operander) kan se. Hittar
tio ROM-strängar/fragment (`GPR MONITOR`, `TEMPO`, `" BARS - KEEP
TRACK?"` m.fl.) som osammanhängande, nolltermineraded fragment i
`asr10.bin` — inga i någon diskavbild — och visar att menyrader byggs
ihop vid visning, inte lagras hela. Noll absoluta referenser till någon
strängadress hittades, strukturellt förväntat för en indexbaserad
fragmenttabell, inte ett osökt hål. Redan-tillämpad §8.10-granskning
(huvudagenten, eftersom den tidigare körningen ingick i den reverterade
fork-commiten): nedgraderar `$00E66E`/`$0073A8` och typ-`$0E`s
`$006014`-landningsben till `[OPEN, prefetch-osäkert]`; `$F8C588` och
`$00A304` orörda. Föregående tillägg var
`note-velocity-structure-and-sequencer-silence.md`:
fångar och rättar en egen felslutsats (Slot 5s `$782A` "83 Hz-poll" var
en CPU-prefetch-artefakt från en intilliggande ovillkorlig gren, inte
en verklig kontroll) innan den rapporterades som fynd, identifierar
strukturen Slot 5 faktiskt läser (`$000D08`=velocity, `$000D11`=not,
två vanliga globala byte, värdebekräftade mot kända konstanter),
hittar skrivarna, och bekräftar att sekvenserarens `$17`-kedja aldrig
skriver dit — strikt avgränsat till just den vägen, inget påstått om
uppspelning i stort. `$00A304`s rutin bekräftas aldrig exekvera i något
nått tillstånd. Föregående `slot5-connects-notes-to-voice-programming.md`:
läser alla sex schemaläggarslottars identitet (inte bara beläggning) —
Slot 5 (`$00780C`) kopplades till ett once-per-note-förlopp. Två senare
rättelser gäller när fyndet bärs vidare: `$00782A`s ~83 Hz-läsning var
prefetch, inte execution, och det verifierade PC-korrelerade anropet är
`$007830` -> `$007CA8`, inte `$007C7C`. Nollresultatet gällde bara den
specifika `$17` sequence-creation-kedjan och säger inte att aktiv
sequencer-playback är tyst. Journalen hittar dessutom för
första gången på tre försök en verklig ROM-referens till
diagnostiksträngarnas adress (`$00A304`, en gränskontroll mot exakt
`$101C`-`$103C`) via statisk ROM-sökning i stället för live-tappar —
den semantiska kopplingen obekräftad, men en konkret ny adress att gå
vidare från. Föregående `trap-c-and-the-real-note-path.md`: disassemblerar
TRAP #C ($F88174) och visar att dess egen data i tempokedjan är Slot 3s
självupparmning (typ $0E, nedräknande fält), inte sekvensinnehåll.
Spårar sedan bakåt från bekräftade ES5506-skrivningar under en verklig
tangenttryckning och lokaliserar blocket `$007C7C`-`$007CEE` — en
fristående kodregion som aldrig
förekommer i någon TRAP #9/#C-registerfångst. Slutsats: tempokedjan och
den verkliga notvägen är två orelaterade delsystem, inte en pipeline
med en grind någonstans i mitten. Senare PC-korrelation korrigerar
entryn till `$007CA8`; `$007C7C` är `[DISPROVEN]` som exekverande entry
och läses som data av `$007E24`. Föregående
`execution-traced-clock-and-sequencer-stepper.md`:
löser `$000F58`-motsägelsen med exekveringstappar ($F8C588 nås aldrig,
0 träffar mot en validerad 1000 Hz-kontroll; den verkliga kedjan
installerar i Slot 3, `$F8F2FA`, inte Slot 2 som förra rundan antog),
hittar en verklig, oberoende sekvenserarstegare (`$F902D8`, körs exakt
vid pulstakten, ankrad på `$001098`, kedjar till `$F91F00`, avslutas i
ett tidigare odokumenterat `trap #c`), mäter `$00E66E`s grind som
enbart MIDI-klocka (ej kopplad till stegaren), och nedgraderar
`$00017E` efter att både skrivare och läsare identifierats som enbart
boot-interna. Diagnostikmenyns bakåtspårning blockeras av samma
ROM/RAM-övergångsväxling som redan dokumenterats. Föregående
`command-pages-and-clock-verdict.md`: bekräftar `$06`=
Command (Per: "$06 + $0C" gav "NO COMMANDS ON PAGE") och katalogiserar nio
fullständiga kommandosidor ordagrant — GPR MONITOR/ESP TESTS finns i ingen av
dem. Tre förutsagda tal prövades och höll inte: `$00828E` som mastertempo
återkallas (matchar inte det uppmätta `$B70`-värdet), den förväntade 36 Hz
MIDI-klockan uteblir (andra klockdelningssteget är permanent stängt i alla
lägen sessionen kunde nå, verifierat två oberoende vägar), och `$D42`-listan
bekräftas läst tom genomgående (utfall 2: en orelaterad servicelista, inte
sekvenserns händelsedispatch). Bygger ett nytt Lua-bibliotek,
`lua/lib/asr10_taps.lua`, som gör SS8.6-misstaget (osparade tapp-handtag,
som bet till två gånger) strukturellt svårare att upprepa. Föregående
`tempo-clock-consumer-chain.md` verifierar temposhärledningen oberoende
(tickfrekvens exakt 1000,0 Hz, pulsfrekvens exakt 144,0 Hz) och spårar
pulskonsumenten två nivåer djupt första gången. Den exakta fysiska
RAM-dekodningen från `stereo-ram-decode-analysis.md` förblir öppen.

## `archive/` — 28 filer

Avslutade eller ersatta berättelser, samt projektets handoffdokument enligt etablerad
konvention (`asr10-handoff-*`, `asr10-mame-handoff-*`).

| path | kategori | status | beskriver |
|---|---|---|---|
| `asr10-handoff-2026-08-04.md` | archive | **historisk** | handoff efter konsolideringen; dess `./mess`, blockerare och arbetsordning är passerade |

De 27 tidigare filerna är provenance. `sources/` inventeras i dess egen README.
Den generella `asr10_trace.lua` ligger i `lua/archive/`; aktuella regressioner och
verktyg listas i `lua/README.md`.

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
