# Stereo RECORD allocator area and object setup

Date: 2026-08-23
Machine: `asr10booth`, V3.50
Scope: Phase 5B, firmware/static analysis and observation only

## Result

`$7CE510` is `[Verified firmware heap]`. More specifically, it is the
calculated header address of the free remainder produced when the first L+R
WaveSample block is resized inside the selected instrument's nested packed-
block heap. It is not a fixed table, MMIO register, stereo header, or second
sample object.

The physical classification is narrower:

- `[Verified hardware capability]` The service manual specifies supported 2,
  4, 8 and 16 MB configurations and says the ASR-10 can address 16 MB.
- `[Verified firmware expectation]` ROM sizes RAM by testing aliases at
  `$008000/$408000/$808000/$C08000`; the path selected in the current model
  constructs a contiguous heap reaching `$F77C00` before later reservations.
- `[Likely hardware RAM, 8/16 MB configurations]` `$7CE510` falls inside the
  address interval firmware treats as RAM for those configurations.
- `[OPEN exact hardware decode]` No available digital-board schematic proves
  the address-line/chip-select decode for this exact byte address. It is not
  promoted to unconditional `[Verified hardware RAM]`.

Stereo sample-object creation is not complete in the current model. Firmware
has already created two adjacent `UNNAMEDLAYER` blocks and the first
`UNNAMED WS` WaveSample, then fails while extending that WaveSample's payload
extent. The companion continuation at `$0174F4` is not reached. Therefore:

- stereo uses a larger metadata object: `[DISPROVEN]`;
- stereo uses two companion layers and per-channel WaveSamples: `[Verified
  firmware/manual]`;
- a complete runtime stereo WaveSample pair: still `[OPEN]` at this boundary.

## Method

Static evidence used the hash-verified interleaved ROM image:

```text
SHA-256 fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf
```

`unidasm -arch m68000` covered `$F8A0C0-$F8A562`, `$F8B188-$F8B260`,
the live high-RAM code at `$FFBED4-$FFBF10`, and OS `$0173E0-$0175BE`.

Runtime probes:

- `../lua/archive/record-stereo-allocator-probe.lua`: L+R heap roots,
  capacity plan, block chain, object prefix, failure and live witness;
- `../lua/archive/record-stereo-allocator-left-control.lua`: identical LEFT
  destination selection without the stereo failure.

Commands used no `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 75 \
  -autoboot_script docs/asr10/lua/archive/record-stereo-allocator-probe.lua
```

The LEFT run substituted the control wrapper.

## 1. RAM sizing and global heap

ROM `$F8A166-$F8A244` writes distinct signatures:

```text
$008000 <- $00000000
$408000 <- $00001111
$808000 <- $00002222
$C08000 <- $00003333
```

It then reads `$008000` and `$808000` and selects one of several base/size
pairs according to aliasing. The live path was:

```text
D4 = [$008000] = $00000000
D5 = [$808000] = $00002222
$F8A210: $0C62 <- $00F80000
$F8A228: $0C4E <- $00000000
```

ROM reserves `$10000`, `$8000`, and a later `$400`, producing initial heap
base `$010000`, size `$F67C00`, end `$F77C00`. Later firmware reservations
move the observed top-level roots to:

```text
$0C4E base  $0002A000
$0C52 end   $00F71000
```

This is a hardware-capacity test, not an arbitrary allocator constant. The
current driver's three isolated alias shadows cause the maximum-size branch;
that result describes the current model's detected configuration, not every
physical ASR-10.

## 2. Nested allocator context

`$F8A11C-$F8A160` selects an object-local allocator context. Given parent
object `A0`, it stores:

```text
$0C5E = A0 + $290
$0C66 = A0 + decode_header([A0])
```

For the L+R destination instrument:

```text
parent              $02B600
parent header       $4000F450 -> decoded size $F45400
nested heap start   $02B890 = parent + $290
nested heap end     $F70A00 = parent + $F45400
```

`[Verified firmware/runtime]` `$7CE510` lies inside this selected instrument's
nested heap, not in a separate stereo pool.

### Header format

The allocator decodes a header by:

```text
lsr.w #4
swap
lsr.l #4
```

The resulting bit 0 is the free flag; the remaining value is block size.
Sizes are rounded to 16 bytes by `$F8A430-$F8A44C`.

Observed L+R chain immediately after the failed split:

| address | encoded header | decoded size | free | observed prefix |
|---:|---:|---:|---:|---|
| `$02B890` | `$68000000` | `$680` | 0 | `2 HALL REVERB` data |
| `$02BF10` | `$0E000000` | `$E0` | 0 | `UNNAMEDLAYER`, layer 1 fields |
| `$02BFF0` | `$0E000000` | `$E0` | 0 | `UNNAMEDLAYER`, layer 2 fields |
| `$02C0D0` | `$44007A20` | `$7A2440` | 0 | `UNNAMED WS` WaveSample |
| `$7CE510` | readback zero | expected `$7A22C0` | expected 1 | missing backing |

Thus the same nested heap carries effects/instrument data, layer records and
WaveSample storage. `$7CE510` itself would hold only the next free-block
header; it is not payload or object metadata.

## 3. Exact origin of `$7CE510`

### Capacity planner

Live high-RAM bytes disassemble as:

```asm
$FFBED4  move.l  $0C6A,D2
$FFBED8  move.w  $0BA4,D0
$FFBEDC  bsr     $FFBF00
$FFBEDE  cmpi.b  #2,$016F
$FFBEE6  move.w  $D486,D0       ; companion layer in L+R
$FFBEEA  bsr     $FFBF00
$FFBEEE  cmpi.b  #2,$016F
$FFBEF6  lsr.l   #1,D2          ; split total capacity per channel
$FFBEF8  move.l  D2,$0BD6
```

`$FFBF00` calls `$F8B188` to decode the selected block size, subtracts the
WaveSample metadata prefix `$120`, and adds that reclaimable capacity to the
running total. The measured writes were:

```text
LEFT  $0BD6 <- $00F44710
L+R   $0BD6 <- $007A2310
```

`[Verified]` `$7A2310` is deliberate per-channel recording capacity. It is
not stale data and not an address.

### WaveSample extent and split

The L+R setup then executes:

```asm
$0174D2  A2 <- [$0C10]          ; first WaveSample = $02C0D0
$0174D6  D0 <- $0BD6            ; $007A2310
$0174DA  lsl.l #8,D0
$0174DC  movep.l D0,($F8,A2)    ; WaveSample planned end
$0174EC  jsr $8BF4.w            ; -> $F8B1F0
```

`$F8B1F0` reads the packed `+$F8` endpoint, converts it back to `$7A2310`,
adds the fixed `$120` prefix, and calls the packed allocator:

```text
requested before rounding  $7A2310 + $120 = $7A2430
rounded request            $7A2440
candidate                  $02C0D0
candidate old size         $F44700
split address              $02C0D0 + $7A2440 = $7CE510
free remainder             $F44700 - $7A2440 = $7A22C0
expected encoded header    $2C107A20
```

The current model accepts the write tap but has no storage at `$7CE510`, so
readback is zero and `$F8A55E` raises System Error 57.

## 4. Stereo object creation boundary

Before the failure, firmware has allocated adjacent layer blocks and a first
WaveSample. The manual independently specifies that stereo recordings use
adjacent companion layers, LEFT in the odd layer and RIGHT in the next even
layer.

The next static instructions are:

```asm
$0174F0  move.w $D486,D2         ; companion-layer index
$0174F4  jsr    $8BEE.w          ; -> $F8B1C0
```

`$F8B1C0` enters the parent nested heap, locates the selected companion-layer
block, invokes the V3.50 object/heap maintenance target `$00D2DE`, and restores
the outer context. It is not reached in the L+R run.

The verified boundary is therefore:

```text
two companion layer records
 -> first UNNAMED WS created
 -> first WS planned endpoint = $7A2310
 -> resize/split fails at $7CE510
 -> companion-layer continuation not reached
```

The exact later instruction that creates or attaches the RIGHT WaveSample is
`[OPEN]`, because the only runtime path stops before `$0174F4`. Calling
`$F8B1C0` itself "RIGHT WaveSample creation" would exceed the evidence.

## 5. Classification and falsifiers

| claim | status | falsifying observation |
|---|---|---|
| `$7CE510` is a firmware heap address | `[Verified firmware/runtime]` | an intact run showing it outside `$0C5E-$0C66` or used as MMIO/fixed table |
| it is the split remainder header | `[Verified firmware/runtime]` | allocator arithmetic or write target differs with the same inputs |
| stereo metadata is intrinsically larger than mono | `[DISPROVEN]` | a distinct larger stereo object type/header rather than two ordinary layer/WS objects |
| address is RAM in the detected max configuration | `[Likely hardware]`, `[Verified firmware expectation]` | board decode or real-machine bus trace shows the address unmapped in an 8/16 MB configuration |
| complete stereo object pair works | `[OPEN]` | requires passing the split and observing both finalized WaveSamples |

The simplest surviving explanation is that the model advertises the maximum
RAM configuration to ROM's alias test but backs only fragments of the address
range that the resulting firmware heap legitimately uses.

## Verification and scope

- L+R: PASS, System Error 57 with 462,338-write live witness
- LEFT control: PASS, Level Detect with 108,502-write live witness
- ROM hash matched the normative manifest
- no C++ or executable machine-model change
- no `mem_map`, SCC, ADC or ES5510 change
- no `-log`
- no `static/*.csv` edit

No model change is proposed as part of this phase. A later implementation task
must represent a coherent detected RAM configuration; patching only
`$7CE510-$7CE513` would satisfy one header read while leaving the advertised
heap internally inconsistent.

Task-scoped line accounting is 525 lines added, 7 replaced or removed, net
+518. Lua accounts for +193/-0 in two archived observation/control scripts.
Documentation accounts for +332/-7 across this investigation, current status,
manifest, Lua index and routine index. The additions retain the raw
reproduction and static proof; executable C++ accounts for +0.
