# ASR-10 V3.50 — audio-rate mode state

## Executive summary

ASR-10 documentation independently establishes two effect-selected **system
sample-rate / polyphony** classes:

| Manual class | Exact system rate shown elsewhere in manual | Advertised polyphony |
|---|---:|---:|
| 30 kHz | 29.7619 kHz | 31 voices |
| 44 kHz | 44.1000 kHz | 23 voices |

This is not an inference from MAME arithmetic. The Musician's Manual says
that changing effect can change system sample rate; its Sample Rate page calls
the actual values `29.7619` and `44.1000 KHZ`; and the Service Manual says the
current effect algorithm determines the current system sample rate. The manuals
are therefore an independent oracle for a future A -> B -> A test: the FX
Select rate page must say `31 VOICES AT 30 KHZ` or `23 VOICES AT 44 KHZ`.

The more specific model in this investigation is **not yet verified**. The
ES5506 specification makes `ACTV=$1f` exactly 32 active slots and `$17`
exactly 24; the manuals instead call the two ASR-10 modes 31 and 23 voices.
No evidence presently equates the user-visible polyphony reserve with an
otherwise unused OTTO slot. V3.50 writes `$1f` at boot and no observed path
writes either `$17` or `$16` afterwards. Likewise, the exact arithmetic of
`Y2/2` and `Y3/2` has no ASR-10 board-routing witness.

There is therefore no sample-rate implementation in this change and no runtime
A -> B -> A trace. A prior effect-browser action only reached a file-browser
completion; the separately traced built-in effect upload fails its ESP
readback verification before it proves that a new current-effect state was
committed. A page visit is not a valid rate-state oracle.

## Evidence labels

* `[Verified source]` — local ASR-10 manual/service manual or Ensoniq chip
  data sheet.
* `[Verified firmware]` — V3.50 static code/object evidence.
* `[Verified runtime]` — a reproduced current-harness observation.
* `[Derived arithmetic]` — calculation from verified values only.
* `[Likely]`, `[OPEN]` and `[DISPROVEN]` follow
  `reference/methods-hypothesis-management.md`.

## 1. Independent documentation evidence

| Source | Claimed rate | Voice count | Context | Confidence |
|---|---:|---:|---|---|
| Musician's Manual, Effects section 5 | 30 kHz / 44.1 kHz | 31 / 23 | FX Select rate page; changing effect can change rate | `[Verified source]` |
| Musician's Manual, Sampling section | 29.7619 / 44.1000 kHz | — | actual sampling/system rate; current effect selects it | `[Verified source]` |
| Musician's Manual, Audio Tracks section | current effect rate | — | determines AudioSample recording rate; changing it changes existing AudioSample pitch | `[Verified source]` |
| Service Manual, Digital I/O Information | 30 / 44.1 kHz | 31 / 23 | current effect algorithm determines system rate; DI-10 output is enabled only at 44.1 | `[Verified source]` |
| V3.50 disk image, offset `$1bb65` | `44K` | `23VOICE` | literal `FX OFF 23VOICE 44KVAR` in OS data | `[Verified firmware data]` |

The manuals distinguish system rate from stored sample format. For example, the
Service Manual separately permits 44.1/48-kHz **digital input**; that is not
evidence that all OTTO playback runs at either input rate. The Audio Tracks
warning confirms that effect choice changes a system-wide audio timebase
relevant to stored audio, not merely an effect parameter.

The disk-image string is corroborating firmware data, not a decoded descriptor
field: its record type, owner and update path have not been established. It
must not be promoted to a named rate flag from text alone.

## 2. 31/23 documented voices versus 32/24 OTTO slots

`ES5506.pdf`, rev. 2.3 p. 6, defines ACTV as zero-based: the register value
plus one is the number of active OTTO voices. Consequently:

| State | OTTO ACTV | OTTO slots | Manual-facing number | Relation |
|---|---:|---:|---:|---|
| boot state | `$1f` | 32 | 31 voices in the normal 30-kHz class | not proven equivalent |
| proposed high state | `$17` | 24 | 23 voices in the 44-kHz class | not proven equivalent |
| literal 23-slot state | `$16` | 23 | 23 voices | no matching Y3/2 arithmetic |

The firmware's `$8000` voice-record table has 32 entries and the boot routine
at `$f8cf06-$f8d00e` writes `ACT=$1f`, `MODE=$0d` `[Verified firmware]`.
That table and the ACTV value establish hardware capacity, not the manual's
reserved/polyphony policy. The targeted static consumer search found no
allocator limit or effect-state field that can be tied to 31/23, and no
ACTV-producing path other than boot initialisation. Immediate `$17` and `$1f`
values occur in unrelated 68k data/control code, so an occurrence without a
path to the ES5506 ACTV write is deliberately not rate evidence.

Existing boot/load/select/playback traces have no later ACTV or MODE write
`[Verified runtime]`. Thus a real `ACTV $1f <-> $17` transition is currently
**not observed**, rather than disproven on physical hardware.

## 3. Effect/algorithm path and firmware representation

The V3.50 effect path is real but currently incomplete:

```text
current/built-in effect object
  -> $f973f0 record parser and ESP uploader
  -> ES5510 host-window writes ($fc3000-$fc31ff)
  -> firmware verify pass
  -> current effect installation / rate-state consumer     [OPEN]
```

`$f973f0` transfers records to the ESP host interface. The previously
live-confirmed object at `$fff9bca0` has record type 2 and 89 records; earlier
static work found header text beginning `3 1 VOIC...`. The uploader/verify pass
proves an algorithm-data route, but it has not decoded a field as rate, voice
limit, or rate-mode selector. The current model reaches an ESP readback
mismatch during that upload, so it does not prove the object's active
installation or a 30/44 state transition.

No V3.50 global variable, allocator limit, CS1 write, MC68302 GPIO output, or
ES5506 shadow register is yet identified as the consumer representation of the
documented mode.

## 4. Candidate A/B states and runtime gate

The manuals make the intended A/B oracle unambiguous:

| Candidate | Required oracle | Current result |
|---|---|---|
| A, low rate | successfully current effect reports `31 VOICES AT 30 KHZ` | boot's 31-voice object is evidenced, but its installed current-effect/rate state is not independently captured |
| B, high rate | successfully current effect reports `23 VOICES AT 44 KHZ` | no successful 44-kHz effect installation in current model |
| A again | same successful 31-voice state restored | cannot test before A/B installation is established |

The prior Effects interaction reached `FILE 16  LUSH PLATE` and then `DISK
COMMAND COMPLETED`, with no proved installed algorithm. The built-in effect
upload instead reaches `EFFECT DOWNLOAD FAILED` / `ERROR 032` at its known ESP
host/readback boundary. Neither is the required oracle. Therefore no Lua rate
tap was installed for this pass: it could only have collected unclassified
UI/upload noise, contrary to the A != B and final-A == initial-A criterion.

## 5. ACTV result

The explicit result is:

```text
ACTV $1f -> $17: not observed
ACTV $17 -> $1f: not observed
ACTV $1f -> $16: not observed
```

This is a bounded negative result for already reproduced V3.50 boot, load,
select and note-play paths. It does not show whether physical ASR-10
reconfigures OTTO during a successfully installed 44-kHz algorithm, whether
the marketing voice count leaves a slot reserved, or whether clock selection
alone performs the rate switch.

## 6. Y2/Y3 `/2` evidence

The arithmetic fits exactly:

| Candidate source | Slots | ES5506 formula result | Status |
|---|---:|---:|---|
| `Y2/2 = 15,238,090 Hz` | 32 | 29,761.895 Hz | `[Derived arithmetic]` |
| `Y3/2 = 16,934,400 Hz` | 24 | 44,100.000 Hz | `[Derived arithmetic]` |
| `Y3/2 = 16,934,400 Hz` | 23 | 46,016.304 Hz | `[Derived arithmetic]`, falsifies this combination for 44.1 |
| 16,228,800 Hz | 23 | 44,100.000 Hz | `[Derived arithmetic]`, source not identified |

There is support for the *capability* of a `/2` stage, but not for this ASR-10
routing. The board inventory contains SN74F74 flip-flops and SN74F161 counter
logic, while the local ES5701 data sheet specifies two fixed dividers,
20->10 MHz and 16->8 MHz. Neither source names Y2/Y3, an OTTO input, a mux,
or a rate-select control. The ES5505 family precedent which feeds a halved
30.47618-MHz crystal is useful context only; it is not ASR-10 board evidence.

The Service Manual's `No LRCLK input to 68302` diagnostic says that the
relevant clock reaches the MCU from the analog-board side. It supports a
board-level framing path, but identifies neither its oscillator nor its
relationship to OTTO's master-clock input.

## 7. ES5510 consistency check

ESP is a sample-period DSP with serial BCLK/WCLK/LRCLK interface pins
`[Verified source: ES5510.pdf]`. The manuals also say that changing effect
rate changes recorded-audio playback pitch and controls availability of the
DI-10's 44.1-kHz main mix. Together these facts make a system-level OTTO/ESP/
audio-frame transition **likely**: a complete implementation cannot assume
that only dry ES5506 timing changes while an active ESP remains on an unrelated
frame rate.

That is an architectural constraint, not a proven wire. ES5510 has its own
clock input and the current MAME 10-MHz disabled device is explicitly a
placeholder. No ASR-10 source proves that it receives Y2/2, Y3/2, the same
clock as OTTO, or a firmware-programmed rate bit. A pure ACTV switch is
therefore not an evidence-supported complete system model.

## 8. Current MAME mismatch and Tutorial Song boundary

Current MAME directly supplies Y2 to ES5506 and boot retains 32 active slots:

```text
30,476,180 / (16 * 32) = 59,523.789 Hz
```

That is neither documented ASR-10 system rate. The driver also routes ES5506
directly to speakers, synthesizes a PB3 LRCLK signal, and keeps ES5510 disabled;
it has no verified board clock, serial-audio or ESP framing model.

If the intended low mode is 29,761.895 Hz, the present OTTO rate is exactly
2.000000 times it: +1200 cents / one octave, with half-duration sample, loop
and envelope time. Such a mismatch **can explain** a globally octave-up result,
shortened samples, altered loop timing and envelope/frame timing. It does
**not explain** wrong instrument or sample assignment, nor establish that every
observed blip has a rate cause. Tutorial Song remains a symptom and a separate
validation problem.

## 9. Hypothesis verdict and smallest next experiment

HYPOTHESIS:

    Y2/2 + 32 slots ≈ 29.7619 kHz
    Y3/2 + 24 slots = 44.1 kHz

VERDICT:
    [PARTIALLY SUPPORTED]

VERIFIED:
    The ASR-10 documentation defines effect-selected system modes of 29.7619
    kHz/31 voices and 44.1000 kHz/23 voices. The ES5506 rate equation, boot
    ACTV=$1f, current direct-Y2 MAME rate, effect upload route, and ESP's
    sample-period/serial role are independently established.

LIKELY:
    A correct future model needs one board/system audio-frame decision shared
    coherently by OTTO, ESP and the output path, rather than an effect-name
    special case inside ES5506.

ARITHMETIC ONLY:
    Y2/2 with 32 slots produces 29,761.895 Hz; Y3/2 with 24 slots produces
    44,100 Hz. No ASR-10 `/2`, mux, selector or 24-slot witness exists.

DISPROVEN:
    The prior premise that the documented pair itself is merely an unverified
    30/44 hypothesis is false. The pair is explicitly documented. It is also
    false that 31/23 can silently be read as ES5506 ACTV 32/24 slots.

OPEN:
    The current-effect rate-state representation; a successful 44-kHz effect
    installation; ACTV $1f/$17 or $16 transition; the Y2/Y3 physical routes;
    the board clock/serial framing glue; ES5701's exact ASR-10 role; and the
    shared ES5510 timing path.

CURRENT MAME:
    Direct Y2 -> ES5506 at 59,523.789 Hz with ACTV=$1f, synthetic MCU LRCLK,
    direct speaker routing and disabled placeholder ESP. It cannot represent
    the documented mode state or its board-level audio path.

EVIDENCE-SUPPORTED TARGET:
    Firmware-selected current-effect mode -> verified board clock/frame state
    -> coherent OTTO + ESP + output timing. The physical selector and exact
    clocks remain intentionally unspecified.

NEXT MINIMAL STEP:
    Make one documented 44-kHz effect reach a *successfully committed* current
    effect state (not merely a browser page), then capture its rate-page oracle
    and the reversible 30 -> 44 -> 30 differential at only the current-effect
    state consumer, ES5506 ACTV/MODE, identified board-control candidates and
    ESP control boundary. Do not implement a rate change before that
    discriminator exists.
