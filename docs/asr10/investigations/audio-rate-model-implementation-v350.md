# ASR-10 V3.50: rejected ACTV effective-clock implementation experiment

> **Historical rejected policy.** This ACTV-only `set_unscaled_clock()` trial
> remains `[DISPROVEN]`. It is not the current model: the later mode-driven
> `[Likely functional]` policy is documented in
> `audio-rate-y2-y3-synthetic-policy-v350.md` and summarised in
> `../HANDOFF.md`.

## Scope and decision

This was the first bounded implementation experiment following the verified
effect A -> B -> A ACTV transition.  It tested whether the current observed
chip-facing ACTV write can safely serve as the ASR board's effective-clock
proxy.  The experiment was **reverted**: no C++ behavior change remains.

The rejection is not a claim about physical Y2/Y3 routing, ES5701, or a
physical divider.  It is a current-emulator behavioral result: the proposed
proxy changes an independently established note-audio result by an unexpected
amount, so it is not yet a sufficiently narrow model boundary.

## Candidate boundary examined

`es5506_device::write()` calls `m_stream->update()` before it applies ACTV.
`device_t::set_unscaled_clock()` then invokes ES5506's
`device_clock_changed()`, which recomputes the generic stream rate and calls
`m_stream->set_sample_rate()`.  Thus MAME supports a runtime clock change and
the generic device owns the actual ACTV/stream semantics.

The temporary ASR-only wrapper therefore did exactly this after forwarding
the verified ACTV host write (`$FC205F`, device host offset `$2f`) to the
generic ES5506:

| ACTV | temporary ASR effective clock | generic resulting rate |
|---:|---:|---:|
| `$1F` | 15,238,090 Hz | 29,761.895 Hz |
| `$17` | 16,934,400 Hz | 44,100 Hz |

Those effective clocks are arithmetically consistent with Y2/2 and Y3/2, but
the implementation deliberately did not name or model a physical mux,
divider, or ES5701.  The policy read no firmware RAM; it reacted only to
ACTV.  It also left PB3 unchanged.

## Failure witness

The normal V3.50 `note_audio` path still booted, loaded, selected the
instrument, received MIDI, and programmed OTTO:

```text
NOTE_AUDIO_STRUCTURAL rhra=3 voice_writes=2400 samram_writes=524290
```

The independent `-wavwrite` frequency check then changed from the accepted
baseline

```text
peak=3847  freq=262.3 Hz
```

to

```text
peak=3234  freq=196.7 Hz
```

`196.7 / 262.3 = 0.750`.  This is neither the proposed low-mode 1/2 relation
nor a documented ASR system-rate relation.  The device did not crash and the
audio path remained non-silent, but the changed pitch is an unexplained
side-effect outside the two-mode contract.  The full regression consequently
failed only `note_audio_wav` (expected 230--290 Hz).

Per the implementation gate, the test bound was **not** loosened and no
compensating FC, ACTV, PB3, serial, or oscillator hack was attempted.  Tutorial
Song was not run: the targeted, known-note acceptance had already falsified
the candidate boundary, and Tutorial cannot be used to compensate for it.

## Reversion and status

The temporary wrapper, effective-clock constants, and Lua clock probe were
removed.  Rebuilding the original direct-Y2 configuration restored:

```text
PASS note_audio_wav peak=3847 freq=262.3Hz
PASS regression
```

The full suite again has 16 test controls and 17 PASS lines, exit 0.

## Verdict

```text
IMPLEMENTATION BOUNDARY:
    ACTV post-write ASR board wrapper

API SAFETY:
    MAME supports the runtime clock/stream-rate transition

SEMANTIC SUFFICIENCY:
    [DISPROVEN for this model]

WHY:
    The wrapper changes the known-note result by x0.750, not by a
    documented or predicted relation.

PHYSICAL CLOCK ROUTING:
    [OPEN]

ES5701:
    [OPEN]

PB3:
    unchanged; still a separate open board/frame question
```

## Next minimal experiment

Do not retry the clock policy.  Compare the firmware's programmed voice FC
and related voice state for the same known note in successfully committed
mode-0 and mode-1 effects.  That discriminates whether firmware already
compensates a system-rate change in voice programming, before any future
effective-clock model is reconsidered.
