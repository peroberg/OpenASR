# ASR-10 save-state audio MVP

## Scope

This change makes the supported quiescent save-state workflow usable for
GOOD/BAD audio experiments. It does not claim support for a save captured
during an active floppy transfer.

## Fixed canonical state

`es5510_device` now serializes the pipeline values that survive one DSP
instruction/frame boundary: `state`, ALU source/destination,
multiply-accumulator source/destination, and the three RAM pipeline cycles.
These are canonical execution state; they are restored verbatim rather than
derived or reset after load.

`mc68302_device::device_post_load()` now removes any physical dynamic internal
window left by the pre-load machine and reinstalls the window described by the
restored BAR state. The physical mapping bookkeeping is intentionally
runtime-only; the saved logical BAR/window fields remain authoritative.

`asr10booth` consequently advertises `MACHINE_SUPPORTS_SAVE`.

## Runtime evidence

[VERIFIED] A V3.50 idle state captured at 35 s and restored in a new process
continued to 40.016667 s with equal low-RAM and Sample-RAM hashes and equal
firmware state word `$0D04`. The Lua observation caught the CPU four bytes
apart inside its normal polling loop, so PC alone is not used as an exact
instruction-phase oracle.

[VERIFIED] With `JM DIGI SYN` selected and a real MIDI note-on, a state saved
at 25.333333 s, run for 300 ms, restored in the same process, and run for the
same 300 ms produced bit-identical 48 kHz three-channel PCM.

[VERIFIED] The same real-note test persisted to disk and restored in a fresh
MAME process. The post-restore PCM was bit-identical to the fresh-run control
after accounting for the one output frame at which `-wavwrite` starts its new
file; this is capture framing, not an emulated audio sample difference.

[VERIFIED] The persistent test was repeated after the documented ROM-HALL FX
selection (`$0CE3 = $00`). The effect-active control and restored 300 ms PCM
were again bit-identical after the same one-frame capture alignment.

## Limitation

[OPEN / unsupported] `upd765` deliberately does not serialize its live
transfer/checkpoint fields. Do not create ASR-10 save states while a floppy
transfer is active. This does not constrain the supported EPS16+/CD
GOOD/BAD-audio workflow: take the state only after loading has completed and
the machine is quiescent.
