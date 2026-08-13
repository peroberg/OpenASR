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
| `archive/load_departure.lua` | PC-/schedulerprobe när FDC-pollningen upphör. |
| `archive/load_destination.lua` | A1-destinationsprobe; den första globala A1-kartan är `[RETRACTED]`. |
| `archive/load_dma_probe.lua` | DMA-fönsterprobe för instrumentinläsningen. |
| `archive/load_stall.lua` | Passiv manuell inläsningslogg. |
| `archive/load_timeline.lua` | Instrumentinläsningens avslutande tidslinjeprobe. |
| `archive/tick_stall.lua` | DUART tick-/IMR-/counterprobe under stall. |
| `archive/vector_table_compare.lua` | Jämför levande låg-RAM-vektorer mot ROM-kopian `$F82000`. |
