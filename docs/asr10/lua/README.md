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

## Verktyg

Återanvändbara moduler eller generella prober.

| script | användning |
|---|---|
| `lib/asr10_display.lua` | Läser VFD-output och översätter segmentmönster till rå text. |
| `lib/asr10_regression.lua` | Gemensam testhjälpare för PASS/FAIL, displayväntan och maskinavslut. |

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
| `archive/load_departure.lua` | PC-/schedulerprobe när FDC-pollningen upphör. |
| `archive/load_destination.lua` | A1-destinationsprobe; den första globala A1-kartan är `[RETRACTED]`. |
| `archive/load_dma_probe.lua` | DMA-fönsterprobe för instrumentinläsningen. |
| `archive/load_stall.lua` | Passiv manuell inläsningslogg. |
| `archive/load_timeline.lua` | Instrumentinläsningens avslutande tidslinjeprobe. |
| `archive/tick_stall.lua` | DUART tick-/IMR-/counterprobe under stall. |
| `archive/vector_table_compare.lua` | Jämför levande låg-RAM-vektorer mot ROM-kopian `$F82000`. |
