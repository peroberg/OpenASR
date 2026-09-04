# ASR-10 Firmware & Board Architecture (Consolidated)

**Date:** 2026-07-13  
**Machine:** `asr10booth` — `src/mame/ensoniq/asr10_boot.cpp`  
**ROMs:** `asr-648c-lo-1.5b.bin` / `asr-65e0-hi-1.5b.bin` — **hi file = even byte lane** (`unidasm` needs `-xchbytes`)  
**OS images:** `V161.img`, `V350.img` where noted

Evidence levels: **[DYN]** observed in emulator run · **[STAT]** verified in ROM/image bytes · **[MODEL]** strong working model · **[HYP]** hypothesis · **[OPEN]** unresolved.

This document supersedes the architecture sections of all earlier handoffs
(`asr10-handoff-2026-06-22.md`, `asr10-mame-irq6-duart-handoff-2026-07-12.md`,
`duart-panel-rx-tx-machinery.md`). Superseded claims are listed in §10.

---

## 1. Memory model and OS loader chunk map [STAT]+[DYN]

### 1.1 CPU address semantics [STAT]

The 68k/68302 CPU address semantics remain unchanged:

- absolute-word addressing sign-extends:
  - `$00e2.w` → CPU `0x0000E2`
  - `$8258.w` → CPU `0xFF8258`
  - `$86f8.w` → CPU `0xFF86F8`
  - `$a67e.w` → CPU `0xFFA67E`

This is CPU-visible address formation only. It does **not** imply that the
`00xxxx` and `FFxxxx` CPU views share the same physical backing RAM.

The important distinction is:

```text
CPU semantics:
  abs.w >= $8000 -> FFxxxx
  abs.w <  $8000 -> 00xxxx

Physical backing:
  decided by board decode / chip-selects / loader destination view
  not implied by abs.w sign extension
```

### 1.2 Retracted global mirror model [DYN]

The previous model:

```text
0x000000–0x00FFFF aliases 0xFF0000–0xFFFFFF
```

is retracted.

A clean non-aliased loader run proves that the OS loader intentionally writes
different contents to the low and high CPU views at the same low offsets. The
global alias experiment made later low-view chunks overwrite previously loaded
high-view code and produced the false `CA7E → FC6000 → ERROR 129` cascade.

Therefore:

- keep 68k abs.w sign-extension;
- do **not** globally mirror `00xxxx` and `FFxxxx`;
- treat low and high CPU views as separate loader destinations/backings unless a
  more precise hardware decode proves otherwise.

### 1.3 Logged loader chunks [DYN]

Relevant V161 loader chunks from the clean non-aliased run:

```text
low-system-block diagnostic:
  loader chunk 5 / FDC transaction 6
  destination: 000000..0003FF, low view
  includes:
    0000E0..0000EB =
      B2 2A FF F8 89 68 FF F8 85 54 27 00

    long[$00E2] = FFF88968
    long[$00E6] = FFF88554

chunk 5:
  destination: 000944..000B43, low view
  includes:
    000AE2..000AE5 = 00008B00

chunk 11:
  destination: FF8000..FFA1FF, high view
  includes:
    FF8258 = 8140
    FF825A = FFFF
    FF86F8..FF86FF = 00000000... at this checkpoint

chunk 12:
  destination: FFA200..FFC9FF, high view
  parser / overlay code:
    FFA67E = 3078
    FFA680 = 0346
    FFA682 = 3250
    FFB22A = 1239
    FFB22C = FFFC
    FFB22E = 4813

chunk 16:
  destination: 009C00..00B5FF, low view
  different legitimate low-view data/code:
    00A67E = CA7E
    00A680 = 000E
    00B22A = 344E
    00B22C = 4E4B
    00B22E = 4EB8

chunk 19:
  destination: FC6000..FC61FF, FC/DPRAM/local view
  includes:
    FC6000 = 46FC 2700 0188 0004 54AF 0002 4A94 0188 ...

chunk 20:
  destination: 000944..000B43, low view
  again includes:
    000AE2..000AE5 = 00008B00
```

The focused low-system diagnostic and the earlier broad chunk recorder use
different local chunk-counting scopes. FDC transaction number and destination
range are therefore retained alongside the reported chunk number. The proven
provenance is transaction 6 to low destination `000000..0003FF`.

### 1.4 Corrected memory model [DYN]+[MODEL]

The clean working model is:

```text
00xxxx and FFxxxx are separate CPU-visible RAM destinations/backings.
```

The loader intentionally populates both views. They are not globally mirrored.

MC68302 chip-select / remap writes (`FC6830..FC683E`, later `FC684A=0000`) occur
early; no relevant CS/remap write was observed between the high chunk 12 and low
chunk 16 loader passes. Current evidence therefore supports **separate low/high
RAM views**, not runtime bank switching between those chunks.

### 1.5 Consequences [DYN]

- The parser code at `FFB22A` is valid high-view code loaded by chunk 12.
- The bytes at `00B22A` are different legitimate low-view data/code loaded by
  chunk 16.
- The previous global FF/low alias made chunk 16 corrupt chunk 12.
- The resulting `CA7E` / `FC6000` / `ERROR 129` path was alias-induced and is
  retracted as a clean-run blocker.
- `000AE2 = 00008B00` is legitimate low-view loader data from chunks 5/20.
- `FF0AE2` is not written in the clean run.
- The previous single-linear OS mapping model is invalid.

### 1.6 Low-system callback initialization [STAT]+[DYN]

V161 contains:

```text
img[0x30e2] = FFF88968 FFF88554
```

In the clean non-aliased run, loader chunk 5 / FDC transaction 6 writes the
source block directly to low CPU destination `000000..0003FF`.

Observed destination bytes:

```text
0000E0..0000EB =
  B2 2A FF F8 89 68 FF F8 85 54 27 00
```

Therefore:

```text
long[$00E2] = FFF88968
long[$00E6] = FFF88554
```

The write-watch identifies loader code at `fb8ab6` as the writer. The values
remain present at `LOADING SYSTEM` and final dispatcher idle.

No corresponding signature was observed in high `FFxxxx` or FC/local backing.

This closes the earlier `FF0AE2` misprojection:

```text
img[0x30e2] initializes the low DUART callback table at $00E2/$00E6.
It is not high-view FF0AE2 dispatch data and does not explain node type 89A2.
```

The surrounding bytes also show that the callback family is initialized as one
contiguous low-system block:

```text
$00DE = FFB22A
$00E2 = FFF88968
$00E6 = FFF88554
$00EA = 00002700
```

`$00EA = 00002700` resembles interrupt/status-register initialization data, but
its exact role remains unresolved. [HYP]

---

## 2. SCN2681 DUART at `$FC4800` [STAT]

Standard SCN2681 register file on odd byte addresses. Every known firmware access
fits this map:

```text
FC4803 read  = SRA        FC4813 read  = SRB
FC4805 write = CRA        FC4815 write = CRB
FC4807 r/w   = RHRA/THRA  FC4817 r/w   = RHRB/THRB
FC4809       = IPCR/ACR   FC480B read  = ISR
FC480B write = IMR        FC480D/0F    = counter/timer
```

`IMR = 0x2B` (bits 0,1,3,5 = TxRDYA, RxRDYA, counter, RxRDYB). **TxRDYB (bit4) is
not interrupt-enabled** — Channel B transmit is not interrupt-driven. [STAT]

IRQ6 demux `f884be` [STAT]:

```text
bit5 -> ($00de)
bits1/2 -> ($00e2)
bit0 -> ($00e6)
bit3 -> $8638.w
unknown bit -> moveq #$91; trap #0 = ERROR 145 "Unknown DUART interrupt error"
```

This independently confirms IRQ6 = DUART.

The low callback table is now proven to be loaded directly from V161:

```text
$00DE = FFB22A
$00E2 = FFF88968
$00E6 = FFF88554
```

### Channel model

```text
Channel B — panel MCU channel [MODEL, strong]:
  display output bytes written to THRB/FC4817 through the f89a72 ring path and
  the early-boot polled path f89cb0; panel keys/ACK/status arrive on RHRB/SRB
  through the ffb22a parser. RxRDYB (bit5) is the panel input interrupt.

Channel A — separate serial TX/RX service, plausibly MIDI [MODEL, strong]:
  f884fc arms TX via CRA; f88554 is the TxRDYA trampoline; f8857a the
  continuation; RxRDYA (bits1/2) service = f88968. Not the main panel path.
```

Channel A is **no longer the best panel-TX candidate**. Do not describe
`f884fc/f8857a` as panel-output machinery.

---

## 3. MC68302 [STAT]/[DYN]

`GIMR=0x8040` → dedicated interrupt mode:

```text
IRQ1 -> vector 0x51 -> ff87ce -> jmp $a48c
IRQ6 -> vector 0x56 -> f884be
IRQ7 -> vector 0x57 -> unused stub f882da -> ERROR 139
```

Internal registers observed:

```text
FC6812  GIMR
FC6814  IPR
FC6816  IMR
FC6818  ISR
FC6850  TMR1
FC6852  TRR1
```

No internal level-4 source is active post-load in the current clean run.

MC68302 chip-select / remap configuration writes are observed early:

```text
FC6830..FC683E
FC684A = 0000 later
```

No relevant CS/remap write was observed between the high chunk 12 and low chunk
16 loader passes. This supports the current model that low/high destinations are
separate views/backings rather than the result of runtime banking between those
chunks.

### 3.1 Corrected Port A/B GPIO register map [STAT]

Internal base `FC6000`. Register map, sourced from the RTEMS `m68302.h`
register structure and the MC68302 User's Manual, independently anchored by
the already-observed `FC6830-FC683E` chip-select initialization writes:

```text
FC681E  PACNT
FC6820  PADDR
FC6822  PADAT
FC6824  PBCNT
FC6826  PBDDR
FC6828  PBDAT  (low byte FC6829 = PB7..PB0)
FC682A  reserved
FC682C  reserved
FC682E  reserved
FC6830  BR0
FC6832  OR0
FC6834  BR1
FC6836  OR1
FC6838  BR2
FC683A  OR2
FC683C  BR3
FC683E  OR3
```

This **retracts** an earlier, informal doubt (never committed to this doc, but
carried across sessions) that Port B might live at `FC6834/6835` instead of
`FC6824/6826/6828`. `FC6834/6836` are confirmed **BR1/OR1** — chip-select
base/option registers, not Port B — matching this driver's own long-standing
`m68302_register_name()` candidate labels at offsets `0x24/0x26/0x28`
(`port_b_control_candidate` / `port_b_direction_candidate` /
`port_b_data_bits0_2_control_lrclk_bit3_candidate`), which the corrected map
now confirms rather than contradicts.

**Phase 2 GPIO/PAR-correlation experiment** (`ASR10_EXPERIMENT_MC68302_GPIO_TRACE=1`,
log-only, no analog model): tested whether PBDAT bits 2:0 act as an
analog-mux channel select feeding the ES5506 host-port PAR register
(`docs/asr10/es5506-chain-verification.md`). Findings, from two independent
captures (45s and a longer run reaching the same steady state):

- **Stage 1 gate PASSES**: `PBCNT=0x80` (bits 2:0 = 0, GPIO mode, not
  peripheral IACK7/6/1) and `PBDDR=0x97` (bits 2:0 = 7, configured as
  outputs), both set at `fb8e16`/`fb8e1e` very early in boot (`fire_count=0`).
  Bits 2:0 are genuinely configured as GPIO outputs.
- **Stage 2 correlation FAILS**: PBDAT bits 2:0 are written **exactly once**
  (`fb8e2e`, value `0b111`) and are **never rewritten again** in either
  capture. The routine at `0067f6` (previously guessed to be a
  "bits0-2 strobe set" — `ori #$07`) executes exactly once (`fire_count=142`)
  and, empirically, only ORs in bit 3 (the LRCLK candidate bit); it does not
  touch bits 2:0. Across every observed `00686e`/`FC60B0` PAR-measurement
  pass (fire_counts 146, 150, 154, 158, 162, 166, 170, 174 in both captures,
  identically), PBDAT bits 2:0 read back a single constant value (`7`), and
  PAR itself reads constant `0`. The required evidence for an analog-mux
  hypothesis — at least two distinct bits-2:0 values, each stable during its
  own measurement pass — does not exist in either capture.
- **Verdict: B — PB2:0 are configured as GPIO outputs, but no PAR
  correlation is observed.** Stage 3 (diagnostic analog-mux model) is
  **not** implemented, per the experiment's own gating (Stage 3 requires
  Stages 1 *and* 2 to both pass; only Stage 1 did).

ROM addresses the internal block via base `$FC6000` in the OS-load DMA setup
(`+0x802/804/808/80c/810`). The OS load itself completes by CPU FIFO polling once
the FDC is reachable.

`FC6000..FC61FF` is also populated by loader chunk 19. This confirms that the
DPRAM/local/thunk area is real loaded content. However, the previous execution of
the `FC6000` thunk during the `CA7E/ERROR129` run was alias-induced downstream
corruption and is not a clean default-run blocker.

---

## 4. Board decode [STAT]/[MODEL]

Known board parts:

```text
U20 = SCN2681 DUART
U28 = MC68302
U34 = uPD72069 FDC
U41 = ES5701 SuperGLU
U43 = ES5510
U5  = custom PAL "ASR-10 V1.1" + 74HC138/139 decoders
```

FDC/uPD72069 access is verified on the `$FC4001/03` byte-wide window during the
OS-load path. `$FC5001/03` remains a leading but unproven peripheral-window /
possible partial-decode alias candidate. Earlier evidence involving `FC500x` was
contaminated by the global FF/low alias experiment and must not be used as proof
that `$FC5001/03` is FDC.

Byte-wide peripherals on odd addresses are expected on a 68k bus. However,
device identity must be established from clean-run access sequences, caller
context, and board decode evidence.

SuperGLU is audio-bus glue only:

```text
CPU <-> ESP/OTIS
sample-RAM latching
clock division
Buchty VHDL reference
```

It is unrelated to FDC/DUART.

---

## 5. Trap table at ROM `f82080` [STAT]

| Trap | Handler | Semantics |
|------|---------|-----------|
| #2 | `f88066` | pool test: carry set iff `$0b7f ≥ 0xBB` |
| #3 | `f88078` | alloc node from free list `$0b6c` → A5; ERROR 144 if empty |
| #4 | `f880a2` | **release** A5 → free list `$0b6c`, `$0b7f--` |
| #6 | `f880d6` | pop node from active slot (`$0b6a`) queue → A5; promotion helper |
| #9 | `f88138` | post A5 to slot A1: **bit7 protocol**; see §6 |
| #D | `f881f6` | post A5 to service channel A1: if idle → `jsr (A1)` handler; else enqueue |
| #10/#11 | `ffff88e8/e2` | OS-installed; unanalyzed |
| #12 | `f88174` | service-channel post variant; unanalyzed |

Old logs reading trap #4 as "scheduler/event service" must be re-read as
**free/release**.

```text
$0b6c = free-list head
$0b7f = allocation count
```

---

## 6. Slot scheduler [STAT]+[DYN]

Six slots, stride `0x16`, list `$00c6..$00c8`.

Pending rule:

```text
pending iff byte +2 != byte +3
```

Slot bases:

```text
23d4  slot0
23ea  slot1
2400  slot2
2416  slot3
242c  slot4
2442  slot5
```

Lowmem pointer family:

```text
$00d8 -> slot0
$00da -> slot2
$00dc -> Channel A TX queue
```

### Trap #9 wake protocol [STAT]+[DYN]

Trap #9 uses a bit7 protocol:

- direct promotion to `+0c` only when bit7 is set and `+0c` is empty;
- always `bclr #7`;
- Slot3 state `8080` is compatible → `0080`, runnable, node dispatched at
  `f8f37a` [DYN];
- Slot0 state `0202` is parked on bit1 — trap #9 queues into `+10/+12` without
  waking it.

This is by design.

### Slot0 bit1 wake [STAT]+[DYN]

Only verified statically-coded immediate bit1 clear:

```asm
f89ac2: bclr #1,$0002(A0)
```

via `$00d8`, gated by ring counter `byte[$03bc]` reaching zero
(see `panel-protocol.md`).

Dynamic `bclr Dn` sites exist in the scheduler core:

```text
f880c8
f880f6
f8811a
f8816e
f8826a
f8831a
```

These are wake-bit-generic and must not be confused with the verified static
immediate slot0 bit1 clear.

ROM treats `0202` as a named state:

```asm
f919e6/f91a16:
  movea.w $00d8.w,A0
  cmpi.w #$0202,...
```

### Slot0 typed system message queue [STAT]

`f97f10` is a typed consumer:

```asm
f97f10: moveq #0,D4
        movea.w $00d8.w,A0
f97f16: tst.w $0010(A0)
        beq rts
f97f1c: trap #6                      ; pop → A5
f97f1e: cmpi.w #$8810,$0002(A5)      ; type filter (8810 ≈ disk completion?)
f97f26: (match) move.w $0004(A5),D4
f97f2a: trap #4                      ; releases node REGARDLESS of type
```

Warning: `f97f10` must never be treated as the generic `14f4/89a2` consumer.
Invoking it on a queued `89a2` node would destroy the node. It proves slot0 has
typed consumers and that not every reader is safe for every node type.

The correct `89a2` consumer is **unknown**.

### Parked resume `f87f66` [STAT]

`f87f66` is pure context restore:

```asm
move.l USP,A0
push saved PC/SR
movea.l (A0)+,A1
movem.l (A0)+,D2-D7/A2-A6
jmp (A1)
```

The meaningful target is the restored A1 at `f87f74`, not `f87f66` itself.
Instrumentation must log restored A1/A5/A2 and the final `jmp` target.

---

## 7. Service channels and `$14C0` [STAT]

Service-channel object layout:

```text
+00 handler ptr
+04 current
+06 head
+08 tail
+0c count
+10 active
```

Trap #D behavior:

```text
idle   -> current=A5, active=1, jsr (A1)
active -> enqueue
```

`$14C0` instance, loaded from `img 0x3ac0`, identical V161/V350:

```text
+00 = FFF884FC
```

This is **Channel A TX-arm**, therefore the Channel A / likely MIDI TX service
channel.

Preceded at `$14a8` by 12 buffer pointers, stride `0x48`
(`$1050..$13b0`) [STAT data; buffer role HYP]. `f884fc` has zero static refs in
boot ROM — trap #D is its only known invocation route.

Channel A TX lifecycle [STAT]:

```text
arm:
  CRA = $04
  $86fc = A5
  $86f8 = f8857a

TxRDYA:
  f88554 trampoline
  jmp ($86f8)

continuation:
  f8857a feeds bytes

completion:
  f8855e:
    trap #4
    next buffer from $00dc via f8822c
    none -> CRA = $08 (TX disable)
```

The earlier `f88554 -> $86f8=00210021` crash was a diagnostic artifact
(TxRDY asserted without transmitter-enable check).

---

## 8. Node dispatch values [STAT]/[MODEL]

Node `+02` carries mixed semantics:

- small even values (`0002..001e`, `0006`) participate in table/type dispatch
  at `f8f37a` [DYN for `0006`];
- literal typed matches exist, e.g. `8810` in `f97f10`;
- larger values (`882e/880a/8810/89a2`) are written by routines such as
  `f88b4e/f89012/f8903c/f8f2e6/f9098e`.

### `89a2` status [OPEN]

The previous model:

```text
word[FF8258] = 8140
FF8140 + signed(89a2) = FF0AE2
[FF0AE2] = FFF88968
```

is retracted.

Clean loader evidence shows:

- `FF8258 = 8140` is real high-view data from chunk 11.
- `FF0AE2` is not written in the clean run.
- `000AE2 = 00008B00` is legitimate low-view loader data from chunks 5 and 20.
- The values `FFF88968 / FFF88554` previously projected onto `FF0AE2` came from
  an invalid single-linear OS-load assumption.

Clean loader provenance now proves:

- `img[0x30e2..0x30e9] = FFF88968 FFF88554`;
- loader chunk 5 / FDC transaction 6 writes the containing source block
  directly to low destination `000000..0003FF`;
- loader code at `fb8ab6` writes:
  - `long[$00E2] = FFF88968`
  - `long[$00E6] = FFF88554`;
- the callback values remain present at `LOADING SYSTEM` and final dispatcher
  idle;
- no corresponding signature was observed in high `FFxxxx` or FC/local
  backing.

Therefore `img[0x30e2]` is proven low-system callback initialization, not
high-view `FF0AE2` dispatch data. It does not explain node type `89a2`.

The correct consumer/meaning of node `14f4 +02 = 89a2` remains unknown.

The RAM `$89a2` jump block (`jmp $3a62/$3ab2/$3ba0`) is real OS code but is not
the dispatch target; the earlier interpretation was a red herring.

---

## 9. Error codes anchored in code [STAT]

`139` unused vector (`f882da`, `moveq #-$75`); `144` alloc-fail (trap #3,
`moveq #$90`); `145` unknown DUART interrupt (`f884f8`, `moveq #$91; trap #0`).

`129` belongs to the 68k exception family and should be treated as generic
address error / odd address until the exception frame proves the faulting cause.

In the alias experiment, `ERROR 129` was caused downstream of global FF/low
alias-induced code corruption and is retracted as a clean-run blocker.

---

## Appendix A — Error codes anchored to code

Sources: ASR Service Manual error table + ROM byte verification. Manual text is
diagnostic documentation, **not** runtime evidence — the ROM anchors are [STAT].

### A.1 Common error path [STAT]

All stubs load the code into D0 and branch to the common tail:

```asm
f88280: move.w D0,$00c0.w
```

Then panel prompt:

```text
ERROR NNN - REBOOT ?
```

Then input poll through `f89cd4/f89dec`.

Runtime signature:

```text
word[$00c0] = ff<code>
```

### A.2 68k exception family: 128–139 [STAT]

Stub table: `f882aa–f882da`.

| Err | Meaning (manual) | ROM stub | Note |
|-----|------------------|----------|------|
| 128 | bus error | `f882aa` (`moveq #$80`) | vector 2 |
| 129 | odd address / address error | `f882ae` (`moveq #$81`) | vector 3 |
| 130 | divide by zero | `f882b6` (`moveq #$82`) | |
| 131 | illegal instruction | `f882b2` (`moveq #$83`) | |
| 132 | CHK out of bounds | `f882ba` | |
| 133 | TRAPV overflow | `f882be` | |
| 134 | privilege violation | `f882c2` | |
| 135 | trace | `f882c6` | |
| (136) | line 1010 | — | no error stub: Line-A is repurposed as a service |
| 137 | line 1111 emulator | `f882d2` (`moveq #$89`) | |
| 138 | spurious interrupt | `f882d6` | |
| 139 | unused vector | `f882da` (`moveq #$8b`) | proven cause of old raw-autovector experiment failure |

Group-0 frame at `ERROR 129` handler entry:

```text
(SP+0).w  access info
(SP+2).l  fault address
(SP+6).w  IR
(SP+8).w  SR
(SP+A).l  fault PC
```

Note: the stacked PC may point after the faulting instruction. Use the IR field
to identify the actual faulting instruction.

`ERROR 129` after the global FF/low alias experiment was caused downstream of
alias-induced corruption and must not be treated as the current clean-run fault.

### A.3 Resource/service errors anchored in code [STAT]

| Err | Meaning (manual) | Anchor | Note |
|-----|------------------|--------|------|
| 144 | out of buffers — "usually too much incoming MIDI data" | trap #3 alloc fail: `moveq #$90` when free list `$0b6c` empty | manual's MIDI attribution independently supports **Channel A = MIDI** → [MODEL, strong] |
| 145 | unknown DUART interrupt | `f884f8`: `moveq #$91; trap #0` | IRQ6 demux fall-through; confirms IRQ6 = DUART |
| 080 | bad buffer to MIDI | unanchored | same broader MIDI/buffer family as 144 |
| 020 | unknown button event | panel parser family; raw→key map at `fff82484` | association |
| 009 | no LRCLK to 68302 | known; analog-board clock | experiment `..._LRCLK_CLOCK_BIT3` |

### A.4 Disk/FDC-related notes

Two-digit file-operation errors `00–22` are filesystem-level. Errors `32–35`
and `40–44` are NEC uPD72069/FDC-related.

Manual distinction:

```text
error 40  = disk controller / drive family
error 040 = ESP
```

Slot0 queue nodes of type `8810` consumed by `f97f10` plausibly carry
disk-operation completions [HYP].

Do not conflate three-digit `040` ESP with two-digit `40` disk/FDC.

### A.5 Caution

Manual error codes label firmware anchors and guide diagnostics; they are not
direct proof of runtime cause.

In particular, `ERROR 129` means an address error occurred, but does not by
itself distinguish:

```text
bad map experiment
invalid jump target
wrong stack/frame
odd pointer
device-map side effect
genuine firmware exception path
```

In the current evidence set, the observed `ERROR 129` path from the alias run is
retracted as a map-experiment artifact.

---

## 10. Superseded claims

```text
OLD → NEW

Channel A TX path is panel TX
  → Channel B ring is the panel-output path

A0 55 00 is a panel message needing ACK
  → Channel A service TX; reclassified

f884fc/f8857a is panel-output machinery
  → Channel A TX service, likely MIDI

$03bc counts abstract outstanding ops
  → byte count of the $0378 output ring

trap #4 = scheduler/event service
  → free/release to $0b6c

$0b6c/$0b7f = finalizer ptr/countdown
  → free-list head / allocation count

RAM $89a2 jump block = boot continuation
  → red herring; 89a2 meaning/consumer currently unknown

89a2 = signed table offset to FF0AE2/F88968
  → retracted; based on invalid single-linear OS-load model

"$03bc→0 releases node 14f4 directly"
  → wake only; iterative loop; see current-blocker.md

f8f37a handles 89a2 like 0006
  → unobserved; do not assume

OS file loads contiguously to FF0A00
  → retracted; loader uses multiple chunks/views

global FF/low RAM mirror
  → retracted; clean run proves separate low/high loader destinations

FF0AE2 contains FFF88968/F88554
  → retracted; FF0AE2 is not written in clean run

img[0x30e2] belongs to FF0AE2 dispatch data
  → retracted; loaded directly to low $00E2/$00E6 callback table

000AE2/FF0AE2 mismatch proves alias bug
  → retracted; 000AE2 is legitimate low-view loader data

CA7E at FFA67E is firmware pseudo-op
  → alias-induced artifact; clean high view has FFA67E=3078

FC6000/ERROR129 is default blocker
  → alias-induced corruption cascade; not present in clean non-aliased run

FC5001/03 proven FDC alias
  → not proven; remains possible peripheral-window hypothesis
```

---

## 11. Current clean-run blocker summary

The clean non-aliased default run returns to the pre-alias state:

```text
LOADING SYSTEM / load progress
→ dispatcher idle at f87f96/f87f9a
```

Current blocker:

```text
iterative Channel B panel-output/ACK loop
$03bc output-ring drain/wake
slot0 bit1 release
unknown consumer/promotion path for node 14f4/+02=89a2
```

The `CA7E/FC6000/ERROR129` path is retracted as an alias-induced artifact.

Completed proof step:

```text
Low-system callback mapping is closed:
  V161 img[0x30e2..0x30e9]
  → chunk 5 / FDC transaction 6
  → low destination 0000E2/0000E6
  → FFF88968 / FFF88554

The callback data is unrelated to the queued node type 89a2.
```

Next proof steps:

```text
1. Reconstruct the natural Channel B output stream:
   log every f89a8a/f89a72 enqueue in a clean, non-aliased run:
     - common sequence number
     - caller PC
     - D2 byte
     - printable ASCII representation
     - byte[$03bc] before/after
     - ring pointers $03b8/$03ba
     - slot0 state
     - whether this happens before or after slot0 wake

2. Correlate the full panel chain:
     PANEL_ENQUEUE
     PANEL_THRB
     PANEL_RHRB
     PANEL_COMPLETE
     PANEL_WAKE

   PANEL_WAKE must also include:
     - slot0 state before/after
     - slot0 queue head at $23e4
     - slot0 queue tail at $23e6
     - node 14f4 type at $14f6

Goal:
  recover the actual display/panel byte stream and determine the minimal
  authentic ACK/status behavior required from a panel-MCU stub.
```
