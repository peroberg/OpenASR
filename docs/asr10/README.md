# ASR-10 MAME boot/emulation notes

This directory contains the working knowledge for bringing the Ensoniq ASR-10 up in MAME.

The current working target is not yet a clean production driver. It is an instrumented boot harness used to discover how the ASR-10 ROM talks to hardware.

## Files

- `status.md`  
  Current truth: branch, target, build commands, known blockers, current next step.

- `roadmap.md`  
  Project phases from boot harness to usable ASR-10 emulation.

- `running.md`  
  Build and run commands, log filters, regression commands, media paths.

- `hardware-map.md`  
  Known and suspected chips: MC68302, uPD72069 FDC, ES5506, ES5510, SCSI, panel/frontpanel MCU, Super-GLU.

- `boot-flow.md`  
  ROM boot path, prompt decisions, lowmem status fields, known PCs.

- `fdc.md`  
  Floppy/FDC findings, command traces, ASR disk geometry, current image-format problem.

- `panel-input-display.md`  
  Display text path, panel/input status path, `$FC4800-$FC481F` candidate map.

- `memory-map.md`  
  Known/suspected ASR-10 address map and relation to ROM/RAM/sample-RAM/MMIO.

- `experiments.md`  
  Every intentional stub/path-opener and what it proved or did not prove.

- `vfx-reuse.md`  
  Which parts of VFX/TS/SD MAME work are likely reusable: ES5506, ES5510, pump, audio path.

- `open-questions.md`  
  Unknowns that must not be silently treated as facts.


---
