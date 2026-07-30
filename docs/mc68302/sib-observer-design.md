# MC68302 SIB Observer Design (future-phase reference)

Flyttad från `~/develop/mc68302/docs/mc68302/sib-observer-design.md`
2026-07-30, oförändrad — det är redan ett designförslag, inte ett
"fungerar nu"-påstående om sidoprojektets kod. `docs/asr10/PLAN.md`
fas 3 nämner "SIB Observer" och "Semantic Guard" som mekanismer att
**flytta, inte bygga om**; det här dokumentet är den designen. Fas 3
steg 1 bygger bara den enkla klassificeringsräknaren (se
`semantic-coverage.md`), inte denna fullständiga observatör. Se
`docs/mc68302/README.md`.

This is a design note for a passive SIB observer: something that
records raw SIB accesses and semantic events without changing emulator
state.

## Goals

The observer should describe MC68302 internal-register behavior
without changing emulator state. It should help detect when ASR-10
firmware reaches new SIB needs, especially late configuration writes
and status polls.

It must not:

- hardcode ASR-10 PC values,
- hardcode ROM opcodes,
- inject register semantics,
- modify bus routing,
- suppress CPU exceptions,
- drive devices.

## Event Model

Suggested event shape:

```cpp
struct SibAccess {
    std::uint64_t instructionCount;
    std::uint64_t cycleCount;
    std::uint32_t pc;
    std::uint32_t pcAfter;
    std::uint32_t address;
    std::uint16_t offset;
    AccessWidth width;
    AccessKind kind;
    std::uint8_t functionCode;
    std::uint32_t oldValue;
    std::uint32_t value;
    std::uint32_t newValue;
    SibRegisterId registerId;
};
```

Possible `AccessKind` values: `Read`, `Write`, `ReadModifyWriteRead`,
`ReadModifyWriteWrite`, `StatusClear`, `ConfigurationWrite`,
`DataWrite`, `CounterRead`.

The base event should remain factual. Higher-level classification can
be layered on top.

## Register Identity

Use a typed register ID rather than string-only classification —
one enum value per named SIB register (`Cmr`, `Sapr`, `Dapr`, `Bcr`,
`Csr`, `Fcr`, `Gimr`, `Ipr`, `Imr`, `Isr`, `Pacnt`, `Paddr`, `Padat`,
`Pbcnt`, `Pbddr`, `Pbdat`, `Br0`..`Or3`, `Tmr1`, `Trr1`, `Tcr1`,
`Tcn1`, `Ter1`, `Wrr`, `Wcn`, `Tmr2`, `Trr2`, `Tcr2`, `Tcn2`, `Ter2`),
matching `docs/mc68302/sib-register-map.md`.

## Phase Model

Suggested diagnostic phases: `Reset`, `EarlyInternalMapping`,
`SibInitialization`, `PostSibInitialization`, `EarlyFirmware`,
`CurrentBootFront`. Phase changes should be based on observed events,
not fixed PCs. For ASR-10, "SIB initialization complete" is best
marked by the last initial PIO configuration write
(`docs/mc68302/observed-access-coverage.md`), followed only by
ISR status-clear traffic.

## Baseline and Change Detection

The observer should support: baseline snapshot after total reset,
baseline snapshot after BAR/SCR bootstrap, first read per register,
first write per register, first post-init write per register,
changed-from-baseline detection, changed-from-last-write detection,
write-without-change detection.

For byte accesses to word registers, report even/high or odd/low byte
lane, full word before, byte value, full word after. For
read-modify-write instructions, group the read and write phases under
one instruction event when the trace provides the same instruction
count and PC.

## Severity

- `INFO`: first initialization write, passive data write, expected status clear.
- `NOTICE`: value changed during initialization, repeated write with same value, first read of a passive register.
- `WARNING`: configuration register changed after initialization, unknown register access, unsupported width, unimplemented status dependency.
- `ERROR`: illegal width, bus-model contradiction, internal access not routed through the internal-register handler, impossible state transition for implemented semantics.

## Register Classes

- `Configuration`: BAR/SCR, GIMR, IMR, PACNT/PBCNT, PADDR/PBDDR, BR/OR, WRR, TMR/TRR, IDMA control/address/count.
- `StatusEvent`: IPR, ISR, CSR, TER, SCCE.
- `Data`: PADAT, PBDAT, parameter RAM, SCC data registers.
- `Counter`: TCN, WCN.
- `Reserved`: documented reserved offsets.
- `Unknown`: not in the current register map.

## Allowlist Principle

Do not encode an allowlist as permanent boot behavior. Use it only in
diagnostics to separate expected known writes from new observations.
Example ASR-10 diagnostic allowlist (from `observed-access-coverage.md`
plus `timer2-interrupt-spec.md`): early CS0..CS3 BR/OR writes, PIO
setup writes, `GIMR=0x8040`, `IMR=0x0000` (early), IPR/ISR
write-one-to-clear sequences, `WRR=0x0000`, `TRR2=0x3F01`,
`TMR2=0x003B`. Any later write to these configuration registers should
be at least `WARNING` until reviewed.

## Ring Buffer

A compact ring buffer of recent events — instructions, SIB accesses,
bus accesses, phase transitions, semantic events — for post-stop
diagnosis. Suggested sizes: 32 instructions, 64 bus accesses, 64 SIB
accesses, 32 semantic events. Not for printing full execution on
success.

## Semantic Events

`InternalBaseConfigured`, `ScrConfigured`, `ChipSelectConfigured`,
`PioDirectionConfigured`, `PioDataChanged`, `InterruptMaskConfigured`,
`InterruptStatusClear`, `TimerConfigured`, `WatchdogDisabled`,
`FirstPostInitConfigurationWrite`, `UnknownSibAccess`, plus a Timer-2
specific subset (`Timer2Enabled`, `Timer2Disabled`,
`Timer2ReferenceChanged`, `Timer2ReferenceMatch`,
`Timer2ReferenceEventSet/Cleared`, `Timer2PendingSet/Cleared`,
`Timer2InterruptMasked/Asserted/Acknowledged`,
`Timer2InServiceSet/Cleared`). These should always link back to one or
more raw `SibAccess` records; the observer records, it does not derive
or enforce emulator behavior.

## Explicit Non-Goals

The observer should not implement: MC68302 interrupt-controller
semantics, Timer 1/2 counting, IDMA, PIO signal routing, watchdog
expiration, chip-select routing, ASR-10 device behavior, PC-based boot
control. (In MAME terms: this stays a Lua-reachable diagnostic per
`CLAUDE.md` rule 3, not C++ that changes machine behavior.)
