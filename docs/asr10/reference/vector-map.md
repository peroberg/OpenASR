# ASR-10 vector map

Kanonisk uppslagstabell for 68k- och MC68302-vektorer som ar
dokumenterade for ASR-10. Historik och experimentresonemang hor hemma i
`../investigations/`.

Status:

- **[V]** verifierat mot ROM-bytes, livekorning, servicehandbok eller
  MC68302-manual
- **[L]** sannolikt, harlett men inte livebekraftat
- **[H]** hypotes eller historisk uppgift som inte far byggas vidare pa
  utan ny matning

Tabelloffset ar `vektor * 4`. For MC68302-genererade vektorer med
observerad `GIMR=$8040` galler:

```text
vector = (GIMR.V7..V5 << 5) | source_low_5 = 0x40 | source_low_5
```

Efter IACK laser 68000-processorn handler-longword ur vektortabellen.
Det ar en separat CPU-minneslasning, inte samma busscykel som IACK.

## 68k reset/exceptions

| Vector | Offset | Source | Delivery | Handler | Further dispatch | Status | Source |
|---:|---:|---|---|---|---|---|---|
| 0 | `$000` | initial SSP | reset table fetch | `$00000300` | stack pointer, not code | [V] | `boot-sequence.md`, `subroutine-index.md` |
| 1 | `$004` | initial PC | reset table fetch | `$0000000C` in reset overlay / ROM routine `$F8000C` | bootstrap starts with `move.w #$2700,sr` | [V] | `boot-sequence.md`, `subroutine-index.md` |
| 2 | `$008` | bus error | CPU exception vector fetch | likely `$F882AA` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 3 | `$00C` | address error | CPU exception vector fetch | likely `$F882AE` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 4 | `$010` | illegal instruction | CPU exception vector fetch | likely `$F882B2` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 5 | `$014` | divide by zero | CPU exception vector fetch | likely `$F882B6` | ERROR 130 path through `$F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 6 | `$018` | CHK/CHK2 | CPU exception vector fetch | likely `$F882BA` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 7 | `$01C` | TRAPV | CPU exception vector fetch | likely `$F882BE` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 8 | `$020` | privilege violation | CPU exception vector fetch | likely `$F882C2` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 9 | `$024` | trace | CPU exception vector fetch | likely `$F882C6` | `moveq #error,D0`; `bra $F88280` | [L] mapping, [V] bytes/error meaning | `subroutine-index.md` |
| 10 | `$028` | Line-A emulator | CPU exception vector fetch | likely `$F882CA` | older note: patches stacked SR/CCR, skips opcode | [L] | `memory-map.md` |
| 11 | `$02C` | Line-F emulator | CPU exception vector fetch | unknown, within stub range if table continues | unknown | [H] | `subroutine-index.md` |
| 12 | `$030` | reserved/unassigned | CPU exception vector fetch | unknown, within stub range if table continues | unknown | [H] | `subroutine-index.md` |
| 13 | `$034` | coprocessor protocol | CPU exception vector fetch | unknown, within stub range if table continues | unknown | [H] | `subroutine-index.md` |
| 14-23 | `$038-$05C` | CPU exceptions/reserved | CPU exception vector fetch | not documented | not documented | [H] | none |

`$F88280` is the verified shared exception tail: it masks interrupts,
writes D0 to `($00C0).w`, calls `$F977B0`, `$FF8D44`, `$F8CF9C` and
`$F89D46`, then jumps to `$FB8E3E`. The exact vector-table longwords for
the stub range are still not live-verified.

## TRAP #0-#15

| Trap | Vector | Offset | Source | Delivery | Handler | Further dispatch | Status | Source |
|---:|---:|---:|---|---|---|---|---|---|
| #0 | 32 | `$080` | firmware hard-error service | `trap #0` | not mapped | `$F884F8` raises ERROR 145 by loading D0=`$91` before trap | [V] use, [H] handler | `subroutine-index.md` |
| #1 | 33 | `$084` | firmware service | `trap #1` | older note: `$F87F76` | unknown | [H] | investigations |
| #2 | 34 | `$088` | allocator/node service | `trap #2` | not mapped | partially observed around browser/node flow | [H] | `../investigations/filesystem-browser-map.md` |
| #3 | 35 | `$08C` | allocator/node service | `trap #3` | not mapped | partially observed around browser/node flow | [H] | `../investigations/filesystem-browser-map.md` |
| #4 | 36 | `$090` | node/free service | `trap #4` | not mapped | partially observed around node teardown | [H] | `../investigations/filesystem-browser-map.md` |
| #5 | 37 | `$094` | scheduler/service | `trap #5` | older note: `$F880B6` | older note: falls into trap #6 path | [H] | `../investigations/filesystem-browser-map.md` |
| #6 | 38 | `$098` | scheduler/service | `trap #6` | older note: `$F880D6` | older note: yield-like path | [H] | `../investigations/filesystem-browser-map.md` |
| #7 | 39 | `$09C` | scheduler yield | `trap #7` | `$F88108` | enters scheduler context-save flow | [V] | `subroutine-index.md` |
| #8 | 40 | `$0A0` | scheduler sleep | `trap #8` | `$F8812C` | writes active slot counter, then yields through trap #7 path | [V] | `subroutine-index.md` |
| #9 | 41 | `$0A4` | node/post service | `trap #9` | older note: `$F8813C` | posts node used after KEYBOARD TUNED | [L] | `boot-sequence.md`, `../investigations/filesystem-browser-map.md` |
| #10 | 42 | `$0A8` | unknown | `trap #10` | not documented | not documented | [H] | none |
| #11 | 43 | `$0AC` | unknown | `trap #11` | not documented | not documented | [H] | none |
| #12 | 44 | `$0B0` | firmware service | `trap #12` | older note: `$F88174` | unknown | [H] | `../investigations/filesystem-browser-map.md` |
| #13 | 45 | `$0B4` | firmware service | `trap #13` | not mapped | observed callers, handler identity unknown | [H] | investigations |
| #14 | 46 | `$0B8` | firmware service | `trap #14` | not mapped | unknown | [H] | investigations |
| #15 | 47 | `$0BC` | firmware service | `trap #15` | older note: `$F88056` | unknown | [H] | investigations |

## Autovectors

| Vector | Offset | Source | Delivery | Handler | Further dispatch | Status | Source |
|---:|---:|---|---|---|---|---|---|
| 24 | `$060` | spurious interrupt | CPU autovector/reserved interrupt path | not documented | not documented | [H] | none |
| 25 | `$064` | autovector level 1 | autovector IACK | older note: ERROR 139 path | unused in current DUART path | [H] stale | `hardware-map.md` |
| 26 | `$068` | autovector level 2 | autovector IACK | older note: ERROR 139 path | unused in current DUART path | [H] stale | `hardware-map.md` |
| 27 | `$06C` | autovector level 3 | autovector IACK | older note: ERROR 139 path | unused in current DUART path | [H] stale | `hardware-map.md` |
| 28 | `$070` | autovector level 4 | autovector IACK | older note: ERROR 139 path | unused in current DUART path | [H] stale | `hardware-map.md` |
| 29 | `$074` | autovector level 5 | autovector IACK | older note: ERROR 139 path | unused in current DUART path | [H] stale | `hardware-map.md` |
| 30 | `$078` | autovector level 6 | autovector IACK | older note: ERROR 139 path | not the current DUART IRQ6 path | [H] stale | `hardware-map.md` |
| 31 | `$07C` | autovector level 7 | autovector IACK | older note: ERROR 139 path | unused in current DUART path | [H] stale | `hardware-map.md` |

Current ASR-10 DUART IRQ6 is vectored IACK `$56`, not autovector level 6
`$1E`.

## MC68302 internal interrupt vectors

All internal MC68302 INRQ sources below are fixed CPU level 4. Source bit
is the corresponding `IPR`/`IMR`/`ISR` bit, except bit 0: `IMR` bit 0 is
undefined and `ISR` bit 0 is always zero. `IMR=1` enables a source;
`IMR=0` masks it.

| Vector | Offset | Source | Bit | Delivery | Handler | Further dispatch | Status | Source |
|---:|---:|---|---:|---|---|---|---|---|
| `$40` | `$100` | level-4 error | 0 | level-4 IACK with no eligible INRQ | not identified | none documented | [V] chip, [H] ASR handler | `../../mc68302/interrupt-source-map.md` |
| `$41` | `$104` | PB8 | 1 | internal INRQ from Port B interrupt input 0 | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/interrupt-source-map.md` |
| `$42` | `$108` | SMC2 | 2 | internal INRQ from SMC2 event | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$43` | `$10C` | SMC1 | 3 | internal INRQ from SMC1 event | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$44` | `$110` | Timer3/watchdog | 4 | internal INRQ from watchdog/timer event | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/watchdog-spec.md` |
| `$45` | `$114` | SCP | 5 | internal INRQ from SCP event | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$46` | `$118` | Timer2 | 6 | internal INRQ from `TER2` reference/capture event if unmasked | not verified | no Timer2 interrupt observed | [V] chip/init, [H] handler | `../../mc68302/timer2-interrupt-spec.md`, `boot-sequence.md` |
| `$47` | `$11C` | PB9 | 7 | internal INRQ from Port B interrupt input 1 | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/interrupt-source-map.md` |
| `$48` | `$120` | SCC3 | 8 | internal INRQ from SCC3 event register/mask | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$49` | `$124` | Timer1 | 9 | internal INRQ from Timer1 event | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/interrupt-source-map.md` |
| `$4A` | `$128` | SCC2 | 10 | internal INRQ from SCC2 event register/mask | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$4B` | `$12C` | IDMA | 11 | internal INRQ from IDMA CSR normal/error event | `$FFFF87E8` -> `$87E8.w` -> `$251A` / high-view `$F01B1A` | MC68302/SIB/IDMA completion dispatcher; reads `$FC680E`, masks/clears IDMA bit, then `jmp [$0402]` | [V] chip, [V] ASR handler | `../../mc68302/idma-spec.md`, `storage-completion-dispatch.md` |
| `$4C` | `$130` | SDMA bus error | 12 | internal INRQ from SDMA bus error | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$4D` | `$134` | SCC1 | 13 | internal INRQ from SCC1 event register/mask | not identified | none documented | [V] chip, [H] ASR use | `../../mc68302/communications-block-map.md` |
| `$4E` | `$138` | PB10 | 14 | internal INRQ from Port B interrupt input 2 | older note: `$F88F06` | not current baseline evidence | [V] chip, [H] stale ASR handler | `hardware-map.md`, `../../mc68302/interrupt-source-map.md` |
| `$4F` | `$13C` | PB11 | 15 | internal INRQ from Port B interrupt input 3 | older note: `$F88F22` | not current baseline evidence | [V] chip, [H] stale ASR handler | `hardware-map.md`, `../../mc68302/interrupt-source-map.md` |

Timer 2 exact ASR-10 state: ROM writes `TMR2=$003B` and `TRR2=$3F01`.
`TMR2=$003B` decodes as `PS=0`, `CE=00`, `OM=1`, `ORI=1`, `FRR=1`,
`ICLK=01`, `RST=1`, so the counter can be enabled and can produce local
reference events. In the verified boot snapshot `IMR` bit 6 is masked
(`IMR=0` at `$FB8E7E`), so Timer 2 must not assert CPU level 4 there.
No Timer2 IACK, vector `$46` handler, or actual Timer2 interrupt is
verified for the current boot.

## External MC68302 IACK vectors

These are MC68302-supplied external request vectors at `GIMR=$8040`.
External levels 2, 3 and 5 have no MC68302-generated vector in the
manual table and need an external vector source or autovector.

| Vector | Offset | Source | Delivery | Handler | Further dispatch | Status | Source |
|---:|---:|---|---|---|---|---|---|
| `$51` | `$144` | external IRQ1 | MC68302 external IACK if enabled for vector generation | `$FFFF87CE` -> `$87CE.w` -> `$00BAB6` / high-view `$F114B6` | shared storage/device completion dispatcher; FDC and SCSI branches before `jmp [$0402]`; physical source/routing [OPEN] | [V] chip, [V] ASR handler, [OPEN] board source | `../../mc68302/vector-origin-map.md`, `storage-completion-dispatch.md` |
| `$56` | `$158` | DUART IRQ6 | DUART `irq_cb` asserts board IRQ6; MC68302 IACK returns `$56` | `$F88300` | counter-ready tick production is verified here; DUART RxRDYB dispatch is verified through `$F884BE`, but branch ordering from `$F88300` to `$F884BE` is not fully documented | [V] | `../investigations/duart.md`, `boot-sequence.md`, `../../mc68302/vector-origin-map.md` |
| `$57` | `$15C` | external IRQ7 | MC68302 external IACK if enabled for vector generation | not identified | not documented | [V] chip, [H] ASR use | `../../mc68302/vector-origin-map.md` |

DUART subpaths currently documented:

- Counter-ready path: SCN2681 counter/timer reaches ready state, DUART
  IRQ asserts, vector `$56` reaches `$F88300`; `$F88300` stops the
  counter via `$FFFC481F`, updates scheduler counters and runs secondary
  callbacks every tenth tick.
- Channel B RX path: panel response enters `mc68681_device` channel B
  RX-FIFO, SRB RxRDY and ISR bit 5 are owned by the DUART, `irq_cb`
  asserts IRQ6, vector `$56` reaches firmware, and `$F884BE` dispatches
  bit 5 through `($00DE)` to a handler that pops RHRB. The target behind
  `($00DE)` is not identified.

## Known gaps

- Live vector-table longwords for CPU exceptions and TRAPs have not been
  captured as a single verified table.
- The exact mapping from `$F882AA-$F882DA` to CPU vectors is not fully
  verified; bytes and error-code meanings are verified.
- TRAP handlers other than #7 and #8 are mostly investigation-level.
- The indirect targets behind DUART dispatcher pointers `($00DE)`,
  `($00E2)`, `($00E6)` and `($8638)` are not identified.
- No current ASR-10 source is identified for external IRQ1/IRQ7.
- SCC1-SCC3, SMC1-SMC2, SCP, SDMA, IDMA, watchdog and PB8-PB11 have
  documented MC68302 vector identities but no verified current ASR-10
  handler/use, except that the FDC path is verified not to use IDMA.
- The later path that may unmask `IMR` to `$E480` is not localized here.

## Stale or retired vector notes

- Older notes that autovectors `$19-$1F` lead to ERROR 139 are not the
  current DUART IRQ6 mechanism.
- Older accepted-looking `$4E/$4F` runs and `FC6818=4000/8000` handler
  paths predate the current flagless boot and are not current baseline
  evidence.
- Older Timer2/AN414 speculation about missing DUART counter start is
  retired. MC68302 Timer2 vector `$46` and DUART external vector `$56`
  are separate mechanisms.
- `0x2400` service-source notes in old maps are not a substitute for a
  verified MC68302 source/handler chain.

## Autovector IRQ6 vs vectored IACK `$56`

68000 autovector level 6 is vector `$1E`, table offset `$078`, and is
used only when the interrupt acknowledge cycle returns the autovector
response. The current ASR-10 DUART path is different: the DUART asserts a
board IRQ6 input, the MC68302 supplies vector `$56` during IACK, and the
CPU fetches the handler longword from `$158`.

---

## Femkategorimodellen — hall isar fem olika saker som alla kallas "vektorer"

Tillagt 2026-08-04 ur den statiska ROM/OS-analysen. Se `rom-os-abi.md` och
`os-image-layout.md`.

| # | kategori | var den finns | nar den galler |
|---|---|---|---|
| 1 | ROM:s resetvektorer | ROM `$F80000-$F80007`, synliga pa `$000000` genom CS0-overlagget | fran reset till BR0-omprogrammeringen |
| 2 | OS-bildens vektoravbild | OS-fil `+0x0000-0x03FF`, pa disk | statisk data, kors aldrig darifran |
| 3 | Installerade vektorer | RAM `$000000-$0003FF` | efter installation, resten av korningen |
| 4 | MC68302:s avbrottsvektorer | genereras av SIB via GIMR/IPR/IMR | lopande |
| 5 | Hoppbordsposter som ser ut som vektorer | bindningstabellen, DPRAM-tabellen | lopande |

Kategori 2 och 3 har samma innehall men ar **inte samma sak**, och tidpunkten da 2 blir
3 ar fortfarande `[OPEN]`.

### OS-bildens vektoravbild `[V]`

`os[0x000:0x0C0]` — vektor 0 till 47 — ar **byte-identisk mellan V1.61 och V3.50**.
I `0x0C0-0x3FF` skiljer sig **13 byte** (offset 199, 201, 212, 213, 215, 217, 219, 224,
225, 418, 419, 470, 471). Det omradet ar OS-variabler, inte vektorer.

```
v0   SSP   $00000300
v1   PC    $00000000        <-- noll i BADA versionerna
v2   $FFF882AA   v3 $FFF882AE   v5 $FFF882B6   v9 $FFF882C6   v10 $FFF882CA
v4   $FFFC6000 (DPRAM)      v11 $FFFC6014 (DPRAM)
v15  $FFF882DA   v24 $FFF882D6   v25-31 $FFF882DA
v32  $FFF88280 (exception-svans)   v33 $FFF87F76 (schemalaggaren)
v40  $FFF8812C   v47 $FFF88056
```

Av de 48 verkliga vektorerna (0-47) pekar **42 in i ROM och 2 i DPRAM**.

**`v1 = 0` betyder att ROM inte kan gora en mjuk reset ur bilden.** Det finns ingen
startadress i OS-filen. Se `rom-os-abi.md` §6.

### Installationen till `$000000-$0003FF` ar INTE verifierad `[OPEN]`

Under segment 1-regeln (`RAM = OS_offset + 0xA00`, se `os-image-layout.md`) hamnar
bildens vektoravbild pa RAM `$000A00-$000DFF`, **inte** pa `$000000`. Nagot maste kopiera
ned den, eller installera tabellen separat. Vilken rutin, och nar, ar okant.
Experiment E1 (skrivtapp pa `$000000-$0003FF` och `$000A00-$000DFF` med PC loggat)
avgor det.

Tidigare formulering "OS-vektortabellen skrivs till RAM" har anvants som om den vore
verifierad. Det ar den inte.

### `$0000C0-$0003FF` ar variabler, inte vektorer `[V]`

158 langord i det omradet klassades tidigare som "lagminnesvektorer" och betraktades som
ankarkandidater. De ar OS-variabler som ligger i den oanvanda delen av
vektortabellsregionen (vektor 48-255 anvands inte).

### Att inte forvaxla

| ser ut som | ar faktiskt |
|---|---|
| `$FFFC6000` i vektor 4 | en post i DPRAM-hoppbordet, inte en hanterare |
| `$FFF882DA` x34 i bilden | catch-all-stubben, alltsa vektorer — **inte** 34 anrop |
| `$8D50.w` | slot i bindningstabellen pa RAM `$008D50`, inte en variabel |
| langord i `$0000C0-$0003FF` | OS-variabler, inte vektorer |
