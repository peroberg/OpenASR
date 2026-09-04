# ES5510 effect upload and commit — ASR-10 V3.50

> **Superseded as current execution boundary.** This journal remains the
> provenance for host upload/readback. The later bounded functional execution
> and frame-adapter policy is in
> `es5510-upload-executable-contract-v350.md` at commit `7bfd2f3722d`.
> It must not be read as saying that ASR has no pump or no ES5510 execution;
> physical routing/HALT remains `[OPEN]`.

## 1. Executive result

The stated blocker does **not** reproduce at the current revision.  V3.50
reaches the ES5510 effect-loader host protocol, verifies its host readback,
and returns to a usable, reversibly selected current-effect state.  The
historical `EFFECT DOWNLOAD FAILED` / `ERROR 032` was caused by an incomplete
**ASR-10 address adapter**, not by an incorrect generic `es5510_device`
readback implementation.

The resolved edge is:

```text
V3.50 effect record type 1
  -> FC3001..FC3011 latch bytes
  -> FC31C1 write
  -> ASR adapter fixed-offset wrapper
  -> stock ES5510 host offset E0 (Write select: GPR + INSTR)
  -> GPR/instruction storage
  -> firmware read-select/readback verification
```

The current map includes that edge.  No C++ change is warranted in this
round, and this result does not enable ESP execution, audio routing, or a
30/44.1 kHz switch.

## 2. Failure reproduction and first divergence

### Historical failure `[Verified historical runtime]`

`filesystem-browser-map.md` §§4.27–4.28 preserves the bounded, live failure
trace and its subsequent correction.  Before the `$FC31C1` route existed, a
type-1 HALL REVERB record wrote its GPR and instruction latches and then wrote
`$FC31C1 <- $00`.  The ASR map sent `$FC3182-$FC31FF` to passive RAM, so that
write never reached the stock device's host offset `$E0`.

The first data divergence was consequently not a status timeout or a
device-generated unexpected value:

```text
upload:  FC3001..FC3011 <- 04 00 00 ff ff ff ff 90 40
attempted commit: FC31C1 <- 00       (fell into RAM in the old map)
verify reselect:  FC3101 <- ff; FC3101 <- 00; FC3141 <- 00
read:             FC3001 -> 00
firmware compare: F97574, observed 00 != D2 expected 04
```

`$F97580` is the real internal-retry increment and `$F97596` sets failure
Carry.  The old miss therefore propagated to the documented download failure.
The historical trace also distinguishes the *upload* commit from the
verify-pass `$80/$A0` reselect/recommit sequence; the latter is deliberately
a readback operation, not the original commit.

### Current reproduction `[Verified runtime]`

A temporary Lua probe retained both program-space tap objects and had a live
boot witness (`FILE 1  TUT0RIAL BNK`).  It watched only `$FC3000-$FC31FF` and
loader PCs `$F973F0-$F977FF`; it was deleted after the run.  It found:

```text
host reads/writes:              6583 / 5955
loader-window host events:      12128
F97574 compare-window reads:      822
F97574 readback mismatches:         0
firmware error byte $00C0:          00
firmware retry byte $0E8C:          00
```

The 68000’s visible PC has advanced over the compare instruction when the
memory callback runs, so the probe accepts `$F97574-$F9757A` host reads.  It
compares each returned byte with live `D2`, the firmware's expected value.
The result is a direct positive readback witness, not merely absence of the
error string.

The same run entered FX Select, chose the neighbouring Bank selection, then
returned to the original Instrument selection.  Each transition caused new
ES5510-host-window writes (4613 and 4629 respectively), while the firmware
error/retry bytes remained zero:

```text
FX Select:       "FX?IN5T    HALL RE?ERB"
Bank selection:  "FX?BANK    HALL RE?ERB"
Instrument restore: "FX?IN5T    HALL RE?ERB"
```

`?` is the existing, out-of-scope incomplete display-control decoding.  The
stable `FX?IN5T` / `FX?BANK` distinction and reversible transition are the
current-effect-state witness; this is not evidence that ESP microcode is
executing or that a 44.1-kHz algorithm was selected.

## 3. Firmware host contract

### Firmware facts `[Verified static]`

The V3.50 loader entry is `$F973F0`.  Its upload loop builds latch bytes at
`$FC3001-$FC3011`; the record-type classifier near `$F97450` chooses a select
address.  Type 1 leaves the default `$1C0` displacement, producing active
byte address `$FC31C1`; types 2–4 select the documented `$C0`/`$A0` paths as
appropriate.

For type 1 the transaction is:

| Firmware operation | ASR address | ES5510 host operation | Purpose |
|---|---:|---:|---|
| write GPR latch bytes | `$FC3001,$03,$05` | `$00-$02` | assemble 24-bit GPR latch |
| write instruction latch bytes | `$FC3007,$09,$0B,$0D,$0F,$11` | `$03-$08` | assemble 48-bit instruction latch |
| commit | `$FC31C1` | `$E0` | write selected GPR plus instruction |
| reselect for verify | `$FC3101` | `$80` | reload stored state into latches |
| readback re-commit | `$FC3141` | `$A0` | writes back the just-reloaded GPR latch |
| read latch bytes | `$FC3001...` | `$00...` | compare each byte with expected `D2` |

The loader is therefore a write-then-readback verifier.  It does not expect a
synthetic last-write echo: it expects the selected GPR/instruction storage to
have been committed, then explicitly reloaded into the host latches.

## 4. ES5510 specification and MAME implementation

The local ESP specification (`docs/ensoniq/ES5510.pdf`, Rev. 2.4) establishes
the relevant architectural contract: the ESP has an 8-bit multiplexed host
address/data interface, on-chip GPR/microprogram storage, host access to that
state, and externally synchronized sample processing.  It does not support
inventing an ASR-specific effect echo.

The current generic device implements the corresponding host-latch model
synchronously in `src/devices/cpu/es5510/es5510.cpp`:

| Firmware operation | Documented / generic role | Current MAME behavior | Match |
|---|---|---|---|
| offsets `$00-$02` | GPR host latch bytes | MSB-to-LSB 24-bit latch assembly | Yes |
| offsets `$03-$08` | instruction host latch bytes | 48-bit latch assembly | Yes |
| `$80` | read select | reloads stored GPR/instruction into host latch | Yes |
| `$A0` | GPR write select | commits GPR latch via `write_reg` | Yes |
| `$C0` | instruction write select | commits instruction latch when selected | Yes |
| `$E0` | combined GPR + instruction select | commits both at the selected index | Yes |
| host transaction completion | host-register transaction | immediate; no deferred queue/timer exists in `host_r`/`host_w` | Matches the firmware's immediate verify use |

The generic device's host behavior was also compared against the existing
Ensoniq reference integration: `esq5505.cpp` maps its ESP host window directly
to `es5510_device::host_r/host_w` with low-byte access.  Historical captured
VFX/SD evidence in `archive/vfx-es5510-comparison.md` shows the same
write/select/readback form successfully round-tripping GPR and instruction
state.  This is reference evidence for the generic device boundary, not a
reason to copy VFX board policy into ASR-10.

## 5. Device-versus-board boundary

### Root cause `[Verified]`

The historical defect was an ASR-10 mapping omission.  A single-word MAME
mapping supplies relative offset zero, so ASR's sparse selector addresses
need thin wrappers which pass the *fixed* ES5510 host offset.  The old map had
wrappers for `$80`, `$A0`, and `$C0`, but not `$E0`.

The existing correction in `src/mame/ensoniq/asr10_boot.cpp` routes:

```text
FC3100-FC3101 -> host offset 80   (read select)
FC3140-FC3141 -> host offset A0   (write select GPR)
FC3180-FC3181 -> host offset C0   (write select instruction)
FC31C0-FC31C1 -> host offset E0   (write select GPR + instruction)
```

It forwards to the stock device; it neither fakes a result nor changes generic
ES5510 semantics.  That was the minimum correct mapping fix and is already
present in the starting tree.  No additional fix was made here.

### What remains incomplete `[Verified/Open]`

The ASR machine constructs the ES5510 as a host-state device and calls
`set_disable()`.  This is sufficient for firmware upload, readback and effect
commit, but not for ESP microcode execution.  There is no ASR ES5510 pump,
external delay/work-RAM model, serial framing, clock topology, or audio route.
Those are separate architectural gaps; none participates in the successful
host readback proven above.

## 6. Effect object to committed state

For the observed HALL REVERB family, the evidential chain is:

```text
effect object / record descriptor
 -> V3.50 loader F973F0
 -> latch writes to FC3001..FC3011
 -> type-selected FC31C1 combined commit
 -> ES5510 GPR + instruction arrays
 -> F97574 byte comparisons against D2
 -> no retry / no Carry failure
 -> firmware FX current selection is usable and reversibly changes
```

This maps the loader boundary required for later rate work.  It intentionally
does not name unknown effect-object fields or claim that the sampled UI page
is the documented 44.1-kHz algorithm.  The display decoder renders the
rate-page digits incompletely in the current scope, so it is not a rate oracle.

## 7. A→B→A and rate observation

The effect selection exercised here was:

```text
Instrument HALL REVERB -> Bank HALL REVERB -> Instrument HALL REVERB
```

That is a reversible **effect current-state** witness.  It is not the required
documented 29.7619-kHz algorithm → documented 44.1-kHz algorithm → original
algorithm experiment.  No ES5506, CS1, GPIO, latch, or ES5510 execution-state
rate differential was captured in this task.

The documented 31/23 playable-polyphony relationship remains separate from
ES5506 32/24 slot interpretation.  This upload result provides no new evidence
that a reserved slot accounts for the difference.

## 8. Minimal next experiment

Use the now-working effect loader to identify two effect objects through a
non-display oracle (object metadata or a firmware current-effect/rate state),
one documented 29.7619-kHz and one documented 44.1-kHz.  Then run a bounded
A→B→A Lua capture of only the previously identified clock/control boundaries.
Do not change ES5506 clocking or ACTV before that differential establishes a
real firmware-to-board control edge.

## 9. Classification

ROOT CAUSE:
    Historical `EFFECT DOWNLOAD FAILED` was an ASR-10 glue/mapping omission:
    `$FC31C1` fell into passive RAM instead of reaching stock ES5510 host
    offset `$E0`, so type-1 combined GPR+instruction commits never occurred.

FIRMWARE EXPECTS:
    Latch writes, a type-selected combined commit, explicit `$80` read-select,
    latch-byte readback, and equality with the expected `D2` byte at the
    `$F97574` compare.  It retries only on a real mismatch.

ES5510 SPEC SAYS:
    ESP exposes host-visible program/register state through an 8-bit
    multiplexed host interface; the relevant MAME select/latch operations
    model that storage boundary.  The specification does not justify a fake
    last-write response.

MAME DID:
    Current generic host operations synchronously committed/reloaded the
    selected GPR/instruction state.  The prior ASR map failed only to route
    `$E0`; current `asr10_boot.cpp` routes it.  In the new run, 822 targeted
    readbacks all equalled `D2`, with retry `$00` and error `$00`.

FIX:
    Already present before this investigation: the ASR fixed-offset wrapper
    maps `$FC31C0-$FC31C1` to generic host offset `$E0`.  No change made in
    this round; no generic ES5510 change, fake readback, rate change, or DSP
    enablement is needed for upload/commit.

EFFECT COMMIT:
    [VERIFIED] Host readback verification passes and firmware reaches a
    reversible current-effect selection state.

30 -> 44 -> 30 RATE DIFFERENTIAL:
    [BLOCKED] Upload is no longer the blocker, but this run did not establish
    two documented rate-mode effect states with a non-display rate oracle.

31/23 vs 32/24:
    [OPEN] This upload trace neither proves nor disproves one reserved slot.

NEXT MINIMAL STEP:
    Select two documented-rate effect objects using metadata/firmware state,
    then make a narrowly scoped A→B→A board-control capture.  Do not infer a
    rate switch from the effect UI or alter audio timing first.
