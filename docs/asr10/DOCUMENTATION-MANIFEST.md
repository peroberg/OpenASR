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
| `display-protocol.md` | **aktuell, state-machine-implementerad** | runtimeinventering 2026-08-24, byte/state-analys och implementation 2026-08-26 | Channel-B-protokoll, ASR-ägd cursor, `$62/$63` selected-field-state, attribut, outputregister och öppna frameklasser | TEMPO `90 -> 91 -> 90` firmwareverifierad; `$67`, `$74-$76` och övriga OPEN-klasser kvarstår |
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

## `investigations/` — 116 Markdown-filer (räknat om 2026-08-29)

Experimenthistorik och aktivt drivna frågor. Antalet är inventerat från trädet,
inte en permanent invariant. Filerna kan innehålla ersatta claims; deras README
pekar uttryckligen på `current-status.md` och `reference/` som aktuell sanning.
Senaste tillägget är `audio-system-frame-model-v350.md`: en
arkitektur-först-syntes av ES5506/ES5510/ES5701-specifikationerna, den lokala
ES5701-VHDL-filen och V3.50:s verifierade A/B/A. Den fastställer ES5506 som
audio-frame-rate-gränsen och en mode-samordnad firmwarekonfiguration, men
lämnar den fysiska ASR-klockans producent och route `[OPEN]`; ingen ACTV-,
ES5701-, PB3- eller oscillatorpolicy implementeras. Föregående
`fmfx-serialized-effect-control.md`: korrigerar den
angivna men frånvarande `.efe`-sökvägen till den faktiska read-only
`FMFX.hfe`-disken, verifierar dess `$0018` EPS-16 Plus fristående
FM+FX-effectobjekt och läser det strukturellt alignade fönstret
`+$62..+$6A=00 00 72 73 00 00 00 00 00`.  Den sista bounded
`$0018`-kontrollen finner att alla fyra artifact-namngivna `44K`-leads i
`WBFX38` är `$0003` Instrument-containrar, inte standalone effects, och
att inget explicit 30k-fall finns lokalt.  `$00` är därför endast en
rate-okänd observation och hela serialiserade `+$66`-grenen är **PARKED**,
inte uppgraderad till en 30-kHz-kontroll.  Föregående `waveboy-hfe-effect-rate-controls.md`: kvalificerar två
read-only WaveBoy HFE-artifacts, dekodar dem med befintlig MAME-`floptool` till
EPS-16-format och inventerar 55 type-`$0003` Instrument-containrar. Den
oberoende Tempo Sync'd Delays 30/44-kHz-pardokumentationen kan inte knytas till
en lokal container, så inget container-`+$66` läses som effectfält. Föregående
`effect-serialized-30k-controls-v350.md`: gör en
deduplicerad artifact-inventering av V1.61/V3.50 och fastställer att de tolv
44-kHz-effectfilerna är byteidentiska mellan bilderna; ingen kvalificerad
serialiserad 30-kHz-kontroll finns lokalt. Därför lämnas den binära
`+$66`-klassificeraren och explicit sample-rate-semantik OPEN. Föregående
`effect-serialized-mode-byte-differential-v350.md`:
läser `+$66` direkt från samtliga tolv lokalt tillgängliga V3.50 type-`$0021`
effectfiler.  Musikerhandbokens oberoende Version-2-klassificering anger hela
denna kohort som 44 kHz, och samtliga tolv har `$01`; den saknar dock en
serialiserad 30-kHz-kontroll och uppgraderar därför inte bytefältet till en
binär rate-classifier eller explicit sample-rate-semantik. Föregående
`effect-object-filesystem-provenance-v350.md`: mappar
44LUSH-objektets redan transportverifierade bytes `$82/$83/$01` från
`C76/H0/R15`, sektoroffset 100/101/102, till block `$0BEE`, V3.50-katalogens
type-`$0021` `44LUSH PLATE`-post och exakt file offset `+$64..+$66`.
Den direkta serialiserade proveniensen är verifierad för det fönstret, men
fältssemantik är fortsatt OPEN. Föregående
`effect-object-floppy-byte-provenance-v350.md` sluter
den nuvarande MAME-modellens transportkedja för 44LUSH-objektets bytes
`+$64/+65/+66=$82/$83/$01`. FDC READ DATA `$46`, drive 0/head 0 och verifierad
C/H/R/N `76/0/15/2` levererar sektoroffset 100/101/102 genom extern IDMA till
`$0062B664..666`; grannkorrelationen visar bytebevarande över samma sektor.
Detta är inte fil-/record-/fältssemantik: den frågan är nästa avgränsade domän,
och construction class är fortsatt OPEN. Föregående
`effect-object-install-producer-v350.md` identifierar
den nuvarande modellens producent av 44LUSH-objektets sekventiella
installationsström som MC68302 extern IDMA. FDC:s `dma_r()`-byte `$01` når
DAPR `$0062B666`; `$82/$83/$01` korrelerar över `+$64/+65/+66`. Detta är
inte i sig en filformatmappning, så serialiserad direktkopiering,
ROM/OS-transformering och runtime-härledning är fortsatt öppna. Föregående
`effect-mode-construction-semantics-v350.md` korrigerade den för snäva
handoff-slutsatsen efter destinationstappen.
`effect-mode-object-construction-v350.md` bevaras som den smalare
destinationstappens provenance och dess
stoppgräns, inte som aktuell slutgräns för konstruktionsemantiken.
Föregående tillägg är `effect-mode-metadata-source-v350.md`: bekräftar att
`$0E92` pekar direkt på current-effect-objektet och att dess byte `+$66`
kopieras till `$0CE3`: ROM HALL `$FFF9B68C=$00`, 44LUSH
`$0062B666=$01`, sedan ROM HALL `$00`. Detta är direct effect-metadata
provenance, inte belägg för explicit sample-rate-/clock- eller ES5701-semantik.
Föregående tillägg är `pitch-mode-d2-source-v350.md`: följer endast D2 som
skrivs till `$0D66` och når en direkt, lokal `$0CE3`-test/branch. Zero väljer
`#$8DF5`, nonzero `#$94C3`, och `$F8CCD6` persisterar resultatet. Detta binder
effect-selected mode till den globala pitch-offsetkedjan utan att påstå fysisk
rate/clock-, ACTV- eller ES5701-semantik. Föregående tillägg är
`voice-pitch-mode-source-v350.md`: följer den lokala
note-setupen `$007962..$007984` ett steg uppströms och isolerar `$0D66` som
dess enda reversibla indirekta mode-source. `$F8CCD6` skriver
`$8DF5 -> $94C3 -> $8DF5` före notes; `$007980` subtraherar värdet och ger
exakt -1742 i voice `$86(A4)` för både 3C och 3D. D2/branchprovenance, direkt
`$0CE3`/ACTV-koppling och fysisk clockrouting lämnas uttryckligen open.
Föregående tillägg är `pitch-mode-additive-input-v350.md`: avgränsar den första
direkta reversibla mode-termen före `$F8D33E -> $0D80` till voice-record
`$86(A4)` (observerad `$815E`). Den skrivs vid note setup som
`$B5B0 -> $AEE2 -> $B5B0`, adderas direkt till D0 och ger exakt -1742
pitch-units för två noter; en oberoende tvånotesmätning ger 256 units/semiton
och den implicita ratio 0.674992 ligger inom 0.0177% av 29.7619/44.1. Detta är
inte en påstådd fysisk clock- eller ES5701-väg; writer-inputens provenance är
fortsatt open. Föregående tillägg är
`pitch-engine-rate-mode-provenance-v350.md`, som når `$0D80` som första
persistent modeberoende pitchboundary, och därefter
`voice-fc-rate-mode-differential-v350.md`: visar i en successful ROM-HALL ->
44LUSH -> ROM-HALL A/B/A att samma uppmätta JM-DIGI-voice får ett reversibelt
FC-intervall vars mittpunktsratio ligger inom 0.03% av 29.7619/44.1, medan
CR/bank/START/END/ACCUM är lika. Detta är firmware/current-MAME
voice-programming, inte fysisk klockrouting. Föregående tillägg är
`audio-rate-model-implementation-v350.md`: avgränsar och
återställer ett ACTV-drivet effective-clock-experiment. MAME:s runtime-
clock-API fungerar, men en oacceptabel och oförklarad known-note-förändring
262.3 -> 196.7 Hz falsifierar ACTV ensam som tillräcklig ASR-policygräns;
ingen kod lämnas kvar. Föregående tillägg är
`audio-frame-timing-actv-differential-v350.md`: mäter den
verifierade ROM HALL REVERB -> 44LUSH PLATE -> ROM HALL REVERB-transitionen.
Den visar invariant syntetisk PB3 vid 44.1k toggles/s (22.05k fullcykler/s),
medan ACTV `$1F -> $17 -> $1F` byter generisk ES5506-stream-rate
59,523.789 -> 79,365.052 -> 59,523.789 Hz. Det är en
current-MAME-modellgräns, inte fysisk clock-routing. Föregående tillägg är
`analog-selector-control-map-v350.md`: kartlägger hela den
funktionella PBDAT/PAR-domänen 0-7. Selector 1 är en verifierad ASR-88-villkorsväg
med `[Likely]` mono/channel-pressure-semantik; selector 6 är
`[Verified unreachable]` i den avgränsade V3.50-acquisitionvägen. Journalen
verifierar även MR. KNOB = Data Entry, avför Input Level från denna scan,
karakteriserar selector 7 som kalibreringsreferens och specificerar rått
10-bitars board-callbackkontrakt. Kontraktet är nu implementerat och
runtime-verifierat med `lua/analog_pot_wiring_verify.lua`: den generiska
MC68302 PBDAT-latchen går till ASR-maskinens selector-mux och vidare till
generisk ES5506 PAR, utan Input Level, selector-6-semantik eller klassificerad
selector-1-producer. Föregående tillägg
är `analog-control-acquisition-v350.md`: verifierar en
kontinuerlig V3.50-kedja PBDAT PB2-PB0 -> cirka 1,98 ms -> ES5506 PAR ->
kontrollspecifika RAM-block, 500 läsningar/s både idle och diagnostik. Visar
att `EXAMINE ANALOG INPUTS` endast väljer viewer-index/RAM-cell, kartlägger
0=PITCHWHL, 2=MODWHEEL, 4=PEDAL, 3=VOLUME, 5=MR.KNOB, 7=REFRENCE, och
graderar U55 HC4051 till `[Likely acquisition mux]` men lämnar fysisk pinrouting
`[OPEN]`. Korrigerar dessutom dagens DUART-indexerade PAR-callback som plumbing,
inte firmwarevald kanalmodell. Föregående tillägg är
`display-stability-workflows-v350.md`: verifierar verkliga
V3.50-flöden för TEMPO/BAR, LOAD, Command/Master Tune, Edit Instrument, REC SRC,
VOLUME och annunciator+clear utan cursor-drift, stale glyphs eller state leakage.
Statusen är uttryckligen "workable/stable for navigation", inte fullständigt
förstått protokoll. Föregående tillägg är
`display-protocol-state-machine-v350.md`: fångar
filbrowser, REC SRC, VOLUME, FX och EDIT SEQUENCE/TEMPO byte för byte med live
renderer-/shadow-state. Verifierar `$62` som selected-field-anchor och `$63`
som field-relative partial rewrite; implementationen ägs nu av
`asr10panel_device` och TEMPO round-trip-regressionen är grön. Den ursprungliga
analysen förklarar det tidigare appendfelet utan firmwarehack. Rättar dessutom
THRB-adressen
`$FC480D` -> `$FC4817`, nedgraderar `$74-$76` från nibble-only animation till
`[OPEN]` one-operand panel-control/output (bl.a. `$74 $40`), och sparar exakt
TEMPO-fixture plus deterministisk replay och firmwaretest. Föregående
tillägg är `slot5-pc-correlation-and-atrk-slot-table.md`: kontrollerar
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
