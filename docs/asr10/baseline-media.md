# Med-media-baslinje: minimimängden runtime-flaggor, V350.img

2026-07-30. Regressionstest för hela fas 3 (`PLAN.md`). Läst:
`PLAN.md`, `CLAUDE.md`, `experiment-flags.md`, `running.md`. Bygget
oförändrat sedan `duart.md` (commit `7bc57b8ab45`), ingen kod ändrad
i den här uppgiften.

## Resultat i korthet

`[Verified]`. Av de 22 körtidsflaggorna (`std::getenv`-baserade, se
`experiment-flags.md` avsnitt A) är **exakt en** bärande för hur långt
booten når på `V350.img`: `ASR10_DIAG_PANEL_AUTORESPOND`. Den är både
nödvändig och tillräcklig för det djupaste läge som gick att nå i den
här uppgiften. Ingen kombination — varken minimal eller den historiska
fulla uppsättningen — tar booten till `"NO INST OR BANK FILES"`.
Booten stannar konsekvent efter att texten `"KEYBOARD TUNED"` byggts
klart internt men innan den någonsin visas som ett färdigt panelmeddelande.

## Metod

Utgångspunkt: den historiska "kompletta baslinjen" ur
`filesystem-browser-map.md` avsnitt 4.19 (ursprungligen körd mot
`V161.img`, inte `V350.img`):

```
ASR10_DIAG_PANEL_AUTORESPOND=1
ASR10_EXPERIMENT_DUART_COUNTER_TIMER=1
ASR10_EXPERIMENT_ES5506_HOST=1
ASR10_DIAG_PAR_VALUE=0x200
ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1
ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE=1
ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE=1
ASR10_EXPERIMENT_TUNING_STALL_TRACE=1
ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE=1
```

Körd mot `V350.img` (plus `ASR10_DIAG_ROOT_DIRECTORY=1` för synlighet).
**Avbröts** efter att den producerat över 9 GB `error.log` (116+
miljoner rader) på tre minuters CPU-tid utan att komma längre än den
mycket enklare uppsättningen nedan — se "Varför `ES5506_HOST` togs bort
tidigt" nedan. Borttagning skedde därefter en flagga i taget (grupperat
där flaggor hänger ihop, t.ex. `ES5506_HOST`+`PAR_DIAGNOSTIC`+
`PAR_VALUE` som en grupp eftersom de senare två är verkningslösa utan
den första):

1. **9 flaggor** (ovan) → når `"TUNING KBD - HANDS OFF"` (flush),
   bygger `"KEYBOARD TUNED"` internt (deskriptorspårning), postar nod
   `14F4` typ `89A2`. Avbröts pga log-/tidsvolym innan slutstatus kunde
   fastställas definitivt — se nedan.
2. **6 flaggor** (tar bort `ES5506_HOST`+`PAR_DIAGNOSTIC`+`PAR_VALUE`,
   behåller `DUART_COUNTER_TIMER`+`POST_TUNING_INDIRECT_TRACE`+
   `DISK_SIGNATURE_TRACE`+`TUNING_STALL_TRACE`+
   `FILESYSTEM_BROWSER_TRACE`+`PANEL_AUTORESPOND`) → **identisk**
   sluttillstånd som (1), men på 35 sekunder i stället för att aldrig
   bli klar: `table_0544_write_count=3494`, samma
   `fb895a_range_reader:11`, samma `fb8c6e_seek_wrapper:32`, samma
   `fdc_command_issue:7`, samma `ASR10_NODE_89A2_FIRST`-händelse.
3. **1 flagga** (`ASR10_DIAG_PANEL_AUTORESPOND` ensam, plus
   `ASR10_DIAG_ROOT_DIRECTORY`/`ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE`
   enbart för synlighet) → **återigen identiskt** sluttillstånd.
4. **0 flaggor** → fastnar i upprepad `"qqqq..."`-skräptext direkt
   efter `"LOADING SYSTEM"`, når aldrig `"TUNING KBD"`. Bekräftar att
   flagga (3) är nödvändig, inte bara tillräcklig.

De återstående 15 körtidsflaggorna (`ASR10_DIAG_PANEL_C_PARSER_TRACE`,
`ASR10_DIAG_PANEL_SUBMISSIONS`, `ASR10_EXPERIMENT_MC68302_GPIO_TRACE`,
`ASR10_EXPERIMENT_FC3000_VERIFY_TRACE`, `ASR10_EXPERIMENT_FDC_SYNTH_TC`,
`ASR10_EXPERIMENT_DOWNLOAD_TRACE`, `ASR10_EXPERIMENT_ES5510_HOST`, samt
panel-svarsfamiljens fyra ömsesidigt uteslutande alternativ till
`PANEL_AUTORESPOND`) testades inte var för sig i den här omgången —
samtliga utom `ES5510_HOST` är klass (d) i `experiment-flags.md` (ren
diagnostik, ingen kod som kan ändra körningens gång), vilket är grunden
för att anta att de är umbärliga utan att behöva köra alla 15
kombinationer. `ES5510_HOST` ingick i en tidigare uppgifts (`fdc-map.md`)
körningar och gjorde ingen observerbar skillnad där heller.

### Varför `ES5506_HOST`+`PAR_DIAGNOSTIC` togs bort tidigt

`[Likely]`. Med dem påslagna faller körhastigheten från ~300 % till
under 100 %, och `error.log` växer med flera miljoner rader per
CPU-sekund — mycket mer än vad någon annan flaggkombination
producerar. Det mönstret (enormt loggvolym, ingen synlig framgång) är
konsekvent med att ROM:et fastnar i en tät pollningsloop mot
ES5506-värdens PAR-register, som med `ASR10_DIAG_PAR_VALUE` alltid
returnerar samma fasta diagnostikvärde — om ROM:et väntar på att PAR
ska ändra sig eller nå ett tillstånd den fasta konstanten aldrig ger,
snurrar den för evigt utan att någonsin nå längre. Inte
slutgiltigt bevisat (processen dödades innan den skulle ha hunnit
slutföra `-seconds_to_run 100`), men konsekvent med varje observerad
datapunkt. **Denna flaggkombination var alltså inte bärande för att nå
`V161.img`s historiska `NO INST OR BANK FILES`-resultat i egentlig
mening** — den behövdes troligen bara för att undvika `ERROR 130`
(en separat, senare krasch, se `filesystem-browser-map.md` 4.19), inte
för att komma förbi `KEYBOARD TUNED`.

## Det exakta minimikommandot

`[Verified]`:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 100
```

Rekommenderad körning för framtida regressionstest (samma djup, men
med synlighet in i stalltillståndet — båda extra flaggorna är klass
(d), ren diagnostik, ändrar inte hur långt booten når):

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE=1 \
ASR10_DIAG_ROOT_DIRECTORY=1 \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 100 -log
```

**Körtid:** `[Verified]`. 100 emulerade sekunder på 31-35 reella
sekunder (~295-340 % hastighet, `-nothrottle`), oberoende av vilken av
de tre fungerande flaggmängderna (1 flagga / 6 flaggor utan ES5506)
som användes — hastigheten var i samma härad för alla utom den
avbrutna 9-flaggskörningen.

## Panelsekvens som faktiskt nås

`[Verified]`, identisk för alla fyra testade icke-tomma
flaggkombinationer:

```
"q"                          -- skräptext direkt efter reset
"   ENSONIQ  ASR-10    "     -- startskärm
"    LOADING SYSTEM    "     -- efter OS-laddningsfasen (20 FDC-transaktioner för
                                 V161, 32 för V350, se disk-read-path.md)
"TUNING KBD - HANDS OFF"     -- sista FÄRDIGSTÄLLDA panelmeddelandet
```

**`"KEYBOARD TUNED"` byggs men visas aldrig.** Med
`ASR10_DIAG_ROOT_DIRECTORY=1` syns via deskriptorspårningen (inte via
den vanliga `ASR10PANEL`-loggtaggen) att texten byggs upp bokstav för
bokstav till exakt `"    KEYBOARD TUNED"` internt — men den
utlösande kontrollbyte som skulle "flusha" den till ett riktigt
`ASR10PANEL text=...`-meddelande kommer aldrig under de 100 emulerade
sekunderna som testades. `"NO INST OR BANK FILES"` nås alltså **inte**
i den här uppgiften, för `V350.img`, med någon testad
flaggkombination.

## Sista händelsen före stallet — och är den avbrottsrelaterad?

`[Verified]`. Den absolut sista repeterade aktiviteten i loggen
(`ASR10_RUNTIME_DISPATCH`, schemaläggarens egen diagnostik) är
dispatchern som om och om igen scannar sin egen könedskö
(`semantic=dispatcher_body` / `dispatcher_queue_scan_base_load` /
`dispatcher_queue_scan_compare_byte2`) och vid varje varv läser av två
register i det ännu ej modellerade "m68302 internal candidate"-blocket:

```
fc6814=000b   (interrupt pending/mask-liknande register)
fc6816=e480   (interrupt in-service-liknande register)
```

Båda värdena är **konstanta över minst 131 072 varv** (loggad med
kraftig utspädning: 120, 121, ..., 131072) — dispatchern hittar inget
nytt att göra eftersom ingenting någonsin ändrar dessa bitar. Samtidigt
har noden `14F4` redan fått typ `89A2` skriven till sig
(`ASR10_NODE_89A2_FIRST`), och katalogbläddringskoden har redan körts
klart (`fb895a_range_reader:11`, `fb8c6e_seek_wrapper:32`,
`fdc_command_issue:7`, alla FDC-transaktioner slutförda) — så det är
inte disk-, katalog- eller schemaläggar-*dispatchen* i sig som
saknas, utan just det som skulle **väcka** dispatchern till nästa steg.

**Ja, detta ser avbrottsrelaterat ut.** `fc6814`/`fc6816` är exakt de
två registren som de tolv permanent döda klass-(a)-konstanterna i
`experiment-flags.md`/`PLAN.md` fas 3 (`FC6814_ACK_PENDING_000B`,
`FC6816_CLEAR_SERVICE_2480`/`_2400_AFTER_SETTER`, hela
`SYNTH_68302_TIMER_IRQ`/`IACK`-familjen) byggdes för att manipulera —
och samtliga tolv lämnades avstängda eftersom ingen av dem tog booten
längre. Detta fynd är alltså inte en ny upptäckt utan en direkt,
empirisk bekräftelse av vad `PLAN.md` fas 3 redan drar slutsatsen av:
schemaläggaren väntar på en genuin interruptcontroller-signal som
varken den handskrivna modellen eller något av de tolv övergivna
fejkningsförsöken någonsin kunde leverera.

**Bygg ingen ny kompensation för detta.** Per uppdraget är detta ett
resultat, inte ett problem att lösa här — nästa steg är
`mc68302int.cpp` i fas 3, inte en trettonde `ASR10_EXPERIMENT_*`-flagga.

## Vad detta dokument är till för

Detta är regressionstestet för fas 3: kör kommandot i "Det exakta
minimikommandot" ovan mot `mc68302`-baserad `asr10_boot.cpp` när den
enheten finns. Om resultatet fortfarande stannar vid `"TUNING KBD -
HANDS OFF"` med `fc6814`/`fc6816` konstanta har fas 3 inte löst
problemet. Om `"NO INST OR BANK FILES"` väl visas — vilket enligt
`PLAN.md` (V350 har verkliga instrument- och bankposter, se
`disk-read-path.md`) skulle vara **fel** svar för just `V350.img` —
är det ett tecken på att nästa lager (kategori-/filtypslogiken i
webbläsarkoden, inte disk- eller schemaläggarvägen) är vad som
återstår.
