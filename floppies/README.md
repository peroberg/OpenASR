# OpenASR — Floppy Disk Directory

This directory is intended for user-provided floppy disk images for the Ensoniq ASR-10.
**No copyrighted OS images, factory floppy images, or demo disks are distributed with OpenASR.**

## Floppy Disk Images

The ASR-10 boot harness (`asr10booth`) reads 3.5" high-density (1.44 MB / 1.6 MB raw Ensoniq format) and double-density disk images:

- **Raw image format**: `.img`, `.mfi`, `.hfe`, `.dsk`
- **Supported Ensoniq OS releases**:
  - Version 3.50 (recommended baseline: `floppies/asr10booth/V350.img`)
  - Version 1.61 (`floppies/asr10booth/V161.img`)

## Recommended Layout
```text
floppies/
└── asr10booth/
    ├── V350.img    # Ensoniq ASR-10 OS V3.50 floppy disk image
    └── V161.img    # Ensoniq ASR-10 OS V1.61 floppy disk image
```

## Regression Test Prerequisite
The project acceptance test suite (`docs/asr10/regression-test.sh`) defaults to using:
```sh
docs/asr10/regression-test.sh floppies/asr10booth/V350.img
```
Users must supply their own legally obtained `V350.img` disk image to execute the regression tests.
