# SCC RX payload format and recording-RAM layout

Date: 2026-08-23
Machine: `asr10booth`, V3.50
Scope: Phase 4B, post-framing SCC bytes through recorded-sample consumption

## Result

The measured digital contract is:

```text
SCC1 (LEFT) or SCC2 (RIGHT) RX bytes
  -> byte-identical MC68302 descriptor buffer
  -> byte-identical IDMA word copy
  -> consecutive big-endian signed 16-bit PCM words in low RAM
  -> direct ES5506 bank-1 word fetches from the same backing
```

This is `[Verified runtime/current model]` for mono LEFT and RIGHT. There is no
CPU-side endian, sign or width conversion in the measured path.

Stereo `L+R` is `[Verified firmware]` as two separate destination ranges, one
per SCC/control object. It is not runtime-verified: the current run reaches
raw `ERR0R ?57 - REB00T` when the destination instrument is selected, before
Level-Detect and before any SCC event. Consequently the evidence supports
layout **C, separate sample buffers**, not `L R L R` or block interleave, but
the mode-2 runtime path remains `[OPEN]` at that earlier allocation failure.

**Later correction:** Phase 5A captured `D0=$39` at `trap #0`; this is System
Error 57, not 157. It comes from a failed allocator readback at the currently
unmapped CPU address `$7CE510`, before sampling. See
`record-completion-analysis.md`.

## Method and boundary

The retained probes are:

- `../lua/archive/scc-rx-payload-format-probe.lua`: LEFT/SCC1, three
  descriptors and the post-record ES5506 consumer check;
- `../lua/archive/scc2-rx-payload-format-probe.lua`: RIGHT/SCC2 control;
- `../lua/archive/scc-rx-stereo-layout-probe.lua`: bounded L+R failure;
- `../lua/lib/scc_rx_record_probe.lua`: shared observer/source runner.

The source writes only post-framing bytes to the already established hidden
`SCC1RX`/`SCC2RX` test boundary. The MC68302 model owns buffer filling,
descriptor completion and interrupt generation; firmware owns threshold/range
selection and IDMA programming. The probes never write descriptors, IDMA
registers, destination RAM, firmware state or ES5506 state.

Commands used no `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 75 \
  -autoboot_script docs/asr10/lua/archive/scc-rx-payload-format-probe.lua
```

The SCC2 and stereo runs used the same command with their respective script.

## 1. Raw byte and word format

### Byte preservation

The LEFT run completed three SCC1 descriptors:

| input pattern | source range | destination range | bytes | exact matches |
|---|---:|---:|---:|---:|
| counter plus `$7FFF` trigger at `$40` | `$F76606-$F7691F` | `$02C110-$02C429` | 794 | 794/794 |
| `AA 55 AA 55 ...` | `$F76920-$F76C3F` | `$02C42A-$02C749` | 800 | 800/800 |
| `00 00 80 00 FF FF ...` | `$F76C40-$F76F5F` | `$02C74A-$02CA69` | 800 | 800/800 |

Observed IDMA events were three vector `$4D` SCC1 completions followed by
three vector `$4B` IDMA completions. The destination advanced consecutively:

```text
$02C110 -> $02C42A -> $02C74A -> $02CA6A
```

Raw destination words from the third descriptor were:

```text
bytes       big-endian signed     little-endian signed
00 00                 0                     0
80 00            -32768                   128
FF FF                -1                    -1
```

No CPU or IDMA byte swap, sign extension, offset-binary conversion or packing
occurred. The IDMA's word accesses preserve the two byte lanes exactly.

### Firmware interpretation

The prior threshold note named `$00D54A`; a fresh live dump proves the actual
V3.50 runtime address is `$FFD54A`. Low `$00D54A` contains unrelated code.
`$FFD54A-$FFD566` is:

```asm
move.w  $d160.w,D0       ; threshold magnitude
moveq   #$0e,D3
move.w  (A0)+,D1         ; big-endian 16-bit input word
bpl     $FFD556
neg.w   D1               ; absolute value of signed input
cmp.w   D0,D1
bcs     $FFD55C
rts                       ; threshold reached
adda.l  D3,A0             ; 2-byte read + 14 = 16-byte probe stride
cmpa.l  A1,A0
bcs     $FFD550
ori     #1,CCR            ; no crossing in range
rts
```

Thus firmware treats payload words as signed 16-bit amplitudes for threshold
detection. It samples one of every eight words for this decision; that
16-byte stride is a level-detection optimization, not a payload frame size.

### ES5506 interpretation

The current ES5506 bank spaces are `ENDIANNESS_BIG`, 16-bit, word-addressed.
`generate_pcm()` casts each fetched word to `s16`. CPU destination `$02C110`
therefore corresponds exactly to bank-1 word address `$016088`:

```text
CPU byte address = ES5506 word address * 2
$02C110          = $016088 * 2
```

After Cancel/root-key handling, a read tap over recorded bytes
`$02C110-$02CA69` observed:

```text
MC68302 CPU reads       0
ES5506 bank-1 reads     2403
ES5506 first/last word  $016088 / $016534
saved value comparisons 256/256 exact, 0 mismatches
live low-RAM writes     1,317,367
```

The CPU-read zero is therefore backed by a live witness and by the positive
ES5506 fetches. The simplest surviving explanation is also directly observed:
the recorded range is attached as a WaveSample and consumed from the same RAM,
without a CPU conversion pass. An optional later normalize/edit command may
transform data, but no such command ran here.

Status:

- 16-bit: `[Verified runtime/firmware/device]`;
- big-endian: `[Verified firmware/device]` and byte-pattern confirmed;
- signed PCM: `[Verified firmware/device]`;
- 8-bit or 24-bit payload semantics in this recording path: `[DISPROVEN]`;
- physical ADC serial framing/bit width before the post-framing ingress:
  unchanged `[OPEN]`.

## 2. Channel selection and layout

The manual defines Field 2 as `LEFT`, `RIGHT`, and `L+R`. Firmware's `$016F`
gates match those modes uniquely:

| `$016F` | manual mode | accepted SCC continuation | runtime result |
|---:|---|---|---|
| 0 | LEFT | SCC1 only | `$4D`, 794 exact bytes to `$02C110` |
| 1 | RIGHT | SCC2 only | `$4A`, 794 exact bytes to `$02C110` |
| 2 | L+R | SCC1 and SCC2 | blocked before Level-Detect by System Error 57 |

The RIGHT control used constant `$AA` bytes plus the same `$7FFF` trigger.
Firmware selected `$F74B06-$F74E1F`, copied 794/794 exact bytes, delivered one
`$4A` and one `$4B`, and advanced SCC2's destination to `$02C42A`. SCC1 did
not advance.

An earlier RIGHT calibration used only repeating `0000/8000/FFFF`, without a
controlled positive trigger marker. One 800-byte feed then produced two IDMA
ranges, `$F74E06` length 26 and `$F74E20` length 800; the latter was an
unfilled next buffer. That run is not used for a format or pretrigger claim.
It established that the input vector was nondiscriminating, and was replaced
by the explicit `$7FFF` run above. Why that calibration published the second
range remains `[OPEN]` rather than being explained after the fact.

ROM `$F95EB2` prepares destinations. The decisive branch is:

```asm
$F95EF4  SCC1 object +$20 <- first destination
$F95EF8  cmpi.b #2,$016F
$F95F00  mode 2: D3 += D2       ; second destination = first + half
$F95F04  other:  D2 += D2       ; one shared mono allocation
$F95F0A  SCC2 object +$20 <- A3+D3
$F95F0E  SCC1 object +$24 <- D2
$F95F12  SCC2 object +$24 <- D2
```

Mode 2 therefore allocates two disjoint halves and leaves each SCC object with
its own destination pointer. No interleaver appears in `$00643C/$0064BA`, the
type `$0E` consumer or IDMA; each completed range is copied contiguously to its
own object destination. This agrees with the manual's statement that stereo
samples become companion WaveSamples/layers.

The failed live L+R run is retained as negative evidence:

```text
$016F                     0 -> 1 -> 2
instrument selection      raw display `ERR0R ?57 - REB00T` (System Error 57)
level-4 $4A/$4D/$4B       0 / 0 / 0
first SCC byte accepted   no (probe stopped at instrument error)
live low-RAM writes       570,540
```

Do not use reset-time object values after the error as stereo destinations.
The cause was outside Phase 4B and is now localized in Phase 5A; see the
correction above.

## 3. Block size and timing

Observed MRBLR is 800 bytes and each channel has eight descriptors:

| quantity | bytes | 16-bit samples per channel |
|---|---:|---:|
| normal descriptor | 800 | 400 |
| first selected range in this run | 794 | 397 |
| eight-descriptor ring per channel | 6,400 | 3,200 |
| two complete stereo rings | 12,800 | 3,200 per channel |

Pure arithmetic at the two manual sample rates:

| block | 29,761.9 Hz | 44,100 Hz |
|---|---:|---:|
| 800-byte descriptor/channel | 13.440 ms | 9.070 ms |
| measured 794-byte first range | 13.339 ms | 9.002 ms |
| eight-descriptor ring/channel | 107.520 ms | 72.562 ms |

For stereo the two SCCs carry one 800-byte channel block each, so duration per
descriptor remains the same while aggregate payload rate is 119,047.6 or
176,400 bytes/s. These are block-duration calculations, not clock-source or
cycle-timing claims.

## 4. Hypothesis status

| claim | observation | status and falsifier |
|---|---|---|
| RX payload is big-endian signed 16-bit PCM | `$FFD54A` signed-magnitude word reader, byte-exact IDMA, direct ES5506 `s16` fetch | `[Verified]`; falsified by a valid mode that transforms/repacks the recorded range before playback |
| LEFT/RIGHT use SCC1/SCC2 | mode 0 produced only `$4D`; mode 1 only `$4A`; both copied exact source bytes | `[Verified functional mapping]`; physical pin naming remains outside this result |
| stereo is interleaved in one RAM buffer | mode-2 allocator creates separate object destinations and no interleaver exists downstream | `[DISPROVEN firmware]` |
| stereo uses separate sample buffers | `$F95EF8-$F95F12` separates destination halves for mode 2 | `[Verified firmware]`, `[OPEN runtime]`; a successful L+R run with one interleaved destination would falsify it |
| CPU converts recorded PCM after IDMA | zero CPU reads while ES5506 fetched the exact recorded range | `[DISPROVEN measured path]`; a different source/edit mode may still add a conversion |

Physical ADC routing, SCC serial framing, real-board RAM decode, sample clocks,
ES5510 and PB9/PB10/PB11 are unchanged by this result.

## Verification

The final LEFT, RIGHT and bounded L+R-negative probes emitted `PASS`. No C++ or
machine configuration changed, so the existing executable regression suite
was not rerun for this documentation/Lua-only phase.

Phase 4B line accounting is 706 lines added and 2 replaced lines removed:
419 lines of retained Lua source/wrappers, 255 lines in this investigation,
and 32 additions across status, index, Lua inventory and the corrected live
threshold address. The clean additions are the reproducible measurement
harness plus its evidence record; executable machine-model code added: 0.

No `static/*.csv` file was edited and no `-log` was used.
