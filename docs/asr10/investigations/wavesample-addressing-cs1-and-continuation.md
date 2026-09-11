## A. EXECUTIVE RESULT

Investigation date: 2026-09-11. Baseline: `9b01d2d26900bd127e75e5edfc29f3d19160a0ec`. Completed investigation; no production changes.

- [VERIFIED — documentary] Ensoniq explicitly discusses ordinary WaveSamples exceeding 2M samples, separately from RAMTrack AudioSamples; the modulation RANGE is not a universal payload limit.
- [OPEN] The exact maximum supported individual WaveSample remains unestablished. Field capacity, available memory, and a successfully playable allocation are different limits.
- [VERIFIED — representation] Packed object offsets, a `$120`-byte WaveSample header, shared PCM ownership, and fixed-point sample/loop offsets agree between specification, firmware, disk data and live objects.
- [OBSERVED] An authentic JM DRUMS voice crosses `$6FFFFE → $700000` with unchanged `[6,7,8,9]` translation; source-file PCM, loaded memory and fetched data agree.
- [OBSERVED] JM DIGI SYN makes five autonomous bidirectional loop turns without CS1 or address-register rewrites and with IRQE clear.
- [OBSERVED — SYNTHETIC TEST] Unmodified firmware given a large reverse endpoint programs `[6,9,10,11]`. Consecutive initial values are not the only conditional firmware behavior.
- [INFERRED] Bound, mode-specific firmware callbacks implement continuation within one PCM owner using transwave events and remapping, not a demonstrated chain of new WaveSample objects.
- [VERIFIED — current mechanism] MAME's unconditional `START == END` stop blocks the exercised large-endpoint setup before continuation; a tap-free control reproduces the stop.
- [OBSERVED] Ordinary OS setup reads a CS1-table word before deriving the fourth word. Existing advice to remove readback conflicts with executed firmware.
- [OPEN — physical] Exact translation storage, voice synchronization, read decode, DTACK and board ownership remain unproven.

## B. WAVESAMPLE DATA MODEL

### Objects and units

The following is the current V3.50 loaded-OS representation, with boot ROM 1.50B.
It is not a claim that every historical EPS-family release has identical limits.
Manufacturer evidence is detailed in J; executable evidence and addresses are in I.

| Object/field | Meaning and units | Evidence level |
| --- | --- | --- |
| Root array beginning `$00101C` | CPU pointers to instrument allocations | [OBSERVED] live JM and TUTORIAL loads |
| Instrument `+$64 + 4*l`, l=0..7 | Packed relative byte offsets to up to eight layer objects | [VERIFIED] object decoding, loaded OS and External Command Specification |
| Instrument `+$84 + 4*w`, w=1..127 | Packed relative byte offsets to WaveSample objects; a zero entry is absent | [VERIFIED] same evidence |
| WaveSample `+$00` | Packed allocator block metadata, including allocation extent and free flag | [VERIFIED — static], corroborated by captured blocks |
| WaveSample `+$0A` | Twelve name characters in alternating bytes | [OBSERVED], specification corroboration |
| WaveSample `+$22` | PCM-owner WaveSample ID; zero means this object owns its data | [VERIFIED] OS owner-resolution instructions; [OBSERVED] header-only copies |
| WaveSample `+$24` | Copy-layer metadata | [VERIFIED — specification]; not needed by the measured PCM-owner resolution |
| WaveSample `+$EE` | Playback/loop mode; exercised 0=forward one-shot, 1=reverse one-shot, 3=bidirectional loop | [OBSERVED] ordinary 0/3; synthetic 1. Mode 2 forward-loop identification is static/specification |
| `+$F0` | Sample start offset | [VERIFIED] packed fixed-point offset relative to owner PCM |
| `+$F8` | Sample end offset | [VERIFIED] same units; not itself the allocator's block size |
| `+$100` | Loop start offset | [VERIFIED] same packed representation |
| `+$108` | Loop end offset, with sub-sample precision used by ES5506 END | [VERIFIED] same packed representation |
| Owner object `+$120` | First PCM byte | [VERIFIED] OS addition, specification and file/memory/fetch comparison |

For four bytes `b0,b1,b2,b3` at an instrument pointer-table entry:

```text
relative_byte_offset =
    (((b3 >> 4) << 16) | (b0 << 8) | b2) << 4
object_address = instrument_address + relative_byte_offset
```

The packed offset has 24 meaningful byte-address bits with 16-byte alignment.
For example, the specification's packed `$2300 $4510` decodes to `$00123450`.
This is an object offset, not a physical-page list.

Each sample/loop field is read with `MOVEP.L`: bytes at F, F+2, F+4, F+6 form a big-endian 32-bit value R.

```text
integer 16-bit-word offset = R >> 9
integer byte offset        = (R >> 8) & ~1
loop-END word + 4-bit fraction = R >> 5
```

Thus the raw representation has 23 integer word-offset bits and nine fractional bits,
equivalently 24 integer byte-offset bits and eight fractional bits.
This does not establish that every consumer accepts every bit pattern or every fractional value.
The unsigned representational ceiling is word offset `$7FFFFF`, or even byte offset `$FFFFFE`;
it is not a proved maximum allocatable or playable length.

The firmware's resize routine uses the greatest relevant sample END among an owner and its copies,
shifts right eight, and includes the `$120` header before allocation/rounding.
This independently establishes byte units in allocation.
Do not infer valid payload length from allocated padding, and do not globally assume an inclusive/exclusive endpoint convention without examining the operation concerned.

### Layer selection and one-voice identity

[OBSERVED] For the measured note, the selected layer's key-map byte is at
`layer + 6 + 2*note`; the covered note range is 21..108.
The selected WaveSample object becomes `voice_record+$1E` at OS `$007892`.
The voice record is `$008000 + $D8*v`.
A WaveSample copy can supply different parameters while referring to the original owner's PCM.

The full measured example is:

```text
Instrument $6C3000, JM DRUMS
  → Layer 1 $6C3290, DRUMS
  → note 100, key-map byte $6C335E = 15
  → WaveSample 15 at $6F7E00, owner ID 0
  → PCM $6F7F20
  → hardware voice 1, record $0080D8
  → CS1 entry 0 at $FF7F00
```

This establishes one selected WaveSample for this voice, not a rule that a keypress produces only one voice:
layering and stereo can involve multiple voices.

### What is and is not a size limit

[VERIFIED — ASR manual] MOD AMT/RANGE scales wave modulation, with RANGE values up to 2 MegaSamples.
The separate maximum loop-length statement is 1.5 MegaWords.
Neither statement identifies the maximum individual PCM allocation.

[VERIFIED — documentary] The November 1995 ASR Service Manual, printed p.31, describes playback of regular WaveSamples greater than 2M samples, independently of RAMTrack AudioSamples. It records a fixed 30 kHz problem on 8/10 MB configurations. This is ASR-specific evidence against a universal 2M-sample payload cap. [Ensoniq service manual](https://www.deepsonic.ch/deep/docs_manuals/ensoniq_asr_service_manual.pdf)

[DISPROVEN — universal bound] “Every individual ASR WaveSample is limited to one static 4 MiB logical window” is not a viable universal model.
[OPEN] An exact maximum individual size, including firmware version, memory configuration, editing/import path and allocation overhead, remains unresolved.
The report does not replace that unknown with “16 MB per WaveSample.”

## C. MEMORY / ALLOCATION MODEL

[VERIFIED — static functional design] The examined allocator manages contiguous objects in a contiguous instrument allocation.
It tracks total free space and the largest free extent, coalesces adjacent free blocks, moves blocks to compact space, and fixes relocated pointers.
The inspected representation contains a single owner-data base plus offsets, not an extent list.

Important routines:

- `$F8A430`: allocation rounding is context-dependent: 512 bytes at the top level, 16 bytes for nested objects.
- `$F8A44E...`: scans free extents; `$C6A` is total free and `$C6E` largest free.
- `$F8A246...`: compares the request with both quantities.
- `$F8A2A8...`: scans/coalesces; `$F8A320–338` moves contiguous data; `$F8A350–388` fixes pointers.
- `$F8B1F0–B260`: resolves the owner and sizes against owner/copy sample ends.

Allocator header decoding follows `LSR.W #4; SWAP; LSR.L #4`.
The decoded low bit is the free flag; the remaining value is the byte extent.
These are static instructions plus observed valid object layouts, not a newly executed fragmentation/compaction experiment.

[INFERRED] A shortage of contiguous space is addressed by relocation/compaction, not by making one WaveSample a scatter/gather list.
No elaborate allocate/free fragmentation experiment was warranted after identifying this representation and allocator design.
Physical DRAM contiguity is a separate board-level question.

### Actually loaded material

Values below are live decoded instrument allocation bytes, not an inference from whole-bank file size.
“Largest END offset” is the integer byte offset, not a universal exact payload-count convention.

| Instrument | Allocation bytes | Layers | WS headers | PCM owners | Largest owner END offset, bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| JM DIGI SYN, separately loaded | 171,008 | 5 | 5 | 1 | 165,408 |
| OB-8*, TUTORIAL bank | 85,504 | 4 | 4 | 1 | 80,446 |
| JM CLAV | 277,504 | 4 | 8 | 2 | 150,490 |
| HIGH STRINGS | 33,792 | 3 | 9 | 3 | 15,956 |
| MOOG POP 1 | 20,480 | 4 | 4 | 1 | 15,700 |
| DEMO PERCS | 203,776 | 1 | 24 | 18 | 26,878 |
| JM DRUMS | 369,664 | 8 | 57 | 12 | 57,622 |

[OBSERVED] JM DIGI SYN has one 165,712-byte owner block and four `$120`-byte header-only copies.
Counting five WaveSamples as five PCM allocations would be wrong.
JM DRUMS demonstrates many independently located PCM owners in one instrument.

[VERIFIED — representation; INFERRED — larger instances] An instrument can exceed one voice's window through the sum of many WaveSamples and metadata.
An individual active voice need not address that sum.
No authentic >4 MiB instrument or individual WaveSample was loaded in the present 2 MiB emulator.

A bounded offline inventory of the existing CDR1 image found 613 instrument entries across 105 directories.
Its largest individual instrument file was PWRGTRCHDS, 2,043 × 512 = 1,046,016 bytes.
This is only a directory/file-size result.
The archive's CDR3 Studio Essentials set is incomplete: r16, r18, r19 and r24 are missing.
It did not supply a complete large fixture, and missing volumes were not guessed or reconstructed.

A current normal RECORD-left setup reached level detection with a WaveSample at `$62BFF0`,
PCM `$62C110`, planned byte capacity `$1C4710`, packed END `$1C471000`,
and owner allocation `$1C4A10`.
That is an allocation/setup witness, not evidence that this amount of audio was recorded.
Old opcode-read-tap “entry” counts from the allocator probe are not reliable execution counts because of prefetch.

## D. CS1 TRANSLATION MODEL

### Firmware setup

[VERIFIED — executed ordinary setup] OS `$007B9C–7BC6` resolves the PCM owner,
adds `$120`, and shifts its byte pointer left eight into D3.
The entry at `$F8E256` separates the upper physical-megabyte nibble from the intra-megabyte fixed-point offset:

```text
P = owner_WaveSample + $120
N = (P >> 20) & $F
B = P & $FFFFF

D3 = B << 8
D4/D5/D7 += D3             ; mode-selected START/END/initial position
table[0..2] = N,N+1,N+2    ; ordinary branch
```

OS `$007C68` then READS table word 2, adds one, and writes table word 3.
Runtime observations show `$FF7F04=8 → $FF7F06=9`.
The PC printed by a bus tap can be ahead of the writing/reading instruction;
e.g. a read at instruction `$007C68` appears with PC `$007C6A`.

[OBSERVED — SYNTHETIC TEST] The large reverse branch changes words 1 and 2 before that read,
yielding `[6,9,10,11]`.
Therefore “the firmware always writes N,N+1,N+2,N+3” is too strong.
This is not evidence that the PCM allocation is fragmented.

The initialized voice/table relationship and current driver use entry `v-1` for voices 1..31.
Voice 0 shares entry 0 in the model.
Only voice 1's full note lifetime was measured here; the complete relationship also rests on initialization/static/source evidence.

### Current MAME fetch formula

For a 21-bit logical wavetable word address W and current hardware voice v:

```text
entry = (v > 0) ? v - 1 : 0
page  = (W >> 19) & 3
Pbus  = (table[entry][page] << 20) | ((W & $7FFFF) << 1)

backing_address = Pbus % $200000
```

`Pbus` is the translated firmware-visible byte address.
The modulo step is the emulator's present storage topology, not a proven physical ASR wiring formula.
The table contains independently stored 16-bit words in MAME.
Its physical storage width is unknown.

[VERIFIED — implementation] See [driver mapping](../../../src/mame/ensoniq/asr10_boot.cpp), especially `voice_bank_r/w` and `es5506_wavetable_r`.
All four ES5506 bank address spaces use that callback.
That does not prove four independently wired ASR hardware banks.

[OBSERVED] At `W=$080000`, page changes from 0 to 1.
The JM DRUMS fetch moves from physical megabyte 6 to 7 with no CS1 rewrite.
This is direct evidence for the formula's 1 MiB page semantics on the measured path.

### Lifecycle

Ordinary measured lifetime:

```text
bind hardware voice to selected WS
 → reset/dummy voice programming
 → derive owner base and write CS1 words 0–2
 → program CR, START, ACCUM and END
 → read word 2 and write word 3
 → set playback parameters/FC
 → sample fetches, autonomous loop or one-shot end
 → firmware cleanup/dummy programming
```

The cleanup can write word 3 later in time.
That is not continuation: the musical voice has already stopped and its address registers/FC have been replaced with the idle/dummy configuration.
No second bind occurred during either authentic captured note.

### Physical boundary

[VERIFIED — functional] The MC68302's programmed CS1 write decode covers `$FF6000–FF7FFF`; the observed table area is `$FF7F00–FF7FFF`.
[INFERRED — physical architecture] External board behavior must provide functionally corresponding translation.
[OPEN — ownership] Exact storage IC, table width, voice counter/synchronization, read decode and DTACK source are unknown.
A write-only CS1 select does not establish that CPU reads at those addresses are impossible: other board decode can exist.
This investigation provides no new schematic, netlist or physical measurement to identify it.

The existing ES5701 specification-based rejection of an internal banking-table storage model remains bounded to that specified model.
It is not a proof that ES5701 has no board-level participation, nor proof of another IC's ownership.

## E. ES5506 PLAYBACK MODEL

### Generic chip/device behavior

[VERIFIED — specification/source] ES5506 ACCUM supplies the initial position.
START is a loop/reverse boundary, not an automatic command to begin there.
The current device uses a 32-bit address accumulator comprising 21 integer word-address bits and 11 fractional bits.
START and END have their specified register granularity; END provides finer loop-end resolution than an integer-only address.

For the ordinary firmware path, after adding the owner's intra-megabyte offset:

```text
ES_ACCUM = D7 << 2
ES_START = D4 << 2
ES_END   = D5 << 2
W        = ACCUM >> 11
```

For integer byte offset x, this is `(B+x)*1024`.
The driver fetch converts W back to a translated 16-bit PCM word.
Interpolation can fetch the adjacent word, so a fetch of `W=$080000` can occur while saved ACCUM still lies just below that integer boundary.

Relevant control bits are STOP0/STOP1=`$0001/$0002`, LEI=`$0004`,
LPE=`$0008`, BLE=`$0010`, IRQE=`$0020`, DIR=`$0040`, IRQ=`$0080`.

Forward end detection is `ACCUM > END`; reverse detection is `ACCUM < START`,
unless LEI suppresses boundary handling:

| Loop bits | Generic/current device boundary action |
| --- | --- |
| Neither LPE nor BLE | Set STOP0 |
| LPE only | Wrap to opposite boundary, preserving overshoot |
| BLE only, transwave | Jump to opposite boundary, preserve overshoot, clear loop bits and set LEI |
| LPE + BLE | Reflect the overshoot and reverse DIR |

IRQE requests an end-related IRQ independently of whether the action is stop, wrap or reflection.
IRQV identifies one pending voice; bit 7 means no pending vector.
Reading IRQV acknowledges the selected event; queued events can subsequently be selected.

These are generic capabilities, not proof of ASR use.
References: [ES5506 specification](../../ensoniq/ES5506.pdf), printed pp.10–11 and 29;
[source boundary handling](../../../src/devices/sound/es5506.cpp), `check_for_end_forward/reverse`, `generate_pcm`, `generate_irq`, and IRQV reads.

### Actual ordinary ASR use

[OBSERVED] JM DIGI SYN, note 60:

- Voice 1, owner PCM `$62C9E0`; static map `[6,7,8,9]`.
- CR `$4018` initially, changing to `$4058` on reverse portions; IRQE remains clear.
- START `$0FE8A800`, END `$153FF980`, initial ACCUM `$0B278000`.
- Five DIR turns at emulated times 30.976345, 34.338597, 37.700480, 41.061691 and 44.424515 seconds.
- This covers two complete back-and-forth cycles plus the next upper turn.
- No CS1 or START/END/ACCUM reprogramming after initial setup during the measured 20 seconds.

[OBSERVED] JM DRUMS note 100 is one-shot: CR `$4700`, IRQE clear.
It reaches END and sets STOP0, observed as `$4701`, before firmware installs the dummy configuration.
No interrupt-mediated continuation occurs on this path.

[VERIFIED — current MAME limitation for the exercised setup] `generate_samples` unconditionally sets STOP0 whenever START equals END.
The large-endpoint firmware setup intentionally produces equal boundaries with distinct ACCUM.
The resulting stop is observed even with all taps removed.
The specification describes transwave/LEI behavior and permits a new loop start beyond loop end; it does not document MAME's blanket equality-stop rule.
This establishes an implementation obstacle, not a license to remove the rule without a separate device-level correctness investigation.

## F. END / IRQ / CPU CONTINUATION

### Ordinary tested notes

[RULED OUT — for these measured paths] END-interrupt continuation is not how the authentic JM loop or JM DRUMS one-shot progresses.
IRQE is clear; the loop turns without address writes, and the one-shot stops.

The observation remained live: the 20-second loop recorded 64,389 MMIO write witnesses and 20,003 interrupt acknowledgements, with zero vector-$47 acknowledgements and zero IRQV reads.
The detailed two-second drum capture recorded 2,961 writes, 2,003 acknowledgements and 4,944 host reads, again with zero vector $47/IRQV.
These are positive witnesses for the observation windows, not a positive test of the ES5506-to-PB9 IRQ chain itself.
No new positive ES5506 IRQ event was obtained.

### Large-endpoint path

[VERIFIED — static] ROM `$F8E27E` compares the normalized endpoint with `$40000000` in firmware fixed-point units.
That is the endpoint relative to the mapping origin, including the owner's intra-megabyte offset—not merely the WaveSample length.

The forward large branch can advance the origin in 2 MiB steps if the initial position requires it.
Otherwise it saves a terminal physical-page index in voice `+$BD`, a terminal endpoint in `+$C2`,
sets temporary START=END at the 2 MiB logical byte position, arms IRQE+transwave,
and selects callback `$FFFF8EE2`.
The reverse branch selects `$FFFF8EEE` and can establish non-consecutive initial entries.

[OBSERVED — SYNTHETIC TEST] Both setup branches execute when a loaded JM descriptor is changed to a 5 MiB END.
Forward produces CR `$4030`, START=END=`$80000000`, ACCUM `$0B278000`.
Reverse produces CR `$4070`, the same boundaries, ACCUM `$CB278000`, and map `[6,9,10,11]`.
Only mode/END were changed; no 5 MiB allocation or PCM extension was made.

[VERIFIED — negative mechanism result] Current MAME stops those setups at equal boundaries before useful progression.
The first forward host-read STOP witness is at 24.622473250 seconds; it is not claimed to timestamp the precise sample cycle that set STOP.
Cleanup is observed around 24.633412 seconds.
The tap-free control already has CR `$4031` and unchanged ACCUM at 24.623 seconds.
The reverse capture already shows STOP in its last setup-state snapshot, before its later host-read STOP witness.
Do not interpret either as a genuine reached long-sample END.

[INFERRED — intended functional design] The bound callbacks alternate temporary loop boundaries and modify middle translation entries, then install the final endpoint.
Together with IRQE/transwave setup, the simplest interpretation is CPU-serviced continuation within the same WaveSample's contiguous PCM.
[OPEN — executed behavior] No continuation callback was observed executing after a genuine END on a valid large WaveSample.
This investigation does not verify streaming, successful long playback, or chaining different WaveSample objects.

### IRQ provenance that is established statically

```text
ES5506 IRQ callback
 → driver es5506_irq_w()
 → modeled MC68302 PB9 rising assertion
 → pending bit $0080, IRQ level 4 / vector $47
 → vector table points to ROM $F8D072
 → save ES page; read IRQV via DPRAM $FC60BC
 → record = $8000 + (IRQV & 31)*$D8
 → call record+$26
 → restore page and RTE
```

Driver callback: [asr10_boot.cpp](../../../src/mame/ensoniq/asr10_boot.cpp).
MC68302 pending/IACK model: [mc68302.cpp](../../../src/devices/machine/mc68302.cpp).
Loaded vector and binding tables were captured.
This chain is not a new end-to-end runtime witness.
Physical PB9 polarity/wiring and cycle-accurate IRQ latency remain open.

## G. COMPETING-MODEL VERDICT

| Model | Result and supporting evidence | Contradiction / remaining uncertainty | Discriminating evidence |
| --- | --- | --- | --- |
| A: contiguous relocation window | [OBSERVED] Ordinary setup `[N,N+1,N+2,N+3]`; authentic 1 MiB crossing succeeds | [DISPROVEN — necessity in executed setup] Synthetic reverse input makes actual firmware write `[6,9,10,11]`; ordinary behavior is not a universal restriction | A valid large reverse WS on expanded hardware, with setup capture |
| B: independent four-page translation | [VERIFIED — MAME mechanism]; [OBSERVED — conditional firmware setup] individual words are changed independently | Arbitrary scatter/gather use is not established; one PCM owner is contiguous in the inspected allocation model; exact hardware value range remains open | Correlate non-consecutive table words with one authentic allocation and fetched physical pages |
| C: active runtime remapping | [INFERRED] Bound callbacks rewrite middle table entries | No active-lifetime remap observed in authentic notes; synthetic large paths stop before the relevant event | Same-voice/owner trace across a genuine continuation threshold |
| D: CPU-mediated segment continuation | [INFERRED] IRQE+transwave, callback selection, ISR dispatch and remapping/endpoint code form a coherent design | [RULED OUT — measured ordinary paths]; no genuine long END→ISR→reprogramming witness | IRQV, voice identity and register/CS1 writes around the same genuine threshold |
| E: bounded WaveSample needing no continuation | [OBSERVED] Sufficient for the small ordinary tested material | [DISPROVEN — global one-window restriction] ASR-specific manufacturer evidence describes >2M-sample regular WaveSamples; exact largest supported object still open | A valid >4 MiB owner with known extent, then observe its actual playback |

A capability and firmware use are different propositions.
Non-consecutive page contents can serve continuation of contiguous PCM; they do not by themselves imply fragmented allocation.
The next experiment in O is one experiment capable of discriminating several of these models.

## H. RUNTIME EXPERIMENTS

Raw evidence was retained outside the repository in a session-local temporary directory.
Its external `MANIFEST.md` lists each generated file, size, hash and disposition.
These artifacts are not a durable repository archive; see P.
No WAV equality claim is made: these tests observed programming, state and raw fetches.

### H1. Real objects and owner allocation

- Question: what does one instrument/WS actually contain?
- Fixture: V350 floppy, FILE 2 JM DIGI SYN and FILE 1 TUTORIAL BNK.
- Positive witness: expected panel load-completed state; valid root/layer/WS objects; executed voice binding; disk-to-live PCM equality.
- Observation: counts and allocation extents in C; JM has four header-only copies; JM DRUMS has 57 WS headers but 12 owners.
- Result: [OBSERVED] real multi-WS and shared-data representation, not an inference from aggregate file size.
- Scope/evidence: `jm*.bin`, `tutorial*.bin`, `*-objects.json`, `jm-file.bin`, `jm-drums-file.bin`, inventories. No expanded-memory load.

### H2. Authentic physical-megabyte crossing and one-shot end

- Question: does a real voice select the next translation entry at a 1 MiB boundary?
- Fixture: TUTORIAL BNK → JM DRUMS → Layer 1 → WS15 → MIDI note 100.
- Positive witness: one voice-1 bind generation, full CS1/ES register setup, fetched PCM, ongoing MMIO/IACK/host-read counts.
- Observation: PCM base `$6F7F20`; sample END offset `$AB38` (43,832 bytes); allocation `$AC60` including header, leaving `$AB40` PCM capacity. START=ACCUM=`$3DFC8000`, END=`$40A96000`, map `[6,7,8,9]`.
- Result: [OBSERVED] real page crossing at emulated time 51.318033669 seconds; no active map change; one-shot STOP observed at 51.448073125 before cleanup.
- Scope/evidence: `cross100.log`, `cross100-detail.log`, corresponding dumps, `cross100-control.log`, `cross100-passive.log`.

| Logical word | Translated byte address | Expected source word | Fetched word |
| --- | --- | --- | --- |
| `$07FFFC` | `$6FFFF8` | `$FFF9` | `$FFF9` |
| `$07FFFD` | `$6FFFFA` | `$0005` | `$0005` |
| `$07FFFE` | `$6FFFFC` | `$0011` | `$0011` |
| `$07FFFF` | `$6FFFFE` | `$000A` | `$000A` |
| `$080000` | `$700000` | `$0000` | `$0000` |
| `$080001` | `$700002` | `$FFFE` | `$FFFE` |
| `$080002` | `$700004` | `$FFFB` | `$FFFB` |
| `$080003` | `$700006` | `$FFF4` | `$FFF4` |
| `$080004` | `$700008` | `$FFF6` | `$FFF6` |

The 43,832-byte file/live comparison span has SHA-256
`09baa2adb30ec53eeaff6ce8408276071ba6350f90cbdba4efed3bb25007e1fb`.
The detailed capture checked 127 sampled/boundary fetch values with zero mismatches.
Its total fetch count includes idle/dummy fetches after stop; it is not a musical-sample count.

### H3. Loop lifetime without remapping

- Question: does a normal loop require repeated CPU continuation?
- Fixture: JM DIGI SYN, MIDI note 60, 20-second post-note window.
- Positive witness: one bind, continuing ACCUM/FC updates, five numerical DIR turns, live write/IACK counts.
- Observation: register values and times in E; five setup CS1 writes total, including the preceding dummy-page write; no later map/address-register rewrite.
- Result: [RULED OUT — this measured path] CPU-mediated loop continuation. ES5506 performs the turns.
- Scope/evidence: `jm-loop.log`, `jm-loop-control.log`, `jm-loop-passive.log`. 1,169 sampled fetch checks, zero mismatches.
- Caution: the original diagnostic “wraps” counter also counts initial ACCUM programming. Only DIR turns at the actual loop boundaries are used as loop evidence.

### H4. Large forward endpoint

- Question: does firmware have a different setup for an endpoint outside the initial logical window?
- Fixture: [SYNTHETIC TEST] loaded JM WS `$62C8C0`, mode 0, packed END changed to `$50000000`; MIDI 108.
- Positive witness: actual firmware executes the conditional setup and writes CR `$4030`, equal `$80000000` boundaries and distinct ACCUM.
- Observation: STOP appears before progression, without IRQV/vector-$47; normal cleanup follows. Tap-free control reproduces the unchanged ACCUM and STOP.
- Result: [OBSERVED] conditional long-setup behavior; [VERIFIED — current mechanism] equality-stop obstruction; [OPEN] successful long continuation.
- Scope/evidence: `synthetic5m.log`, `synthetic5m-control.log`, `synthetic5m-passive.log`. No valid large payload or allocation; no forced IRQ, callback or ES state rescue.

The first forward run had a preselected longer observation window; once inspected, causality was localized to the first stop.
Subsequent controls were limited to one second and then 0.25 seconds after setup.
No conclusion is drawn from the later dummy fetch stream.

### H5. Large reverse endpoint / non-consecutive table

- Question: are consecutive initial table values mandatory in firmware?
- Fixture: [SYNTHETIC TEST] same descriptor, mode 1, same 5 MiB END; MIDI 108; 0.25-second post-note window.
- Positive witness: actual CS1 writes and callback-pointer writes; table read returns 10 and the OS writes 11.
- Observation: initial 6,7,8 becomes 6,9,10,11; callback `$FFFF8EEE`; CR `$4070`; equal boundaries and distinct reverse ACCUM.
- Result: [OBSERVED] non-consecutive conditional setup; no fragmented allocation or active remap proven; same early MAME stop.
- Scope/evidence: `synthetic5m-reverse.log` and dumps. No successful long reverse playback.

### H6. Sampling allocation sanity check

- Question: do allocation units agree with the packed endpoint representation on a current normal setup?
- Fixture: existing RECORD-left probe with V350 media.
- Positive witness: level-detect UI and 112,436 observed data writes; populated WS/header and descriptor values.
- Observation: planned sizes in C.
- Result: [OBSERVED] current setup allocates a contiguous owner and represents its planned extent in the expected units.
- Scope/evidence: `record-current.log`. Not a full recording, a maximum-size experiment, or permission to reopen audio-input work. Opcode-prefetch “entry” counts were rejected.

### Controls and reproducibility

Observation used external Lua, the existing regression helper and `-autoboot_script`; no C++ hooks or rebuild.
Retained taps covered voice-record writes `$8000–9AFF`, MMIO writes `$FC2000–FF7FFF`,
host reads `$FC2000–207F`, and interrupt-acknowledge reads `$FFFFF0–FFFFFF`.
Optional fetch taps covered all four ES5506 bank spaces; readbacks compared translated RAM values.
No ROM opcode tap is used to claim playback execution.

The reduced-observation controls omit fetch taps, retaining host/MMIO observation.
Matched shared trace records include 178 loop records and the first 33 crossing records.
Those controls alone would not exclude shared read-hook perturbation.

Three final passive controls removed ALL memory taps before the note and sampled only saved device items.
They reproduce ordinary crossing progression, five loop-direction changes, and the synthetic forward early stop.
Passive controls cannot prove absence of bus writes/IRQs: their zero tap counters are disabled counters, not negative evidence.
They test whether the central outcomes depend on those taps.
Sampling itself and normal Lua/UI stimulus remain present.

Runner configuration was the existing `./mame asr10booth`, V350 floppy,
`SDL_VIDEODRIVER=dummy -video none -sound none -nothrottle -skip_gameinfo -autoboot_delay 0`.
The script waited for the known FILE 1 display, selected FILE 2 for JM or FILE 1 for TUTORIAL,
completed the load and selected the instrument, then mounted the MIDI stimulus.
For synthetic tests it changed only WS mode and the four alternating END bytes after loading.
MIDI 100/108 fixtures changed the note byte of the existing 30-byte `noteon.mid`; note 127 was generated but unused.
The last controls had 65-second overall safety limits, or 85 seconds for the bank-load control; they exited earlier on completion.

The 170-line final Lua observer and four small analytical scripts were removed after transferring findings to this report, per repository rules.
Precise formats, addresses, stimuli, snapshots and hashes are retained so observation can be reconstructed without keeping a second harness.
No source patch, regression-suite run, or production behavioral change was performed.

## I. ROM / FIRMWARE EVIDENCE

The runtime is boot ROM 1.50B plus loaded V3.50 OS, not a “V3.50 ROM.”
ROM bytes were interleaved from `asr-65e0-hi-1.5b.bin` and `asr-648c-lo-1.5b.bin`.
Interleaved SHA-256:
`fe290ea4e52e7c9d229cc6e19529fdc6e5b33e74ddfcd54345660121a19cabbf`.
The existing MAME executable was used without rebuilding; its hash, the V350 image hash,
and the source-reference hashes are recorded in the artifact manifest.
Observed state corroborates the relevant current source mechanisms; the executable hash is not a reproducible-build attestation.

| Address/routine | What it establishes | Execution status in this investigation |
| --- | --- | --- |
| ROM `$F8A166...` | Alias-sensitive RAM probing and logical base/size selection | Static; resulting `$600000/$200000` configuration observed |
| ROM `$F8A246–A46C` | Contiguous allocator, rounding, total/largest free, compaction | Static; resulting real allocated objects and RECORD setup observed; no new compaction witness |
| ROM `$F8B1F0–B260` | Owner/copy-aware maximum-END resize, byte conversion, header overhead | Static |
| OS `$007892` | Store selected WS in voice `+$1E` | [OBSERVED] actual write at PC reported as `$007896` |
| OS `$007B9C–7BC6` | Resolve owner, add PCM header size, construct fixed-point base | Executed setup corroborated by register values and CS1 writes |
| OS `$007BCE–7BF4` | Select start/end/loop fields by mode | Ordinary forward/bidirectional and synthetic reverse setup observed |
| ROM `$F8E256–E336` | Normalize base, program first three entries, select long forward/reverse setup | Ordinary and synthetic conditional setup observed |
| OS `$007C46–7C6E` | Convert to ES fixed point, program addresses, read table word 2 and write word 3 | [OBSERVED] register writes and table read/write |
| ROM `$F8E338/E384/E3D2` | Forward conditional remap/temporary boundary/final endpoint callbacks | Static only; bindings observed |
| OS `$00F096`, ROM `$F8E46C/E4BA` | Current reverse callback family | Static only; bindings observed |
| ROM `$F8D072–D0C2` | IRQV dispatch to `voice+$26` callback | Static; vector binding observed, not executed by these notes |
| DPRAM `$FC6000/FC6014/FC613C` | Control/ACCUM exception thunks and START/END writes | Loaded code plus actual setup writes |

Current callback bindings captured in high RAM:

```text
$FF8ED0 → $F8E256
$FF8ED6 → $F8E338
$FF8EDC → $F8E384
$FF8EE2 → $F8E3D2
$FF8EE8 → $00F096     (not the original ROM $F8E420)
$FF8EEE → $F8E46C
$FF8EF4 → $F8E4BA
```

For forward continuation, the static state machine is:

- `E3D2`: compare table[2]+1 with terminal page. If terminal, set word 3, install saved END and clear continuation-related controls.
- Otherwise `E384`: copy word 2 to word 1, set logical START=1.5 MiB / END=2.5 MiB, rearm transwave/IRQE, select `E338`.
- `E338`: set word 2=word 1+1, restore equal 2 MiB boundaries, rearm and return to `E3D2`.

[INFERRED] These alternating windows allow uninterrupted physical progression while logical addresses jump.
The callback instructions are directly established; uninterrupted playback and timing are not.

The current reverse binding `$FF8EE8 → $00F096` matters:
that loaded-OS routine selects `$FFFF8EEE`, whereas ROM `$F8E420` selects `$FFFF8EE2`.
Describing the current path from the ROM alone would silently substitute stale behavior.

The `$4AFC` and `$F000` words in these paths are intentional exception-based fast-register-write operations.
Current vectors lead to loaded DPRAM code which performs MOVEP writes and advances the saved PC.
A generic disassembler's “ILLEGAL” or spurious PMOVE decoding is not evidence of a firmware crash.
No callback was “tested” by directly invoking it.

## J. SPEC / MANUAL EVIDENCE

| Source | ASR-specific or generic? | Useful evidence and limits |
| --- | --- | --- |
| [Ensoniq ASR Musician's Manual](../../ensoniq/ensoniq_asr10_manual.pdf), memory expansion and EDIT WAVE sections | ASR-specific | Total memory configurations; 16-bit word/sample terminology; modulation RANGE and separate loop-length bounds. Neither is a proved individual payload maximum |
| [Ensoniq ASR Service Manual, Nov. 1995](https://www.deepsonic.ch/deep/docs_manuals/ensoniq_asr_service_manual.pdf), printed pp.30–31, software notes | ASR-specific | Explicit large regular-WS playback case, distinct from RAMTracks; documentary counterexample to a universal one-window WS model. Not a runtime trace from this session |
| Ensoniq ASR-10 External Command Specification, 6 Jan. 1993, body p.37 and Appendix B pp.53–58 | ASR-specific body; appendix explicitly covers original EPS/EPS-16+/ASR family | Fixed-point units, offset packing, contiguous instrument and `$120` WS header, copied-data structure. Early range/transfer comments must not be silently generalized |
| [ES5506 OTTO Rev.2.3](../../ensoniq/ES5506.pdf), printed pp.10–11,29,41 | Generic chip | START/END/ACCUM, transwave/LEI, IRQV and sample addressing. Does not establish ASR firmware use or physical board routing |
| MC68302 and ES5701 specifications, as indexed in the existing CS1 board-frontier investigation | Generic chips | Programmed CS decode and specified glue capabilities. No new physical-board attribution |
| Gary Giebler's Ensoniq Floppy Diskette Format | Independent format description, not ASR silicon proof | Read-only directory/FAT inventory; actual V350 extracted PCM was checked against runtime memory |

The External Command Specification and disk-format PDF were extracted from the existing local
`cds/Ensoniq-ASR-10-Archive-001.zip`, not added to the repository.
Their exact filenames/hashes are in the artifact manifest.
The scanned specification/manual pages were rendered and visually checked; OCR was used as an aid, not as sole evidence for critical numbers.

Material tension: early external-transfer sections pp.8–12 and Appendix B p.58 contain 20-bit/`0..FFFFF` limits or comments.
The same document explains 32-bit packed fields; the later ASR service notes and executable large-window branch contradict treating those comments as a universal ASR payload cap.
[OPEN] Exact V3.50 large SysEx-transfer limits were not established.
No assumption of import support was substituted for native allocation/playback evidence.

## K. MEMORY-EXPANSION IMPLICATIONS

[VERIFIED — current source] There are two backing regions:

```text
$000000–0FFFFF : lowmem backing
$100000–1FFFFF : sample-RAM backing
$200000–F7FFFF : aliases modulo $200000
$F80000–FBFFFF : ROM region, with bootstrap/CS0 behavior
```

This must not be called “CS0 RAM.”
CS0 concerns the bootstrap/ROM selection; CS1 contains the voice translation register area.

[OBSERVED] Current firmware chooses logical base `$600000`, size `$200000`;
the loaded heap boundaries are `$62A000–7F1000`.
WaveSample pointers in physical-megabyte numbers 6 and 7 are routine even in this 2 MiB model.
Writing page values above 1 does not prove that expanded independent RAM is installed.

[VERIFIED — implementation limitation] Modulo-`$200000` loses distinctions between 6/8/10 and between 7/9/11.
The synthetic reverse mapping is meaningful as firmware programming, but MAME cannot give all those page numbers independent PCM storage.
The prior causal CS1 fix established necessary per-voice page selection for its fixtures; it did not validate the full high-address space.

[VERIFIED — static configuration branches] The RAM-probe code contains other logical base/size results,
including base 0 with size `$800000`, base `$600000` with size `$400000/$A00000`,
and base 0 with size `$A00000/$F80000`.
These values describe firmware address beliefs produced by probing.
They do not, by themselves, prove a flat physical DRAM organization or an exact modern emulator configuration map.

Faithful 8/10/16 MB support would require configuration-specific independent storage and documented probe/alias behavior,
consistent CPU and wavetable translations, and matching save/reset handling.
It would also require resolving the large-playback obstacle where applicable.
No memory redesign was attempted.

## L. DISCREPANCY LEDGER

| Existing statement | Relevant correction / status |
| --- | --- |
| CS1 board-frontier §6: no firmware read dependence; `voice_bank_r` could become unmapped | [DISPROVEN — executed path] OS `$007C68` reads table word 2 to derive word 3. Do not remove readback. CS1 write-select configuration is not a complete read-bus schematic |
| Same document: “bit-identical” with 99.89% correlation | Correlation below 100% and differing peaks do not establish bit identity. The prior reproduced causal fix is not reopened |
| “Arbitrary segments” / “full functional equivalence with authentic hardware” | Current modulo backing and limited witnesses do not establish full expanded-memory or physical equivalence. Conditional non-consecutive setup is not scatter/gather proof |
| Driver comment: setup at `$F8E250`, N from A3, four consecutive writes | Actual entry is `$F8E256`; owner PCM is resolved first into D3; ROM writes three words and loaded OS reads/derives the fourth. A3 can be a parameter copy, not the owner |
| Instrument-to-OTTO reference: producer of voice `+$1E` open | Current loaded OS producer is `$007892`, with runtime write witness |
| Early external-command 20-bit comments interpreted as an ASR global maximum | Scope/version tension remains explicit; not reconciled into a fictitious maximum |
| Old allocator/memory-belief notes used as present capacity or failure state | Current loaded heap and successful record setup supersede those historical observations; allocator rounding also differs at top/nested levels |

Existing canonical reset/clear-asymmetry status remains [OPEN — EVIDENCE NEEDED], with no identified historical causal fix.
This investigation supplies none and makes no reset change.
Storage/SCSI, sequencer event-mask, established audio-rate policy and save-state milestones were not reopened.
The new findings refine the WaveSample/translation frontier; they do not retroactively assign every old audio symptom to it.

## M. REMAINING OPEN QUESTIONS

Ranked by expected information gain:

1. [OPEN] A valid >4 MiB WaveSample on an expanded ASR: actual allocation, setup and first continuation event. This would distinguish real ASR use from synthetic descriptor reachability and test the equality-boundary issue.
2. [OPEN] Exact largest supported individual WS per firmware/configuration/path, including overhead, signed consumers and endpoint/padding semantics. Field width and total memory are insufficient.
3. [OPEN] Configuration-specific independent-memory/probe/alias map for 8/10/16 MB; observations of page values alone cannot settle it.
4. [OPEN] Current V3.50 continuation timing and all mode-specific behavior, including reverse and loop/modulation interactions. Bound callback code is not an execution witness.
5. [OPEN] Physical table storage/width, voice synchronization and CPU read decode/DTACK. Requires new board evidence, not another inference from MAME.

No further fragmentation branch is recommended from current evidence:
the inspected object/allocator model is contiguous, and non-consecutive CS1 values have another concrete explanation.
No whole-system regression or old GOOD/BAD provenance branch is required for these conclusions.

## N. IMPLEMENTATION CONSEQUENCES

NO PATCHES were made.

| Category | Consequence warranted by evidence |
| --- | --- |
| Required correctness work, conditional on supporting the exercised large path | Investigate ES5506 equal-START/END handling against the specification and a genuine device/ASR witness. The current unconditional stop is a demonstrated obstacle; a blanket removal is not yet a verified fix |
| Required correctness work for expanded-memory support | Replace parity-only modulo backing with independently represented configured memory and accurate alias/probe behavior, after establishing that map. Current 2 MiB covered workloads are not equivalent to expanded support |
| Fidelity improvement | Validate continuation IRQ/host timing and interpolation at remapped boundaries with real material; avoid guessing from callback reachability |
| Physical-model refinement | Only with new evidence: read decode, DTACK, storage width and voice synchronization. No ES5701/PAL ownership assignment follows now |
| Documentation-only correction | Fix the readback, setup/owner-pointer, bit-identity and overstrong equivalence claims during a separate reviewed documentation pass |
| No action | Preserve the causal CS1 fix, current readback, frozen unrelated tracks and unresolved reset/clear item. Do not install instrumentation or synthetic descriptors as production behavior |

## O. RECOMMENDED NEXT EXPERIMENT

Exactly one: on a real expanded ASR-10 running V3.50, load or record one known mono WaveSample with at least a 5 MiB PCM extent and identifiable data, then capture its object/owner allocation, hardware-voice identity, all four translation words, CR/START/END/ACCUM and IRQV across the first continuation threshold.

The discriminating result is whether a valid owner produces the equal-boundary/transwave setup and then, for the same voice, an actual IRQV callback followed by remapping and correct next PCM.
Capture only setup and the first threshold; stop at the first semantic divergence.
No such hardware capture facility or authentic expanded-memory execution was available in this session.

## P. WORKTREE ACCOUNTING

HEAD at investigation completion, before the documentation commit: `9b01d2d26900bd127e75e5edfc29f3d19160a0ec`.

`git status --short` at investigation completion, before repository housekeeping:

```text
?? 3rdparty/portaudio/bindings/java/jportaudio/bin/
?? docs/asr10/investigations/wavesample-addressing-cs1-and-continuation.md
```

- Repository files created: this investigation report.
- Existing repository files modified: none.
- Repository files deleted: none.
- Commits created during the investigation: none; subsequent documentation housekeeping is separate.
- Known unrelated untracked PortAudio artifact: untouched.
- Temporary artifacts: retained outside the repository in the session-local directory.
- Full temporary-file enumeration: external `MANIFEST.md`, including hashes and deleted-file entries; not committed with this report.
- Temporary instrumentation/analytical scripts created then removed: `capture.lua` (170 lines), `objects.py` (40), `static_analysis.py` (13), `midi.py` (6), `disk_inventory.py` (43): 272 lines added and 272 removed; net zero retained script lines.
- Redundant extracted `cdr1.bin` (410,894,400 bytes) and `cdr1.cue` (67 bytes) removed after inventory. They are reproducible from the untouched source CHD; no original media was removed.
- Other generated logs, memory dumps, MIDI variants, rendered/OCR pages, extracted reference PDFs, inventories and disassembly remain in the temporary directory for review. They are not source or fixture changes.
- The report adds 688 lines; the temporary artifact manifest adds 252 lines. No existing tracked lines were removed. These pure documentation additions retain findings/provenance while the 272-line observer/analysis code is removed.

Raw evidence in the temporary directory is not a durable repository archive.
Preserve it separately before deleting that directory if independent reanalysis of this exact capture is required.
The report records the model, discriminating observations, caveats and reconstruction procedure without committing ROM/media dumps or copyrighted reference copies.
