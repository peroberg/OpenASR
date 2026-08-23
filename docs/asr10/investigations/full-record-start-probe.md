# Full RECORD/start probe

Datum: 2026-08-22. Firmware/media: V3.50. Observation i Lua; ingen
maskinmodell ändrad.

## Slutsats

**[Verified runtime]** Den dokumenterade samplingssekvensen nådde
`Enter-Yes` från Level-Detect. Firmware accepterade startkommandot och visade
rått `WAITING...272 5EC LEFT` (`5/S` är samma kända segmentambiguitet; manualens
text är `WAITING...272 SEC LEFT`). Där stannade sekvensen under hela det
12 sekunder långa observationsfönstret. `RECORDING` nåddes inte och ERROR
005/006 visades inte.

SCC1/SCC2 konfigurerades om tre gånger när den oladdade instrumentslotten valdes
och Level-Detect byggdes upp. Efter att Level-Detect stabiliserats skedde inga
ytterligare SCC-, descriptor- eller IMR-skrivningar vid tröskeländringen eller
`Enter-Yes`. Alla 16 descriptorer, deras längder och de 12 800 bufferbytena var
oförändrade genom `record_start+12s`. Level-4 IACK, descriptorbuffer-skrivningar
och sample-RAM-skrivningar var samtliga noll med levande positiva vittnen.

Den smala förutsägelsen "RECORD/start ger omedelbart ERROR 005/006 i den
nuvarande modellen" är därmed `[DISPROVEN]` för denna sekvens. De bredare
identitetshypoteserna `SCC <-> audio input` och `SCC <-> keyboard` förblir
`[OPEN]`: inget RX-event eller tröskelöverskridande inträffade, så försöket nådde
inte ett tillstånd där deras förutsägelser skiljer sig.

**Senare rättelse, 2026-08-23:** detta dokument kallade först BTN_0A-ändläget
"minimum threshold" utifrån MAME:s hostetikett `Down`. En efterföljande live-tap
visade att BTN_0A i stället ökade `$017C` från 2 till det verifierade maxvärdet
20. Fasnamnet `threshold_min` nedan bevaras endast som råprovens historiska
etikett. Se `waiting-exit-condition.md`.

## Reproduktion

Probe: `docs/asr10/lua/archive/full-record-start-probe.lua`.

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 60 \
  -autoboot_script docs/asr10/lua/archive/full-record-start-probe.lua
```

Ingen `-log` användes. Den slutliga råkörningen sparades temporärt som
`/tmp/asr10-full-record-start-final.log`:

```text
log SHA-256    0fca66651965a64179bde530c1c51b19544098e3bf51d0605954550e76eaff66
probe SHA-256  843d4396c08c54a8cd933dd5b8128da48adbcd471f9528bb1447561f5a2fe87c
V350 SHA-256   2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b
mame SHA-256   9b7db6d1953ba0ef9bcf5e900dea6be469f4570eb4dd895d5e5c7c448aa16b30
```

Proben installerar CPU-space-IACK-tappar från reset. SIB-tapparna installeras
först vid `FILE 1`, efter den sista kända BAR-omläggningen. Alla tap-handtag
hålls levande i en top-level-tabell.

## Exakt sekvens och stoppunkt

Sekvensen följer `ASR10_manual.pdf`, "Easy Sampling", sidorna 144-147:

| tid | kontroll | observerat tillstånd |
|---:|---|---|
| 16.300 s | `BTN_20`, Sample-Source Select | `REC SRC=INPUTDRY LEFT` |
| 16.880 s | `BTN_02`, oladdad Instrument 1 | blank -> `SHUFFLING DATA` -> Level-Detect-glyfer |
| 17.960-20.840 s | `BTN_0A` x24, hostetikett Down | `$017C` drivs till maxändläget 20; inga nya glyphändringar efter tryck 10 |
| 20.840 s | `BTN_23`, Enter-Yes | `WAITING...272 SEC LEFT` vid 20.860 s |
| 32.920 s | 12 s efter start | samma `WAITING...272 SEC LEFT` |

Den råa slutraden var:

```text
RECORD_FINAL t=32.920000 phase=record_start pc=F87FA2
  display="WAITING...272 5EC LEFT"
  glyphs=2436,00F7,0309,0301,0309,2836,00BD,4000,4000,4000,
         00DB,0007,00DB,0000,00ED,0079,0039,0000,0038,0079,0071,0301
```

`$00ED` är displaydekoderns dokumenterade `5/S`-ambiguitet. Texten rapporteras
därför både rått och manualnormaliserat; den råa raden har inte skrivits om.

**[Verified runtime]** Firmwarestoppunkten är standby-läget `WAITING`, inte ett
fel och inte `RECORDING`. **[Likely]** Den enklaste förklaringen är att modellens
insignal aldrig överstiger tröskeln; manualen säger uttryckligen att firmware
stannar i `WAITING` tills detta sker. Proben mäter ingen analog amplitudkälla och
uppgraderar därför inte orsaken till `[Verified]`.

## SCC1/SCC2 och descriptorer

Vid alla stabila snapshots (`baseline`, `sample_source`, `level_detect`,
`threshold_min` [historiskt felmärkt fasnamn; faktiskt maxläge], start +200 ms,
+1 s, +5 s och +12 s):

```text
SCC1/SCC2: SCON=7000 SCM=703B DSR=0000 SCCE=FF00 SCCM=0500
            MRBLR=0320 RBPTR=0000 CURRENT=0000 RXTMP=0000
GIMR=8040 IPR=000B IMR=E480 ISR=0080 SIMODE=4189
```

Descriptorlayouten var byteidentisk med tidigare mätning:

| kanal | index | status | längd | buffer |
|---|---:|---:|---:|---:|
| SCC1 | 0-6 | `$D000` | `$0000` | `$F76600` ... `$F778C0`, steg `$320` |
| SCC1 | 7 | `$F000` | `$0000` | `$F77BE0` |
| SCC2 | 0-6 | `$D000` | `$0000` | `$F74B00` ... `$F75DC0`, steg `$320` |
| SCC2 | 7 | `$F000` | `$0000` | `$F760E0` |

När `BTN_02` byggde Level-Detect skedde tre fulla descriptoromskrivningar per
kanal, 102 word-skrivningar vardera. Samma fas innehöll fem avstängningar
(`SCM1/2=$7033`, `IMR=$C080`) och tre återarmeringar
(`SCM2/1=$703B`, `IMR=$E480`). Den sista återarmeringen var klar vid
17.143116 s. Därefter, genom startkommandot och 12 s `WAITING`, observerades
inga fler relevanta skrivningar.

Detta avgränsar den tidigare korrelationen: SCC-arbetet hör till uppbyggnaden av
Level-Detect i denna körning. `Enter-Yes`/RECORD-start gav ingen separat
SCC-reaktion.

## Data, IACK och levande vittnen

```text
descriptor_writes_scc1=102 descriptor_writes_scc2=102
buffer_writes_scc1=0 buffer_writes_scc2=0
sample_ram_writes=0
level_4_iack=0
level_6_iack=18411, vector=56
ram_witness_writes=1369215
```

- Varje 6 400-byte-kanalbuffer hade `nonzero=0`, bytesumma `$00000000` och
  `changed_from_baseline=0` vid samtliga åtta snapshots.
- CPU-space-tappen såg 18 411 level-6-IACK med vektor `$56`, varav 12 110
  under `record_start`, medan samma tap såg noll level-4-IACK. Detta är det
  levande vittnet för IACK-nollresultatet.
- Sample-RAM-tappen över `$100000-$1FFFFF` såg noll skrivningar efter `FILE 1`.
  En samtidigt installerad syskon-tap över aktivt RAM såg 1 369 215 skrivningar.
- Descriptorbuffer-tappen såg noll skrivningar. Descriptorernas egna SIB-tappar
  såg samtidigt 204 skrivningar och alla snapshots läste samma bufferadresser;
  proben var alltså aktiv under det relevanta fönstret.

Ingen träff sågs i `scc_rx_common`, `$006482` (005-vägen) eller `$006492`
(006-vägen). Fem anrop till `$F8C0E6`/`scc_disable_both` under Level-Detect
korsbekräftas av fem konkreta skrivsekvenser från PC `$F8C0F4/$F8C0FC/$F8C106`;
inga sådana anrop skedde i `record_start`.

## Hypotesrevision

### Omedelbart 005/006 vid RECORD/start

Hypotes: Den nuvarande modellens saknade audioingång ger omedelbart ERROR
005/006 när `Enter-Yes` startar sampling.

Förutsägelse: Felvägen `$006482` eller `$006492` exekveras och displayen visar
005/006 efter startkommandot.

Övergivandevillkor: Startkommandot når ett annat stabilt firmwaretillstånd utan
kodväg eller feltext.

Evidensdomän: V3.50 i nuvarande MAME-modell.

Observation: `WAITING...272 SEC LEFT` var stabilt i 12 s; noll träffar på båda
felvägarna; descriptorstatus var oförändrad.

Tidigare status: `[OPEN]`. Ny status: `[DISPROVEN]` för denna smala, omedelbara
förutsägelse.

Överlevande alternativ: 005/006 kräver det redan statiskt verifierade
ringkonfliktvillkoret och behöver inte vara en no-input-timeout; ett senare
tröskelöverskridande kan nå en annan väg.

Nästa diskriminerande experiment: driv en verifierad signal över tröskeln utan
att ändra SCC-modellen och mät samma vektor.

### SCC bär audioingång

Hypotes: SCC1/SCC2 deltar i audioingångens datapath.

Observation: Level-Detect bygger om och armerar SCC-ringarna, men RECORD-start
ger ingen ny SCC-aktivitet, RX-data, descriptorförändring, IACK eller sample-RAM-
trafik. Sekvensen når inte `RECORDING`.

Tidigare status: `[OPEN]`. Ny status: `[OPEN]`.

Överlevande alternativ: SCC kan bära synk/control snarare än PCM; kan vara en
annan perifer väg som återanvänds vid sampling; kan sakna fysisk källa i modellen.

Övergivandevillkor: verifierad `RECORDING` med audio/sample-RAM-trafik men fortsatt
ingen SCC-aktivitet skulle tala starkt mot att SCC krävs för datapathen. Verifierad
PCM i ringarna skulle i stället uppgradera hypotesen.

### SCC bär keyboard

Hypotes: En eller båda SCC-kanalerna terminerar keyboardkortets länk.

Observation: Den nya RECORD-delen gav ingen identifierad byte och skiljer inte
kanalerna åt. Tidigare fysisk och timingbaserad evidens är oförändrad.

Tidigare status: `[OPEN]`. Ny status: `[OPEN]`.

Övergivandevillkor: en verifierad annan digital-board-terminering för keyboard-
länken, eller identifierad SCC-data med oförenlig semantik.

## PB9/PB10/PB11

Ingen ny PB9/PB10/PB11-källa eller level-4-IACK observerades. Deras status som
separata, avmaskerade modelluckor med riktiga handlers är oförändrad. SCC-resultatet
används inte för att omklassificera dem.

## Begränsningar och verifiering

- Ingen SCC- eller interrupt-controller-implementation.
- Ingen C++-, `mem_map`-, klock-, ES5510- eller bankändring.
- Ingen `-log`.
- Baslinjeregression före experimentet: 8/8 tester, 9 PASS-rader.
- Slutregression redovisas i commitens leveransnotering.
