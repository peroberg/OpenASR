# SCC receive to IDMA transfer

Date: 2026-08-23
Machine: `asr10booth`, V3.50
Scope: Phase 4A, CP-produced SCC1 data through IDMA completion

## Result

**Result A, with a model-boundary qualification:**

```text
virtual post-framing SCC1 bytes
  -> mc68302_device RX descriptor completion
  -> level 4 / vector $4D
  -> firmware $00643C / $0064BA
  -> CMR $37A1 internal IDMA start
  -> 794 bytes read from $F76606-$F7691F
  -> 794 bytes written to $02C110-$02C429
  -> CSR normal completion / IPR bit 11
  -> level 4 / vector $4B
  -> firmware $00AA48
  -> object destination $02C110 -> $02C42A
```

All 794 destination bytes matched the deterministic SCC source exactly. The
current model's complete `SCC1 RX -> CP descriptor -> IDMA -> recording RAM`
chain is therefore `[Verified runtime]` for the measured full-descriptor case.

`$02C110` is sample-accessible RAM in the current MAME model because it is in
the low-memory backing also exposed as ES5506 bank 1. The physical ASR-10 RAM
decode/bank represented by that CPU address remains `[OPEN hardware]`; this
experiment made no `mem_map`, ES5506 or bank change.

## 1. Previous IDMA boundary

Before Phase 4A, `mc68302_device` supported these pieces:

| Function | Previous behavior |
|---|---|
| SAPR/DAPR/BCR/CMR/FCR | CPU-visible storage |
| STR/start | CMR bit 0 armed a working count |
| active/busy | `m_idma_active` plus nonzero working count |
| source access | no SAPR dereference |
| destination access | byte writes only through `idma_transfer_in(data)` |
| source increment | absent; external caller supplied each byte |
| destination increment | hard-coded for the external byte path |
| byte count | external/FDC path used the measured but narrow `BCR-1` convention |
| word transfer | absent |
| CSR completion | bit 0 set when the external byte feed exhausted its count |
| IDMA interrupt | absent from IPR/ISR arbitration and level-4 IACK |

That external path is board-assisted: FDC DRQ calls `dma_r()` in the ASR-10
driver and passes the resulting byte to `idma_transfer_in()`. It is appropriate
for measured CMR `$0D51`, whose SAPR points at board-specific FDC glue rather
than an ordinary CPU-visible FIFO address.

Sampling uses a different, independently observed mode:

```text
CMR $37A1
source increment
destination increment
word source/destination size
internal request generation
normal/error interrupt enable
```

Firmware already programmed every register correctly. The Phase 3B first stop
was therefore category **C**, followed by **D**: the model marked the channel
active but had neither internal SAPR-to-DAPR accesses nor completion-event
delivery.

## 2. Implemented boundary

Only the exact observed `$37A1` start form gained internal transfer behavior.
It is not a general IDMA engine.

On the CMR write, the device:

1. captures SAPR, DAPR and BCR as working source, destination and byte count;
2. schedules an independent zero-delay device timer;
3. reads/writes words through the CPU program address space;
4. handles an odd final byte if BCR is odd;
5. increments both working pointers and decrements the full BCR count;
6. publishes advanced SAPR/DAPR and BCR zero;
7. clears STR and sets CSR bit 0;
8. gates normal completion through CMR.INTN, IPR/IMR/ISR bit `$0800`;
9. supplies IDMA vector `$4B` from the configured GIMR base.

CMR `$0D51` remains on the existing external-request byte path, including its
separate `BCR-1` convention. Phase 4A does not reinterpret or modify FDC
transfers.

Not implemented:

- general CMR mode decoding;
- arbitration or cycle-accurate bandwidth;
- DREQ/DACK/DONE pins;
- bus-error/error-completion behavior;
- non-observed increment/size combinations;
- chaining;
- physical ADC or SCC serial framing.

The zero-delay timer separates the bus-master operation from the CMR register
write, but its timing is not claimed as silicon timing.

## 3. Deterministic live test

The retained probe is
`../lua/archive/scc-idma-transfer-probe.lua`. It supplies the same 800-byte
post-framing SCC1 vector as Phase 3B and does not write descriptors, IDMA
registers, destination memory, CSR, IPR, ISR or interrupt lines.

Command, without `-log`:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -skip_gameinfo \
  -autoboot_delay 0 -seconds_to_run 60 \
  -autoboot_script docs/asr10/lua/archive/scc-idma-transfer-probe.lua
```

The descriptor vector contains zero sample words at each 16-byte boundary and
`$7FFF` at offsets `$40/$41`. Firmware retains 58 bytes of pretrigger history,
so it selects source offset `$06`:

```text
descriptor buffer $F76600
selected SAPR     $F76606
range end         $F76920 (exclusive)
BCR               $031A = 794
DAPR              $02C110
```

### Register start

Before SCC input:

```text
CMR=$0002 SAPR=$00000000 DAPR=$00000000 BCR=$0000
CSR=$0000 IPR=$0000 IMR=$E480 ISR=$0000
```

Firmware then reached `$00B4C0` and wrote:

```text
CMR=$37A1 SAPR=$00F76606 DAPR=$0002C110 BCR=$031A FCR=$99
```

The CMR write occurred at PC `$00B4C6`; the slight PC advance is the tap's
post-write observation point.

### Bus accesses

Read/write taps were enabled only after the `$37A1` start and source counting
was stopped at vector `$4B`, before any verification reads:

```text
source read bytes       794
source first/last       $F76606 / $F7691F
destination write bytes 794
destination first/last  $02C110 / $02C429
destination matches     794
destination mismatches  0
```

The device used 397 word read/write operations. The probe reports byte
coverage, derived from each access mask, so the result is directly comparable
with BCR.

### Completion state

At the level-4 `$4B` IACK:

```text
CMR  $37A0   STR cleared
SAPR $F76920 advanced to exclusive source end
DAPR $02C42A advanced to exclusive destination end
BCR  $0000
CSR  $0100   byte CSR bit 0 on the high lane
IPR  $0800
IMR  $EC80
ISR  $0800   source marked in service by IACK
```

Observed interrupt and firmware path:

```text
level-4 vector $4D  1
level-4 vector $4B  1
$00643C              reached
$0064BA              reached
$00B478              reached
$00B4C0              reached
$00AA48              reached
```

After firmware completion handling:

```text
CMR=$0002 SAPR=$F76920 DAPR=$02C42A BCR=$0000 CSR=$0000
IPR=$0000 IMR=$E480 ISR=$0000
object +$20=$02C42A
display="REC0RDING 272 5EC LEFT"
state $0D04=$0003
```

The low-RAM witness observed 661,190 writes during the measured interval, so
all zero-event assertions in the probe would have had a live instrument.

## 4. Destination classification

`$02C110-$02C429` lies in the driver's existing
`$000000-$0FFFFF` low-memory map. After boot remapping, writes land in
`m_lowmem_shadow` through `lowmem_w()`.

The existing ES5506 bank-1 wavetable map exposes word range
`$000000-$07FFFF` through the same `low_rom_or_lowmem_r()`/`lowmem_w()`
backing. Therefore:

- `[Verified current model]` IDMA writes the same backing memory that ES5506
  bank 1 can read as sample data;
- `[Verified runtime]` all 794 selected SCC bytes reached that backing;
- `[OPEN hardware]` the real digital-board decode, installed RAM device and
  physical bank corresponding to CPU `$02C110` are not established here.

Calling this destination "sample RAM" is valid only with the explicit
current-model qualification. No new mapping is inferred from the transfer.

## 5. Status changes

| Claim | Previous | Observation | New status / falsifier |
|---|---|---|---|
| `$37A1` starts internal memory transfer | `[OPEN implementation]` | exact SAPR/DAPR bus accesses after the firmware CMR write | `[Verified runtime]` for `$37A1`; falsified by a repeatable start with no accesses while the same registers remain valid |
| IDMA copies full firmware BCR | `[DISPROVEN]` in the old globally applied `BCR-1` bridge | BCR 794 produced 794 reads, writes and matches | `[Verified runtime]` for `$37A1`; FDC `$0D51` remains separate |
| IDMA normal completion reaches firmware | `[OPEN implementation]` | CSR/IPR/ISR state, vector `$4B`, `$00AA48` | `[Verified runtime]`; falsified by valid completion without the event chain |
| recording destination is sample-accessible | `[OPEN]` physical identity | destination is shared low-memory/ES5506-bank-1 backing in this driver | `[Verified current model]`, `[OPEN hardware]` |
| full CP/SCC to recording-RAM chain works | descriptor-to-IDMA setup only | 800-byte CP input led to 794-byte firmware-selected copy and completion | `[Verified runtime]` for the measured SCC1 case |

ADC identity, physical SCC routing, SCC2, repeated descriptor wrap and real
IDMA arbitration are unchanged and remain outside this result.

## 6. Verification

Before and after implementation, `docs/asr10/regression-test.sh` passed all 8
test cases and emitted 10 `PASS` lines:

```text
boot display button button_upper nodisk file_loaded
mc68302_guards note_audio note_audio_wav regression
```

Build:

```sh
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j4
```

No `mem_map`, ES5506, ES5510, bank, clock or ADC code changed. No `-log` was
used and no `static/*.csv` file was edited.

Executable source delta for Phase 4A, measured from the completed Phase 3B
tree:

```text
mc68302.cpp       +69 / -14
mc68302.h         +13 / -22
asr10_boot.cpp      0 / 0
```

The executable net increase is 46 lines. The ASR-10 harness remains 1,078
lines and did not grow. The 274-line Lua probe contains all task-only driving
and instrumentation and is retained as reproducible provenance. No temporary
C++ instrumentation was added; the deletion list consists of stale IDMA
comments replaced by the measured two-mode boundary.
