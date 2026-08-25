# Transport confirmed dynamically; A/B click investigation; $17's context-dependence resolved; single-instrument loading found

Six-part task, following manual discovery of the transport: `$1D`=Play,
`$17`=Stop/Continue, both confirmed dynamically this task. Scope: why case
A (`TUTORIAL BNK`→`TUTORIAL SEQ`→`$1D`) sounds right and case B (fresh
boot→`ATRK TUT BNK`→`$1D`) mostly clicks.

## Del 1 — transport verified; `$17`'s conflict resolved as context-dependence, not a wrong attribution

**[Verified runtime]** `$1D` starts playback: pressed after loading
`TUTORIAL BNK`+`TUTORIAL SEQ`, ES5506 register-write traffic jumps
immediately (0→66995 writes across the play window) and stays high while
waiting. `$17` genuinely stops it: **zero** new ES5506 writes during a 2s
window right after the first `$17` press (was 0 the instant before too —
a live, in-window witness, not just an installation-time check). A second
`$17` press resumes: 29605 more writes follow, and freshly-programmed
voices show new, varied START/END/ACCUM values distinct from their
pre-stop state — a genuine resume, not a fresh "create new sequence"
allocation (which would show a single new object being built, not many
voices being re-triggered with musically-varied addresses).

**`$17`'s conflict resolved: genuinely context-dependent, not a wrong
attribution.** Tested three contexts in this task:

1. **Bare idle, immediately after boot, nothing loaded**: `$17` has
   **zero effect** — display unchanged, and the guard test (`$20`
   afterward) shows the normal `"REC 5RC?INPUTDRY LEFT "`, not
   `"5T0P 5EQUENCER FIR5T  "`.
2. **Bank loaded, no sequence**: also **zero effect** — display stays
   `"BANK L0AD C0MPLETED   "`, guard test again normal.
3. **Sequence loaded, actively playing (after `$1D`)**: `$17` stops and
   resumes exactly as Stop/Continue, per the register evidence above.

The prior round's `"CREATE NEW 5EQUENCE"` finding
(`bank-loading-and-transport-context.md`) required a specific 23-button
navigation prefix (`$00`-`$16` in sequence, then `$17`) — a deep
Command-category navigation state, not idle. That finding stands; it was
not reproduced or contradicted here (not retested this task, out of the
time budget), but it is now understood as **one more context** among
several, not evidence against Stop/Continue. **Conclusion: `$17` is
reused across at least two different screen contexts** — a
"Create New Sequence" command reachable via a specific deep Command-menu
path, and Stop/Continue in the active-transport context — the same kind
of raw-code reuse this project already established for `$10`/`$11`
(Left/Right navigation vs. category paging). Neither attribution was
wrong; both are real, in different states.

**Method note, worth keeping**: five rounds of measurement-only sweeps
never found the transport because every sweep tried single-press
candidates from states where the transport was inert by construction —
no sequence loaded, so nothing to play or stop. **A button with zero
effect in every state tried can be missing a *state*, not a function.**

**Does `$1D` reach `$007C7C`? Measured, PC-correlated: not confirmed —
and the real address is different.** A PC-correlated read-tap on
`$007C7C-$007C7D` during the whole Play window in case A: 374 hits,
**zero** with `PC==$007C7C` (`matched=false` on every single one, per
§8.10 — a read-tap hit is not execution). The actual reader PC is
**`$007E24`**, 373 of 374 times, with one earlier hit from `$F87FCA`
right as `$1D` was processed. This is a real correction, not a repeat of
the prior claim: `$007C7C` is being read as data from `$007E24`
repeatedly through the whole play session; whether `$007C7C` itself ever
executes during Play is **`[OPEN]`** by this measurement, and the address
actually driving the repeated per-tick activity during playback is
`$007E24`, previously undocumented in this role.

## Del 2 — the A/B state diff: a per-slot association table, and what's missing in B

Captured full `$000000-$02FFFF` snapshots for both cases immediately
after loading, before pressing `$1D`.

**Found a per-slot record table at `$001098`, ~72-byte stride, at least
8 slots**, each ending in a slot-index byte (`$01`-`$07`+) and holding a
2-byte reference/count field. This is the clearest "association" signal
in the whole diff:

- **Case A** (`TUTORIAL BNK`): the field takes **5-6 distinct values**
  across its populated slots (`$02D2`, `$018E`, `$0028`, `$0042`,
  `$021B`, `$00A7`) — one value per slot, no repeats — and the remaining
  slots are genuinely zeroed/empty.
- **Case B** (`ATRK TUT BNK`): the same field **cycles through only 3
  distinct values** (`$00EC`, `$0020`, `$0025`) repeating across at
  least 7 populated slots — no empty slots, every slot redundantly
  reusing one of just 3 underlying references. 3 is exactly the instrument
  count the bank provides (`BLUES ORGAN`/`BLUES BASS`/`BLUES DRUMS`).

**This table is fully deterministic**, not history-dependent: byte-for-byte
identical across two independent `ATRK TUT BNK` loads in Del 4's own
control run (see below) and case B's original snapshot — the redundancy
is established directly by the bank's own on-disk data every time, not a
leftover from what loaded before it.

**Answer to "which associations exist in A that B lacks":** in A, each
populated slot carries its own distinct instrument reference and unused
slots are left empty; in B, every slot is populated, but with **redundant
references to only 3 real instruments**, never a 1:1 per-slot mapping.
Not "missing" in the sense of absent data — over-shared in a way A's
table never is.

Other checked regions: `$000D08-$001C` (note/velocity fields) identical
(zero) in both, as expected with no panel note pressed yet. `$000944-
$000B42` (the known IDMA staging/scratch buffer) shows the same
already-documented reused-counter pattern in both — not meaningful,
consistent with prior rounds' finding.

## Del 3 — voice registers: redundant sample assignment in B, not a degenerate single-voice envelope

Read CR/START/END/ACCUM (high page) and LVOL/LVRAMP/RVOL/RVRAMP/ECOUNT
(low page) for every voice via a write-tap decoder on `$FC2000-$FC207F`
(same accumulation logic as `es5506_device::write()`, read directly from
`src/devices/sound/es5506.cpp`, not modified).

**Case A's active (nonzero-volume) voices show diverse sample
assignments**: START/END spans of roughly 1100-1400 words for most
voices, with a few larger ~16000/33000-word spans — each voice mostly
pointing at a different region.

**Case B's active voices show heavy redundancy**: across ~18 active
voices, only **4 distinct START/END pairs** are in use. Nine voices
share one identical 602-word span (`$06961C-$069876`); three more share
an identical 196-word span (`$066831-$0678F5`). Neither span is
degenerate on its own (`START` is not `≈END`, and neither points outside
loaded data) — the anomaly is **the same short sample triggered on far
more voices simultaneously than there are distinct sounds**, not a
truncated/empty single-voice envelope. This is a real, measured
structural difference; whether it is *the* mechanical cause of the
audible clicking is not proven this task (see below) — it is reported as
the strongest candidate, not a closed case, per the project's own rule
that an architecture claim needs a case that breaks, and this one has a
structural anomaly but not yet a demonstrated audio failure tied to it.

**ECOUNT**: case A's active voices read `$165` (357); case B's read
`$1FF` (511, the register's 9-bit maximum) on every active voice, both
immediately after `$1D` and 3 seconds later — frozen in both snapshots
in both cases, so this alone doesn't discriminate; B sitting at the
saturated maximum in every voice, every time, is at least consistent
with (not proof of) an envelope that never advances.

**LVRAMP/RVRAMP**: varied per-voice values in both cases, no clean
distinguishing signal found in the time available — left `[OPEN]`.

**Audio (`-wavwrite`, both cases)**: an RMS-envelope click heuristic
(threshold-crossing search for loud-then-silent 5ms windows) found
**zero** matches in either case's post-`$1D` audio — the heuristic
was miscalibrated for this signal, not a negative result: B's overall
level is much quieter than A's (mean RMS 327 vs. 1199 over the same
kind of window), so an absolute-amplitude click detector is confounded
by the level difference. A relative/spectral detector was not built this
task. **The audible-click claim itself is not independently confirmed by
this task's audio analysis** — it rests on the user's own listening and
the register-level redundancy finding above, not on a measured acoustic
signature.

## Del 4 — what A establishes that B lacks: partially answered, one clean negative, one real lead, one honest deviation

**Clean negative**: the Del 2 slot table is **not** the mechanism — it
is byte-identical across two separate `ATRK TUT BNK` loads regardless of
what loaded in between (this section's own control run), so whatever "A
leaves behind" is not reflected there.

**A real, concrete lead, imperfectly controlled**: the intended sequence
(`ATRK`→measure→`TUTORIAL BNK`→measure→`ATRK`→measure) was not achieved
exactly as specified. Loading a bank that bundles a Song (`ATRK TUT BNK`)
was found to switch the file browser's `$0A` (Up) direction to
Seq/Song-type-filtered browsing afterward (`$0B`/Down continues to walk
the full raw catalog regardless — itself a real, useful navigation
finding for future work); as a result the middle load in this run landed
on a display reading `"TUT0RIAL 5NG"`, which **does not exist as a named
entry in `V350.img`'s own catalog** (checked directly — likely a
synthesized/virtual "current song" screen, not a real file), not
`TUTORIAL BNK` as intended. Reported as a deviation, not glossed over.

What the actual run (`ATRK`→[`TUTORIAL SNG`-labeled screen]→`ATRK` again)
did show: a region at `$02CA80` onward holds real, named reverb-effect
preset strings (`"HALL REVERB"`, `"JUST REVERB"`, `"MORE REVERB"`,
`"ALSO REVERB"`) after the **first**, fresh-boot `ATRK` load — and reads
as **entirely zeroed** after the **second** `ATRK` load, following the
intervening non-standard load. A concrete, reproducible difference, but
because the middle step was not the intended `TUTORIAL BNK`, it cannot
yet be tied cleanly to the task's own "B after A improves" hypothesis —
flagged as the strongest next lead for a rerun with correct navigation
(explicitly finding and using whatever button reliably returns the file
browser to Instrument/Bank-type filtering after a Song-bundling bank
load, not assumed this task).

**Not reached**: sample-RAM content comparison and a wavesample
directory/allocation table were not localized this task — `[OPEN]`,
budget ran out before this angle.

## Del 5 — individual instrument placement: found, dynamically confirmed

**Static**: `V350.img`'s own catalog (read directly, disk offset
`0x600`, 26-byte entries) lists `BLUES DRUMS` (index 12), `BLUES BASS`
(13), and `BLUES ORGAN` (14) as **standalone type-`$0003` instrument
files**, entirely separate from `ATRK TUT BNK` (index 11, type-`$001E`
bank) — the same catalog entry that supplied `"FILE 11 ATRK TUT BNK"`
and `"FILE 9  TUTORIAL SEQ"` (index 9) in earlier live navigation,
confirming FILE N is the catalog's own raw entry index, not a
per-category counter.

**Dynamic**: navigated to `"FILE 12 BLUE5 DRUM5"` via the ordinary
LOAD/INST browser (same `$0A` used for banks) and pressed `$23`. Display:
**`"PICK IN5TRUMENT BUTT0N"`** — verbatim, a real firmware prompt, not
inferred. A bank load never shows this prompt (it populates every slot
from its own predetermined internal list); a single-instrument load
requires the user to pick the destination slot explicitly via a physical
Instrument-Select button. Pressing `$02` (already known to double as the
first such confirm in the standard load flow) proceeded through
`"L0ADING BLUE5 DRUM5"` to `"FILE L0ADED"`. This fully answers the task's
question: single-instrument loading uses the identical file-browser
mechanism as bank loading, differing only in this one extra
slot-selection prompt.

**None of the 9 already-catalogued Command-mode categories**
(`command-pages-and-clock-verdict.md`) contains a "place instrument at
slot" command — the closest are `COPY INSTRUMENT` (moves an
already-resident instrument between slots, not a disk load) and
`IMPORT NON-ASR SOUNDS` (external samples, not disk banks). The real
mechanism is the ordinary file browser plus the `PICK INSTRUMENT BUTTON`
prompt, not a Command-category action.

## Summary

- **Del 1**: `$1D`=Play and `$17`=Stop/Continue dynamically confirmed via
  ES5506 register-write activity (stops to zero, resumes with fresh
  voice programming). `$17`'s "Create New Sequence" attribution stands
  for its own (deep Command-menu) context — resolved as genuine
  context-dependence, not an error. `$007C7C` is read but not
  PC-confirmed executing during Play; the real repeated-activity address
  is `$007E24`.
- **Del 2**: a per-slot association table at `$001098` shows A with
  distinct 1:1 slot-instrument references and empty unused slots; B with
  every slot populated but redundantly referencing only 3 real
  instruments. Deterministic, not history-dependent (confirmed via Del 4's
  control).
- **Del 3**: B's active voices redundantly share 4 sample regions across
  ~18 voices (9-way and 3-way duplication); A's are diverse. ECOUNT
  saturated at max in B, not independently conclusive. Audio click
  signature not confirmed by this task's own WAV analysis — the
  redundancy finding is a strong candidate mechanism, not a closed case.
- **Del 4**: the slot table is not what A leaves behind (ruled out,
  deterministic). A real lead — an effects-preset table present after a
  fresh-boot `ATRK` load, zeroed after a repeat load — surfaced despite
  an imperfectly-controlled middle step (loaded a non-catalog "TUTORIAL
  SNG" screen, not `TUTORIAL BNK`, due to a genuine post-bank-load
  navigation-context switch found this task). Sample-RAM content and any
  wavesample directory: not reached, `[OPEN]`.
- **Del 5**: single-instrument loading confirmed dynamically —
  `"PICK IN5TRUMENT BUTT0N"` — same file browser as bank loading, one
  extra slot-selection step, no Command-mode category involved.

## Rules check

No firmware variable written, read-only throughout. No model change — A/B
measurement only. No `mem_map` change, no WD33C93, no ADC, no SCC code,
no ES5510 activation. No `-log`; `-wavwrite` used for both cases'
audio capture. No forks (§11) — every experiment, snapshot, and edit in
this file was run directly by the active main agent. Display text
verbatim throughout. `$007C7C`'s read-tap result is reported per §8.10:
a hit is not execution, and it is marked `[OPEN]`, not claimed either
way. The static PC-relative tooling upgrade named in a prior round
remains worth doing but is not blocking and was not done this task, per
this round's own framing (broad usefulness, not urgent, deferred after
this task).

## Deletion accounting

No C++ changed. No Lua committed to the tree — every script this task
used (`case_a_full.lua`, `case_b_full.lua`, `del4_control.lua`,
`del5_single_inst.lua`, and the smaller `$17`-context probes) lived in
the session scratchpad, per the instrumentation-is-deleted-when-done
rule. Documentation additions: this file and a `current-status.md`
summary section.
