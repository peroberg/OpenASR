# ASR-10 boot/runtime timeline

Scope: V3.50 observation pass, no injection, no stimulation, no `-log`.

Evidence distribution in this document:

| evidence | count |
|---|---:|
| `[Verified dynamic]` | 9 |
| `[Verified static]` | 3 |
| `[Likely]` | 1 |
| `[OPEN]` | 4 |

## Timeline

| time | dominant PC region | phase | what advances it | evidence |
|---:|---|---|---|---|
| reset | ROM | Reset starts at `$F8000C`. ROM is the permanent service library, not a bootloader that exits. | Reset vector and ROM control flow. | `[Verified static]` |
| 0.000030250 s | ROM -> DPRAM | First observed region handoff is `$F8006C -> $FC6200`. | DPRAM bridge code. Source differs from older `$F80072` note and should be re-read before treating source PC as final. | `[Verified dynamic]`, `[OPEN]` source PC |
| 2.948605375 s | ROM/FDC | First calibrated loading-profile window starts on first FDC access. | FDC reads/writes. In 20 s: 1,875,897 reads and 359 writes. | `[Verified dynamic]` |
| 2.948605-22.948605 s | ROM, disk loops | Loading profile is disk dominated. Top PCs are FDC wait/data loops (`$FB8D6C/$FB8D72/$FB8D74/$FB8D70/$FB8D6E`, then `$FB8AA8/$FB8ABE/$FB8ABC`). | FDC/data-transfer polling and fixed delay loops. | `[Verified dynamic]` |
| 15.011101250 s | ROM -> high RAM | First stored ROM-to-high-RAM sample is `$F87EBC -> $FF8D44`; first stored ROM-to-low-RAM sample is `$F87EF6 -> $00B008`. | Installed vectors/handlers and loaded RAM code begin participating while ROM continues executing. | `[Verified dynamic]` |
| 15.021-15.166 s | ROM/high RAM | DUART initialization and handler installation region. IMR writes include `$00`, `$2B`, `$09`, then `$2B` again with pending IRQ. | Firmware writes DUART IMR, then IRQ6 handling begins. | `[Verified dynamic]` |
| 15.032439875 s onward | ROM IRQ dispatcher | IRQ6 accept returns vector `$56` and lands at `$F884BE`. | Driver `maincpu_iack_r(6)` supplies `mc68302_device::irq6_ack_vector() == $56`; vector-table target is `$F884BE`. | `[Verified dynamic]`, `[OPEN]` hardware vector source |
| 15.032-60.0 s | ROM IRQ dispatcher | 45,095 IRQ6 accepts are all DUART-pending. ISR histogram: `$08` 44,942; `$20` 148; `$28` 4; `$01` 1. | DUART counter-ready bit 3 dominates. RxRDYB bit 5 is present 152 times. TxRDYA bit 0 appears once. | `[Verified dynamic]` |
| 16.171608375 s | high RAM + ROM | `FILE 1  TUTORIAL BNK` becomes visible; `$0003C0 = $B392`, sign-extending to `$FFB392`. | Runtime receive path jumps through `$0003C0`; ROM services remain active. | `[Verified dynamic]` |
| 16.171631250-16.173213750 s | high RAM receive path | Runtime receive path performs 12 SRB/RHRB pairs at `$FFB0BC/$FFB0D4`, consuming 12 `$FF` ACK/status bytes. | Channel-B FIFO bytes from the existing ACK/status harness. | `[Verified dynamic]` |
| 16.171608-36.171608 s | ROM scheduler loop | FILE 1 profile is scheduler dominated: 28,315 samples, 385 distinct PCs, 0 `stop` samples, top-3 share 33.9043 %, 0 FDC accesses. | Scheduler scans slots at `$F87F92-$F87FD0`; idle path opens interrupts and loops. | `[Verified dynamic]` |
| runtime provenance | high RAM view of OS image | `$FFB0B0` bytes probably come from V3.50 OS image at disk offset `0x00D6B0`, mapping to RAM `$00B0B0` under segment 1. | Full 64-byte sequence matches V350 once and not interleaved ROM. | `[Likely]`, `[OPEN]` direct load vs patch/relocation |

## IRQ6 dispatcher

`$F884BE` is a DUART ISR demultiplexer, not a single-purpose receive handler:

```asm
f884be  movem.l D0-D3/A0-A2/A5,-(A7)
f884c2  move.b  $fffc480b.l,D0       ; DUART ISR
f884c8  move.b  D0,D1
f884ca  btst    #5,D0                ; RxRDYB
f884d0  movea.l $de.w,A0
f884d4  jmp     (A0)
f884d6  and.b   #6,D1                ; channel A Rx/break group
f884dc  movea.l $e2.w,A0
f884e0  jmp     (A0)
f884e2  btst    #0,D0                ; TxRDYA
f884e8  movea.l $e6.w,A0
f884ec  jmp     (A0)
f884ee  btst    #3,D0                ; counter ready
f884f4  jmp     $8638.w
f884f8  moveq   #-$6f,D0
f884fa  trap    #0                   ; ERROR 145 if no tested bit matched
```

The dominant IRQ6 source in the measured run is DUART ISR bit 3, counter ready. The
handler routes that bit to `$8638.w`. Channel-B receive (`bit 5`) is a small minority,
and matches the 12 observed receive pairs plus mixed `$28` samples.

Autovector 30's target is different:

```asm
f882da  moveq   #-$75,D0
f882dc  bra     $f88280
```

`$F882DA` is the unused-vector/error stub path. `$F884BE` is therefore not equivalent
to autovector 30 and should not be described as an autovector handler.

## Scheduler loop

The hot FILE 1 loop is the scheduler scan:

```asm
f87f92  movea.w $c6.w,A2
f87f96  move.b  ($2,A2),D0
f87f9a  move.b  ($3,A2),D1
f87f9e  eor.b   D1,D0
f87fa0  beq     $f87fc2
f87fa2  move.l  ($6,A2),-(A7)
f87fa6  move.w  ($a,A2),-(A7)
f87faa  movea.w ($e,A2),A0
f87fae  move    A0,USP
f87fb0  clr.w   ($2,A2)
f87fb4  movea.w ($c,A2),A5
f87fb8  clr.w   ($c,A2)
f87fbc  move.w  A2,$b6a.w
f87fc0  rte
f87fc2  adda.w  #$16,A2
f87fc6  cmpa.w  $c8.w,A2
f87fca  bcs     $f87f96
f87fcc  move    #$2000,SR
f87fd0  bra     $f87f92
```

The loop tests slot byte `+2` against byte `+3`; equality means no work for that slot,
and inequality restores the saved context and returns into that task. `$00C6` is the
slot-table base and `$00C8` the end. Existing static/runtime documentation identifies
writers to the tested fields in early scheduler setup and producer paths, but this pass
did not enable a write watchpoint for those bytes; current-run writer identity remains
`[OPEN]`.

## Region handoff inventory

Handoff logging is region-classified, not PC-pair deduped. It stores the first 16 samples
per class and reports truncation per class.

| class | count | stored | truncated |
|---|---:|---:|---:|
| ROM -> DPRAM | 45,806 | 16 | 45,790 |
| ROM -> LOW_RAM | 2,944,192 | 16 | 2,944,176 |
| ROM -> HIGH_RAM | 88,831 | 16 | 88,815 |
| DPRAM -> ROM | 1,196 | 16 | 1,180 |
| DPRAM -> LOW_RAM | 95 | 16 | 79 |
| LOW_RAM -> ROM | 6,371 | 16 | 6,355 |
| LOW_RAM -> DPRAM | 68 | 16 | 52 |
| LOW_RAM -> HIGH_RAM | 769 | 16 | 753 |
| HIGH_RAM -> ROM | 2,657 | 16 | 2,641 |
| HIGH_RAM -> DPRAM | 4 | 4 | 0 |
| HIGH_RAM -> LOW_RAM | 3,119 | 16 | 3,103 |

This distribution is the runtime shape expected from the static ABI model: ROM remains
central while loaded RAM code, DPRAM thunks and low-memory vectors/slots exchange
control with it repeatedly. There is no single OS takeover point in this observation.
