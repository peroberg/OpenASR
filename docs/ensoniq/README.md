# Ensoniq source index

This directory contains local source specifications for Ensoniq audio silicon.
Treat these files as chip-level evidence only: they describe what each device can
do in general, not how a specific ASR-10 board wires the devices.

| file | role in current ASR-10 work |
|---|---|
| `ES5506.pdf` | ES5506/OTTO wavetable/sample playback synthesizer: host registers, voice state, sound-memory interface, PAR, PAGE, IRQV/IRQB. |
| `ES5510.pdf` | ES5510/ESP digital signal processor: host interface, GPR and microinstruction memory, external RAM/I/O interface, execution and halt model. |
| `ES5510-programming.pdf` | Additional ESP programming notes; not the primary source for the current architecture checkpoint. |
| `ES5511.pdf` | Related ESP-family reference; not used as direct ASR-10 evidence in the current checkpoint. |
| `ES5701.pdf` | ES5701/Super-GLU audio and sound-memory glue: 68000-to-ESP, 68000-to-OTIS, OTIS-to-static-memory interface and clock generation. |
| `EPS.pdf` | Product/service source material for EPS-family comparison; not direct ASR-10 board wiring evidence. |
| `EPS-ES5700.pdf` | EPS/ES5700-family source material; useful only as comparative context unless separately tied to ASR-10. |

Evidence rule:

- A chip specification supports `[Verified silicon spec]`.
- ASR-10 ROM/OS code supports `[Verified firmware]`.
- A reproduced MAME run supports `[Verified runtime]`.
- Physical ASR-10 board routing remains `[OPEN]` unless a board-level source
  identifies the wiring.
