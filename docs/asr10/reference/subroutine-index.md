# ASR-10 rutinindex

Slå upp en adress, förstå vad som händer där. Ingen historik, inga
experiment, inga resonemang - de hör hemma i `investigations/`.
Läs `boot-sequence.md` för den ordnade reset-till-browser-kedjan.
Läs `vector-map.md` för CPU-exceptions, TRAP-vektorer, autovectors och
MC68302 IACK-/interruptvektorer.

Statusmärkning gäller enskilda påståenden:

- **[Verified]** - verifierad mot disassemblering, live-körning eller
  servicehandbok
- **[Likely]** - sannolik, härledd men inte bekräftad
- **[Hypothesis]** - hypotes, får inte byggas vidare på utan mätning

## Sorterad adressöversikt

Adressen är identiteten; namnet får ändras när förståelsen förbättras.

```
0067EC  analog_calibrate_ch7
006800  reference_division
00680C  analog_calibrate_ch5
00683A  analog_calibrate_ch0
006864  analog_sample_8x
0068C8  analog_calibrate_ch7_alt
00740C  jumptable_dispatch_15entry
0077C2  sched_slot5_poller
F8000C  reset_entry
F87F40  sched_trap_entry
F87F80  sched_context_save
F87F92  sched_dispatch_scan
F87FCC  sched_idle_loop
F88078  trap3_enqueue
F880A2  trap4_dequeue
F880D6  trap6_rearm
F88108  trap7_handler
F8812C  trap8_handler_sleep
F88138  trap9_slot_install
F88280  exception_tail
F882AA  exception_stub_table
F88300  irq6_tick_producer
F8845A  duart_chan_a_break_recover
F884BE  duart_irq_dispatch
F884F8  raise_error_145
F884FC  duart_chan_a_continuation_stash
F88AA2  midi_panel_data_byte_handler
F89AA2  panel_display_driver
F89D46  error_message_formatter
F8DAFE  par_read_raw
F8DB1E  par_read_scaled
F8DB30  par_filter_half
F8DB4C  par_filter_three_quarter
F8DB6E  par_threshold_dispatch
F8E1DA  duart_opr_table_writer
F97BC6  duart_init_table
FB7BEE  ipcr_change_detect
FB8AA2  fdc_msr_data_poll
FB8D1E  fdc_wait_cb
FB8D40  fdc_wait_rqm_dio
FB8D6C  fdc_delay_loop
FB8D78  fdc_wait_variant3
FB8E7E  interrupt_timer_init
FC60B0  movep_par_read
FFB20A  panel_frame_second_byte_bit7
FFB392  panel_frame_classify
FFB43E  panel_midi_completion_consumer
FFB488  panel_midi_completion_consumer_b73c
FFB4CC  panel_midi_completion_consumer_b770
FFB56E  completion_post_dispatch
FFB6C4  key_range_check
```

## Konventioner som måste läsas först

**Teckenutvidgning.** `jsr $xxxx.w` och `move $xxxx.w` med bit 15 satt
utvidgas till `$FFxxxx`, inte `$00xxxx`. `$FFFC4803` och `$00FC4803` är
**samma** 24-bitarsadress. OS:et använder båda ändarna av kortformen:
låga celler positivt (`$0CE3`), höga negativt (`$FFD0B0`).

**Diskresident kod.** För verifierade adresser i den observerade
V350-överlagringen har `file_offset = RAM + 0x2600` stämt. Kontrollera
alltid bytes mot live-RAM innan närliggande adresser disassembleras.
Metod i `investigations/os-code-extraction.md`.

**ROM-avbilden.** `asr10.bin`, 256 KB, mappad `$F80000`-`$FBFFFF`, rak
big-endian ordläsning. Offset 0-7 är SSP och PC; kod börjar på offset
`$0C`.

**Generella dataaccesspår.** Kör `docs/asr10/lua/asr10_trace.lua` med
`ASR10_TRACE=<duart|es5506|...>`; två körningar diffas radvis och första
skillnaden är svaret.

**Känd blind fläck.** Sökning efter absoluta adresser i ROM hittar
**inte** registerrelativa accesser (`($2,A0)` med basen i ett register).
Det har missat fynd flera gånger: CTU/CTL/ACR i en tabellstyrd init, en
tredje IMR-skrivning, feltabellens `FFF8xxxx`-pekare, och hela
DUART-breakrutinen på `$F8845A`. Hittar du inte en referens du vet ska
finnas - anta registerrelativ form innan du drar en slutsats.

## Bootstrap och undantag

### `$F8000C` reset_entry

**[Verified]** `move.w #$2700,sr` maskar avbrott. PC kommer ur
ROM-vektorn på offset 4.

Inputs: resetvektorerna i ROM.

Side effects: SR maskas innan bootstrap fortsätter.

### `$F88280` exception_tail

**[Verified]** `ori #$700,SR`; skriver felkoden ur D0 till `($00C0).w`;
anropar `$F977B0`, `$FF8D44`, `$F8CF9C`, formateraren `$F89D46`;
`move.w #$1600,D7`; `jmp $FB8E3E`. Återvänder inte i observerad väg.

Called by: exception-stubbarna `$F882AA`-`$F882DA`.

Calls: `$F977B0`, `$FF8D44`, `$F8CF9C`, `$F89D46`; `jmp $FB8E3E`.

Inputs: felkod i D0.

Outputs: `($00C0).w`/`($00C1).w` innehåller felkoden för formateraren.

Side effects: maskar avbrott och lämnar exceptionflödet via `$FB8E3E`.

### `$F882AA` exception_stub_table

**[Verified]** Bytesen är fyra byte per post: `moveq #kod,D0` /
`bra $F88280`. Felkoderna och servicehandbokens betydelser är
**[Verified]** mot ROM/servicehandboksmatchningen.

Stubbarna ligger i ordning förenlig med vektor 2-9; vektormappningen är
**[Likely]**, felkodernas betydelser **[Verified]**.

Calls: `$F88280`.

Outputs: D0 = felkod.

### `$F884F8` raise_error_145

**[Verified]** `moveq #$91,D0` / `trap #0`. Detta är firmwareupptäckt
ERROR 145, inte ett CPU-undantag.

Outputs: D0 = `$91`.

### `$F89D46` error_message_formatter

**[Verified]** Läser felkoden ur `($00C1).w`, bygger tre siffror och
formar `ERROR nnn - REBOOT ?` ur `$FFF824AA` + `$FFF824B1`.

Inputs: `($00C1).w`.

Outputs: paneltext via paneldrivrutinens vanliga väg.

### `$FB8E7E` interrupt_timer_init

**[Verified]** Initierar MC68302-avbrotts- och timerregister: GIMR
`$FC6812` <- `$8040`, IMR `$FC6816` <- 0, ISR `$FC6818` <- `$FFFF`,
IPR `$FC6814` <- `$FFFF`, Timer2 TRR `$FC6852` <- `$3F01`, TMR
`$FC6850` <- `$003B`.

Side effects: initierar MC68302:s interruptcontroller och Timer 2.
Vektorstatus och skillnaden mot DUART IRQ6-vägen finns i
`vector-map.md`.

## Schemaläggare

Uppgiftskontrollblock, stride `$16`, tabellgränser i `($00C6).w` och
`($00C8).w`. Sex slots. Layout:

```
+$00 raknare      +$02 pending    +$03 vilomonster   +$06 sparad PC (long)
+$0A sparad SR    +$0C sparad A5  +$0E sparad USP    +$14 troskel
```

### `$F87F40` sched_trap_entry

**[Verified]** `movem.l` sparar register, `move SR,(A7)`, patchar
återhoppsadressen på stacken med `$FFF87F66`.

Outputs: stackram för kommande kontextsparning.

### `$F87F80` sched_context_save

**[Verified]** USP -> `+$0E`, `(A7)+` -> SR `+$0A`, `(A7)+` -> PC
`+$06`, maskar avbrott.

Inputs: aktiv slot och trap/exception-stack.

Outputs: slotens sparade kontext.

### `$F87F92` sched_dispatch_scan

**[Verified]** Läser `+$02` mot `+$03`, `eor.b`; skiljer de sig
återställs kontexten och `rte` in i uppgiften. Aktuell uppgift skrivs
till `($0B6A).w`.

Inputs: primär slot-tabell.

Outputs: `($0B6A).w`.

### `$F87FCC` sched_idle_loop

**[Verified]** `move.w #$2000,SR` öppnar avbrott helt, sedan
`bra $F87F92`. Att stå här är normalt.

Side effects: väntar på nästa producent som gör en slot redo.

### `$F88108` trap7_handler

**[Verified]** Schemaläggartillstånd + gren till kontextsparning. Tar
D0 som parameter.

Calls: `$F87F80` via schemaläggarflödet.

### `$F8812C` trap8_handler_sleep

**[Verified]** Skriver D0 till aktiva slotens `+$00` = räknare. Faktisk
väntetid är `counter - threshold`, inte D0 direkt.

Inputs: D0 = ny räknare.

Outputs: aktiv slots `+$00`.

### `$F88300` irq6_tick_producer

**[Verified]** Kvitterar via `move.b ($FFFC481F).l,D0` (Stop Counter).
Dekrementerar varje slots `+$00`; när `d1 <= ($14,a0)` görs `bclr` i
`+$02` så uppgiften blir redo. Räknare 0 gör att sloten hoppas över.
Tickräknare i `($0B82).w`; var tionde tick körs sekundärtabellen ur
`($00CA).w`, stride `$1A`, callback-pekare på `+$16`.

Known indirect entry: IRQ6-vektorvägen.

Inputs: DUART counter/timer IRQ6, primär och sekundär tabell.

Outputs: slotarnas pending-byte och `($0B82).w`.

### `$F88078` trap3_enqueue

**[Verified]** TRAP #3 (vektor 35). Generisk kö-enqueue-primitiv, inte
notspecifik logik. Allokerar en nod ur en liten pool
(`$14F4`-`$150C`), skild från de sex schemaläggarslotsen. Källa:
`investigations/keyboard-and-sample-bridge-3.md` (disassemblering av
handlaren), `investigations/keyboard-and-sample-bridge-2.md` (första
identifieringen som generisk kö, inte notspecifik).

Outputs: A5 = ny könod.

### `$F880A2` trap4_dequeue

**[Verified]** TRAP #4 (vektor 36). Generisk kö-dequeue-primitiv,
motparten till `trap3_enqueue`. Källa: samma som ovan.

### `$F880D6` trap6_rearm

**[Verified]** TRAP #6 (vektor 38). Allmän "växla målets
pending-bit och återladda från en arbetsobjekt-pekare"-primitiv,
strukturellt lik `trap9_slot_install` men verkar på `($0B6A).w`s
aktuella slot istället för en extern `A1`. Anropas som resident
uppgifts första åtgärd vid avsändning, konsekvent med "denna uppgift
parkerar om sig själv". Källa: `investigations/
keyboard-and-sample-bridge-3.md`.

### `$F88138` trap9_slot_install

**[Verified]** TRAP #9 (vektor 41). Bryggan mellan
`trap3_enqueue`s kö och de sex schemaläggarslotsen -- **tidigare
oidentifierad**, hittad genom att korrelera en riktig knapptryckning
mot alla sex slots. `A1` = målslotens adress, `A5` = en
`trap3_enqueue`-allokerad könod. `bclr.b #7,$2(a1)` växlar sloten
idle (`$80/$80`) -> pending (`$00/$80`) -- matchar den uppmätta
skrivningen exakt. Källa: `investigations/
keyboard-and-sample-bridge-3.md`.

Inputs: `A1` = målslot, `A5` = könod.

Side effects: `$2(a1)` bit 7 rensas; sloten blir redo för avsändning.

### `$00740C` jumptable_dispatch_15entry

**[Verified]** Äkta, gränskontrollerad hopptabell. Läser ett index ur
`$2(a5)` (könoden), gränskontrollerar mot `$E` (15 poster), tabellbas
`$67AC`, anropar `trap #4` igen, `jsr`:ar sedan till det uppslagna
målet. Index utanför intervallet faller igenom till `trap #0`. Källa:
`investigations/keyboard-and-sample-bridge-3.md`.

### `$FF9650` shared_absolute_jmp_vector_table

**[Verified]** En delad vektortabell av absoluta `jmp`-instruktioner;
andra anropsställen indexerar troligen in på andra offset. Det
anropsställe som identifierats går in på offset 0, landar på
`jmp $FFF90890`. Källa: `investigations/
keyboard-and-sample-bridge-3.md`.

## DUART och panel

DUART-register: bas `$FFFC4801`, stride 2, alltså register *n* på
`$FFFC4801 + 2n`. Kanal B = panel är **[Verified]** genom faktisk
paneltrafik på THRB/RHRB och genom RX-FIFO-fixen. Kanal A = MIDI är
**[Verified]** sedan `keyboard-and-sample-bridge-4.md`: DUART-kanal A
kopplad in, en riktig note-on injicerad headless via en Standard MIDI
File, och firmwarets egen statusbyte-/databyte-klassificerare
(`midi_panel_data_byte_handler`) live-disassemblerad och
live-uppmätt -- inte längre bara `esq5505.cpp`-stöd.

### Panel-RX-kedja

**[Verified]** Panelsvar -> `mc68681_device` kanal B RX-FIFO -> SRB
RxRDY -> ISR bit 5 -> `irq_cb` -> IRQ6 -> `$F884BE` -> handler via
`($00DE)` -> RHRB pop.

### `$F884BE` duart_irq_dispatch

**[Verified]** Dispatchlogiken läser ISR (`$FFFC480B`) och testar bitar:
bit 5 RxRDY B -> indirekt pekare `($00DE).w`; bitar 2|1 kanal A ->
indirekt pekare `($00E2).w`; bit 0 TxRDY A -> indirekt pekare
`($00E6).w`; bit 3 counter ready -> indirekt pekare `($8638).w`. Ingen
träff -> ERROR 145. Bit 4 (TxRDY B) testas inte.

Handleridentiteterna bakom pekarna är okända; ingen har följt vart de
pekar.

Known indirect entry: IRQ6-vektorvägen.

Calls: indirekt via `($00DE).w`, `($00E2).w`, `($00E6).w`, `($8638).w`.

Inputs: DUART ISR.

Side effects: dispatchar eller reser ERROR 145. IACK-/vektordetaljerna
finns i `vector-map.md`.

### `$F8845A` duart_chan_a_break_recover

**[Verified]** Läser RHRA, läser SRA, skriver CRA `$40`/`$50`/`$20`/`$01`.
Om SRA bit 7 (Received Break) är satt läses RHRA en extra gång för att
kasta null-byte. Registerrelativ och därför osynlig för absolutsökning.

Inputs: kanal A status och RX-register.

Side effects: återställer kanal A-mottagaren.

### `$F89AA2`-`$F89D22` panel_display_driver

**[Verified]** Tio referenser till THRB `$FFFC4817`. Skriver panelens
22-teckensrader. Felformateraren ligger direkt efter på `$F89D46`.

Outputs: kanal B TX till panelen.

### `$F8E1DA` duart_opr_table_writer

**[Verified]** `A0 = $F8E252`, index ur `($0170).w`, `OPR = ~tabell[i]`.
Tabell `2E 57 36 00` ger OPR `$D1, $A8, $C9, $FF`. Följs av
`bclr #7,($00FC6823).l` (PIO port A).

Inputs: `($0170).w`.

Outputs: DUART OPR och MC68302 PIO port A bit 7.

### `$F97BC6` duart_init_table

**[Likely]** Tabellstyrd DUART-init med (offset, värde)-par och
terminator `00FF`: `0A->16` IMR, `10->0E` MR1/2B, `1A->18` OPCR,
`1C->14` SetOPR, `1E->0C` ResetOPR.

### `$FB7BEE` ipcr_change_detect

**[Verified]** `tst.b IPCR` följt av ovillkorlig `st`; rensar latchen
och accepterar "ingen förändring".

### `$FB7C84`

**[Verified]** `btst #4` på IPCR.

### `$FB7CA8`

**[Verified]** `btst #2,($FFFC481B).l` på DUART Input Port.

### Panel-/MIDI-notdispatch

Denna klunga tillkom efter att `$FFB43E` identifierats **två gånger**,
i två separata utredningar, för att det första fyndet
(`panel-completion-consumer-v350.md`, statisk) inte stod med här och
låg begravt tills `keyboard-and-sample-bridge-4.md` (live) hittade det
på nytt. Adressen är identiteten -- håll den här klungan uppdaterad
när en ny rutin i notvägen fastställs.

### `$F88AA2` midi_panel_data_byte_handler

**[Verified]** Databytehanterare, delad mellan MIDI och panel. Lagrar
notnummer i `$FF87C4`, anropar `$F8892A`, återladdar `$FF87BC` till
sig själv (stödjer MIDI running status), och hoppar vid ett nollskilt
notvärde till `$8788.w` -> `$FFB43E`. **Inte samma adress som
`fdc_msr_data_poll` (`$FB8AA2`)** -- de ligger nära i hex men är
orelaterade rutiner i helt olika delsystem; se den rutinens egen post.
Källa: `investigations/keyboard-and-sample-bridge-4.md`.

Calls: `$F8892A`; hoppar till `panel_frame_classify`-familjens
måladress `$FFB43E` för nollskilda notvärden.

### `$FFB392` panel_frame_classify

**[Verified]** Första bytets tillståndslogik i panelens seriella
protokoll. Testar D1, lagrar till `$03C4`, väljer nästa tillstånd.
Källa: `investigations/panel-protocol-state-machine.md`.

### `$FFB20A` panel_frame_second_byte_bit7

**[Verified]** Andra bytets tillstånd för första byte med bit 7 satt.
Återställer `$03C0` till `$FFB392`, rensar bit 7 först -- motsvarigheten
till `$FFB0E0` för fallet bit 7 rensad. Källa: `investigations/
panel-protocol-state-machine.md`.

### `$FFB43E` panel_midi_completion_consumer

**[Verified]** Den delade avslutningskonsumenten för både panel- och
MIDI-data -- bekräftat genom direkt adressmatchning (inte slutledning):
`midi_panel_data_byte_handler`s `jmp $8788.w` landar exakt här.
`move.b $303.w,$302.w`, `trap #3`, lagrar D1/D3 i `($4,A5)`/`($6,A5)`,
`jsr $F87FD2`, `trap #4`, `jsr key_range_check` (`$B6C4`), grenar
sedan genom `completion_post_dispatch` (`$B56E`) eller returnerar tyst
vid `$FFB486`: `rts` sker om och endast om `lowmem[$171]==1` OCH
(`key_range_check`s D3-resultat `& lowmem[$CDE]`)`==0`. Källa:
`investigations/panel-completion-consumer-v350.md` (första, statiska
fyndet) och `investigations/keyboard-and-sample-bridge-4.md` (live
disassemblerad avslagsgren, andra fyndet -- se rubriken ovan).

Calls: `trap #3`, `$F87FD2`, `trap #4`, `key_range_check` (`$FFB6C4`),
`completion_post_dispatch` (`$FFB56E`).

### `$FFB488` panel_midi_completion_consumer_b73c

**[Verified static]** Samma trap-/tjänsteform som `$FFB43E`, men
anropar `$B73C` och kan sätta `A1=#2` innan gren genom
`$B56A`/`completion_post_dispatch`. Returnerar direkt vid `$FFB4CA`
när efterservice-villkoret säger att inget ska postas. Källa:
`investigations/panel-completion-consumer-v350.md`.

### `$FFB4CC` panel_midi_completion_consumer_b770

**[Verified static]** Anropar `$B770`, beräknar/väljer en bit i D3, och
kan: anropa `trap #2`; anropa `$B55A` (skriver en sex-byte-post
relativt `A5+2`: `A1`-ord, en rensad byte, D2-byte, D1-byte, D3-byte);
anropa `trap #9` med `A1=#2438`; anropa `$B7A0`; grena genom
`completion_post_dispatch`; eller returnera direkt vid `$FFB558`.
Källa: `investigations/panel-completion-consumer-v350.md`.

### `$FFB56E` completion_post_dispatch

**[Verified]** `panel_midi_completion_consumer`s "proceed"-mål.
`trap #2`; tidig retur vid `$FFB594` om carry sätts. Annars: `bclr.b
#$f,D1`; testar `lowmem[$171]`/`lowmem[$3BD]`; leder antingen direkt
till `trap #4` eller (via `$B596`) till `$B55A`s postningsväg +
`trap #d`. Källa: `investigations/keyboard-and-sample-bridge-4.md`.

### `$FFB6C4` key_range_check

**[Verified]** Tonartsintervallkontroll, inte en residens-/pekar-/
längdkontroll. Om `lowmem[$31C]!=0`: läser `lowmem[$330]` som pekare
till en instrument-/keygroup-deskriptor, jämför notnumret (D2) mot ett
lågt/högt delningspunktspar på `(pekaren)+$3C`/`+$3E`. Utanför
intervallet -> D3 förblir 0. Innanför -> D3 = `lowmem[$332]`.
`lowmem[$330]`/`$332` skrivs (för instrumentval) av `BTN_02` i
idle-kontext -- se `find-instrument-select.lua`,
`investigations/keyboard-and-sample-bridge-5.md`. Numerisk stängning
samma dokument: röst 1/2:s levande `START`/`END` konverterar till ett
bankrelativt ordintervall som landar helt innanför
`$000944-$0552FF` -- den bevisat riktiga instrumentnyttolasten,
inte en andra sampel-RAM-pool. Källa: `investigations/
keyboard-and-sample-bridge-4.md` (grenstruktur) och `investigations/
keyboard-and-sample-bridge-5.md`/`-6.md` (levande värden, numerisk
stängning).

Inputs: `lowmem[$31C]`, `lowmem[$330]`, D2 (notnummer).

Outputs: D3.

### `$F884FC` duart_chan_a_continuation_stash

**[Verified]** Nås via TRAP #d (vektor 45, en tidigare oidentifierad
"uppskjutet arbete"-kö-primitiv: om målpostens `+$10`-flagga är 0,
länkas objektet in och funktionspekaren i postens egen `+0`-fält
anropas synkront; annars länkas det bara in för senare tömning).
Skriver `$04` till DUART:s Channel-A Command Register (`$FFFC4805`,
registerindex 2), sparar A5 och en fortsättningspekare (`$FFF8857A`) i
`$FF86F8`/`$FF86FC`. Generisk DUART-kanalskötsel/fortsättningsmekanik,
inte röst- eller sampelkod. Källa: `investigations/
keyboard-and-sample-bridge-4.md`.

## Analoga ingångar och ES5506 PAR

### Board-default för PAR

I nuvarande källa används `asr10_boot_state::analog_r()` via
`es5506_host.read_port_cb().set(FUNC(asr10_boot_state::analog_r))` i
`asr10_boot()`. Paneldevice kan uppdatera kanalvärden via
`m_panel->write_analog().set(FUNC(asr10_boot_state::analog_w))`.

**[Verified]** Callbacken läser den firmware-valda kanalen ur panelens analoga
latch och returnerar motsvarande 10-bitarsvärde. Omappade kanaler har
board-defaults så att OS-kalibreringen inte dividerar med noll. Kanal 7 mäts
åtta gånger; summan blir D2 och D2 är divisor i kalibreringsfaktorn. Övriga
kanaler primar filter, centrum och trösklar.

### `$F8DAFE` par_read_raw

**[Verified]** `movea.l #$00FC2001,A0` / `jsr $FFFC60B0` / `asl.w #6,D2`
/ `ori #1,CCR` / `rts`.

Calls: `$FC60B0`.

Outputs: rå PAR-data i D2, skiftad till firmwareformat.

### `$F8DB1E` par_read_scaled

**[Verified]** Samma grundläsning som `$F8DAFE`, därefter `bra $F8DB6E`.

Calls: `$FC60B0`, `$F8DB6E`.

### `$F8DB30` par_filter_half

**[Verified]** `add.w ($6,A2),D2` / `roxr.w #1,D2`, carry-bevarande
medelvärde. Tillståndscell `(A2+6)`.

Inputs: D2 och filtertillstånd.

Outputs: uppdaterat filtervärde.

### `$F8DB4C` par_filter_three_quarter

**[Verified]** `0,75 * gammalt + 0,25 * nytt`, samma tillståndscell som
ovan.

### `$F8DB6E` par_threshold_dispatch

**[Verified]** Tröskeljämförelse mot `(A2+8)` och `(A2+$A)`, hopptabell
på `$F8DB8C` indexerad med `(A2)`.

Calls: fem kända men ännu oidentifierade hopptabellmål, se "Kända
okända".

### `$F8DB12`, `$F8DB18` parameterblock

**[Verified]** Detta är data, inte kod. Sex byte vardera, laddas i A3.
**[Hypothesis]** Byte 1 verkar vara offseten till tillståndscellen inom
instansblocket.

## Diskresident OS (V350)

Ange V350-offset bara när bytesen är verifierade mot live-RAM.

### `$006864` analog_sample_8x

**[Verified]** `moveq #7,D7`, åtta varv. Per varv: `trap #8` skriver
D0=4 till aktiva slotens räknare; faktisk väntan är `counter - threshold`
och beror på slotens tröskel, som inte är uppmätt för just den här
sloten. Rutinen yieldar därefter via `trap #7`, läser PAR med
`jsr $FFFC60B0`, gör `asl.w #6` + `lsr.w #3` (netto `raw << 3`) och
ackumulerar i D6. Returnerar summan i D2. Fullt utslag = `$FFC0`.
Väljer ingen kanal.

Called by: `$0067F4`, `$00681C`, `$00684A`, (`$0068C8`).

Calls: `$FC60B0`, `trap #8`, `trap #7`.

Inputs: aktivt PBDAT-kanalval och ES5506 PAR.

Outputs: D2 = åtta mätningars summa.

Side effects: schemaläggaryield mellan mätningarna.

### `$0067EC` analog_calibrate_ch7

**[Verified]** `ori.b #$07,($00FC6829).l` väljer kanal 7, anropar
`$006864`, lagrar råsumma i `$0DD6`, dividerar `$A3480000` med D2 på
`$006800`, och lagrar faktor på `$0DF2`.

V350-offset: verifierat för de bytes som utgör rutinen.

### `$006800` reference_division

**[Verified]** `divu.w D2,D0`. Fäller ERROR 130 när D2 = 0. Endast noll
är dödligt; `bvc` + klamp till `$FFFF` hanterar overflow avsiktligt.

Inputs: D2 = kanal 7-summa.

Outputs: D0 = kalibreringsfaktor eller CPU divide-by-zero.

### `$00680C` analog_calibrate_ch5

**[Verified]** `andi.b #$F8` + `ori.b #$05` väljer kanal 5, mäter,
primar filtercellen `($0DC2+6)`, sätter `A3 = $F8DB12`, och anropar
`$F8DC2E`.

Calls: `$006864`, `$F8DC2E`.

### `$00683A` analog_calibrate_ch0

**[Verified]** `andi.b #$F8` + `ori.b #$00` väljer kanal 0, mäter,
`mulu` mot faktorn, `swap`, lagrar `centrum+$528` på `$0DDE` och
`centrum-$528` på `$0DE0`.

Calls: `$006864`.

### `$0068C8` analog_calibrate_ch7_alt

**[Verified]** Kanal 7 igen, instansblock `$0DD0`, `A3 = $F8DB18`,
`jsr $F8DB4C`, egen kopia av divisionen.

Calls: `$006864`, `$F8DB4C`.

### `$0077C2` sched_slot5_poller

**[Verified]** Slot 5:s oändliga bakgrundspollare. `trap #8` med D0=100
ger faktisk väntan `100 - threshold`; för slot 5 är threshold `$58`, dvs
12 tick. Skannar tolv poster ur `($FFD0B6+index)` med en `trap #7` per
post och hoppar tillbaka. Oändligheten är avsiktlig.

Calls: `trap #8`, `trap #7`, indirekta callbacks.

## FDC-väntningar

Timeoutvärdet sätts på `$FB7BD6`: `move.l #$00013880,($0476).w` =
80 000.

### `$FB8D1E` fdc_wait_cb

**[Verified]** `btst #4,($FFFC4001).l`; MSR bit 4 = CB (Command Busy).
Timeout -> `$049D=$0D`, `$04AE=$20`.

### `$FB8D40` fdc_wait_rqm_dio

**[Verified]** `btst #7` / `btst #6`; väntar på RQM=1 och DIO=0.
Timeout -> `$04AE=$21`.

### `$FB8D78` fdc_wait_variant3

**[Verified]** Tredje väntvarianten. Timeout -> `$04AE=$22`.

### `$FB8D6C` fdc_delay_loop

**[Verified]** Fördröjningsloop med push/pop av D3.

### `$FB8AA2` fdc_msr_data_poll

**[Verified]** FDC-dataöverförings-/MSR-pollningsloop: `move.b
($FFFC4001).l,D1`, `bne $FB8AA2`. Del av CMD46/READ DATA-transaktionen.
1 674 022 träffar uppmätta i en full körning. **Inte samma adress som
`midi_panel_data_byte_handler` (`$F88AA2`)** -- ligger nära i hex men
är orelaterade rutiner i helt olika delsystem. Källa:
`investigations/driver-instrumentation-audit.md`,
`investigations/runtime-cycle.md`, `investigations/evidence-tree.md`.

Alla tre verifierade FDC-väntningar uppfyller villkoret på första
pollningen i den verifierade V350-körningen; timeoutvägen tas inte och
`$049D` blir aldrig `$0D`. Detta stänger FDC-spåret i referensen.

## ROM-tabeller och dataformat

| Adress | Innehåll |
|---|---|
| `$F8050E` | **[Verified]** Namngiven feltabell, 29 pekare i `FFF8xxxx`-form till 22-teckensmeddelanden. Koder utanför faller igenom till `ERROR nnn - REBOOT ?`. |
| `$F82484`-`$F824A9` | **[Verified]** Panel raw->mapped-prefix, 38 byte. Lookup-bas verifierad vid `$F89D9C`; separat dataobjekt `$F824AA` begränsar prefixen. Lookupkoden saknar övre runtime-guard. |
| `$F824AA` | **[Verified]** ROM-sträng utanför `$FB8EEE`-tabellen: `45 52 52 4F 52 20 00` = `"ERROR "\\0`, laddad av `$F89D4A 247c fff8 24aa`. |
| `$F824B1` | **[Verified]** ROM-sträng utanför `$FB8EEE`-tabellen: `20 2D 20 52 45 42 4F 4F 54 20 3F 00` = `" - REBOOT ?"\\0`, laddad av `$F89D70 247c fff8 24b1`. |
| `$F824C8` | **[OPEN]** Longword-pekartabell: `$FFF824E8`, `$FFF824EA`, `$FFF824EC`, `$FFF824F0`, `$FFF824EA`, `$FFF824F0`, `$FFF824EC`, `$FFF824F2`. Konsument och semantik ej identifierade. |
| `$FB8EC8`-`$FB8F2D` | **[Verified]** Bootmeddelandetabell, 17 poster i format `<kod.w><pekare.l>`, avgränsad av att stringdata börjar på `$FB8F2E`. Från `$FB8EEE` kan samma byte också läsas som 10 poster i formen `<pekare.l><kod.w>`; det är en överlappande vy in i samma tabellområde. |
| `$FB8F2E`-`$FB90AA` | **[Verified]** Bootmeddelanden, byte-exakt avskrivna från tabellen nedan. De är NUL-terminerade, normalt 22 tecken före terminator. |
| `$F81003`-`$F81F87` | **[Verified]** Ordfragmentvokabulär, 256 NUL-terminerade fragment. Skärmar byggs av fragmentindex med inbäddade kontrollbyte (`1F xx`, `16 xx`, `13 xx`). |
| - | **[Verified]** Strängtabellformat: `<pekare.l><bredd.b><antal.b>`. 114 självvaliderande förekomster i ROM. Ex: `$F853DA` -> `$F853E0`, 12x3 = `LOW VOLTAGE` / `HIGH VOLTAGE` / `ESP RAM TEST`. |

Byte-exakta bootmeddelanden från `<kod.w><pekare.l>`-tabellen:

| post | entry | kod | pekare | hex | ASCII |
|---:|---:|---:|---:|---|---|
| 0 | `$FB8EC8` | `$0100` | `$FFFB8F2E` | `20 20 44 49 53 4B 20 4E 4F 54 20 46 4F 52 4D 41 54 54 45 44 20 20 00` | `"  DISK NOT FORMATTED  "\\0` |
| 1 | `$FB8ECE` | `$0200` | `$FFFB8F46` | `20 44 49 53 4B 20 44 41 54 41 20 43 4F 52 52 55 50 54 45 44 20 20 00` | `" DISK DATA CORRUPTED  "\\0` |
| 2 | `$FB8ED4` | `$0400` | `$FFFB8F5E` | `20 20 20 4D 49 53 53 45 44 20 4C 4F 41 44 20 44 41 54 41 20 20 20 00` | `"   MISSED LOAD DATA   "\\0` |
| 3 | `$FB8EDA` | `$0500` | `$FFFB8F76` | `20 20 50 4C 45 41 53 45 20 49 4E 53 45 52 54 20 44 49 53 4B 20 20 00` | `"  PLEASE INSERT DISK  "\\0` |
| 4 | `$FB8EE0` | `$0800` | `$FFFB8F8E` | `20 44 52 49 56 45 20 4E 4F 54 20 52 45 53 50 4F 4E 44 49 4E 47 20 00` | `" DRIVE NOT RESPONDING "\\0` |
| 5 | `$FB8EE6` | `$1000` | `$FFFB8FEC` | `20 20 4F 2E 20 53 2E 20 4E 4F 54 20 4F 4E 20 44 49 53 4B 20 20 20 00` | `"  O. S. NOT ON DISK   "\\0` |
| 6 | `$FB8EEC` | `$FE00` | `$FFFB8FBC` | `20 20 20 45 4E 53 4F 4E 49 51 20 20 41 53 52 2D 31 30 20 20 20 20 00` | `"   ENSONIQ  ASR-10    "\\0` |
| 7 | `$FB8EF2` | `$FF00` | `$FFFB8FD4` | `20 20 20 20 4C 4F 41 44 49 4E 47 20 53 59 53 54 45 4D 20 20 20 20 00` | `"    LOADING SYSTEM    "\\0` |
| 8 | `$FB8EF8` | `$1100` | `$FFFB901C` | `42 41 44 20 44 49 53 4B 2F 4E 4F 54 20 45 50 53 20 44 49 53 4B 20 00` | `"BAD DISK/NOT EPS DISK "\\0` |
| 9 | `$FB8EFE` | `$1200` | `$FFFB901C` | `42 41 44 20 44 49 53 4B 2F 4E 4F 54 20 45 50 53 20 44 49 53 4B 20 00` | `"BAD DISK/NOT EPS DISK "\\0` |
| 10 | `$FB8F04` | `$1300` | `$FFFB901C` | `42 41 44 20 44 49 53 4B 2F 4E 4F 54 20 45 50 53 20 44 49 53 4B 20 00` | `"BAD DISK/NOT EPS DISK "\\0` |
| 11 | `$FB8F0A` | `$1400` | `$FFFB901C` | `42 41 44 20 44 49 53 4B 2F 4E 4F 54 20 45 50 53 20 44 49 53 4B 20 00` | `"BAD DISK/NOT EPS DISK "\\0` |
| 12 | `$FB8F10` | `$1700` | `$FFFB904C` | `49 4E 43 4F 4D 50 41 54 49 42 4C 45 20 4F 2E 53 2E 20 44 49 53 4B 00` | `"INCOMPATIBLE O.S. DISK"\\0` |
| 13 | `$FB8F16` | `$1A00` | `$FFFB9064` | `20 20 20 49 4E 43 4F 4D 50 41 54 49 42 4C 45 20 52 4F 4D 20 20 20 00` | `"   INCOMPATIBLE ROM   "\\0` |
| 14 | `$FB8F1C` | `$FD00` | `$FFFB907C` | `20 20 20 20 53 43 53 49 20 49 4E 53 54 41 4C 4C 45 44 20 20 20 20 00` | `"    SCSI INSTALLED    "\\0` |
| 15 | `$FB8F22` | `$FC00` | `$FFFB9094` | `53 45 41 52 43 48 49 4E 47 20 46 4F 52 20 53 43 53 49 20 44 45 56 00` | `"SEARCHING FOR SCSI DEV"\\0` |
| 16 | `$FB8F28` | `$0D00` | `$FFFB8FA6` | `46 49 4C 45 20 4F 50 20 45 52 52 4F 52 20 4E 55 4D 3D 20 00` | `"FILE OP ERROR NUM= "\\0` |

## DPRAM - MOVEP-thunkbibliotek `$FC6028`-`$FC6136`

Alla använder A0 som bas, satt av anroparen. Tjugo thunkar; de tre som
används mot ES5506:

| Adress | Instruktion | Mål med `A0 = $FC2001` |
|---|---|---|
| `$FC60B0` | `movep.l ($68,A0),D2` | `$FC2069/6B/6D/6F` = PAR, registerindex 13 |
| `$FC60B6` | `movep.l ($78,A0),D0` | `$FC2079/...` = PAGE, registerindex 15 |
| `$FC60BC` | `movep.l ($70,A0),D0` | IRQV, registerindex 14 |

**[Verified]** PAR:s värde är 10 bitar högerjusterat i en 32-bitars
latch, så de två första MOVEP-byten är alltid `$00`. Data finns bara på
`$FC206D` (bit 9:8) och `$FC206F` (bit 7:0). Läs aldrig av PAR på
`$FC2069`.

## RAM-celler

| Adress | Innehåll |
|---|---|
| `$00C0`/`$00C1` | Felkod, läses av formateraren |
| `$00C6` / `$00C8` | Schemaläggartabellens start och slut (`$23F6` / `$247A` i V350) |
| `$00CA` / `$00CC` | Sekundär tabell, stride `$1A` |
| `$00DE`, `$00E2`, `$00E6` | DUART-dispatcherns hanterarpekare |
| `$0170` | OPR-tabellindex, 0..3 |
| `$0B6A` | Senast dispatchade uppgift |
| `$0B82` | Tickräknare |
| `$0CE3` | Testad av slot 5 |
| `$0DC2`, `$0DD0` | Reglageinstansblock, `$0E` isär. `+6` filtertillstånd, `+8`/`+A` trösklar |
| `$0DD6` | Kanal 7:s råsumma; inte formellt divisorn |
| `$0DDE` / `$0DE0` | Kanal 0:s dödzon, centrum +/- `$528` |
| `$0DF2` | Kalibreringsfaktor, 0.16 fixpunkt |
| `$FFD0B0` | Slot 5:s skanningsindex |

## Enhetsfönster

| Område | Enhet |
|---|---|
| `$FC2000`-`$FC207F` | ES5506, `.umask16(0x00ff)`. PAR idx 13, IRQV 14, PAGE 15 |
| `$FC3000`-`$FC303F` | ES5510 host. `$FC31C1` = host offset `$E0`, "Write select GPR+INSTR" |
| `$FC4801 + 2n` | SCN2681/MC68681 DUART. Kanal B panel [Verified], kanal A MIDI [Likely] |
| `$FC6000`-`$FC67FF` | MC68302 DPRAM (thunkbiblioteket) |
| `$FC6800`+ | MC68302 SIM-register. GIMR `$6812`, IPR `$6814`, IMR `$6816`, ISR `$6818`, PBDAT `$6828`/`$6829` (bit 2:0 = ADC-kanalval) |

## Diskformat

Katalogen börjar på filoffset **`0x41E`**, 26 byte per post:

```
+0  typ (word)   +2  namn (12 tecken)   +14 storlek i block (word)
+16 flagga       +18 startblock (long)  +22 reserv
```

Typkoder: `$03` INSTRUMENT, `$1C` SEQUENCE, `$1D` SONG, `$1E` BANK,
`$20` OS, `$21` EFFECT.

Kedjan validerar sig själv: `startblock[n] = startblock[n-1] +
storlek[n-1]`, 17 av 17 i V350. Geometri:
`byte_offset = (track_index*20 + (R-1)) * 512`, `track_index = C*2 + H`.

## Kända okända

Formulering per post: känd anropad adress; syfte ännu inte identifierat.

| Adress eller mål | Status |
|---|---|
| `$F977B0` | Känd anropad adress ur exception-svansen; syfte ännu inte identifierat. |
| `$FF8D44` | Känd anropad adress ur exception-svansen; syfte ännu inte identifierat. |
| `$F8CF9C` | Känd anropad adress ur exception-svansen; syfte ännu inte identifierat. |
| `$FB8E3E` | Känd hoppadress ur exception-svansen; syfte ännu inte identifierat. |
| `$F8DC2E` | Känd anropad adress ur kalibreringssteg 2; syfte ännu inte identifierat. |
| `($00DE).w` | Känd indirekt DUART-dispatchpekare; målrutin ännu inte identifierad. |
| `($00E2).w` | Känd indirekt DUART-dispatchpekare; målrutin ännu inte identifierad. |
| `($00E6).w` | Känd indirekt DUART-dispatchpekare; målrutin ännu inte identifierad. |
| `($8638).w` | Känd indirekt DUART-dispatchpekare; målrutin ännu inte identifierad. |
| `$FFF8DBAC` | Känt hopptabellmål från `$F8DB8C`; syfte ännu inte identifierat. |
| `$FFF8DBBC` | Känt hopptabellmål från `$F8DB8C`; syfte ännu inte identifierat. |
| `$FFF8DBC0` | Känt hopptabellmål från `$F8DB8C`; syfte ännu inte identifierat. |
| `$FFF8DBE2` | Känt hopptabellmål från `$F8DB8C`; syfte ännu inte identifierat. |
| `$FFF8DBF2` | Känt hopptabellmål från `$F8DB8C`; syfte ännu inte identifierat. |
| `$E68E` | Känd slot 5-callback; syfte ännu inte identifierat. |
| `$7CF0` | Känd slot 5-callback; syfte ännu inte identifierat. |
| `$71E6` | Känd slot 5-callback; syfte ännu inte identifierat. |
| `$E63C` | Känd slot 5-callback; syfte ännu inte identifierat. |
| `$E66E` | Känd slot 5-callback; syfte ännu inte identifierat. |
| ROM-handlern bakom `$00DE` | Ännu inte lokaliserad. |
| Rutinen som avslutar `KEYBOARD TUNED` | Ännu inte lokaliserad. |
| Root-directory-parsern med typfiltret för `$03`/`$1E` | Ännu inte lokaliserad. |
| Rutinen som formar `FILE 1  TUTORIAL BNK` | Ännu inte lokaliserad. |

## Avförda tolkningar - bygg inte vidare på dessa

- **[Verified]** SRA bit 7 (Received Break) orsakar inte
  browserregressionen. Testad genom att tvingas hög på `7bc57b8ab45`;
  ingen effekt.
- **[Verified]** MAME saknar inte implicit timerstart (AN414).
  `mc68681.cpp` startar timern vid ACR bit 6-övergång.
- **[Verified]** DUART OPR väljer inte ADC-kanal. OPR är konstant vid
  PAR-fönstret. Kanalvalet är MC68302 PBDAT bit 2:0.
- **[Verified]** `$0DD6` är inte formellt divisorn vid `$006800`.
  Divisorn är D2 register-direkt, även om dataflödet kommer från samma
  kanal 7-summa.
- **[Verified]** ES5506 PAR ska inte läsas på `$FC2069`; den byten är
  strukturellt alltid `$00`. Se MOVEP-avsnittet.

## Coverage

Detta index täcker 51 namngivna adresser i den sorterade översikten:
42 ROM-adresser (varav 15 tillagda från
`keyboard-and-sample-bridge-3.md`-`-6.md`s panel-/MIDI-/schemaläggar-
brygg-spår -- se "Panel-/MIDI-notdispatch" och de nya TRAP-postarna
under "Schemaläggare"), 7 diskresidenta V350-adresser och 1
DPRAM-thunk.

Identifierade: 42. Delvis identifierade: 8. Kända okända: 23 poster.
Listan är avsiktligt ofullständig; den markerar vad som är stabilt nog
att bära vidare till kod och vad som fortfarande kräver mätning.

---

## Bindningsslots - en andra sorts rutinidentitet

Tillagt 2026-08-04. Se `rom-os-abi.md` for arkitekturen och
`../static/os-binding-table.csv` for full tabell.

ROM anropar 342 av 723 slots i bindningstabellen pa RAM `$00801E-$009FF6`, fran
1254 anropsstallen, uteslutande med `jsr/jmp abs.w`. **En rutin som nas via tabellen ar
en annan sorts rutin an en som anropas direkt** - slotmalet kan bytas av OS:et per
version. Posterna nedan bor darfor ange sin `bindningsslot` nar en sadan finns.

Kanda slotkopplingar till rutiner som redan star i det har dokumentet:

| slot | ROM-anrop | mal V1.61 | mal V3.50 | kand rutin |
|---|---|---|---|---|
| `$8030.w` | 203 | `$F97662` | `$F97662` | **OIDENTIFIERAD** - mest anropade adressen i maskinen |
| `$8036.w` | 13 | `$F976CA` | `$F976CA` | kritisk sektion, aterstall SR |
| `$8638.w` | 1 | `$F88300` | `$013FD0` | `irq6_tick_producer` - **V3.50 ersatter den med OS-kod** |
| `$8D50.w` | 2 | `$F8C1BE` | `$F8C1BE` | SCC-konfigurationshjalprutin, anropad av scc1_init/scc2_init |
| `$9818.w` | 40 | `$F92478` | `$F92478` | OIDENTIFIERAD |
| `$8042.w` | 35 | `$FB7788` | `$FB7788` | OIDENTIFIERAD |
| `$8C0C.w` | 26 | `$F8B2E2` | `$F8B2E2` | OIDENTIFIERAD |

`$8638.w` ar ett konkret exempel pa patchmekanismen: `irq6_tick_producer` ligger i ROM i
V1.61 men ersatts av OS-kod i V3.50. Totalt 74 slots gor motsvarande byte.

## Nya rutiner ur den statiska ROM/OS-analysen

### `$FC6200` reset_bridge `[V]`

De 14 byte ROM kopierar fran `$F80078` till DPRAM och hoppar till. Innehallet ar kant
byte for byte:

```
33FC 1F01 00FC6830    move.w #$1F01,(BR0)   ROM $000000 -> $F80000
4EF9 FFFB8E06         jmp    $FFFB8E06      PIO-init
```

Maste kora ur DPRAM eftersom BR0-skrivningen drar undan marken under koden.
Forsta verifierade anvandningen av DPRAM som exekveringsyta.

### `$F8C16C` scc1_init `[V]` / `$F8C188` scc2_init `[V]`

`$F8C16C-$F8C187` respektive `$F8C188-$F8C1A1`, bada avslutade med RTS.
Laddar kontrollblock `($12D8).w` / `($1320).w`, parameter-RAM `$FC6400` / `$FC6500`,
registerbas `$FC6880` / `$FC6890`, anropar slot `$8D50.w`, satter `D0` till `$21`
respektive `$23` och anropar CP-handskakningen.

### `$F8C1A2` cp_command_handshake `[V]`

`$F8C1A2-$F8C1BD`. Busy-wait pa CR bit 0 (FLG), skriv `D0` till CR `$FC6860`, busy-wait
igen. `$21` = ENTER HUNT MODE SCC1, `$23` = ENTER HUNT MODE SCC2, `$81` = CP software
reset (utfardas av OS:et).

### `$008D56` scc1_isr `[V]` / `$008D92` scc2_isr `[V]`

`$008D56-$008D91` respektive `$008D92-$008DCF`, bada avslutade med RTE.
**Identisk adress i bade V1.61 och V3.50.** Anropar gemensam mottagningsrutin
`$00643C`, gor EOI till ISR `$FC6818` med `$2000` (SCC1) respektive `$0400` (SCC2).

### `$00643C` scc_rx_common `[start-V]`

Gemensam mottagningsrutin for bada SCC-kanalerna. Vad den producerar ar **oppet** och
en av projektets tre hogst prioriterade oidentifierade rutiner.

### `$00BEE2` scc_receiver_enable (V1.61) `[V]`

`$00BEE2-$00BF27`. Pollar PB3/LRCLK, satter ENR pa SCM2 och SCM1 med tva
registerhallna fordrojningar, anropar `$FFFF8ECA`, avmaskar SCC1+SCC2 i IMR.
Runtime-observerad. **I V3.50 ligger samma kod pa `$00E48A-$00E4D5`** - adressen ar
harledd via segment 2-regeln och inte runtime-observerad.

## Hogst prioriterade oidentifierade rutiner

| adress | varfor | status |
|---|---|---|
| `$F95EAA` | 1 anrop i V1.61, **33 i V3.50** - storsta versionsskillnaden i materialet | oidentifierad |
| `$00643C` | vad SCC-mottagningen producerar | oidentifierad |

Sparas maskinellt i `../static/routines.csv` med `canonical_status =
unresolved_high_priority`.

### `$F97662` host_port_verified_write_read `[V]`

Tidigare den mest anropade oidentifierade ROM-rutinen. Slot `$8030.w` pekar hit
och statiken har 203 ROM-anropsstallen.

`$F97662` testar först `$03C8.w`:

```asm
f97662  4a38 03c8       tst.b   $03c8.w
f97666  66e4            bne     $f9764c
```

Om `$03C8` är icke-noll returnerar rutinen direkt. Om `$03C8` är noll utför
rutinen en host-port-transaktion via `$FFFC3001`, skriver data med `movep`,
läser tillbaka via `$F97718`, jämför mot ursprungsvärdet och provar om upp till
tio gånger.

`$03C8` är därmed en gateflagga, inte en kö eller mailbox. Identifierade ROM-
skrivare i samma test-/kalibreringskluster sätter och nollställer flaggan runt
lågnivåtransaktionerna; se `../investigations/panel-button-sweep-v350.md`.
