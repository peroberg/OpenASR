#!/bin/sh
# ASR-10 acceptanstest: V350 ska na FILE 1  TUTORIAL BNK.
# Korer utan ASR10-miljovariabler.
rm -f error.log
SDL_VIDEODRIVER=dummy \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log >/dev/null 2>&1
grep -q "TUTORIAL BNK" error.log
