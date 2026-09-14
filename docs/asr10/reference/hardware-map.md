# ASR-10 hardware map

This file tracks known, strongly suspected, and currently hypothesized ASR-10 hardware blocks.

Do not treat hypotheses as confirmed hardware behavior. The current `asr10booth` target is still an instrumented research harness, not a clean final production driver.

## Current hardware-side blocker

The emulator reaches:

```text id="uzwgdm"
ENSONIQ ASR-10
LOADING SYSTEM
```

Then loaded runtime code returns to dispatcher idle around:

```text id="s94gzs"
f87f96 / f87f9a / f87fca
```

This is not currently treated as a crash. It is a firmware dispatcher queue scan / idle state.

The current blocker is probably not raw panel input, FDC media format, or simple interrupt vectoring.

Current best hardware-side suspects:

```text id="9ks6xd"
- dispatcher queue re-arm
- slot 2 callback-chain / scheduler re-arm
- producer vs finalizer path after slot 2 completion
- MC68302 timer/service completion, only if firmware branches on it
- FC6884/FC6894 completion behavior
- lowmem service state around $0d06/$0e82
```

## Known or strongly suspected hardware

### Main CPU

Service/manual/board information indicates:

```text id="yplczl"
MC68302 MCU
```

Observed on physical hardware at Main Board U28: `MPU Motorola MC68302FC16C 1C65T QETY9307` (`[OBSERVED — owner-supplied physical inspection]`; see `docs/asr10/reference/physical-component-inventory.md`).

Current harness uses:

```cpp id="11v4kv"
M68000(config, m_maincpu, XTAL(16'000'000));
```

This is a 68000-compatible stand-in.

Implication:

```text id="ij07x5"
The current harness can execute ordinary 68k ROM/runtime code, but does not yet model all MC68302-specific modules.
```

MC68302-specific features likely needed later:

```text id="841gbe"
- BAR/SCR remap/control
- chip selects
- interrupt controller
- timers
- ports
- SCC/serial channels
- DPRAM/parameter RAM
```

Do not immediately implement a full MC68302. Current work should continue reconstructing the minimum control-plane behavior required by the ASR-10 firmware.

### MC68302 / FC68xx current findings

> **RATTAT 2026-08-04 — las detta innan avsnittet nedan.**
> Registeridentiteterna i det har avsnittet var gissningar. Ratt identitet enligt
> MC68302 UM Table 2-9, korsvaliderad mot tre kanda skrivningar:
>
> ```
> $FC6814 = IPR    Interrupt Pending Register   (gissningen stamde)
> $FC6816 = IMR    Interrupt Mask Register      (INTE service/in-service)
> $FC6818 = ISR    In-Service Register
> $FC6884 = SCM1   SCC1 Mode Register           (INTE timer)
> $FC6894 = SCM2   SCC2 Mode Register           (INTE timer)
> $FC68B2 = SIMASK
> $FC68B4 = SIMODE
> ```
>
> Foljden: `$2400` = IMR bit 13 + bit 10 = **SCC1 + SCC2**. Skrivningen pa `$00BF1A`
> **avmaskerar** deras avbrott. Att rensa `$FC6816` maskerar avbrottet — det kvitterar
> ingenting. **Sparet "rensa FC6816" ar avskrivet.** EOI sker till ISR `$FC6818`, vilket
> OS:ets SCC-hanterare redan gor korrekt.
>
> Allt nedan i det har avsnittet ar bevarat som historik men **galler inte**. Aktuell
> modell: `mc68302-status.md`. Runtimeobservationerna: `runtime-service-model.md` —
> observera att de kommer fran en **V1.61-korning**, inte fran nuvarande HEAD.

Current candidate internal/register window:

```text id="7o3o9d"
$FC6800-$FC68FF  MC68302 internal / board-control candidate
```

Important current-phase registers:

```text id="h0e4e1"
$FC6814  pending/status candidate
$FC6816  service/in-service candidate
$FC6818  control/ack/EOI-ish candidate
$FC6884  timer/control/reload candidate
$FC6894  timer/control/reload candidate
```

Current accepted-looking service source:

```text id="z1maen"
0x2400
```

Observed lifecycle:

```text id="3cb36h"
FC6814 000b -> 240b   source/pending injected
IACK vector 0x4e/0x4f
FC6818 = 4000 or 8000 from vector handler
FC6814 240b -> 000b   clears naturally
runtime 00bf1a sets FC6816 c080 -> e480
runtime 00bf22 sets $0d06
optional gated experiment clears FC6816 e480 -> c080
dispatcher still returns to f87f9a idle
```

Vector findings:

```text id="yu01rq"
0x4e -> f88f06 -> writes FC6818=4000
0x4f -> f88f22 -> writes FC6818=8000
```

Both vectors are accepted-looking and converge. The current blocker is not simply choosing between `0x4e` and `0x4f`.

Wrong/deprioritized vectors:

```text id="42qw1o"
autovectors 0x19..0x1f -> ERROR 139 unused vector
0x40 -> ERROR 139 unused vector
0x46 -> ERROR 129 odd address error
```

Timer/control candidates:

```text id="0s50lv"
FC6850=003b
FC6852=3f01
FC6884=703b
FC6894=703b
```

Hypothesis:

```text id="w84yl0"
0x3b may be timer/count/period-related.
0x703b may be mode/control plus count/reload.
```

Open question:

```text id="urgjby"
Should FC6884/FC6894 produce a later timer/completion event that re-arms the firmware dispatcher queue?
```

Current caution:

```text id="fc68caution"
Raise FC68xx/MC68302 back to primary only if a firmware path clearly tests external status/timer/completion and that test controls producer/re-arm after slot 2 completion.
No proven post-set firmware read/test of FC6816 0x2400 is known in the current sequence.
```

### FDC

Floppy controller:

```text id="h247go"
NEC uPD72069
```

Current candidate address window:

```text id="qdt4l4"
$FC4000-$FC4003
```

Observed FDC commands:

```text id="nmjwtc"
36
0B
4F
1E
0E
88
F3
03
07
08
46
```

FDC/media questions are still valid, but FDC is not the current immediate blocker once the emulator reaches `LOADING SYSTEM` and idles in the dispatcher.

Current caution:

```text id="q7sfwx"
Do not fake new FDC behavior to solve the current f87f9a dispatcher idle unless logs show a post-service FDC path is actually being attempted.
```

### SCSI

> **VERIFIERAT 2026-08-04.** SCSI-kretsen ligger pa `$FC5001` (indexregister) och
> `$FC5003` (dataregister), i **CS3**. ROM `$FBB5C0` laddar bada som pekare, och bade
> ROM och bada OS-versionerna skriver `#$18` (WD33C93 Command) foljt av `#$00` (Reset).
> Se avsnittet "SCSI ligger i CS3, inte CS1" langst ned. Hypotesen att CS1 skulle vara
> SCSI-optionen ar motbevisad.

Likely SCSI controller:

```text id="gzi1rt"
AM33C93A-16JC / WD33C93A-compatible
```

Observed on physical hardware at SCSI Board U2: `AM33C93A - 16JC 9607GBA E 1989 AMD` (PLCC-44, `[OBSERVED — owner-supplied physical inspection]`; see `docs/asr10/reference/physical-component-inventory.md`).

Current candidate address window:

```text id="r0oy7s"
$FC5000-$FC501F
```

SCSI should be treated separately from FDC.

Open questions:

```text id="vfxvz4"
- Confirm exact SCSI controller.
- Confirm exact ASR SCSI register window.
- Determine boot priority and probe behavior.
- Determine minimum SCSI behavior needed for OS/runtime to continue.
```

### Audio

See `audio-storage-architecture.md` for the current evidence-separated
checkpoint tying the Ensoniq ES5701/ES5506/ES5510 chip specs to the ASR-10
storage/load boundary model.

Likely family audio path:

```text id="19puuo"
ES5506 / OTIS -> pump -> ES5510 / ESP -> output
```

VFX/TS/SD MAME code is likely valuable reference for:

```text id="oz4qy7"
- ES5506 setup
- ES5510 setup
- pump/routing
- host register patterns
- effect parameter writes
```

ASR-specific unknowns:

```text id="c0q1pt"
- complete ES5506 audio routing beyond the host register window
- complete ES5510/ESP audio execution and routing beyond the host register window
- [Likely] `$FC3001`: ES5510 host latch/register offset `$00`.
  `$F97662` gör host-port write/read mot `$FFFC3001` med retry.
  Adressen ligger i CS2 (`$FC2000-$FC3FFF`) och i samma verifierade
  hostfönster som `$FC3101/$FC3141/$FC3181`.
- sample RAM mapping
- glue/chipselect behavior
- external delay/work RAM for ESP if applicable
```

A/D and PCM caution:

```text id="wsxtw2"
Audio sample data probably does not flow through the MC68302 as ordinary CPU-readable input.
More likely path:
analog input -> ADC/codec -> serial audio clock/data -> OTIS/ESP/audio ASIC path -> sample RAM / DSP path
```

MC68302 may still observe or control audio-related clocks/status. This is relevant because of:

```text id="vgh2pr"
ERROR 009 No LRCLK input to 68302
```

But do not assume PCM sample data itself is transported through MC68302 registers.

### Analog performance/control acquisition

This is separate from PCM sampling. V3.50 continuously writes MC68302 PBDAT
bits 2:0, waits about 1.98 ms, reads the ES5506's 10-bit PAR/POT register and
updates per-control RAM blocks. The runtime selector order is
0,2,5,3,4 with periodic 7; the diagnostic viewer maps these to PITCHWHL,
MODWHEEL, MR. KNOB, VOLUME, PEDAL and REFRENCE respectively. Aggregate PAR
rate is 500/s in both normal idle and diagnostics.

The remaining logical selector values are bounded more tightly in V3.50.
Selector 1 is conditional on boot-ROM model `"88"` and a key-event countdown;
its 0..127 producer is `[Likely]` the ASR-88 mono/channel-pressure source.
Selector 6 has no generator in the analyzed acquisition path and is
`[Verified unreachable]` there, while its physical pin purpose remains
`[OPEN]`. Selector 7 is a periodically filtered calibration reference, not a
host control. `MR. KNOB` is the service diagnostic name for the Data Entry
slider. Input Level is not a member of this PAR scan; it belongs to the
separate audio-input/gain subsystem. See
`../investigations/analog-selector-control-map-v350.md`.

The board has U55 MC74HC4051N, an 8:1 analog mux. Component capability plus
the measured three-bit scan makes U55 `[Likely acquisition mux]`, not
`[Verified acquisition mux]`. Still `[OPEN]`: U55 pin 3 COM destination,
pins 9/10/11 selector source, pin 6 enable and X0-X7 physical control mapping.
See `../investigations/analog-control-acquisition-v350.md`.

The current harness is not yet a faithful implementation: its ES5506 PAR
callback selects an emulator value with `m_duart_io & 7`, not firmware PBDAT,
and the three host adjuster channel labels do not match the measured firmware
selector identities. This is documented technical debt, not fixed in the
analysis round.

### Panel/frontpanel

Evidence points to a panel/frontpanel/DUART-like window:

```text id="yk162b"
$FC4800-$FC481F
```

Known candidates:

```text id="7hds5b"
$FC4817 = display/panel TX data candidate
$FC4809 bit 4 = input/event/status gate candidate
```

Possible hardware involvement:

```text id="e3hmyn"
ENS5702000102 + 80C52 frontpanel/keyboard/display controller
```

Interpretation:

```text id="ngb6f1"
Likely external panel/frontpanel controller or glue, not simply MC68302 internal UART.
```

Current caution:

```text id="f6t3y0"
Panel input is probably not the immediate blocker while the machine is still in LOADING SYSTEM and dispatcher idle. Do not fake LOAD/CMD/EDIT/instrument-select input until logs show the runtime is actually waiting for user events.
```

### Super-GLU / ES5701 class

Reported/claimed functions for ES5701/Super-GLU-class ASIC:

```text id="bqshj8"
- 68000/68302 to ESP interface
- 68000/68302 to OTIS interface
- OTIS to static/sample memory interface
- clock generation
- glue/chipselect/waitstate/bus arbitration behavior
```

Do not try to emulate as a complete chip immediately. Model effects as discovered:

```text id="pc82vu"
- address decode
- sample RAM visibility
- OTIS/ESP host paths
- status bits
- port/bank/remap behavior
- interrupt/completion behavior
```

Possible relation to current blocker:

```text id="l393bq"
[DISPROVEN] `$FC6884`/`$FC6894` and `$2400` are not generic
Super-GLU-adjacent completion/timer/status behavior in the current model:
`$FC6884`/`$FC6894` are MC68302 SCM1/SCM2 and `$2400` is IMR bits SCC1+SCC2.
Storage completion routing remains separate from ES5701/Super-GLU audio glue.

[DISPROVEN — specified ES5701 register/storage model] ES5701 SuperGLU contains zero
internal registers or RAM capable of storing the table, and its specified address outputs
reach only up to LA19. Physical CS1 -> ES5701 board wiring remains [NOT ESTABLISHED / OPEN],
but ES5701 itself cannot store the per-voice sample banking table ($FF7F00-$FF7FFF).
Its role is restricted to audio host/memory bus translation, isolation and clock division.
```

## Current control-plane model

The current boot/runtime path suggests the following control-plane chain:

```text id="2bsw73"
MC68302/board service source 0x2400
-> FC6814 pending bit
-> M68K IACK
-> vector 0x4e or 0x4f
-> FC6818 handler write
-> FC6814 pending clear
-> runtime service setter at 00bf1a
-> FC6816 service/in-service bit set
-> $0d06 service flag set
-> dispatcher returns to f87f9a idle
```

What is still missing:

```text id="18yn8k"
A producer/re-arm path after slot 2 completion, or a proven external status/timer/completion condition that gates that producer/re-arm path.
```

## Current one-line hardware takeaway

The current hardware problem is no longer simply "what chip exists where?". The immediate problem is that after the accepted-looking MC68302/FC68xx `0x2400` service sequence, baseline slot 2 callback enters `007308`, reaches the `00bf1a/00bf22` service setter, and post-service/finalizer code writes slot 2 to equalized `8080`; hardware-side work should only return to FC68xx/MC68302 details once the firmware path shows an external status/timer/completion controls the missing producer/re-arm.

---

## Chip selects - harledda ur ROM:s egna BR/OR-skrivningar `[V]`

Tillagt 2026-08-04. Full avkodning och CS1-dossier i `memory-map.md` §1 och §5.
Detta ar harlett ur firmware, inte ur drivrutinens `mem_map`.

ROM programmerar alla fyra chip selects i en foljd pa `$F8001E-$F8005D`, fore allt
annat. Format enligt MC68302 UM §3.6.2:

```
BR:  15-13 FC2-FC0 | 12-2 BASE ADDRESS (A23-A13) | 1 RW  | 0 EN
OR:  15-13 DTACK   | 12-2 BASE ADDRESS MASK      | 1 MRW | 0 CFC
```

`CFC` ar **OR bit 0**, inte bit 15. `MRW = 0` betyder RW maskad (bade las och skriv).
For samtliga fyra ar CFC = 0 - ingen FC-jamforelse ar paslagen nagonstans.

| CS | BR | OR | fonster | riktning | DTACK | innehall |
|---|---|---|---|---|---|---|
| CS0 (reset) | `$0001` | `$3F82` | `$000000-$03FFFF` | endast lasning | 1 WS | ROM-overlagg vid boot |
| CS0 (efter) | `$1F01` | `$3F82` | `$F80000-$FBFFFF` | endast lasning | 1 WS | ROM, 256 KB |
| CS1 | `$1FEF` | `$FFFE` | `$FF6000-$FF7FFF` | **endast skrivning** | **extern** | per-röst sample-banking-tabell (`$FF7F00-$FF7FFF`); fysisk mottagare öppen |
| CS2 | `$1F85` | `$FFFC` | `$FC2000-$FC3FFF` | las + skriv | extern | ES5506 `$FC2000`, ES5510 `$FC3000` |
| CS3 | `$1F89` | `$7FFC` | `$FC4000-$FC5FFF` | las + skriv | 3 WS | FDC `$FC4000`, DUART `$FC4801`, SCSI `$FC5001` |

Allt utanfor CS0-CS3 och BAR-fonstret (`$FC6000-$FC6FFF`) maste avkodas av kortlogiken.
68302:an gor det inte.

### CS1 `$FF6000-$FF7FFF` — per-röst sample-banking [VERIFIED funktion / OPEN hårdvara]

```
enabled
write-selected / write-only
external DTACK
no function-code comparison
function: per-voice sample banking table at $FF7F00-$FF7FFF (32 voices x 4 words)
```

Fönstret är 8 KB (`$FF6000-$FF7FFF`). Firmware använder de översta 256 byten
(`$FF7F00-$FF7FFF`) för att konfigurera en 32-rösters översättningstabell där varje rösts
4 MB ES5506-adressfönster dynamiskt pekas mot godtyckliga 1 MB DRAM-segment.
Implementeringen i `asr10_boot.cpp` (commit `b7cd112199d`) löste den reproducerade
Bank 11-playability-defekten. Detta förklarar den reproducerade historieberoende
BANK-load-avvikelsen och stämmer med vissa tidigare symptom, men bevisar inte
retroaktivt att alla historiska ljudanomalier hade samma orsak.

**Dokumentationsgräns:** ES5701 SuperGLU saknar register/RAM kapabelt att lagra tabellen
(`[DISPROVEN — specified ES5701 register/storage model]`). Fysisk CS1 $\rightarrow$ ES5701-koppling
är `[NOT ESTABLISHED / OPEN]`. Den fysiska krets som tar emot CS1 och rollen för
PAL U5 / DTACK på det 4-lagers moderkortet är `[OPEN]` då Digital Board-schemat
saknas i servicemanualerna (`DOCUMENTATION FRONTIER REACHED — BOARD OWNERSHIP`).
Se `../investigations/cs1-board-level-implementation-frontier.md`.

### SCSI ligger i CS3, inte CS1 `[V]`

AM33C93A ar WD33C93-kompatibel: ett indexregister och ett dataregister.

```
$FC5001   indexregister
$FC5003   dataregister

ROM $FBB5C0   movea.l #$00FC5001,A4
ROM $FBB5C6   movea.l #$00FC5003,A3
ROM $F8A72C   move.b  #$18,($FC5001)    register $18 = Command
OS            move.b  #$00,($FC5003)    = Reset
```

Bade ROM och **bada** OS-versionerna driver kretsen. Hypotesen att CS1 skulle vara
SCSI-optionen ar darmed motbevisad.
