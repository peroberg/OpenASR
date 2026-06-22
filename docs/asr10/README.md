# ASR-10 MAME boot/emulation notes

This directory contains the working knowledge for bringing the Ensoniq ASR-10 up in MAME.

The current working target is not yet a clean production driver. It is an instrumented boot harness used to discover how the ASR-10 ROM and loaded runtime code talk to hardware.

## Start here

For a new Codex/Claude/Gemini session, start with:

1. `checkpoint.md` — short current truth and next task.
2. `status.md` — broader current status.
3. `experiments.md` — experiment history and what each stub proved.
4. `hardware-map.md` — current register/address map.
5. `asr10-handoff-2026-06-22.md` — long archive only when details are missing.

Do not rely on chat history alone. Treat these files as the project memory.

## Files

- `checkpoint.md`  
  Short current checkpoint for new agent sessions: current phase, safe defaults, confirmed facts, latest negative result, and next task.

- `status.md`  
  Broader current truth: branch, target, build commands, known blockers, and current next step.

- `roadmap.md`  
  Project phases from boot harness to usable ASR-10 emulation.

- `running.md`  
  Build and run commands, log filters, regression commands, media paths.

- `hardware-map.md`  
  Known and suspected chips: MC68302, uPD72069 FDC, ES5506, ES5510, SCSI, panel/frontpanel MCU, Super-GLU.

- `boot-flow.md`  
  ROM boot path, prompt decisions, lowmem status fields, known PCs.

- `memory-map.md`  
  Known/suspected ASR-10 address map and relation to ROM/RAM/sample-RAM/MMIO.

- `experiments.md`  
  Every intentional stub/path-opener and what it proved or did not prove.

- `fdc.md`  
  Floppy/FDC findings, command traces, ASR disk geometry, current image-format problem.

- `panel-input-display.md`  
  Display text path, panel/input status path, `$FC4800-$FC481F` candidate map.

- `vfx-reuse.md`  
  Which parts of VFX/TS/SD MAME work are likely reusable: ES5506, ES5510, pump, audio path.

- `open-questions.md`  
  Unknowns that must not be silently treated as facts.

- `asr10-handoff-2026-06-22.md`  
  Detailed long-form handoff/archive for recovering full context after long sessions or context compaction.

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
make SOURCES=src/mame/ensoniq/asr10_boot.cpp -j1
```

## Current one-line goal

Reach stable progress beyond `LOADING SYSTEM` by reconstructing the ASR-10 control-plane behavior: MC68302 interrupt/service lifecycle, dispatcher queue events, and the hardware-side completion signals needed by the loaded runtime.
