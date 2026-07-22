# ASR-10 Current Status

**Status snapshot date: 2026-07-22.** This is a dated snapshot of a research
harness, not a permanent architecture description. If you are reading this
long after the date above, verify claims against the source and the newer
entries in `panel-protocol.md` / `documentation-audit.md` before trusting it.

This file and `panel-protocol.md` are the only two **canonical** documents in
`docs/asr10/`. Every other file in this directory is candidate material —
historical, partially superseded, or not yet audited. See
`documentation-audit.md` for the full classification and rationale.

## 1. What this is

`src/mame/ensoniq/asr10_boot.cpp` is an instrumented boot/research harness for
the Ensoniq ASR-10, not a clean production MAME driver. It exists to discover
how the ASR-10 ROM and loaded OS talk to hardware, using a mixture of real
MAME devices, limited host-interface models, and hand-written experimental
register/protocol models.

## 2. Build and run (verified against current source)

Build:

```sh
make -j12 SUBTARGET=mess SOURCES=src/mame/ensoniq/asr10_boot.cpp
```

This produces a binary literally named `mess` at the repository root
(`SUBTARGET=mess` is an arbitrary historical label unrelated to the old
"MESS" emulator suite). Do not confuse this with any other binary at the repo
root (`./mame`, `./asr10boot`, etc.) — those are stale builds from other
invocations and do not contain this driver's current code.

Run, using the established minimal baseline flag set (verified present in
the driver as of this snapshot: `ASR10_DIAG_PANEL_AUTORESPOND`,
`ASR10_EXPERIMENT_DUART_COUNTER_TIMER`, `ASR10_EXPERIMENT_ES5506_HOST`,
`ASR10_EXPERIMENT_ES5510_HOST`, `ASR10_EXPERIMENT_PAR_DIAGNOSTIC`,
`ASR10_DIAG_PAR_VALUE`):

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_DUART_COUNTER_TIMER=1 \
ASR10_EXPERIMENT_ES5506_HOST=1 \
ASR10_EXPERIMENT_ES5510_HOST=1 \
ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1 \
ASR10_DIAG_PAR_VALUE=0x200 \
./mess asr10booth \
  -flop floppies/asr10booth/V161.img \
  -seconds_to_run 20 \
  -nowindow \
  -skip_gameinfo
```

Do not add `-log` as a matter of routine — it has produced multi-gigabyte
files in this repository's history. Use bounded, purpose-built diagnostics
instead when investigating something new.

## 3. What is currently proven

Under the configuration above, with `floppies/asr10booth/V161.img` mounted,
the firmware reaches the following observed sequence and holds there as a
live idle state (not a failure loop):

```
ENSONIQ ASR-10 → LOADING SYSTEM → TUNING KBD - HANDS OFF → KEYBOARD TUNED
→ NO INST OR BANK FILES  (correct result: V161.img carries no instrument/bank files)
```

`EFFECT DOWNLOAD FAILED` and `ERROR 032 - REBOOT ?` are both absent. This
required routing ES5510 host offset `0xE0` (fixed in commit `da1b4c385256`,
see `filesystem-browser-map.md` §4.27-4.28 for the full root cause).

**This proves the tested configuration reaches this one observed state for
this one OS-only floppy image. It does not prove every physical ASR-10
state, nor that other floppy images, SCSI, or panel input paths behave
correctly.**

The host-side front-panel transport is mapped in detail in
`panel-protocol.md` §7-§8:

- a software `TRAP #$A` (not a hardware interrupt, and not the pre-existing,
  separate Line-A mechanism) reaches a runtime-installed exception-vector-42
  handler, which flows through a flow-controlled TX-ring enqueue into DUART
  Channel B;
- a marker+payload byte encoder with three D0-selected classes is fully
  characterized (exact marker matrix known);
- the specific `7b,0b,7a,0b` post-scan sequence and the historical visible
  "Z" artifact are both explained down to the exact static source record
  involved;
- the separate Path B direct-text frame beginning with `0x66` is locally
  verified for routine `f89c94`, and the historical leading `F` artifact in
  `FNO INST OR BANK FILES` is explained;
- the bounded panel TX lineage capture proved the two observed `f89c94`
  direct-text invocations were uninterrupted logical items, and that the old
  raw `0x66...NUL` byte-stream framing failed because a later `0x66` came from
  the independent ring-drain producer;
- the current visible baseline is `NO INST OR BANK FILES`, with no leading
  `F` and no final `Z` parser artifact.

**`$c98`'s role as a selection index is a strong inference, not a proven
fact.** The external front-panel controller that consumes this protocol is
strongly inferred (Channel B leaves the host; nothing in the searched
host-side artifacts consumes it) but its firmware is not available in this
repository, so **exact physical panel semantics remain unresolved** and are
not solvable from host-side artifacts alone — see `panel-protocol.md` §7.4
for what external evidence would be needed.

## 4. Device-architecture status (do not overstate this)

| Component | Status |
|---|---|
| uPD72069 / FDC | Real MAME device (`upd72069_device`). ASR-10-specific glue, terminal-count behavior and timing remain provisional. |
| SCN2681 / DUART | **Not** a real `scn2681_device`. A hand-written partial register/shadow model inside `asr10_boot_state`, implementing only the behaviors discovered so far. |
| ES5506 / OTIS | Real MAME host device present. Surrounding ASR-10 audio/sample-memory/board-glue architecture is incomplete. |
| ES5510 / ESP | Real MAME host-interface device present. DSP execution is disabled; upload/readback is modelled, not actual DSP execution or audio processing. |
| Front-panel controller | Not emulated. Its protocol is understood host-side (see above); its physical behavior is not. |
| MC68302 internals | Hand-written partial shadow/behavior model. Not a real `mc68302_device`. |
| SCSI | Shadow/stub only. |

The correct summary sentence is: **the firmware boots through the currently
known stages using a mixture of real MAME devices, limited host-interface
models, and hand-written experimental register/protocol models** — not "all
boot stages use fully modelled real devices."

## 5. Diagnostics

The temporary panel-investigation instrumentation used to prove the PATH A
marker/payload encoder and PATH B direct-text prefix has been removed or kept
out of the parser path. The current parser architecture is:

```text
68k firmware
    |
    v
hand-written DUART shadow
    |
    v
Channel B THRB
    |
    v
temporary upstream Path B provenance bridge
    |
    v
panel_receive_byte()
    |
    v
panel protocol parser
    |
    v
visible display state
```

The temporary bridge lives before `panel_receive_byte()` and exists only to
preserve the locally verified `0x66` direct-text-prefix behavior while the
driver is still attached to the hand-written DUART/register-shadow boundary.
The parser-facing entry point accepts only the transmitted byte.

## 6. Smallest next step, if panel work resumes

Remaining work is split into independent tracks:

1. Remove the temporary Path B provenance bridge.
   The next supported direction is a raw-byte path with a transport adapter
   that preserves firmware-side submission identity. Do not treat this as a
   fully PC-independent design yet: `f89c94` remains the verified firmware
   anchor for Path B.

2. Migrate from the hand-written DUART shadow to a real MAME
   `mc68681_device`/`scn2681_device`. This remains deferred until the panel
   transport cleanup is complete and should not be a single large rewrite:
   timer/counter, IRQ6/IACK, RX/autorespond, SRB/RHRB, and TX timing all need
   independent validation.

3. Continue reverse engineering PATH A and physical panel semantics.
   Disassembling `0x3c7c` may move `$c98` from strong inference to proven
   selection-index behavior, but physical panel semantics still require
   external evidence: panel-controller firmware, service documentation, or
   direct hardware capture.
