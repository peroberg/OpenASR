# OpenASR — ROM Directory

This directory is intended for legally obtained Ensoniq ASR-10 system ROM dumps.
**No copyrighted ROMs or firmware binaries are distributed with OpenASR.**

## Required ROMs for `asr10booth`

Place the ROM binary files or zip archive in this directory:

### Directory Structure (Uncompressed)
```text
roms/
└── asr10booth/
    ├── asr-648c-lo-1.5b.bin  (131,072 bytes, CRC32: 8e437843, SHA1: 418f042acbc5323f5b59cbbd71fdc8b2d851f7d0)
    └── asr-65e0-hi-1.5b.bin  (131,072 bytes, CRC32: b37cd3b6, SHA1: c4371848428a628b5e5a50e99be602d7abfc7904)
```

### Zip Archive (MAME Convention)
Alternatively, MAME standard zip packaging allows:
```text
roms/
└── asr10booth.zip
```
containing the two files above.
