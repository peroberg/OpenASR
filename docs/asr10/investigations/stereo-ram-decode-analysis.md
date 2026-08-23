# ASR-10 RAM decode and stereo sample-object continuation

Date: 2026-08-23
Machine: `asr10booth`, V3.50 boot run
Scope: Phase 5C, static/model analysis of the existing Phase 5B evidence

## Result

The current model's RAM configuration is internally inconsistent:

1. `[Verified current model]` ROM `$F8A166-$F8A244` observes four independent
   probe-shadow locations and selects base `$000000`, size `$F80000`.
2. `[Verified firmware/runtime]` firmware constructs allocators inside that
   advertised interval. The L+R WaveSample split legitimately reaches
   `$7CE510` in the selected instrument's nested heap.
3. `[Verified current model]` the CPU `mem_map` has no backing store at
   `$7CE510`. The allocator's writes are not retained and its immediate
   readback is zero.

This classifies the observed failure as missing coherent model decode/backing,
not a different stereo metadata format or an alternate firmware branch.
Adding RAM is outside this phase; no map change was made.

The hardware classification remains deliberately narrower:

- `[Verified hardware capability]` the service manual documents 2, 4, 8 and
  16 MB configurations and a 16 MB address capability.
- `[Likely hardware RAM, 8/16 MB configurations]` `$7CE510` lies in the range
  firmware advertises and allocates for those larger configurations.
- `[OPEN exact hardware decode]` the available evidence does not include a
  digital-board schematic or real-machine bus trace proving the exact decode
  at `$7CE510`.

Stereo continuation remains `[OPEN runtime]`: the path stops before
`$0174F4`, so a complete RIGHT WaveSample is not observed. Static code shows
where continuation would begin, but not enough runtime evidence exists to
name every later operation.

## Evidence boundary

This phase reuses the two archived Phase 5B probes and their final logs:

- `../lua/archive/record-stereo-allocator-probe.lua`
- `../lua/archive/record-stereo-allocator-left-control.lua`
- `/tmp/asr10-phase5b-stereo-final.log`
- `/tmp/asr10-phase5b-left-final.log`

The L+R run ended with PASS and a 462,338-write live witness. The LEFT control
ended with PASS and a 108,502-write live witness. Neither run used `-log`.
Static analysis uses the hash-verified ROM image:

```text
SHA-256 fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf
```

No new runtime probe was required. The model map and the Phase 5B write/read
sequence discriminate the remaining alternatives directly.

## 1. ROM RAM test

### Probe algorithm

ROM `$F8A166-$F8A244` writes four distinct signatures:

```text
$008000 <- $00000000
$408000 <- $00001111
$808000 <- $00002222
$C08000 <- $00003333
```

It reads `$008000` into `D4` and `$808000` into `D5`. Static branch analysis
gives these selected base/size pairs:

| observed aliases | selected base | selected size |
|---|---:|---:|
| `D4=$3333` | `$600000` | `$200000` |
| `D4=$2222` | `$000000` | `$800000` |
| `D4=$1111`, `D5=$3333` | `$600000` | `$400000` |
| `D4=$1111`, `D5=$2222` | `$600000` | `$A00000` |
| `D4=$0000`, `D5=$3333` | `$000000` | `$A00000` |
| `D4=$0000`, `D5=$2222` | `$000000` | `$F80000` |

These are alias-test results, not fixed claims about all physical machines.
The test determines which RAM configuration firmware will use.

### Branch selected in the current V3.50 boot run

The ROM phase of the current run observed:

```text
D4 = [$008000] = $00000000
D5 = [$808000] = $00002222
$F8A210: $0C62 <- $00F80000
$F8A228: $0C4E <- $00000000
```

Both `$F8A210` writes to the longword at `$0C62` were seen by the live write
tap. This avoids inferring execution only from opcode prefetch. Strictly, the
ROM phase selects the branch during the boot that later runs V3.50; V3.50
itself does not execute the ROM sizing decision.

ROM then reserves `$10000`, `$8000`, and `$400`:

```text
initial allocator base  $010000
initial allocator size  $F67C00
initial allocator end   $F77C00
```

Later reservations produce the observed top-level interval
`$02A000-$F71000`. The selected instrument `$02B600` contains the nested heap
`$02B890-$F70A00`.

## 2. Current model decode

The CPU map relevant to this result is:

| range | current backing |
|---|---|
| `$000000-$0FFFFF` | low ROM/low-memory shadow |
| `$100000-$1FFFFF` | RAM shared with ES5506 sample RAM |
| `$408000-$408003` | isolated two-word probe shadow |
| `$808000-$808003` | isolated two-word probe shadow |
| `$C08000-$C08003` | isolated two-word probe shadow |
| `$F00000-$F7FFFF` | RAM |
| `$F80000-$FBFFFF` | high ROM alias |

`probe_or_alias_region_index()` assigns indices 0 through 3 to
`$008000/$408000/$808000/$C08000`. Reads and writes use
`m_probe_or_alias_region_shadow[index][word]`. The four test locations are
therefore independent in the model; they do not alias each other or a common
contiguous RAM allocation.

That behavior makes the ROM test select the maximum `$F80000` interval. The
map does not then provide contiguous storage for that interval. In
particular, `$7CE510` is in none of the mapped ranges above.

The failure is not read-only or write-only:

```text
$F8A554 writes the two-word free-block header at $7CE510/$7CE512
the address-space tap observes both writes
no mapped storage retains either write
the allocator's immediate validation reads the header back as zero
$F8A55E raises System Error 57
```

`[Verified current model]` both write persistence and read backing are absent.
The common cause is the missing coherent decode/backing for memory that the
same model advertised to ROM. A narrow four-byte patch would hide this first
failure without fixing the larger advertised heap.

## 3. Heap comparison

### Common nested heap

The L+R run observed these blocks before the failed split:

| address | decoded size | content |
|---:|---:|---|
| `$02B890` | `$680` | effect/instrument data beginning `2 HALL REVERB` |
| `$02BF10` | `$E0` | first `UNNAMEDLAYER` |
| `$02BFF0` | `$E0` | companion `UNNAMEDLAYER` |
| `$02C0D0` | initially `$F44700` | first `UNNAMED WS` plus recording extent |
| `$7CE510` | expected free `$7A22C0` | calculated remainder header, not an object |

Thus `$7CE510` is not an existing object used by another subsystem. Other
objects occupy the same heap below it; this exact address is where the split
must create a new free block.

### Mono versus L+R allocation

The capacity planner at `$FFBED4-$FFBEF8` uses the same fixed WaveSample
metadata prefix in both modes:

```text
LEFT planned extent  $F44710
L+R planned extent   $7A2310 per channel
metadata prefix      $000120
```

For L+R, `$0174DC` stores `$7A2310` in packed form at the first WaveSample's
`+$F8` field. `$F8B1F0` adds `$120`, and the allocator rounds `$7A2430` to
`$7A2440`:

```text
old block       $02C0D0 + size $F44700
new first size  $7A2440
split address   $02C0D0 + $7A2440 = $7CE510
free remainder  $F44700 - $7A2440 = $7A22C0
encoded header  $2C107A20
```

This is a capacity split of one ordinary WaveSample block. It is not an
allocation of a larger stereo-specific metadata record.

## 4. WaveSample byte comparison

The first `$120` bytes of the LEFT and L+R `UNNAMED WS` objects were extracted
from the Phase 5B object dumps and compared byte for byte. Differences occur
only at these zero-based offsets:

| offset | classification |
|---:|---|
| `$00/$02/$03` | packed allocator header/size |
| `$D0` | mode-dependent field; exact semantics not established here |
| `$F8/$FA` | packed planned endpoint |
| `$104` | post-endpoint field from the incomplete L+R path |
| `$108/$10A/$10C` | later fields from the incomplete L+R path |

The relevant raw excerpts are:

```text
LEFT   +$00  A100 F440
LEFT   +$D0  0000
LEFT   +$F8  F400 4700 1000 0000
LEFT   +$108 F400 4700 1040 0000

L+R    +$00  4400 7A20
L+R    +$D0  7F00
L+R    +$F8  7A00 2300 1000 0000
L+R    +$108 0000 0000 BF40 0000
```

`[Verified]` both paths use the same `$120` WaveSample metadata layout. Bytes
`+$04-$CF`, including the `UNNAMED WS` identity/body, are identical; `+$D0`
differs. The packed header and planned extent differ with capacity, as
expected. Because the L+R object is captured after the split has failed, the
exact semantics of `+$D0` and the divergent later fields remain `[OPEN]`;
they are not evidence for a different object type.

## 5. Stereo continuation

The first WaveSample path is:

```text
$0174D2  load first WaveSample pointer $0C10 = $02C0D0
$0174DC  write planned endpoint $7A2310 at WaveSample +$F8
$0174EC  call $F8B1F0
          -> packed allocator $F8A44E
          -> split header write/read fails at $7CE510
          -> System Error 57
$0174F0  move companion-layer index $D486 to D2   (not reached)
$0174F4  call $F8B1C0                             (not reached)
```

`$F8B1C0` statically enters the parent's nested heap, locates the selected
companion-layer block, calls V3.50 target `$00D2DE`, and restores the outer
context. The exact instruction that later creates or attaches a RIGHT
WaveSample remains `[OPEN]`, because no successful runtime reaches this call.

The discriminating fact is the stop location: continuation does not reject
stereo metadata. It is never entered because the first ordinary WaveSample
resize cannot persist its remainder header.

## Classification and falsifiers

| claim | status | observation that would overturn it |
|---|---|---|
| current model advertises base 0/size `$F80000` | `[Verified current model/runtime]` | a clean run whose ROM writes a different base/size with the same model configuration |
| advertised interval is not coherently backed | `[Verified current model/static]` | a map/backing allocation covering `$7CE510` and retaining the allocator write in the analyzed revision |
| `$7CE510` belongs to the selected firmware heap | `[Verified firmware/runtime]` | allocator roots excluding it or a caller using it as fixed/MMIO state |
| failure is caused by missing model RAM decode/backing | `[Verified current model]` | retained header readback followed by the same Error 57 branch |
| stereo requires a distinct larger metadata object | `[DISPROVEN]` | a distinct stereo object layout rather than the observed ordinary `$120` WaveSample records |
| `$7CE510` is physical RAM in all ASR-10 configurations | `[OPEN]` | requires board decode or a real-machine bus observation for each configuration |
| `$0174F4/$F8B1C0` completes the RIGHT WaveSample | `[OPEN runtime]` | requires an intact run through the call and observation of the resulting object |

The simplest explanation surviving all observations is that the model's
special alias-test shadows and its actual RAM backing describe different RAM
configurations. No separate stereo firmware route is needed to explain the
observed stop.

## Scope and verification

- no `mem_map` or executable code change
- no SCC, ADC, ES5506, ES5510, bank or clock change
- no new probe and no fabricated runtime input
- no `-log`
- no `static/*.csv` edit
- existing Phase 5B L+R and LEFT controls both report PASS

No regression was rerun because Phase 5C changes documentation only and
reuses completed observation logs. A later implementation task must first
choose one coherent advertised RAM configuration and back its entire usable
interval; this investigation does not prescribe that implementation.
