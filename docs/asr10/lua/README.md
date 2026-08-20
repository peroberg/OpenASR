# ASR-10 Lua Scripts

Observation i Lua, maskinmodell i C++: script i den här katalogen ska antingen
vara regressionstester, återanvändbara hjälpverktyg, eller arkiverade
engångsexperiment. Experiment som besvarat sin fråga flyttas till `archive/`
och raderas inte.

## Regression

Körs av `docs/asr10/regression-test.sh` och ska underhållas.

| script | skyddar |
|---|---|
| `boot.lua` | V3.50 når `FILE 1  TUTORIAL BNK` från reset. |
| `display.lua` | VFD-raden renderas komplett utan hål mitt i texten. |
| `button.lua` | `BTN_0A` via panel-ioport flyttar `FILE 1` till `FILE 2`. |
| `button_upper.lua` | Övre knappporten kan drivas från Lua; `BTN_23` ger två RHRB-byte. |
| `nodisk.lua` | Negativ kontroll för disk/index: utan disk visas `PLEASE INSERT DISK`. |
| `file_loaded.lua` | `BTN_0A/23/02` laddar `JM DIGI SYN`: `FILE LOADED` *och* IDMA-kanalen flyttar hela det uppmätta 172544-byte-lasset (21 arms). Skyddar mot att inläsningen stannar tyst tidigt bakom en oförändrad displaytext. |
| `mc68302_guards.lua` | En ren boot plus inläsning ska ge noll oväntade IACK-par, noll synkrona undantagsträffar (vektor 2/3/8), noll träffar på SIB-register utanför den kalibrerade uppsättningen, noll IDMA-larm, och GIMR:s vektorbasbitar ska stå kvar på det enda värde den hårdkodade formeln antar. Fem vakter (`lib/asr10_guards.lua`), felinjektionstestade var för sig. Se `investigations/mc68302-consolidation.md` och `mc68302-consolidation-2.md`. |

## Verktyg

Återanvändbara moduler eller generella prober.

| script | användning |
|---|---|
| `lib/asr10_display.lua` | Läser VFD-output och översätter segmentmönster till rå text. |
| `lib/asr10_regression.lua` | Gemensam testhjälpare för PASS/FAIL, displayväntan och maskinavslut. |
| `lib/asr10_guards.lua` | MC68302-vakter: IACK-undantagsvektor, synkron undantagshanterare (vektor 2/3/8, kalibrerad mot en känd krasch), SIB-täckning (klassificering portad från `mc68302_device`, allowlist inkluderar nu $0812/GIMR), IDMA SAPR/CMR/BCR, GIMR-vektorbas (pollningsbaserad). Aggregerade larm, första förekomst per villkor. Se `investigations/mc68302-consolidation.md` och `mc68302-consolidation-2.md`. |

## Experiment

Engångsscript vars fråga redan är besvarad eller journalförd. De ligger i
`archive/` för spårbarhet.

| script | status |
|---|---|
| `archive/asr10_display_probe.lua` | Display-/glyphprobe; ersatt av gemensam displayhjälpare. |
| `archive/asr10_e2_mirror_probe.lua` | E2-speglingsprobe; E2 står fortsatt `[OPEN]`. |
| `archive/asr10_trace.lua` | Tidig generell trace. |
| `archive/irq1_chain_probe.lua` | Bekräftade att FDC RECALIBRATE körs två gånger under vanlig boot, före FILE 1, inte bara vid instrumentinläsning. Se `investigations/irq1-storage-completion-probe.md`. |
| `archive/imr_probe_calibration_check.lua` | Positiv kontroll, del 1: tap installerad vid skriptstart ser noll skrivningar i hela `$FC6800-$FC68FF` genom hela boot — visade att instrumentet, inte historien, var fel. |
| `archive/imr_probe_calibration_check2.lua` | Positiv kontroll, del 2: samma tap installerad efter `t=15.5s` fångar omedelbart `$FC6829`-trafik. Lokaliserade felet till `mc68302_device::install_internal_window()`, som river Lua-taps vid varje BAR-skrivning. |
| `archive/imr_unmask_probe.lua` | GIMR/IMR/ISR-skrivningsprobe, tap installerad efter FILE 1. Resultat: noll skrivningar mellan FILE 1 och instrumentinläsningsstoppet. Se `investigations/irq1-imr-unmask-probe.md`. |
| `archive/imr_probe_bar_witness.lua` | Efterkontroll: bevisar att tap-instrumentet ovan inte tystnade mitt i mätfönstret — BAR skrivs sist vid t≈5,4s, långt före tappinstallationen vid FILE 1. |
| `archive/irq1_vector_probe_calibration.lua` | Positiv kontroll: bevisade att `cpu.spaces["cpu_space"]`-tappen rapporterar ordbasadressen (`FFFFF2`/`FFFFFC`), inte drivrutinens byteadress (`FFFFF3`/`FFFFFD`). |
| `archive/irq1_vector_probe.lua` | Mätte den faktiskt hämtade IRQ1-vektorn under den (tillfälligt återinförda) naiva kopplingen. Resultat: `$51`, korrekt mål. Se `investigations/irq1-vector-and-sr-probe.md`. |
| `archive/irq1_sr_mask_probe.lua` | FDC-dialog + SR-masknivå + IACK i en körning. Upptäckte en tap-livslängdsfälla (tap-handtag måste sparas i en variabel eller GC:as den bort). Se samma journal. |
| `archive/irq1_handler_chain_probe.lua` | Följde hanterarkedjan efter SIS: `$0402`-läsning, `jmp (A0)` med `A0=0`, och en verklig Address Error-vektor 3. Avgjorde att kraschen är strukturell (odokumenterad continuation-pointer). Se `investigations/irq1-handler-chain-probe.md`. |
| `archive/fdc_ready_dialogue_probe.lua` | Loggade AUX- och FIFO-dialogen genom hela boot. Hittade motor-off (`aux $0E`) 300µs efter sista READ DATA, 23ms före den enda IACK:en. Se `investigations/ready-line-artifact-probe.md`. |
| `archive/sds_st3_probe.lua` | Försökte spåra om firmware konsulterar SENSE DRIVE STATUS/ST3:s ready-bit. Inte avgörande inom rimlig ansträngning; se samma journal för varför den empiriska regressionstestet blev den avgörande metoden i stället. |
| `archive/ready_disconnect_chain_probe.lua` | Positivt test: `set_ready_line_connected(false)` + naiv IRQ1-koppling tillsammans. Boot kraschar inte längre; instrumentinläsningens kedja når RECALIBRATE, SEEK och READ DATA korrekt och stannar vid en äkta `DISK ERROR - LOST DATA` (IDMA saknas). Se samma journal. |
| `archive/idma_register_probe.lua` | Första IDMA-registerkarteringen. Hittade eget mätfel: enbyte-rekonstruktionen korrumperade 32-bitars SAPR/DAPR-ordskrivningar. Se `investigations/idma-register-map-probe.md`. |
| `archive/idma_register_probe2.lua` | Rättad registerkartering, rått offset/data/mask utan rekonstruktion. Källan till den slutgiltiga registerkartan. |
| `archive/idma_result_check.lua` | Kontrollerade utfallet av den landade IDMA-implementationen: `DISK ERROR - LOST DATA` borta, ersatt av `DISK NOT RESPONDING` vid t≈23,4s. |
| `archive/disk-not-responding-probe.lua` | Diagnostiserade `DISK NOT RESPONDING`: READ DATA:s eget avslutningsavbrott uteblir helt, 4,994s tystnad, sedan generisk firmware-timeout. Motbevisade datarat-hypotesen. Återanvänd efter fixen (`investigations/tc-reentrancy-probe.md`) för att bekräfta 32 rena `$51`-leveranser fram till `FILE LOADED`. |
| `archive/intrq-assertion-probe.lua` | Skiljde "INTRQ hävdes aldrig" från "hävdes men levererades inte": passiv MSR-polling visade `main_phase` fast i `PHASE_EXEC` hela tystnaden, aldrig `PHASE_RESULT`; SR-masken öppnade 576 gånger utan ett enda avbrott. Se `investigations/tc-reentrancy-probe.md`. |
| `archive/load_departure.lua` | PC-/schedulerprobe när FDC-pollningen upphör. |
| `archive/load_destination.lua` | A1-destinationsprobe; den första globala A1-kartan är `[RETRACTED]`. |
| `archive/load_dma_probe.lua` | DMA-fönsterprobe för instrumentinläsningen. |
| `archive/load_stall.lua` | Passiv manuell inläsningslogg. |
| `archive/load_timeline.lua` | Instrumentinläsningens avslutande tidslinjeprobe. |
| `archive/tick_stall.lua` | DUART tick-/IMR-/counterprobe under stall. |
| `archive/vector_table_compare.lua` | Jämför levande låg-RAM-vektorer mot ROM-kopian `$F82000`. |
| `archive/file-loaded-verification-probe.lua` | Verifierade `FILE LOADED` oberoende av displaytexten: 21 IDMA-arm, 172544 byte, 19/21 sektorer byte-för-byte identiska mot `.img`-filen (2 "missmatch" är samma återanvända skrapbuffert `$944`, skriven tre gånger, jämförd mot slutfacit). Källan till `file_loaded.lua`. Se `investigations/file-loaded-verification-probe.md`. |
| `archive/post-file-loaded-probe.lua` | Del 3-observation efter `FILE LOADED`: ES5506/ES5510-registren är redan i kontinuerlig, varierad trafik utan knapptryckning (12634/8390 händelser på 5s tomgång, från `t≈0` i bootet, inte utlöst av inläsningen). Se samma journal. |
| `archive/es5506-offset-diagnostic.lua` | Kastprobe: dumpade rått offset/data/mask för ES5506-skrivningar och hittade att `offset` är den råa JÄMNA CPU-byteadressen (inte udda, till skillnad från FDC/DUART/IDMA i det här projektet) -- källan till att röstregisterdekodern nedan fungerar korrekt. |
| `archive/sample-ram-and-voice-registers-probe.lua` | Del 1+2 av ljudvägskartläggningen: `$100000-$1FFFFF` får 524290 skrivningar, samtliga före `FILE 1`, noll under och efter inläsningen (vittnat mot en syskon-tap). Avkodar alla 32 rösters CR/START/END/ACCUM: röst 0 använder bank 0, röst 1-31 delar identiskt värde i bank 1 -- som är helt okopplad (`.noprw()`) i den här maskinkonfigurationen. Se `investigations/sample-ram-and-voice-registers.md`. |
| `archive/sample-topology-and-payload-probe.lua` | Stänger sampeltopologin numeriskt: röst 0:s START/END/ACCUM omvandlade till ordadress landar innanför `$00000-$7FFFF` -- samma intervall som `$100000-$1FFFFF`. Karakteriserar även det inlästa 172544-byte-lasset (låg nollandel, hela bytevärdesspannet använt). Hittade och fixade samma BAR-återinstallationsfälla (methods-static-analysis.md #8.5) igen -- IDMA-tappen måste installeras efter FILE 1. Se `investigations/sample-topology-closure.md`. |
| `archive/wavwrite-capture.lua` | Del 4: bootar, laddar JM DIGI SYN, låter maskinen gå i viloläge 8s (ingen knapptryckning -- ingen klaviaturmodell finns) under `-wavwrite`-inspelning. Resultat: exakt tystnad hela vägen, förväntat given att ingen not någonsin triggas. Se samma journal. |
| `archive/exception-vector-inventory.lua` | Del 1 av MC68302-konsolideringen: en första variant (tap över hela vektortabellen $000000-$0000FF) gav uppenbart nonsens (en vektor "avfyrad" 1,16 miljoner gånger) -- bevis på att firmware återanvänder delar av det utrymmet för vanlig data. Omskriven till `cpu_space`-IACK-tappar (samma etablerade teknik som `$51`/IRQ1/IRQ6 hela projektet bygger på): exakt två (nivå,vektor)-par avfyras någonsin, nivå1→$51 och nivå6→$56. Se `investigations/mc68302-consolidation.md`. |
| `archive/sib-coverage-inventory.lua` | Del 2: fullständig täckningstabell för $FC6000-$FC6FFF, klassificerad mot `mc68302_device`s egen `classify_offset()`/`classify_full()`. Noll `unknown`-träffar, 88 distinkta `known_unimplemented`-offsets (legitim, omodellerad firmware-trafik). Fångar också alla observerade CMR/SAPR/BCR-värden åt Del 3. Se samma journal. |
| `archive/sync-exception-handler-calibration.lua` | Kalibrering: naiv IRQ1-koppling återinförd tillfälligt (ready-line-fixen bortkommenterad, byggd, körd, reverterad), bekräftade att en läs-tap på undantagshanterarens första ord fyrar vid en äkta vektor-3-krasch (t=15.032688s). Hittade också att vektortabellsadresser måste maskas till 24 bitar. Se `investigations/mc68302-consolidation-2.md`. |
| `archive/sync-exception-handler-probe.lua` | Del 1 (fortsättning): tappar handlaradresser för vektor 2/3/4/8/10/11. Vektor 2/3/8 stänger rent (ROM-handlare, noll träffar); vektor 10 är en äkta, frekvent A-line-trap-baserad OS-mekanism; vektor 4/11 pekar in i SIB-fönstret (inte kodutrymme) och förblir `[OPEN]`. Se samma journal. |
| `archive/gimr-origin-probe.lua` | Del 2: pollar GIMR direkt (immun mot BAR-återinstallationsfällan). Firmware skriver GIMR=$8040 en gång vid t≈0,002s och ändrar den aldrig igen. Bekräftar att den hårdkodade vektorformeln råkar stämma med detta enda observerade värde, inte att den härleder det. Se samma journal. |
| `archive/catchall-ram-inventory.lua` | Del 3 post 1: av 57 möjliga 4KB-hinkar i $FC5020-$FFFFFF (minus SIB-fönstret) träffas bara 9, alla i toppen ($FF7000-$FFFFFF) — stack/systemvariabler. Intervallet är 236 KB, inte megabyte; ~196 KB orört (rättad aritmetik, se `keyboard-and-sample-bridge.md`). |
| `archive/vector-4-11-content-probe.lua` | Klaviaturuppgiften, del 1: dumpar bytes vid vektor 4/11:s tabelladresser. Innehållet ser ut som riktig kod ($46FC $2700 = MOVE #$2700,SR) — tvetydigt på egen hand. Se `investigations/keyboard-and-sample-bridge.md`. |
| `archive/vector-4-11-pc-correlation-probe.lua` | Avgörande test: korrelerar varje tap-träff med CPU:ns PC. Vektor 4:s träffar visar PC=$000010 (dess egen tabellplats, inte $FC6000) — ingen handlare installerad. Vektor 11 alternerar äkta körning vid $FC6014 med anrop från $00002C — samma slutsats. Se samma journal. |
| `archive/button-vs-key-collision-probe.lua` | Bekräftar att en direkt BTN_18-tryckning ger identisk signatur (4 RHRB-byte, ingen reaktion) som en KEY_C-tryckning — klaviaturens nedärvda kodning kolliderar med knappprotokollet. Se samma journal. |
| `archive/key-press-response-probe.lua` | Del 3: mäter allt en tangenttryckning gör efter FILE LOADED — panelbyte, ES5506-röstregister (alla 32 röster), $100000-$1FFFFF, ROM-rösttabellen ($8000). Resultat: noll aktivitet överallt utom panelmottagning. Se samma journal. |
| `archive/xmit-ring-overflow-stress.lua` | Mäter XMIT_RING_SIZE-spill under snabbt spel: 32 av 52 förväntade byte förlorade före fix, 20 efter (renare drop i stället för korrupt overwrite). Se samma journal. |
| `archive/keyboard-wavwrite-capture.lua` | -wavwrite över en riktig tangenttryckning: fullständig tystnad, konsekvent med att ingen röst någonsin programmeras. Se samma journal. |
| `archive/panel-dispatch-memory-dump.lua` | Dumpar rå byte vid $FFB20A/$FFB0E0/$FFB3C0 m.fl. (levande RAM, inte ROM) för offline-disassemblering. Källan till fyndet att $C0-$FF går till kontrollgrenen, inte notväg. Se `investigations/keyboard-and-sample-bridge-2.md`. |
| `archive/trap-3-4-key-press-verify.lua` | Verifierar att en riktig tangenttryckning når TRAP #3/#4 (8/16 träffar, äkta PC=$FFB43E) men fortfarande noll ES5506-skrivningar — kön töms av en okänd konsument. Se samma journal. |
| `archive/trap-handler-memory-dump.lua` | Dumpar TRAP #3/#4-hanterarna ($F88078/$F880A2) för disassemblering. Visar att de är generiska kö-primitiv (enqueue/dequeue), inte notspecifik logik. Se samma journal. |
| `archive/scheduler-and-f87fd2-memory-dump.lua` | Läser bas/gräns ($C6/$C8) och alla sex slots färskt efter FILE LOADED, oberoende av bakgrundsforken. Bekräftar $23F6/$247A/6 slots via egen mätning. Se `investigations/keyboard-and-sample-bridge-3.md`. |
| `archive/scheduler-key-press-correlation.lua` | Korrelerar ett tangenttryck med alla sex slots, $B6C, TRAP #4-entré och avsändningspunkten $F87FC0. Hittar TRAP #9 som den faktiska bryggan mellan kön och schemaläggaren, och att slot 2/3 verkligen avsänds. Se samma journal. |
| `archive/trap6-and-targets-dump.lua` | Dumpar TRAP #6-hanteraren och de två avsända slottens mål ($740C/$FF9650) för disassemblering. Båda är riktiga hopptabell-mekanismer, inte dödpunkter. Se samma journal. |
| `archive/midi-note-on-gate.lua` | MIDI-grinden: injicerar en riktig note-on ($90 $3C $64) headless via `docs/asr10/lua/fixtures/noteon.mid` laddad i den nyinkopplade "mdin"-portens image-device. Resultat: 3 RHRA-läsningar (exakt meddelandet), noll ES5506-röstskrivningar, noll $100000-$1FFFFF-skrivningar -- samma tysta signatur som panelvägen, vilket friar panelspåret som orsak. Se `investigations/keyboard-and-sample-bridge-4.md`. |
| `archive/midi-scheduler-correlation.lua` | Korrelerar note-on-injektionen med alla sex schemaläggarslots. Visar att MIDI väcker exakt samma två slots (2 och 3, samma könoder $1504/$14FC) som en tangenttryckning gjorde föregående uppgift -- MIDI och panel konvergerar på samma mekanism. Se samma journal. |
| `archive/completion-consumer-static-dump.lua` | Dumpar all statisk kod som spårningen gick igenom denna uppgift ($F8899X byteklassificerare, $F88AA2 notdatahanterare, $FF8700-tabellen, $FF877E-hopptabellen, $FFB43E/$FFB6C4/$FFB56E, TRAP #D, $F884FC) för offline-disassemblering. Se samma journal. |
| `archive/decline-condition-probe.lua` | Instrumenterar $FFB43E:s dokumenterade men tidigare ospecificerade avslagsgren ($FFB486) och $FFB6C4:s tonartsintervallkontroll (notnummer mot `(lowmem[$330])+$3C/+$3E`). Resultat för testnoten: avslaget togs INTE ($171==0 kringgår det), körningen fortsatte mot $FFB56E. Se samma journal. |
| `archive/b56e-branch-probe.lua` | Följer $FFB56E:s egna grenar (trap #2, bit15/$171/$3BD-villkor, TRAP #4, $B55A-postning + TRAP #D). Visar att den riktiga noten når hela vägen till en synkron TRAP #D-anrop av $F884FC -- generisk DUART-kanalskötsel, inte röst-/sampelkod. Se samma journal. |
| `archive/find-instrument-select.lua` | Sveper alla 64 panelknappar efter FILE LOADED och bevakar lowmem $330/$332 (samma adresser $FFB6C4 redan visats läsa). BTN_02, tryckt från idle-skärmen, väljer Instrument 1: $330 0000->1098, $332 bit 0 satt. Källan till fyndet att laddning och val av instrument är två skilda steg -- ingen tidigare uppgift i den här serien tryckte en väljarknapp. Se `investigations/keyboard-and-sample-bridge-5.md`. |
| `archive/select-and-play.lua` | Väljer instrumentet (BTN_02) och spelar sedan en not både via panel och via MIDI. Resultat: riktig, formkorrekt röstallokering (röst 1 respektive röst 2, fullständiga CR/START/END/ACCUM) -- kategoriskt annorlunda än alla tidigare mätningar i serien. Fortfarande tyst på -wavwrite: båda rösterna hamnar i bank 1, som aldrig mappats (bara bank 0 delas med CPU-RAM). Se samma journal. |
