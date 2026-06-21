
# running.md

# ASR-10 harness commands

## Build

```sh
git diff --check
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

## No-media regression run

```sh
rm -f error.log
./mame asr10booth \
  -nothrottle -video none -sound none -seconds_to_run 6 -log
```

Useful filter:

```sh
rg "ASR10_FDC_CMD88|ASR10_FDC_CMDF3|ASR10_FDC_CMD46|ASR10_049D_WRITE|ASR10PANEL|PLEASE|LOADING|ASR10HANG|media_mounted|ready=" error.log
```

Expected no-media baseline:

```text
drive_attached=1
media_mounted=0
ready=0
F3/Sense result includes 68,00
Panel shows ENSONIQ ASR-10
No ASR10HANG
```

## Run with raw ASR image

After raw ASR `.img` support exists:

```sh
rm -f error.log
./mame asr10booth \
  -flop "floppies/asr10booth/V161.img" \
  -nothrottle -video none -sound none -seconds_to_run 12 -log
```

Filter:

```sh
rg "Image|Fatal|Error|format=|media_mounted|ready=|ASR10_FDC_CMD88|ASR10_FDC_CMDF3|ASR10_FDC_CMD46|ASR10_FDC_CMD46_RESULT_STORE|ASR10_049D_WRITE|ASR10PANEL|PLEASE|LOADING|ASR10HANG" error.log
```

## Run with V350

```sh
rm -f error.log
./mame asr10booth \
  -flop "floppies/asr10booth/V350.img" \
  -nothrottle -video none -sound none -seconds_to_run 12 -log
```

## Important log terms

```text
ASR10PANEL
ASR10_049D_WRITE
ASR10_PROMPT_SELECT
ASR10_INSERT_DISK_DECISION
ASR10_04B0_COUNTDOWN
ASR10_MEDIA_BRANCH
ASR10_FDC_CMD88
ASR10_FDC_CMDF3
ASR10_FDC_CMD46
ASR10_FDC_CMD46_RESULT_STORE
ASR10FDC_TXN_SUMMARY
ASR10HANG
```

## Commit discipline

Do not commit:

```text
floppies/
*.img
*.hfe unless intentionally using test media policy
error.log
```

Good checkpoint commit:

```sh
git add src/mame/ensoniq/asr10_boot.cpp .gitignore docs/asr10
git commit -m "ensoniq/asr10: checkpoint boot harness findings"
```


---

