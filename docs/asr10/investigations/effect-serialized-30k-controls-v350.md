# Serialized 30 kHz effect controls — ASR-10 V3.50

## Question

Find two independently documented, persisted 30 kHz-class ASR-10 effect
objects with the same serialized object layout as the V3.50 44 kHz files, then
read `+$62..+$6A` and especially `+$66`.  Firmware RAM, ROM-only effects, and
runtime `$00` values are deliberately not substitutes for this artifact control.

## Search scope

The complete local ASR floppy-artifact set consists of:

| artifact | SHA-256 | type-`$0021` files | usable 30 kHz controls |
|---|---|---:|---:|
| `floppies/asr10booth/V161.img` | `2a5cc161e80001daddf532914e80e854d3079f152197ab3def31055f239182f6` | 12 | 0 |
| `floppies/asr10booth/V350.img` | `2636d085a0f95aedd2378a05a35e44cb0ea6c16e24b41c344ed88d68c8c30e4b` | 12 | 0 |
| `asr10booth-images.zip` | container only | the two images above | 0 additional |

The directory inventory was limited to actual type-`$0021` effect entries.
There are no extracted ASR effect files, other ASR disk-image formats, factory
effect disks, or local research fixtures elsewhere in the repository.  A
bounded external archive discovery search found manual and general resource
descriptions, but no locally verifiable original artifact; it contributes no
byte evidence and is not included in the dataset.

## Deduplication

Each V1.61 type-`$0021` file hashes byte-for-byte identically to the
same-named V3.50 file.  The twelve unique serialized effect artifacts are:

| effect | blocks | SHA-256 |
|---|---:|---|
| `44DDL+CHORUS` | 7 | `0b58e96ecaf251d3496f529d120537ba2262ddde3dd696d3c6f5ce594ef6d9c8` |
| `44DDL+CH+REV` | 15 | `8c571f395a72f8c539d4f76b757f63a217eca81f9dbb0f5a27ef3011cb7e70df` |
| `44DLYLFO+REV` | 10 | `3d168e430e42c9a32dd4f2504c86bdb9439767b4b6c2b3d5f6c22b3f1d74a5fe` |
| `44EQ+DDL` | 10 | `27af713a28e93880bd42840d04b67fe01f571443b897535d5abc7f95360d17fb` |
| `44EQ+DDL+CHO` | 15 | `810fd36a0907de24beab4b15f23ffdf40ae644eb528e42d0a90d820a4e578fdd` |
| `44EQ+REVERB` | 12 | `618c0c6d25ec9e476eaf43c84fd37a31284dcce3d77a212ac4a64df148e81c8c` |
| `44EQ+ROT+DDL` | 11 | `eb17859f1a5e3deeb9d3a54981210d39428a2237fe37ddccd8ba6b4139d26aa8` |
| `44LUSH PLAT2` | 8 | `4e7b8160918220c27fa45d807476f4fa350354209073d55644807a217e07d60f` |
| `44LUSH PLATE` | 8 | `7a3664920a47df87d15b0cf559cedb316d6f792da54b7db11fa16024f5b30c47` |
| `44PARAM EQ` | 6 | `5cdf8e88f505779a17a169d69768acb7a6916633b4f9d1aa19635dafe5f4b18e` |
| `44PERC PLATE` | 8 | `c29989762d0f1d1248c3d7462daa611aa2fbaeceab80ccd945190abab0145ac4` |
| `44ROTO+REVRB` | 11 | `8f8bdf017c323315e91b9d09c746ed4dd45d3e8a21e67cb04fe7a54b2d70059d` |

They are the already documented Version-2 44 kHz cohort.  The duplicate V1.61
copies do not add observations and were not counted as independent controls.

## Result

```text
UNIQUE EFFECT ARTIFACTS EXAMINED: 12
SERIALIZED 30 kHz CONTROLS FOUND: 0
```

No candidate satisfies all three required conditions:

1. an actual persistent serialized ASR effect object;
2. an independently documented 30 kHz class; and
3. the established logical object layout with an addressable `+$62..+$6A`
   window.

Consequently no new `+$62..+$6A` extraction was made, no ROM object was
promoted into the serialized dataset, and no artificial control was created.
The prior result remains the complete available direct differential:

| class | unique serialized artifacts | `+$66` result |
|---|---:|---|
| independently documented 44.1 kHz | 12 | 12/12 `$01` |
| independently documented 30 kHz | 0 | not measured |

## Hypotheses and boundary

| hypothesis | verdict | reason |
|---|---|---|
| H1 serialized `+$66` is persistent effect-dependent metadata | PARTIALLY SUPPORTED, unchanged | direct persistence and the 44 kHz cohort remain established; no new class variation was acquired |
| H2 serialized 30 kHz effects use `+$66=$00` | OPEN | no qualifying artifact exists locally |
| H3 serialized 44.1 kHz effects use `+$66=$01` | SUPPORTED, unchanged | 12 unique documented 44 kHz artifacts retain the prior result |
| H4 `+$66` robustly classifies tested 30 kHz/44.1 kHz classes | OPEN | the serialized negative class is absent |
| H5 `+$66` is explicit sample-rate metadata | OPEN | no semantic evidence was added |
| H6 `+$66` is a broader operating-mode field | PARTIALLY SUPPORTED, unchanged | this artifact search does not distinguish the existing alternatives |

Safe terminology remains **serialized effect operating-mode byte** or
**candidate rate-class field**.  Explicit sample-rate semantics, physical rate
control, and every emulator-model conclusion remain open and unchanged.

## Artifact gap and single next experiment

The missing artifact is an original or archive-quality ASR-10-compatible disk
image (or directly extracted effect file) containing at least two different,
independently documented **30 kHz** effect algorithms in the compatible
serialized layout.  Once one is locally available, verify its effect identity
and independent class evidence, then read only `object +$62..+$6A`.  A verified
`+$66=$01` is an immediate counterexample; two independent `$00` controls are
the minimum positive result.

```text
RESULT: INCOMPLETE / ARTIFACT MISSING
MODEL CHANGE JUSTIFIED: NO
```
