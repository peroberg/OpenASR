#!/bin/sh
# ASR-10 acceptanstest: V350 ska na FILE 1  TUTORIAL BNK.
# Kravd flagguppsattning dokumenterad i duart-irq6-regression.md.
rm -f error.log
SDL_VIDEODRIVER=dummy \
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_ES5506_HOST=1 ASR10_EXPERIMENT_ES5510_HOST=1 \
ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1 ASR10_DIAG_PAR_VALUE=0x200 \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log >/dev/null 2>&1
grep -q "TUTORIAL BNK" error.log
