# RECORD completion, Level Detect, and sample-object creation

Date: 2026-08-23
Machine: `asr10booth`, V3.50
Scope: Phase 5A, firmware analysis and observation only

## Result

There are two separate outcomes that must not be merged:

1. Mono LEFT completes RECORD, creates a usable WaveSample object and reaches
   direct ES5506 playback. This path is `[Verified runtime/firmware]`.
2. L+R fails during destination setup, before Level Detect and before RECORD.
   The failure is System Error **57**, not 157, and is caused in the current
   model by a missing CPU-visible RAM backing at the allocator's calculated
   split address `$7CE510`. This boundary is `[Verified current model]`; the
   real-board RAM decode remains `[OPEN hardware]`.

Thus the earlier framing "RECORD reaches ERROR ?57 before producing an object"
is `[DISPROVEN]`. Mono object creation succeeds. The error belongs to the
separate stereo allocation path.

## Method

Retained probes:

- `../lua/archive/record-completion-mono-probe.lua` feeds the same three
  post-framing SCC1 descriptors used in Phase 4B and traces stop, range
  finalization, root-key selection, metadata and ES5506 reads.
- `../lua/archive/record-completion-error57-probe.lua` traces the L+R hard
  error, allocator decisions, stack/caller code and the calculated RAM split.
- `../lua/archive/record-completion-left-allocation-control.lua` applies the
  same allocator probe to LEFT and verifies that it reaches Level Detect.
- `../lua/lib/scc_rx_record_probe.lua` remains the shared post-framing byte
  source and observer.

No descriptor, IDMA register, firmware state, destination byte or object field
was fabricated. Commands used no `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 75 \
  -autoboot_script docs/asr10/lua/archive/record-completion-mono-probe.lua
```

The two allocation runs used the same command with their respective scripts.

## 1. System Error 57

### Numeric identity

The raw VFD decoder displays:

```text
ERR0R ?57 - REB00T
```

The trap observation removes the ambiguous first display glyph:

```text
D0 at trap #0       $0039 = decimal 57
$00C0 error word    $0039
trap instruction    $F8A562
saved return PC     $F8A564
```

ROM code is explicit:

```asm
$F8A55E  move.b  #$39,D0
$F8A562  trap    #0
```

`[Verified]` This is System Error 57. The prior normalization to "ERROR 157"
was wrong and is superseded.

### Call chain

The live user stack and runtime code give this chain:

```text
$00404E  jsr (A0)                  generic selected action
  -> sampling destination setup around $017400
     -> $0174EC  jsr $FFFF8BF4     selected layer/sample record
        -> ROM $F8B1F0-$F8B260
           -> $F8B250 jsr $FFFF8972
              -> ROM allocator $F8A44E-$F8A562
                 -> $F8A55E/$F8A562 System Error 57
```

The L+R setup also has a second call at `$0174F4`, using the companion-layer
index in `$D486`. The observed hard error occurs while the first allocator
operation is splitting the large recording block; it does not reach Level
Detect, SCC or IDMA.

### Exact failing model boundary

The accepted allocator candidate was valid:

```text
candidate A0              $02C0D0
candidate header          $7000F440
requested split extent    $007A2440
calculated remainder      $7CE510
heap lower bound $0C5E    $0002B890
heap upper bound $0C66    $00F70A00
```

Firmware then attempted the remainder-header write:

```text
$F8A554 -> $7CE510  word $2C10
$F8A554 -> $7CE512  word $7A20
final readback             $00000000
```

The negative readback has two live witnesses: 462,338 low-RAM writes during
the measurement window and the two positive writes at `$7CE510/$7CE512`.

The current CPU `mem_map` maps `$000000-$1FFFFF`, three isolated probe aliases,
and then `$F00000+`; it has no RAM backing at `$7CE510`. Therefore:

- `[Verified current model]` the allocator's write is discarded and the next
  header read returns zero;
- `[Verified firmware]` `$F8A44E-$F8A562` treats the resulting block state as
  inconsistent and raises System Error 57;
- `[DISPROVEN]` System Error 57 is caused by SCC, ADC, Level Detect, IDMA or
  incomplete PCM in this run;
- `[OPEN hardware]` the real ASR-10 decode that makes this heap range visible.

The LEFT control reached `$0D04=1` without a trap, with a live 108,502-write
witness. This isolates the failure to L+R's larger/split allocation.

## 2. RECORD state machine

The measured mono transitions are:

| state | writer | observed role |
|---:|---:|---|
| `$0001` | `$FFBE66` | Level Detect active |
| `$0002` | `$FFBFBA` | WAITING after Enter/record start |
| `$0003` | `$0065D2` | SCC threshold/range accepted; RECORDING |
| `$0000` | instruction `$FFC06C` | recording stopped; object finalization begins |

The Lua write callback reports PC `$FFC072` for the final write because it is
observed after the `clr.w $0D04` instruction; static code places the writer at
`$FFC06C`.

The RECORDING loop at `$FFBFBA-$FFC048` accepts:

- panel code `$23` or `$25`, both branching to `$FFC04C`;
- scheduler tag `$90E8`, also branching to `$FFC04C`;
- ordinary scheduler/service traffic, which returns to the loop.

In the measured manual stop:

```text
BTN_22  no state, display or metadata change over 600 ms
BTN_23  state 3 -> 0 and root prompt within 20 ms
```

`[Verified measured path]` BTN_23 is the sample-complete signal in this run.
`[DISPROVEN measured path]` BTN_22 stops this recording. The exact panel name
for code `$25` is not assigned here.

## 3. Level Detect

Level Detect and recording completion are distinct stages:

```text
instrument selected       $0D04=1
Enter/record start        $0D04=2, WAITING
SCC payload threshold     $0D04=3, RECORDING
Enter during RECORDING    $0D04=0, finalize
```

`$FFD54A-$FFD566` reads a big-endian signed 16-bit word, computes its absolute
magnitude, compares it with `$D160`, then advances 16 bytes. The first test
descriptor contains a controlled `$7FFF` marker and reaches the verified SCC
`$90E8` path. Three descriptors produced three `$4D`, three `$4B` and three
IDMA completions.

Therefore `[Verified]` the tested threshold decision uses PCM words, not only
descriptor metadata. Threshold crossing starts RECORDING; it is not the event
that finalizes the finished object.

## 4. Mono range finalization

The common completion path begins at `$FFC04C`. For LEFT it calls `$FFC230`
with the current sample object and the final recording endpoint.

`$FFC230-$FFC294` computes:

```text
relative end = endpoint - object - $120
```

For this run:

```text
object base       $02BFF0
payload start     $02C110 = object + $120
payload end       $02CA6A (exclusive)
relative length   $0000095A = 2,394 bytes
```

It writes the native MOVEP address fields as follows:

| object field | observed value | verified role in this recording |
|---:|---:|---|
| `+$F0` | `0` | sample start offset |
| `+$F8` | `$095A` | sample end offset |
| `+$100` | `0` | loop/start companion field |
| `+$108` | `$095A` | loop/end companion field |
| `+$D0` | `$7F` | completion-set byte; exact wider semantics `[OPEN]` |
| `+$22` | cleared | pointer/state field; exact ownership `[OPEN]` |

There was no metadata change after BTN_22. BTN_23 produced exactly seven byte
changes, including `$7F` and the two `$095A` packed endpoints.

## 5. Root key and sample-object creation

After range finalization, `$FFC098-$FFC0A0` enters `$FFBF10`, which displays
the root-key prompt and waits for a keyboard or Enter event. A keyboard event
from C set `$D489=$3C`; `$FFC118` then copied it to object `+$AA`:

```text
$02C09A = $3C
```

The continuation initializes sample fields, calls the ROM object/list helpers,
updates the parent instrument/keymap, and returns to the mode screen. The
post-root snapshot observed 18 persistent byte changes, including:

```text
parent/header      $02B600: $40 -> $60
sample root key    $02C09A: $43 -> $3C
sample field +$EE  $02C0DE: $03 -> $00
sample field +$110 $02C100: $19 -> $15
new allocator/list records at $02CA70 and $02CC00
```

The exact names of every allocator/list field remain `[OPEN]`; assigning them
from shape alone would exceed the evidence. The object is nevertheless
functionally accepted: after root-key handling the CPU read the PCM range zero
times while ES5506 bank 1 fetched it 2,395 times, and 256 retained reads
matched RAM 256/256.

`[Verified runtime]` The mono sample object is complete enough for immediate
playback, and no CPU PCM conversion occurs during finalization.

## 6. Hypothesis revisions

| hypothesis | previous | observation | new status |
|---|---|---|---|
| RECORD completion fails with Error ?57 | working premise | LEFT reaches root-key, object finalization and ES5506 fetch | `[DISPROVEN]` |
| raw `?57` means Error 157 | assumed in Phase 4B prose | trap `D0=$39`, `$00C0=$39`, ROM literal `#$39` | `[DISPROVEN]`; System Error 57 `[Verified]` |
| System Error 57 is caused by incomplete SCC/audio data | `[OPEN]` | it occurs before Level Detect/SCC; exact missing RAM readback observed | `[DISPROVEN]` |
| mono recording creates a native playable object | `[OPEN]` | endpoint fields, root `$3C`, object/list writes and direct ES fetch | `[Verified current model]` |
| L+R is blocked by absent CPU-visible RAM at its split address | unclassified | writes at `$7CE510/$7CE512`, zero readback, no map there, LEFT control passes | `[Verified current model]`; real decode `[OPEN]` |

The L+R RAM hypothesis would be falsified by a model with correct backing at
the firmware-declared address that still returns zero or reaches the same
`$F8A55E` branch with an intact remainder header.

## Next discriminating experiment

Temporarily back the currently unmapped CPU interval needed to make the
firmware heap contiguous through `$7CE510`, with no SCC/ADC/ES changes, then
repeat only destination selection in L+R mode. Stop at the first of:

1. Level Detect entry, which would verify the RAM-decode diagnosis; or
2. a different exact allocator read/write failure, which would define the next
   missing RAM boundary.

Do not feed SCC bytes in that experiment until L+R reaches Level Detect. The
test is about CPU RAM visibility, not audio transport.

## Verification and scope

- `record_completion_error57`: PASS
- LEFT allocation control: PASS
- `record_completion_mono`: PASS
- mono transfer: 2,394/2,394 matching bytes, `$4D` x3, `$4B` x3
- final consumer: CPU reads 0, ES5506 reads 2,395, retained matches 256/256
- unchanged Phase 4B control: PASS, 2,394/2,394 matching bytes, `$4D` x3,
  `$4B` x3, live witness 1,317,367
- no C++ or executable machine-model change in Phase 5A
- no `mem_map`, ADC, SCC routing, ES5510, clock or bank change
- no `-log`
- no `static/*.csv` edit

Task-scoped accounting from the pre-Phase-5A line counts is 748 lines added,
18 replaced or removed, net +730. Lua accounts for +354/-0: three archived
reproduction/control scripts plus 128 opt-in trace lines in the shared runner.
Documentation accounts for +394/-18. Executable C++ accounts for +0. The net
addition preserves the raw observations and makes all three paths directly
reproducible; none of it remains in the machine model.
