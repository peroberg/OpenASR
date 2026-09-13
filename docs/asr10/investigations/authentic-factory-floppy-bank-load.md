# Authentic Factory Floppy / BANK Acceptance Investigation

## 1. Purpose

The objective of this investigation is to establish authentic Ensoniq factory
BANK-load acceptance fixtures, trace the complete BANK loading workflow under
unmodified ASR-10 V3.50 firmware in OpenASR/MAME, exercise both single-disk and
multi-disk media-swap workflows, and verify audio playback from the restored
instruments, effects, and sequencer songs.

A secondary goal is to determine whether authentic floppy BANK workloads expose
any remaining emulator defects across the FDC (uPD72069), MC68302 IDMA, storage
interrupt routing, volume identification, ES5510 effect upload, and ES5506
voice/sample-banking playback pipelines.

---

## 2. Media Inventory

A recursive survey of `./floppies/` identified 104 files on disk (96 authentic floppy image/container files across eight subdirectories, plus 8 non-disk documentation, image, and archive artifacts):

| Subdirectory | Total Files | Disk Images | Formats Present | Description |
|---|---:|---:|---|---|
| `floppies/asr10booth/` | 3 | 2 | IMG, ZIP | OS boot disks (`V350.img`, `V161.img`, 1,638,400 bytes each) + 1 ZIP archive |
| `floppies/essential/` | 28 | 26 | IMG, GKH, EDA, TXT, JPG | Essential Sound Disks AD-001..008 (8 IMG, 8 GKH, 8 EDA); Demo Disks 1 & 2 (2 EDA); 1 TXT listing, 1 JPG photo |
| `floppies/ASR10-factory-floppy-disks/` | 8 | 8 | EDA | ASR-BOX1.EDA through ASR-BOX8.EDA |
| `floppies/EPS16plusFactory/` | 31 | 30 | IMG, GKH, PDF | 15 raw IMG (`ED-001..015.IMG`), 15 GKH (`ED-001..015.GKH`), 1 PDF sample list |
| `floppies/EPS16plusForASR10/` | 17 | 15 | HFE, TXT, PDF | 15 HFE bitstream containers (`ED-001..015_ASR10.hfe`), 1 TXT listing, 1 PDF sample list |
| `floppies/transwaves/` | 5 | 5 | EDE | Waveboy Transwave collection WB101.EDE through WB105.EDE |
| `floppies/fx/` | 3 | 3 | HFE | Waveboy FX disks (`FMFX.hfe`, `WBFX12.hfe`, `WBFX38.hfe`) |
| `floppies/waveboy/` | 7 | 7 | IMG | Converted raw images for Waveboy effects/instruments |
| `floppies/` (root) | 2 | 0 | MD, DS_Store | `README.md` and `.DS_Store` metadata |
| **Total** | **104** | **96** | | **96 authentic floppy images / containers + 8 auxiliary artifacts** |

---

## 3. Provenance Caveats

- **Essential Sound Disks (`AD-001` to `AD-008`)**: Authentic Ensoniq factory sound library for ASR-10. High confidence.
- **ASR-10 Demo Disks 1 & 2 (`ASR10Demo-Disk1.eda`, `ASR10Demo-Disk2.eda`)**: Authentic Ensoniq factory demonstration 2-disk set (`ASR-001` and `ASR-002`). High confidence.
- **EPS-16+ Factory Disks (`ED-001` to `ED-015`)**: Authentic Ensoniq factory sound library for EPS-16+ (DD 800 KB geometry, Type `$0017` BANKs). High confidence.
- **Waveboy Disks**: Third-party commercial sound/effect disks (Waveboy Industries). Contain custom effect algorithms, non-standard slot structures, and external volume references. Moderate confidence.
- **CDR-16 MOVE ME*8MB**: Explicitly non-canonical for floppy BANK acceptance (previously established to have authoring metadata anomalies under V3.50).

---

## 4. Format Analysis

Empirical structural examination across container pairs established the following format properties:

### A. Raw Disk Images (`.img`)
- Standard raw sector images.
- High Density (HD, ASR-10 native): 3,200 blocks * 512 bytes = **1,638,400 bytes**.
- Double Density (DD, EPS-16+ native): 1,600 blocks * 512 bytes = **819,200 bytes**.
- Block 0: Boot block / reserved filler.
- Block 1 (`$0200`): OS parameter block.
- Block 2 (`$0400`): Volume identification block. Volume ID string at byte `$21F` (`$041F` relative to block 0 or `$021F` relative to block 1).
- Blocks 3..N (`$0600..$0800`): Root directory table (26 bytes per directory entry).

### B. Giebler Disk Images (`.gkh`)
- [VERIFIED] A 58-byte ASCII/binary header beginning with `TDDFI...` prepended to the exact raw disk image.
- Byte comparison: `gkh[58:] == img[:]` across all AD-xxx and ED-xxx pairs (100% byte-for-bit identity).

### C. Ensoniq Disk Archive (`.eda` / `.ede`)
- [VERIFIED] Created by Giebler / Ensoniq archive utilities.
- Header: Exactly 512 bytes of ASCII text (`\r\nASR-10 Disk...` or `\r\nEPS-16 Disk...`).
- Payload: Blocks 1 through N verbatim, omitting Block 0 filler.
- Trailer: Exactly 1 byte EOF marker (`0x1A`).
- [VERIFIED] Byte comparison between `ASR-BOX7.EDA` and `AD-007.img`:
  `eda[512 : len(eda)-1] == img[512 : len(eda)-1]`.

### D. HxC Floppy Emulator Images (`.hfe`)
- Bitstream container format used by HxC floppy emulator hardware.
- [VERIFIED] MAMEs `./floptool flopconvert auto esq16` extracts verbatim raw images from HFE containers, yielding SHA-256 hashes bit-for-bit identical to factory `.IMG` files.

---

## 5. Conversion and Normalization Results

For analysis and MAME mounting, non-raw containers were converted to standard 1,638,400-byte images in `/tmp/openasr-floppy-investigation/`:

| Source File | Destination File | Tool / Method | SHA-256 |
|---|---|---|---|
| `floppies/essential/ASR10Demo-Disk1.eda` | `/tmp/openasr-floppy-investigation/ASR10Demo-Disk1.img` | Python (512B zero-block + payload + zero padding) | `cbba5259af5be808be464d23f137446710b3de27c17f919ff0a5cf1db5d54ad7` |
| `floppies/essential/ASR10Demo-Disk2.eda` | `/tmp/openasr-floppy-investigation/ASR10Demo-Disk2.img` | Python (512B zero-block + payload + zero padding) | `9b81171a5fb7bac6b15e6dca973ce704805edbf9a2404c135ba9c541ca32f820` |

---

## 6. ASR/EPS Filesystem & BANK Binary Anatomy

### A. Directory Entry Layout (26 bytes at `$0600..$0800`):
- `+$00..$01`: File type word (`$0003` = Instrument, `$001C` = Sequence, `$001D` = Song, `$001E` = ASR-10 Bank, `$0017` = EPS-16+ Bank, `$0020` = Operating System, `$0021` = Standalone Effect).
- `+$02..$0D`: Filename (12 ASCII characters, space-padded).
- `+$0E..$0F`: File size in 512-byte blocks.
- `+$10..$11`: File attribute flags.
- `+$12..$15`: Start block number (32-bit big-endian).
- `+$16..$19`: Reserved / disk metadata.

### B. ASR-10 BANK Object Structure (Type `$001E`):
- Minimum container size: 3 blocks (1,536 bytes / `$0600`).
- `+$0000..$0007`: Bank header magic (`60 00 00 00 34 c0 00 00`).
- `+$0008..$001F`: Bank Name (16 bytes, ASCII).
- `+$0020..$0021`: Bank status / slot populated bitmask (`$FFFE` = slots active).
- `+$0024..$0103`: Eight 28-byte Instrument Slot Descriptors.
- `+$0100..$0103`: Sequence/Song reference header (`$000000FF` = active song; `$00008000` = disabled/empty).
- `+$0104..$011F`: Song/Sequence Reference Descriptor (28 bytes).
- `+$015F`: Appended Bank Effect selector (`$04` = appended effect present).
- `+$0600..`: Appended effect payload (size: `(container_blocks - 3) * 512` bytes).

### C. Slot Descriptor Binary Layout (28 bytes):
- Words 0..7 (16 bytes): Word-strided ASCII volume identity.
  - Word 0: `$00` + ASCII char 0
  - Word 1: **File Directory Index (upper byte)** + ASCII char 1
  - Words 2..6: Attribute byte + ASCII chars 2..6
  - Word 7: Attribute byte + ASCII char 7
- Words 8..13 (12 bytes): Instrument assignment, MIDI parameters, key routing.

### D. Firmware Volume Comparison Semantics (ROM `$F89852..$F89860`):
Disassembly of unmodified V3.50 firmware revealed the exact volume verification routine:
```m68k
$F89852: move.b   (a1), d0
$F89854: cmp.b    (a0), d0
$F89856: bne.b    $F89864
$F89858: moveq    #$6, d0
$F8985A: addq.l   #$1, a0
$F8985C: addq.l   #$1, a1
$F8985E: cmpm.b   (a0)+, (a1)+
$F89860: dbne     d0, $F8985A
```
[VERIFIED] The comparator sets `d0 = 6` and loops with `dbne`, comparing **exactly 7 characters** (skipping stride bytes with `addq.l #1`).
This explains why:
- `AD-005` matches `AD-005P` (word 7 `P` is beyond the 7-character limit).
- `AD-001` matches `AD-0010` (word 7 `0` is beyond the 7-character limit).
- `ASR-001` and `ASR-002` do NOT match (`1` vs `2` is character index 6, within the 7-character loop).

---

## 7. Discovered BANK Census

| Media | Volume ID | Bank Name | Blocks | Effect Selector | Appended Effect | Slots Populated | Dependencies | Classification |
|---|---|---|---:|:---:|---|:---:|---|---|
| `floppies/asr10booth/V350.img` | `OS-V350ID` | `ATRK TUT BNK` | 18 | `$04` | `44DDL+CH+REV` (15 blks) | 1..3 | 100% Local to `OS-V350` | Single-Disk Baseline |
| `floppies/asr10booth/V350.img` | `OS-V350ID` | `TUTORIAL BNK` | 7 | `$04` | `TIGHTREVERB` (4 blks) | 1..6 | Local slots 1..6; Song masked (`+$102=$8000`) | Single-Disk Baseline |
| `floppies/essential/AD-001.img` | `AD-001ID` | `PIANO BANK 1` | 8 | `$04` | `HALLREVERB2` (5 blks) | 1 | 100% Local (`GRND PIANO 1`, `GRAND SONG`) | Single-Disk Clean |
| `floppies/essential/AD-001.img` | `AD-001ID` | `PIANO BANK 2` | 8 | `$04` | `HALLREVERB2` (5 blks) | 1 | 100% Local (`GRND PIANO 1`, `PIANO SONG`) | Single-Disk Clean |
| `floppies/essential/AD-002.img` | `AD-002ID` | `SNDTRACKBANK` | 8 | `$04` | `HALLREVERB2` (5 blks) | 1..2 | 100% Local (`ORCH STRINGS`, `ASR FLUTE`, `ORCHSONG`) | Single-Disk Multi-Inst |
| `floppies/essential/AD-003.img` | `AD-003ID` | `ROADS 2 BANK` | 7 | `$04` | `WARM DDL` (4 blks) | 1 | 100% Local (`ROADS KEYS 2`, `GOTTA GO`) | Single-Disk Clean |
| `floppies/essential/AD-003.img` | `AD-003ID` | `CLN GTR BANK` | 7 | `$04` | `PHASER+DDL` (4 blks) | 1 | 100% Local (`CLEAN GUITAR`, `GUIT ON BUY`) | **Fixture A Selected** |
| `floppies/essential/AD-003.img` | `AD-003ID` | `PERC BANK` | 8 | `$04` | `SMALLROOM` (5 blks) | 1 | 100% Local (`PERCUSSION 1`, `LEMON MRNGUE`) | Single-Disk Clean |
| `floppies/essential/AD-004.img` | `AD-004ID` | `FRENCH HRN BNK` | 8 | `$04` | `CONCERTREVERB` (5 blks) | 1 | 100% Local (`FRENCH HORNS`) | Single-Disk Clean |
| `floppies/essential/AD-004.img` | `AD-004ID` | `ROADS 1 BANK` | 7 | `$04` | `PHASER+DDL` (4 blks) | 1 | 100% Local (`ROADS KEYS 1`, `LIL DARLIN`) | Single-Disk Clean |
| `floppies/essential/AD-004.img` | `AD-004ID` | `AIR CHOIR BNK` | 8 | `$04` | `HALLREVERB2` (5 blks) | 1 | 100% Local (`AIR CHOIR`) | Single-Disk Clean |
| `floppies/essential/AD-005.img` | `AD-005ID` | `JAZZ BANK` | 7 | `$04` | `SMALLROOM` (4 blks) | 1..4 | 100% Local (4 insts + `JAZZSONG`) | Single-Disk Multi-Inst |
| `floppies/essential/AD-006.img` | `AD-006ID` | `STEEL BANK` | 8 | `$04` | `HALLREVERB2` (5 blks) | 1 | 100% Local (`STEEL STRING`, `STEPHANIE`) | Single-Disk Clean |
| `floppies/essential/AD-006.img` | `AD-006ID` | `NYLON BANK` | 7 | `$04` | `SLAP BASS` (4 blks) | 1 | 100% Local (`NYLON GTR 1`, `HEARTSONG`) | Single-Disk Clean |
| `floppies/essential/AD-006.img` | `AD-006ID` | `BASS BANK` | 8 | `$04` | `EQ+CMPRESSOR` (5 blks) | 1..3 | 100% Local (3 bass insts + `BOTTOMS UP`) | Single-Disk Multi-Inst |
| `floppies/essential/AD-007.img` | `AD-007ID` | `DRUM BANK` | 7 | `$04` | `ROOMREVERB` (4 blks) | 1 | 100% Local (`STEREO DRUMS`, `STEREO SLAM`) | Single-Disk Clean |
| `floppies/essential/AD-008.img` | `AD-008ID` | `AD-008 BANK` | 7 | `$04` | `DIST+CHO+REV` (4 blks) | 1..8 | 100% Local (all 8 slots + `TIME BOMB`) | **Fixture C Selected** |
| `floppies/essential/ASR10Demo-Disk1.eda` | `ASR-001ID` | `REZO ASR-BNK` | 14 | `$04` | `REVERB+EQ` (11 blks) | 1..8 | Slots 2,4,6,7,8 & Song on `ASR-001`; Slots 1,3,5 on `ASR-002` | **Fixture B Selected** |

---

## 8. Selected Canonical Fixtures

1. **Fixture A — Single-Disk Clean BANK**:
   - Source: `floppies/essential/AD-003.img` -> `CLN GTR BANK` (7 blocks)
   - Scope: 1 stereo instrument (`CLEAN GUITAR`, 845 blocks), 1 song (`GUIT ON BUY`, 9 blocks), 1 appended effect (`PHASER+DDL`, 4 blocks).
   - Rationale: Fully self-contained, fast loading, exercises stereo voice allocation and sequencer playback.

2. **Fixture B — Multi-Disk Media-Swap BANK**:
   - Source: `floppies/essential/ASR10Demo-Disk1.eda` & `ASR10Demo-Disk2.eda` -> `REZO ASR-BNK` (14 blocks)
   - Scope: 8 instruments, 1 song, 1 appended effect (`REVERB+EQ`), spanning volumes `ASR-001` and `ASR-002`.
   - Rationale: Authentic Ensoniq factory demonstration set specifically engineered to test multi-disk loading and interactive media swapping.

3. **Fixture C — Comprehensive Multi-Instrument Showcase**:
   - Source: `floppies/essential/AD-008.img` -> `AD-008 BANK` (7 blocks)
   - Scope: All 8 instrument slots populated (3,100 blocks = 1.58 MB), 1 song (`TIME BOMB`), 1 appended effect (`DIST+CHO+REV`).
   - Rationale: Exercises full 8-track instrument restoration, high memory allocation, and rich multi-track sequencer playback.

4. **Baseline Control Fixture**:
   - Source: `floppies/asr10booth/V350.img` -> `ATRK TUT BNK` (18 blocks)
   - Scope: 3 instruments (`BLUES DRUMS`, `BLUES BASS`, `BLUES ORGAN`), 1 song (`ATRK TUT SNG`), 1 appended effect (`44DDL+CH+REV`).
   - Rationale: Boot-disk resident factory bank requiring no media changes.

---

## 9. Single-Disk BANK Runtime Acceptance

### A. Fixture A (`CLN GTR BANK` on `AD-003.img`)
- Boot command: `./mame asr10booth -flop1 floppies/asr10booth/V350.img -video none -nothrottle`
- At idle `FILE 1  TUTORIAL BNK`, floppy swapped via Lua `flop:unload()` and `flop:load("floppies/essential/AD-003.img")`.
- Directory refreshed via `COMMAND` (`$06`) -> `SYSTEM` (`$1B`) -> `CHANGE STORAGE DEVICE` (`$23`) -> `FLOPPY` (`$23`).
- Navigation: `LOAD` (`$1A`) -> Down Arrow (`$0A`) -> `FILE 2  CLN GTR BANK` -> `ENTER` (`$23`) twice.
- Observed execution:
  - `t=26.49s`: `[LOADING CLEAN GUITAR  ]` (845 blocks transferred via uPD72069 + MC68302 IDMA)
  - `t=37.49s`: `[LOADING GUIT ON BUY   ]` (9 blocks transferred)
  - `t=40.49s`: `[FILE LOADED           ]`
  - Total load time: 14.00 seconds.
- Effect verification: Button `0x07` (`FX Select`) displayed `[FX:BANK   PHASER+DDL  ]`.

### B. Fixture C (`AD-008 BANK` on `AD-008.img`)
- Navigation: Swapped to `AD-008.img`, selected `FILE 1  AD-008 BANK`, pressed `ENTER` twice.
- Observed execution (reverse slot order):
  - `t=26.01s`: `[LOADING ASR LEAD GTR  ]` (Slot 8)
  - `t=28.01s`: `[LOADING DEMO EFFECTS  ]` (Slot 7)
  - `t=39.01s`: `[LOADING DEMO P5-B3    ]` (Slot 6)
  - `t=41.01s`: `[LOADING DEMO FLUTE    ]` (Slot 5)
  - `t=43.01s`: `[LOADING DEMO SYNTH    ]` (Slot 4)
  - `t=49.01s`: `[LOADING DEMO PERCS    ]` (Slot 3)
  - `t=54.01s`: `[LOADING VERSA BASS    ]` (Slot 2)
  - `t=59.01s`: `[LOADING DEMO-KIT-ASR  ]` (Slot 1)
  - `t=71.01s`: `[LOADING TIME BOMB     ]` (Song)
  - `t=76.01s`: `[FILE LOADED           ]`
- Total load time: 50.00 seconds for 1.58 MB across 9 objects.
- Effect verification: Button `0x07` displayed `[FX:BANK   DIST+CHO+REV]`.

---

## 10. Multi-Disk Media-Swap Runtime Acceptance (Fixture B)

- Source media: `ASR10Demo-Disk1.img` (Volume `ASR-001ID`) and `ASR10Demo-Disk2.img` (Volume `ASR-002ID`).
- Selected `FILE 1  REZO ASR-BNK` from Disk 1.
- Initial load phase (Disk 1):
  - `t=27.01s`: `[LOADING DARK LOOP-1   ]` (Slot 8, `ASR-001`)
  - `t=30.01s`: `[LOADING ASR-FEATURES  ]` (Slot 7, `ASR-001`)
  - `t=47.01s`: `[LOADING JM BASS 2     ]` (Slot 6, `ASR-001`)
  - `t=50.01s`: `[LOADING VIOLA-SYNTH   ]` (Slot 4, `ASR-001`)
  - `t=63.01s`: `[LOADING REZ MINIMOOG  ]` (Slot 2, `ASR-001`)
  - `t=66.01s`: `[LOADING ASR-10        ]` (Song, `ASR-001`)
- Media swap request:
  - `t=70.51s`: Firmware reached Slot 5 (`PIG GTR` on volume `ASR-002`).
  - Firmware halted loading, compared mounted volume `ASR-001` against expected `ASR-002`, detected mismatch, and displayed:
    `[INSERT ASR-002 - ENTER]`
- Live swap execution:
  - At `t=71.0s`, Lua script called `flop:unload()`, waited 1,000 ms, then called `flop:load("/tmp/openasr-floppy-investigation/ASR10Demo-Disk2.img")`.
  - At `t=72.5s`, Lua script pressed `ENTER` (`$23`).
- Resumption and completion (Disk 2):
  - Firmware executed `SENSE INTERRUPT / READ SECTOR`, read Block 2 (`ASR-002ID`), verified volume match at `$F89852`, and resumed loading:
  - `t=76.09s`: `[LOADING PIG GTR       ]` (Slot 5, `ASR-002`)
  - `t=84.09s`: `[LOADING PAN-DEDUT     ]` (Slot 3, `ASR-002`)
  - `t=93.09s`: `[LOADING TRANS KIT-2   ]` (Slot 1, `ASR-002`)
  - `t=104.09s`: `[FILE LOADED           ]`
- Total workflow: Successfully completed full multi-disk dependency loading with live medium exchange under unmodified V3.50 firmware.

---

## 11. Playback Acceptance

Playback was verified on both single-note MIDI Note-On and multi-track sequencer playback with audio capture (`-wavwrite`):

### A. Fixture A Playback (`CLEAN GUITAR` + `GUIT ON BUY`)
- Instrument selection: Button `$02` -> `[CLEAN GUITAR  VOLUME 99]`.
- MIDI Note-On: Note 60 (Middle C).
- Voice allocation:
  - Voice 1 (Left Channel): `CR=$4008`, `START=$04F0F000`, `END=$04F9BF00`, `ACCUM=$02B6CA08` (progressing), `FC=$09F6`, `CS1=[15, 16, 17, 18]`, Translated Physical Address `$F09E1E`.
  - Voice 2 (Right Channel): `CR=$4108`, `START=$04F0F000`, `END=$04F9BF00`, `ACCUM=$02B2E252` (progressing), `FC=$0A11`, `CS1=[0, 0, 0, 15]`, Translated Physical Address `$009E1E`.
  - Sample RAM verification at physical address: `04 27 04 27 04 27 04 27`.
- Sequencer Song Playback (`GUIT ON BUY`):
  - Started via `EDIT` (`$05`) -> `SEQ/SONG` (`$15`) -> `PLAY` (`$1D`).
  - Active voices during playback: 5 to 13 concurrent voices.
- Audio Capture (`scratch/fixture_a_cln_gtr.wav`):
  - Duration: 52.50s (2,520,001 frames at 48 kHz).
  - Audio Peak: **32,714** (99.84% full scale).
  - Audio RMS: **826.55** (2.52%).

### B. Fixture B Playback (`ASR-10` on `REZO ASR-BNK`)
- Sequencer Song Playback:
  - Display: `[ASR-10       STEP  1  ]` -> `PLAY` -> `[INTRO-01      001 0001]`.
  - Active voices: 1 to 7 concurrent voices across multi-disk restored instruments.
- Audio Capture (`scratch/fixture_b_rezo.wav`):
  - Duration: 114.82s (5,511,361 frames at 48 kHz).
  - Audio Peak: **4,436** (13.54% full scale).
  - Audio RMS: **91.98** (0.28%).

### C. Fixture C Playback (`TIME BOMB` on `AD-008 BANK`)
- Sequencer Song Playback:
  - Display: `[TIME BOMB    STEP  1  ]` -> `PLAY` -> `[    -2-       001 0001]`.
  - Active voices: 3 to 7 concurrent polyphonic voices across all 8 loaded instruments.
- Audio Capture (`scratch/fixture_c_ad008.wav`):
  - Duration: 87.32s (4,191,361 frames at 48 kHz).
  - Audio Peak: **32,739** (99.91% full scale).
  - Audio RMS: **557.83** (1.70%).

### D. Baseline Fixture Individual Instrument Acceptance (`ATRK TUT BNK`)
- **BLUES DRUMS** (Slot 1, Button `$02`): Note 36 (Bass Drum) -> Voice 1 (`CR=$4818`, `START=$17B7E800`, `END=$184B5C80`, `ACCUM=$179062B5`, `FC=$0309`, `CS1=[0,0,0,15]`, Physical Address `$02F6FD`). Audio Peak: **7,200** (21.97%), RMS: **142.78**.
- **BLUES BASS** (Slot 2, Button `$08`): Note 60 -> Voice 1 (`CR=$4508`, `START=$14B0E000`, `END=$14C3B500`, `ACCUM=$14C307CB`, `FC=$0728`, `CS1=[0,0,0,15]`, Physical Address `$02961C`). Audio Peak: **12,403** (37.85%), RMS: **361.44**.
- **BLUES ORGAN** (Slot 3, Button `$0E`): Note 60 -> Voice 1 (`CR=$4B18`, `START=$13418800`, `END=$13C7AC80`, `ACCUM=$12ED77E8`, `FC=$00FA`, `CS1=[0,0,0,15]`, Physical Address `$026831`). Audio Peak: **5,417** (16.53%), RMS: **210.18**.
- **ATRK TUT SNG** (Sequencer Song): Full polyphonic playback (22 voices). Audio Peak: **32,768** (100.00%), RMS: **1086.63** (3.32%).

---

## 12. Divergence Analysis

[VERIFIED — established workloads] The current driver baseline completed all exercised authentic factory floppy BANK workloads without an observed emulator divergence.
- Unmodified V3.50 firmware loaded, parsed, verified, allocated, and played all authentic factory floppy BANK workloads without emulator intervention.
- The FDC uPD72069 and MC68302 IDMA transferred all disk sectors cleanly.
- The volume comparator correctly accepted local disk volumes and prompted for external volumes.
- Live floppy media exchange in MAME (`flop:unload()` followed by `flop:load()`) correctly signalled media change to the firmware when confirmed with `ENTER`.
- ES5510 effect payloads uploaded and initialized without allocator hangs.
- ES5506 voice allocation, CS1 per-voice sample banking, and pitch calculation operated correctly.

---

## 13. Fix Evaluation

[VERIFIED] **No driver or device code changes were necessary.**
The existing OpenASR architecture (CS1 banking, 16 MiB sample backing, zero-length loop allowance, and IDMA storage path) is sufficient to support the exercised authentic factory floppy BANK workloads. Zero production code changes were made or required.

---

## 14. Regression Verification

The complete OpenASR regression suite (`docs/asr10/regression-test.sh`) was executed to confirm no regressions against the established driver baseline:
- Total test checkpoints: 20 passed, 0 failed, 0 skipped.
- 14 unit/integration test checkpoints via `run_test`: `boot`, `display`, `button`, `button_upper`, `nodisk`, `file_loaded`, `mc68302_guards`, `interrupt_controller`, `memory_size`, `stereo_round_trip`, `display_protocol`, `panel_input`, `panel_navigation`, `display_field_rewrite`.
- 2 audio playback checkpoints via `run_test_audio`: `note_audio`, `note_audio_wav`.
- 4 rate-mode checkpoints via `run_test_audio_aba`: `audio_rate_mode`, `note_audio_wav (mode 1)`, `note_audio_wav (mode 0)`, `note_audio_wav (mode 1)`.

---

## 15. Epistemic Verdicts

- `[VERIFIED — static]` Ensoniq ASR-10 BANK format (Type `$001E`) uses 28-byte slot descriptors with word-strided volume strings and 1-based directory file indexes in Word 1 upper byte.
- `[VERIFIED — runtime]` V3.50 ROM `$F89858..$F89860` implements a 7-character volume comparator (`moveq #6, d0; dbne d0`).
- `[VERIFIED — runtime]` Authentic single-disk factory BANKs (`CLN GTR BANK`, `AD-008 BANK`, `ATRK TUT BNK`) load 100% of referenced instruments, appended effects, and sequencer songs into RAM under unmodified firmware.
- `[VERIFIED — runtime]` Authentic multi-disk factory BANK (`REZO ASR-BNK`) triggers firmware disk prompt `INSERT ASR-002 - ENTER`, resumes loading upon dynamic media replacement, and completes successfully.
- `[VERIFIED — audio]` All exercised restored BANK states produced sustained non-silent audio through the established ES5506/ES5510 path. Measured peak/RMS values showed no recurrence of the historical silent/corrupt BANK-load failure (peaks 13.5% to 100.0%, RMS 0.28% to 3.32%).
- `[VERIFIED — frozen conclusion]` Authentic factory BANK loading — ACCEPTANCE ESTABLISHED for exercised single- and multi-volume workloads. Reopen only on a new failing authentic BANK workload or materially contradictory evidence. (CDR-16 volume metadata anomalies remain isolated as CD-authoring quirks; authentic factory floppy media establishes ground truth. Zero production emulator code changes required).
