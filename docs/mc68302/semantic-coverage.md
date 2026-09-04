# MC68302 Semantic Coverage — classification concept

Flyttad från `~/develop/mc68302/docs/mc68302/semantic-coverage.md`
2026-07-30, **kraftigt omskriven**. Originalet beskrev sidoprojektets
egen kod ("Current Catalog Summary", "Observed ASR-10 Access Status"
med per-register `FullyImplemented`/`MappedNotImplemented`-omdömen) —
det beskriver inte något som finns i MAME. Behållet här är bara
klassificeringsidén, som är den bredare, mer utbyggda versionen av vad
`docs/asr10/PLAN.md` fas 3 kallar `known`/`known-unimplemented`/
`unknown`. Se `docs/mc68302/README.md`.

## Status Meanings (sidoprojektets sexdelade modell)

| Status | Meaning |
|---|---|
| `FullyImplemented` | Documented read/write/reset/timing/interrupt/pin/DMA/bus effects relevant to the register are modeled. |
| `ImplementedExternallyUnconnected` | Chip behavior is modeled, but no board-level destination/source is connected. |
| `MappedNotImplemented` | Offset and register identity are known, but required semantics are incomplete. |
| `ReservedByManual` | Manual marks the offset/range as reserved. |
| `ReadOnly` | Manual-defined read-only semantics are represented. |
| `WriteOnly` | Manual-defined write-only semantics are represented. |

Shadow-only storage (a register that only stores the last-written
value with no side effects) is never sufficient for
`FullyImplemented` in this model.

## Relation to fas 3's simpler classification

`docs/asr10/PLAN.md` fas 3's "inbyggda krav" asks for a 3-way
classification per access — `known`, `known-unimplemented`, `unknown`
— at roughly 50 lines, not this six-tier catalog. The mapping, if a
future step wants the richer version:

```text
known                 <- FullyImplemented, ReadOnly, WriteOnly,
                          ImplementedExternallyUnconnected
known-unimplemented   <- MappedNotImplemented, ReservedByManual
unknown               <- anything not in the register map at all
```

Fas 3 step 1 implements the 3-way version only (see `mc68302.h`'s
`access_class` enum). This file is kept as a design reference in case
a later phase wants the finer-grained distinction (e.g. separating "we
know this register exists but haven't modeled its side effects" from
"the manual itself says this is reserved").

## What is *not* carried forward

The original's "Current Catalog Summary" and "Observed ASR-10 Access
Status" table (register-by-register `FullyImplemented`/
`MappedNotImplemented` judgments, a "Strict Guard Consequence" section,
and a bounded-run instruction/cycle count) described the side project's
own emulator state at a point in time. None of it describes this
MAME device, which starts from zero. Re-derive coverage from
`mc68302.cpp`'s own counters instead of trusting that table.
