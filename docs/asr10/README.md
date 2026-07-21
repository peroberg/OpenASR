# ASR-10 MAME boot/emulation notes

This directory contains the working knowledge for bringing the Ensoniq ASR-10 up in MAME.

The current working target is not yet a clean production driver. It is an instrumented boot harness used to discover how the ASR-10 ROM and loaded runtime code talk to hardware.

## Start here

For a new Codex/Claude/Gemini session, start with, in order:

1. **`current-status.md`** — the canonical, dated current status: build/run
   commands, what is proven, device-architecture status, and the smallest
   next step. Start here.
2. **`panel-protocol.md`** — the canonical front-panel host-side protocol
   findings (TRAP #$A encoder, marker matrix, record library).
3. `documentation-audit.md` — full classification of every other document in
   this directory (candidate/reference/journal/handoff/obsolete), a
   contradiction register, and the source-file structural inventory and
   split proposal. Read this before trusting any file not listed above.

Every other file in this directory is candidate material: historical,
specialist reference, or chronological journal material that has not been
fully reconciled with the current state. Do not treat any of them as
authoritative on their own without checking `documentation-audit.md` first —
several (`checkpoint.md`, `status.md`, `running.md`) describe project phases
that are long superseded.

Do not rely on chat history alone. Treat these files as the project memory.

## Files

Canonical:

- `current-status.md` — dated current status, build/run, device-architecture status, next step.
- `panel-protocol.md` — front-panel host-side protocol findings.
- `documentation-audit.md` — full classification of every file below, contradiction register, source structural inventory and split proposal.

Everything below is candidate material — see `documentation-audit.md` for
current classification (specialist reference / evidence journal / historical
handoff / obsolete) before trusting any of it as current truth:

- `checkpoint.md`, `status.md` — obsolete status snapshots from 2026-06-22; superseded by `current-status.md`.
- `running.md` — obsolete build/run instructions (stale binary name, missing `SUBTARGET=mess`); use `current-status.md` §2 instead.
- `roadmap.md` — project phases; predates the current boot-progress state.
- `hardware-map.md` — known/suspected chip and register map.
- `boot-flow.md` — ROM boot path notes, frozen at 2026-06-22.
- `memory-map.md` — known/suspected ASR-10 address map.
- `experiments.md` — experiment/stub history.
- `fdc.md` — floppy/FDC findings.
- `panel-input-display.md` — earlier panel/display findings, largely folded into `panel-protocol.md`.
- `vfx-reuse.md` — VFX/TS/SD reuse notes; partially superseded now that ES5506/ES5510 real-device integration is done.
- `open-questions.md` — open-questions register; not yet reconciled with `panel-protocol.md` §6.
- `evidence-tree.md`, `current-blocker.md`, `filesystem-browser-map.md` — chronological evidence journals (the latter is the primary evidence log, referenced 87 times from source comments).
- Dated handoff/investigation files (`asr10-handoff-2026-06-22.md`, `asr10-mame-handoff-*.md`, `asr10-panel-slot0-handoff-2026-07-13.md`, `asr10-boot-slot0-2026-07-13.md`, `*-investigation-2026-06-29.md`, `scheduler-slot0-continuation-findings-2026-06-29.md`) — point-in-time handoffs.
- `subsystems.md`, `es5506-chain-verification.md`, `es5701-wiring.md`, `movep-library.md`, `troubleshoot.md`, `vfx-es5510-comparison.md` — narrow specialist references.

## Current discipline

Keep new work narrow and empirical:

- Prefer diagnostics before behavior stubs.
- Keep experiments disabled by default.
- Validate before committing.
- Commit small checkpoints.
- Do not treat unknowns as facts.
- Do not let agent chat history become the only project memory.

## Validation

Before committing harness changes:

```sh
git diff --check
make -j12 SUBTARGET=mess SOURCES=src/mame/ensoniq/asr10_boot.cpp
```

## Current one-line goal

See `current-status.md` for the current state and the smallest next step —
do not treat the line that used to be here as current; it described a phase
(pre-`LOADING SYSTEM` progress) that has long since been passed.
