# ASR-10 Current Blocker — Channel B Output Completion and Slot0 Continuation

**Date:** 2026-07-13, updated 2026-07-16, updated 2026-07-20, updated
2026-07-21 (see below — the effect-download blocker this whole document
chased since §4.19-equivalent sections is now RESOLVED)

---

## 2026-07-21 update — the ES5510 host adapter's missing `0xe0` route
## is implemented. The ASR-10 now completes its built-in effect download
## and reaches `NO INST OR BANK FILES` with no error message. No further
## download blocker is currently known.

Full detail: `filesystem-browser-map.md` §4.27 (root cause) and §4.28
(fix + full acceptance evidence).

**What changed:** the ES5510 host adapter now also routes `FC31C1`
(host offset `0xe0`, stock `es5510_device`'s "Write select - GPR +
INSTR") to the real device. This was the last unmapped offset the
firmware's built-in effect table actually uses (record type 1, e.g. the
live low-RAM "HALL REVERB" effect family) — it previously fell through
to plain `.ram()`, silently discarding the commit.

**Result:** `EFFECT DOWNLOAD FAILED` and `ERROR 032 - REBOOT ?` are both
now **absent** (0 occurrences each) in a ~114.5-emulated-second capture
under the full established diagnostic baseline. The panel reaches
`LOADING SYSTEM` → `TUNING KBD - HANDS OFF` → `KEYBOARD TUNED` →
`NO INST OR BANK FILES` and holds there, with the scheduler/DUART/
ES5506-host activity continuing normally (a live idle state, not a
failure loop). Both known effect objects (the ROM table at `0xfff9bca0`
and the live low-RAM `HALL REVERB` family) verify cleanly with zero
mismatches anywhere in the capture.

**Current blocker:** none currently identified for the effect-download
path. `NO INST OR BANK FILES` is the historically-established correct
result for the mounted `V161.img` (it carries no instrument/bank files
per §4.19/4.20's browser-level finding). Further work on this branch
(audio pump, ESP execution, disk browsing beyond this point, or other
subsystems) is out of scope for `asr10-es5510-host` — see
`filesystem-browser-map.md` §4.28 for the recommendation to continue
any such work on a new branch.

---

## 2026-07-20 update — ES5510 host adapter implemented; the FC300F
## record-0/58 collision this whole document's later sections chased is
## FIXED. The current blocker is a *different, unrelated* download object.
## (Superseded 2026-07-21 above — kept for history.)

Full detail: `filesystem-browser-map.md` §4.19-4.24 (read §4.24 last, it
supersedes the blocker framing below).

**What changed:** a stock `es5510_device` is now instantiated behind the
`FC3000`-`FC31FF` host window, gated by `ASR10_EXPERIMENT_ES5510_HOST=1`.
The previously-root-caused mechanism (record 0 and record 58 of the ROM
effect table at `$fff9bca0` colliding in shared, passive RAM because
nothing implemented the ES5510 host's select/commit protocol) is
confirmed fixed: the record-0/58 verify mismatch (`ASR10_FC3000_VERIFY_
HANDSHAKE`, previously `observed=f0 expected_d2=90` in every capture)
now fires zero times, and the upload loop is observed advancing past
that table into the next record type — proof the collision is
genuinely resolved, not merely masked.

**What has NOT changed:** the boot still does not complete. `EFFECT
DOWNLOAD FAILED` still appears on the panel — but this time for an
unrelated object at `a3=0x010722` (a low-memory address, not the ROM
`fff9bca0` table this fix targeted), which independently exhausts 10
retries and gives up. This exact retry sequence is present, at an
identical count, in the flag-off (ES5510 disabled) capture too — it is
pre-existing, not introduced by the fix. What is new is that `ERROR 032
- REBOOT ?` no longer follows within the 180s capture window (previously
always within ~2s); the system continues running (scheduler/DUART/trap
activity all continue normally) rather than reaching that final panel
state, at least within the time tested.

**Current blocker, precisely (resolved 2026-07-21, see
filesystem-browser-map.md §4.27):** the `0x010722`-cursor/`0x010400`-base
object is a live low-RAM "HALL REVERB" effect. Its type-1 record 0 fails
verify (`observed=0x00 expected=0x04`) because this integration's own
`FC3000`-`FC31FF` adapter map never routed ES5510 host offset `0xe0`
(CPU byte address `FC31C1`, "Write select - GPR+INSTR combined") to the
real device — only `0x00`-`0x1f`/`0x80`/`0xa0`/`0xc0` were mapped, since
those were all the original `fff9bca0` table (which never uses type 1)
required. `FC31C1` falls through to plain `.ram()`, so the type-1
upload commit silently no-ops against RAM and `gpr[0]`/`instr[0]` are
never actually written by the device. **This is a simple, proven
adapter-mapping gap, not a stock-device, firmware, or timing/deferred-
execution issue** — see §4.27 for the complete evidence chain. The
smallest next step is adding one more thin wrapper (`FC31C0`-`FC31C1` →
fixed host offset `0xe0`), mirroring the existing `0x80`/`0xa0`/`0xc0`
wrappers exactly; not yet implemented (observation-only round).

---

Read `architecture.md`, `panel-protocol.md`, and `asr10-panel-slot0-handoff-2026-07-13.md` next; the rest of this document (below) predates the ES5510 work above and describes an earlier tuning/Channel-B stall investigation.

---

## 2026-07-16 update — TUNING KBD stall: precise gap located

(Supersedes earlier drafts of this section — see git history for prior
framings. Current authoritative state below.)

**Real-hardware reference (service manual)** — the required normal sequence
after boot is `LOADING SYSTEM` → `TUNING KBD - HANDS OFF` → all Instrument
LEDs turn off → `FILE 1 TUTORIAL BNK`. The current run reaches
`TUNING KBD - HANDS OFF` and stops; it does not reach `FILE 1 TUTORIAL BNK`.
This external reference rules out "legitimate final idle" as an explanation.

**Build note**: `./mame` in this tree was a stale binary; `./mess` is the one
`make SUBTARGET=mess SOURCES=src/mame/ensoniq/asr10_boot.cpp` rebuilds.
Everything below is from a freshly-rebuilt `./mess`,
`ASR10_DIAG_PANEL_AUTORESPOND=1` only.

### Authoritative result

Using the pre-existing, genuine (non-sampled) Channel B/FDC hooks
(`PANEL_ENQUEUE`, `PANEL_THRB`, `PANEL_COMPLETE`, `PANEL_WAKE`,
`PANEL_F89ACE_CLEAR`, `ASR10_FDC_*` — all fire on real state/memory events
already wired into `asr10_boot.cpp`, not the coarse `pc_poll` sampler):

- All 19 characters of the tuning-status text (`"f    KEYBOARD TUNED"`)
  complete full Channel B transactions — enqueue → THRB → autorespond RX →
  IACK → RHRB → complete → wake → `PANEL_F89ACE_CLEAR` — one at a time, no
  partial or failed transaction (log sequence numbers 668-762).
- Immediately after the last character, `trigger_pc=f880fc` fires (the
  `f880e0-f88130` "Slot continuation and finalizer" code already documented
  above) — this **finalizes** the printing task (slot 0, dispatched at
  `rte_count=1`; the whole 19-character print happens as ordinary subroutine
  execution within that one task, caller return address `fff89a56`, a
  high-view address current tooling cannot disassemble).
- A six-slot dispatch burst follows (`rte_count=2..6`, slots 1,3,0,4,5,
  already documented under "Static disassembly").
- Afterward, using the same genuine hooks: **no new `PANEL_ENQUEUE` event
  and no new FDC event is observed** for the remainder of the run (44 real
  seconds).
- The expected `FILE 1 TUTORIAL BNK` state is not reached.
- **The exact missing continuation remains unresolved** — not which code
  should produce the next step, and not whether "queue producer" is even
  the right frame (a real finalizer, `f880fc`, did fire on schedule, so
  "no producer fires" is not an accurate description).

Three things this result does **not** conflate: characters transmitted
through Channel B (confirmed complete); the harness's `m_panel_text`
accumulation/flush behavior (confirmed: text accumulates correctly, but no
control byte ever triggers a second `flush_panel_text()`, so no separate
`ASR10PANEL text="KEYBOARD TUNED"` line appears in the harness's own log —
a fact about the harness's print/flush mechanism, not about real hardware);
and the real hardware's visible display (not addressed — the service manual
does not state `"KEYBOARD TUNED"` must be its own displayed screen, only
that the *sequence of end states* includes LEDs-off then `FILE 1`).

### ROM/disk search for "TUTORIAL"/"BNK"

Searched the boot ROM and the exact `floppies/asr10booth/V161.img`
(1,638,400 bytes) byte-for-byte for `"TUTORIAL"`, `"TUTORIAL BNK"`,
`"TUTORIAL.BNK"`, `"BNK"` alone, a high-bit-set variant, two
word-interleaved variants, lowercase, and reversed — none found in either
image, in any of these encodings. The ROM does contain a fixed-width
disk-browser category-menu table (`~0xf8ba80`: `"FACTORY SNDS"`,
`"MY SOUNDS   "`, `"FACTORY BNKS"`, `"MY BANKS    "`, `"FACTORY SEQS"`,
`"MY SEQUENCES"`), confirming the browsing UI framework exists, but no
specific filename. **No matching literal representation was found in the
searched ROM or `V161.img`; no RAM-resident match was identified by the
current tooling** — this is not an exhaustive RAM search (no complete
loaded-RAM dump was taken and searched), so a RAM-resident copy in some
other encoding is not ruled out. Whether `V161.img` even has a populated
file directory, or whether "TUTORIAL"/"BNK" is encoded some other way, is
not established.

**Instrument LED writes**: no dedicated LED-clear command or state write
has yet been identified in the captured traffic or mapped hardware state.
(Not claimed: that this is impossible to find, or that it must occur in the
window after the last Channel B character — the command may have been sent
earlier, embedded in control traffic, or use another path entirely.)

### Scheduler context (supporting background, not the next experiment)

The dispatcher suspend path (`f87f76-f87f92`) and both TRAP handlers were
read directly from ROM (not inferred): TRAP #8 (`f8812c`) only writes a
value to the active slot and returns via `rte`; TRAP #7 (`f88108`) compares
a requested value against the active slot's countdown field, conditionally
sets/clears one bit, then always falls into the same save-and-rescan path
as TRAP #8's caller would need to reach some other way. Live counts for
both traps come only from the coarse `pc_poll` timer (default: every 64 CPU
clock ticks, not every tick — short enough that either trap's ~dozen
instructions can be missed entirely), so a TRAP #7 count of zero does not
mean it never fires again; only `m_f87f96_queue_rte_count`, driven by the
genuine `set_rte_callback` hook, is reliable here, and it confirms no
further RTE-based task-switch happens after slot 5 is dispatched. These
scheduler-shape diagnostics (code dumps, TRAP #7/#8 counters, save-probe)
are gated behind `ASR10_EXPERIMENT_TUNING_STALL_TRACE` (off by default,
verified) and are supporting documentation only — the load-bearing result
above comes from the pre-existing Channel B/FDC hooks, not from these.

### 2026-07-16 addendum — filesystem/browser map (PASS 1 static + PASS 2 live)

Full address inventory: `docs/asr10/filesystem-browser-map.md`. Summary:

- Static ROM tracing found the mapped low-level FDC command engine and
  currently known callers (send/status/result primitives at
  `fb8cda`-`fb8dfc`, shared error setter `fb81ae` — not confirmed as
  every command/result/error path the FDC supports), a SEEK wrapper with
  a track cache (`fb8c6e`/lowmem `$49e.w` target, `$4ac.w` cache), a
  generic "load one FDC unit into `$40e.w`" routine (`fb846a`), a
  boot/format-sector loader+validator (`fb82a4`) that loads into a
  lowmem `$544`-based buffer, and a generic bounds-checked range reader
  (`fb895a`) keyed off a fixed lowmem descriptor at `$4fe` (size/limit
  field at `$4fe+0xe` = `$50c`). A separate UI-side accessor (`f894a4`)
  reads the same `$544` buffer as a 40-entry, 26-byte-stride table.
  Filesystem shape remains **uncertain and not established**: the
  leading hypothesis from partial tracing is a flat, category-filtered
  entry table with FAT-like clustered reads rather than a hierarchical
  directory, but this is not confirmed — see the map for the full
  uncertainty markers.
- **2026-07-16/17, live-instrumented pass, twice corrected.** Added a
  single off-by-default flag, `ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE`
  (all state in one `fsb_state` struct, code dumps table-driven), with
  genuine read/write logging on ten candidate fields and entry-proxy hooks
  for `fb82a4`, `fb846a`, `fb895a`, `fb8c6e`, `f894a4`'s canonical entry
  (`f89494`) and table-read (`f894b4`, tracked separately), and FDC
  command issue, plus `read_highview_word()` resolving the driver's
  `0xfc6900-0xffffff` `.ram()` region via a genuine CPU-space read. Full
  detail, including two rounds of correction to earlier overclaims in this
  same pass: `docs/asr10/filesystem-browser-map.md` section 4.
  - **Precise result on `f894a4`**: no execution through the canonical
    `f8948e`/`f89494` entry path was observed, and the `$0544` table-read
    path at `f894b4` was never reached — both zero, at every milestone.
    Section 4.1 enumerates every candidate entry mechanism (direct calls,
    branches, jump-table data, ~69 ROM-wide indirect `jsr (An)` sites,
    loaded pointers) and finds no static caller, but explicitly does not
    claim the routine is unreachable — a live-loaded callback pointer
    reaching it was not exhaustively ruled out.
  - **Validity predicate found, shown, and evaluated**: `$4b2.w`, set by
    `fb9332` inside an error-checked chain (`fb9300`-`fb9342`), called from
    "task1" (`fb92ce`, already named in this codebase's own earlier
    diagnostics) via two direct callers (`fb917a`'s device-enumeration
    loop; `fba828`, reached through an indirect dispatch pointer at
    `$3de.w`). Live: value `0xff` (true), stable at every milestone.
    Precise statement: **the mapped structures are populated and firmware
    validity gate `$4b2` is true and stable** — not generalized beyond
    that predicate.
  - **Boundary**: directory/cache structures are populated before tuning,
    and `$4b2` is true and stable throughout, but no execution through the
    canonical `f894a4` entry path was observed.
  - **Refuted by live data**: `fb82a4`/`fb846a` never fire (0 entries
    each); the real live loader is `fb8ab6`/`fb8a54`, inside the
    already-documented CMD46/READ DATA loop. Confirmed exactly: `$4ac`
    writer `fb8cc2`, `$4be`/`$4bf` writer `fb7b9a`, `$4b3` writer `fb8948`.
  - **Retracted** (address-resolution bug, corrected): slot 0's six
    jump-vector operands and `f894a4`'s own five internal vector calls are
    `jsr $xxxx.w` with bit 15 set, which sign-extends to `0xFFxxxx` on
    this real 68000 — an earlier pass dumped the wrong (low) address and
    reported a nonexistent `$a26e` reference to `$0544`/`$416`/`$4bf` and
    a call chain (`fba0d6`/etc.) that does not exist. Corrected: `$a26e`'s
    real target is a 4-byte `jmp $711e.w`, which loads slot 1's scheduler
    base (`$23ea`) and executes `trap #9` — the same node-promotion trap
    slot 1 itself uses from its own resume code (`$23d4`, slot 0's base).
    None of slot 0's six real targets reference `$0544`/`$04fe`; two
    converge on the scheduler's `trap #9` mechanism and one on the
    already-open "node type 89A2" question from this document's own
    section 9 — real findings, not proof of a filesystem-chain reach.
  - Gate 1 re-verified clean (`GATE1_EXIT=0`, 0 `ASR10_FDC_TC`) after every
    round of correction.
  - Still open: who calls `fb8a44`-`fb8ab6` and task1 itself; where the
    corrected tail-jump targets lead; whether any of the ~69 indirect-jump
    sites reaches `f894a4` live; why `fb82a4`/`fb846a` never fire; the
    filesystem-shape question.

### Smallest corrective experiment (not run yet)

1. A genuine hook (matching how `PANEL_ENQUEUE` is already implemented) on
   the panel-enqueue routine (`f89a72`/`f89a7a`) and on FDC command entry,
   armed for the remainder of the run — directly confirms whether anything
   ever calls either again, without relying on `pc_poll`.
2. Identify which of slots 1, 3, 4, 5 (slot 0 already identified as the
   KEYBOARD TUNED printer) is responsible for the next panel message,
   clearing the Instrument LEDs, or starting a disk-directory read, by
   checking each slot's `+0x06` callback PC against known addresses.
3. Determine whether `V161.img` has a populated file directory at all, and
   if so, its on-disk byte layout (only literal-text encodings were tried
   above).
4. Only if (1)-(3) don't resolve it: read the high-view (`0xffxxxx`)
   targets current tooling cannot disassemble (`fff89a56`; the
   `$7cc4`/`$7164`/`$bf28`/`$bf5a` targets from slot 5's loop).

Not run yet — deliberately, per this task's scope.

---

## Status line

> The clean non-aliased default run reaches the established post-load dispatcher
> idle state.
>
> The alias-induced `CA7E → FC6000 → ERROR 129` path remains retracted.
>
> The Channel B output-ring implementation is now substantially understood:
>
> - `$03BC` counts bytes still waiting in the RAM output ring;
> - `$03C5` indicates whether the Channel B transmit transaction remains active;
> - `$03BC == 0` does **not** mean the final transmitted byte has completed;
> - one additional RX/parser completion is required after the final THRB byte;
> - that final completion reaches `F89A9A` with `$03BC == 0`;
> - `F89ACE` then clears `$03C5`, marking the transmitter truly idle.
>
> This model is dynamically proven.
>
> The previous apparent failure to start the second panel ring was caused by the
> diagnostic stopping one completion too early. After the missing final
> completion is supplied through the normal Channel B RX/IRQ6/RHRB/parser path,
> `$03C5` changes `FF → 00` and the second ring starts naturally with THRB `74`.
>
> The remaining blocker is therefore:
>
> **continue the correct Channel B completion cycle through later output rings,
> then observe whether a ring completion occurs while slot0 is parked as `0202`
> with node `14F4/89A2` queued, and whether the resulting wake dispatches slot0
> and consumes or advances that node.**
>
> The authentic physical ASR-10 panel response protocol remains unknown.
> Diagnostic RX `FF` is proven only as a firmware-accepted completion stimulus.

---

## 1. Retraction: alias-induced `CA7E/FC6000/ERROR129` detour

The previous memory-corruption trace was real, but it was caused by an invalid
experimental map change.

The global alias model:

```text
00xxxx <-> FFxxxx
```

is retracted.

Clean chunk-map evidence proves that low and high views are separate loader
destinations and separate physical backings:

```text
chunk 12:
  destination: FFA200..FFC9FF, high view
  high parser / overlay code:
    FFA67E = 3078
    FFA680 = 0346
    FFA682 = 3250
    FFB22A = 1239
    FFB22C = FFFC
    FFB22E = 4813

chunk 16:
  destination: 009C00..00B5FF, low view
  different legitimate low-view data/code:
    00A67E = CA7E
    00A680 = 000E
    00B22A = 344E
    00B22C = 4E4B
    00B22E = 4EB8
```

With the global alias active, chunk 16 overwrote chunk 12. This corrupted
known-good parser/overlay code and produced the false downstream chain:

```text
high parser code corrupted
→ runtime appeared to execute CA7E operand/data bytes
→ illegal/vector/thunk path
→ FC6000 entered with inherited foreign register context
→ MOVEP wrote 00/90 into slot0 fields via A0=23D4
→ FC600C TST.L(A4), A4=FFFC5001
→ ERROR 129
```

In the clean non-aliased run:

```text
no ERROR 129
no executed FC6000 cascade
dispatcher idle returns at F87F96/F87F9A
```

Therefore:

```text
CA7E at FFA67E as a clean-run pseudo-op: retracted
FC6000/ERROR129 as current blocker: retracted
global FF/low RAM mirror: retracted
```

`FC6000..FC61FF` remains real loaded DPRAM/local/thunk content from chunk 19,
but its previous execution was an alias-induced artifact.

---

## 2. Current clean-run blocker

The clean default run reaches:

```text
LOADING SYSTEM / load progress
→ initial dispatcher activity
→ post-load panel initialization
→ dispatcher idle at F87F96/F87F9A
```

Later in the run, the familiar slot0 state appears:

```text
slot0 base             = 0023D4
slot0 state            = 0202
slot0 queue head/tail  = 14F4 / 14F4
node 14F4 +02          = 89A2
```

The node remains queued and slot0 is not dispatched.

The current blocker is no longer described simply as “`$03BC` does not reach
zero.” The correct output lifecycle has two distinct states:

```text
$03BC = bytes still waiting in the RAM output ring
$03C5 = Channel B transmit transaction active/busy
```

A panel-output transaction is fully complete only when:

```text
$03BC == 0
AND
$03C5 == 00
```

The remaining investigation is:

```text
continue later Channel B rings using the proven completion lifecycle
→ allow final completion after each ring's last THRB byte
→ observe the ring completion that occurs after slot0/node 14F4/89A2 exists
→ determine whether F89AC2 clears slot0 bit1
→ determine whether dispatcher selects slot0
→ trace whether node 14F4/89A2 is consumed, promoted, modified, or retained
```

---

## 3. Proven Channel B transport chain

Channel B is the keypad/display-controller channel.

The following transport path is dynamically proven:

```text
firmware writes THRB at FC4817
→ diagnostic Channel B RX byte is queued
→ SRB RxRDYB becomes active
→ ISR bit5 becomes active
→ IRQ6 is asserted
→ IACK returns vector 0x56
→ firmware enters the normal DUART demultiplexer
→ firmware reads RHRB at FFB242
→ received byte enters the normal parser through $03C0
```

Relevant addresses:

```text
FC4817   THRB on write / RHRB on read
FFB242   normal firmware RHRB read
FFB3BA   common initial parser state
FFB3E4   parser completion call site for RX FF
FFB424   second parser completion call site
F89A9A   output send/completion routine
F89AA4   actual Channel B ring THRB write
F89AB8   decrement of $03BC after launching a queued byte
F89ABE   zero-count continuation
F89AC2   bclr #1,$0002(A0), A0 loaded through $00D8
F89ACE   clr.b $03C5, transmitter becomes idle
```

Relevant low-memory state:

```text
$0378..$03B7  Channel B output ring
$03B8/$03BA   ring pointers
$03BC         bytes remaining in the RAM output ring
$03C0         RX parser-state pointer
$03C4         first byte / parser-state storage
$03C5         output active/busy flag
$00D8         pointer to slot0 record
```

---

## 4. Correct Channel B output lifecycle [PROVEN]

### 4.1 Enqueue and idle kick

Output bytes are enqueued through the ROM path around:

```text
F89A72 / F89A7A  write byte into ring
F89A8A            increment $03BC
F89A8E            tst.b $03C5
F89A92            beq F89A9A
```

When the transmitter is idle:

```text
$03C5 = 00
```

the first enqueue performs an explicit idle kick:

```text
enqueue first byte
→ $03BC 00 -> 01
→ F89A8E sees $03C5 == 00
→ F89A92 branches to F89A9A
→ F89AA4 writes first byte to THRB
```

This is dynamically proven for the first ring's initial byte `71`.

The initial send was observed as:

```text
PANEL_ENQUEUE byte=71
count_03bc_before=00
count_03bc_after=01
idle_03c5=00

PANEL_THRB
pc=F89AA4
byte=71
```

The first byte is therefore not started by a DUART TxRDY interrupt. It is
started explicitly by the enqueue-side `$03C5 == 0` branch.

### 4.2 Sending queued bytes

When `F89A9A` runs with `$03BC > 0`:

```text
F89A9A
→ fetch next queued byte
→ F89AA4 writes byte to THRB
→ update ring read pointer
→ F89AB8 decrements $03BC
```

Therefore `$03BC` counts bytes not yet launched from the RAM ring.

It does not count the byte currently in THRB or in flight toward the panel.

### 4.3 The off-by-one completion state

When the final queued byte is launched:

```text
$03BC 01 -> 00
```

the correct interpretation is:

```text
RAM output ring is now empty
final byte has been written to THRB
final byte is still awaiting panel completion
$03C5 remains FF
```

Thus:

```text
$03BC = 00
$03C5 = FF
```

means:

> no unsent bytes remain, but the transmit transaction is still active.

It does **not** mean that the panel-output transaction is finished.

### 4.4 Final completion and true idle

The final transmitted byte must receive one additional Channel B RX/parser
completion.

That completion calls `F89A9A` again while `$03BC` is already zero:

```text
final THRB byte
→ RX completion
→ IRQ6 / vector 56
→ RHRB at FFB242
→ parser completion path
→ F89A9A with $03BC == 00
→ F89ACE
→ clr.b $03C5
```

The result is:

```text
$03BC = 00
$03C5 = 00
```

Only then is the Channel B transmitter truly idle and ready for a new
enqueue-side kick.

This complete lifecycle is dynamically proven.

---

## 5. Diagnostic parser findings

### RX `00`

From parser state:

```text
$03C0 = B3BA
```

RX `00` follows:

```text
FFB3BA  compare with C0
FFB3C6  test bit7
FFB3CC  install parser state B24E
FFB3D2  store D1 in $03C4
```

Result:

```text
$03C0: B3BA -> B24E
$03C4: 00
```

Therefore:

```text
[RETRACTED]
A single RX 00 is an ASR-10 ACK/completion.

[STAT]
RX 00 is accepted as the first byte of a stateful incoming parser sequence.
```

### RX `FF`

From parser state `B3BA`, RX `FF` follows:

```text
FFB3BA
→ high-control branch
→ FFB3E4
→ F89A9A
```

When `$03BC > 0`, this sends the next queued byte and decrements `$03BC`.

When `$03BC == 0`, this reaches `F89ACE` and clears `$03C5`.

Repeated RX `FF` is therefore a valid diagnostic completion stimulus.

It is not proven to be the authentic physical panel response.

---

## 6. Dynamically proven diagnostic sequence

### 6.1 First two completion cycles

The following cycles are dynamically proven:

```text
THRB 71
→ RX FF
→ normal IRQ6/RHRB/parser path
→ FFB3E4
→ F89A9A/F89AB8
→ $03BC 0E -> 0D
→ next natural THRB 7E

THRB 7E
→ RX FF
→ normal IRQ6/RHRB/parser path
→ FFB3E4
→ F89A9A/F89AB8
→ $03BC 0D -> 0C
→ next natural THRB FC
```

The parser returns to:

```text
$03C0 = B3BA
```

after each completion.

### 6.2 First known ring

The first known output ring is:

```text
71 7E FC 74 07 74 06 74 05 74 04 74 03 74 02
```

Diagnostic completion probes launched and drained all queued bytes through the
normal firmware path.

The earlier diagnostic stopped when:

```text
$03BC 01 -> 00
```

and incorrectly treated that point as the complete end of the ring.

That left:

```text
$03BC = 00
$03C5 = FF
```

because the final THRB byte `02` had been launched but not yet completed.

### 6.3 Final-idle completion proof

A bounded follow-up experiment supplied exactly one further RX `FF` after the
final natural THRB byte `02` had been sent and `$03BC` had reached zero.

The dynamically proven chain is:

```text
final THRB 02 at F89AA4
→ $03BC 01 -> 00
→ one final RX FF through normal IRQ6/RHRB/parser path
→ F89A9A entered with $03BC=00
→ F89ACE reached
→ $03C5 FF -> 00
```

Key proof:

```text
final_idle_f89a9a_entry
  pc=F89A9A
  count_03bc=00
  idle_03c5=FF
  last_parser_pc=FFB3E4

final_idle_03c5_clear
  pc=F89ACE
  previous_03c5=FF
  current_03c5=00
```

This confirms the off-by-one completion model.

### 6.4 Second ring starts naturally

After `$03C5` was cleared, the second output ring began naturally:

```text
PANEL_ENQUEUE byte=74
count_03bc_before=00
count_03bc_after=01
idle_03c5=00

PANEL_THRB
pc=F89AA4
byte=74
```

Thus:

```text
[RETRACTED]
The second ring lacked a natural idle-to-active start mechanism.

[PROVEN]
The second ring failed to start only because the earlier diagnostic omitted
completion for the first ring's final transmitted byte and left $03C5=FF.

[PROVEN]
After the final completion clears $03C5, the second ring starts naturally
through the existing F89A8E/F89A92 idle-kick path.
```

---

## 7. First and second panel rings

### First ring

Observed complete first ring:

```text
71 7E FC 74 07 74 06 74 05 74 04 74 03 74 02
```

This is binary/control traffic, not ASCII display text.

### Second ring

Observed second-ring enqueue begins:

```text
74 01 74 00 74 0F 74 0E 74 0D 74 0C 74 0B ...
```

Together, the repeated pairs resemble a structured range:

```text
74 0F
74 0E
...
74 00
```

Likely categories include panel-state, LED, indicator, track, or controller
initialization, but the exact command semantics remain open.

Do not describe the sequence as a disk filename or display text.

The current evidence shows that the second ring can now begin naturally after
the first transaction receives its final completion.

---

## 8. Slot0 and node `14F4/89A2`

Known later state:

```text
slot0 base             = 0023D4
$00D8                  -> slot0
slot0 state            = 0202
slot0 queue head/tail  = 14F4 / 14F4
node 14F4 +02          = 89A2
```

Slot0 pending semantics:

```text
pending iff byte+2 != byte+3
```

Trap #9 uses bit7:

```text
direct promotion only when bit7 is set and +0C is empty
always bclr #7
slot0 state 0202 is not woken by trap #9
```

The output completion wake path uses bit1:

```asm
F89AC2: bclr #1,$0002(A0)
```

where `A0` is loaded through `$00D8`.

Important timing evidence:

```text
the first ring and second-ring enqueue begin before node 14F4/89A2 exists
node 14F4/89A2 is posted later
slot0 eventually becomes 0202
```

Therefore:

```text
node 14F4/89A2 does not initiate the second ring
```

The current open question is whether a later complete output transaction,
including its final `$03C5`-clearing completion, occurs while slot0 is already
parked as `0202`.

The decisive target remains:

```text
later ring final THRB byte
→ final RX completion
→ F89A9A with $03BC=0
→ F89ACE clears $03C5
→ F89AC2 clears slot0 bit1
→ slot0 0202 -> 0002
→ dispatcher selects slot0
→ node 14F4/89A2 is examined or consumed
```

Whether `F89AC2` occurs before or after `F89ACE` in every relevant completion
variant must be taken from the exact runtime path and not assumed from this
summary.

---

## 9. `89A2` status [OPEN]

Known:

```text
node 14F4 has +02 = 89A2
node is posted to slot0 +10/+12 through trap #9
slot0 later parks as 0202
node remains queued
```

The old high-view dispatch model is retracted:

```text
word[FF8258] = 8140
FF8140 + signed(89A2) = FF0AE2
[FF0AE2] = FFF88968 / FFF88554
```

Why it is retracted:

```text
FF8258 = 8140 is real high-view data from chunk 11
but FF0AE2 is not written in the clean run
```

The low-system-block mapping is now closed:

```text
V161 img[0x30E2]
→ loaded directly by chunk 5
→ destination low $00E2/$00E6
→ values FFF88968 / FFF88554
```

Therefore:

```text
$00E2/$00E6 are legitimate low callback initialization values
they are unrelated to node type 89A2
the old FF0AE2 dispatch interpretation is retracted
```

Current honest position:

```text
the consumer and meaning of node type 89A2 remain unknown
```

Do not force `F97F10` on this node. `F97F10` is a typed `8810` consumer and
releases nonmatching nodes. Calling it on `89A2` would destroy evidence.

**2026-07-17 addendum — a live-confirmed consumer found, partial answer.**
`docs/asr10/filesystem-browser-map.md` section 4.10: slot 0's post-tuning
resume (`ae18` → `jsr $87f2.w` → `ff87f2` → `002b14`) calls `f89170`,
which deserializes node `14f4`'s `+2` field into `A2` via `movea.w`
(sign-extending). Live-captured: `A5=0x0014f4`, `A2=0xFF89A2` — exactly
this node, exactly the documented `+02=89A2` value, now seen sign-extended
into an address register. `002b14` then compares `A2` (via `cmpa.w`,
which also sign-extends its immediate operands) against `0x6b1c`,
`0x6cf2`, and `0xd10a`→`0xFFFFd10a`; `0xFF89A2` matches none, so `jsr
(A2)` at `002b38` is not taken, and the node is instead pushed (via `trap
#4`, verified: vector 36, handler `f880a2`) onto a linked list rooted at
lowmem `$b6c.w` — a different list than `$b6a.w`'s already-documented
active-slot pointer. **This is a different mechanism than the retracted
`FF8258=8140`/`FF0AE2` model above** — it does not reopen that retraction,
it is an unrelated reader of the same field, found via the actual
executed path rather than address arithmetic. Still unknown: who writes
`89a2` into node `14f4`'s `+2` field in the first place, and whether any
node ever carries a value that actually matches one of the three
constants (which would be the first live proof `002b38` is reachable at
all, for any node).

---

## 10. Memory-model facts relevant to the blocker

Keep the distinction:

```text
68k CPU addressing semantics:
  abs.w sign-extension is real
  $8258.w -> FF8258
  $86F8.w -> FF86F8
  $A67E.w -> FFA67E

physical backing:
  00xxxx and FFxxxx are separate clean-run loader destinations
  no global FF/low mirror
```

Clean loader chunk evidence:

```text
chunk 11:
  FF8000..FFA1FF, high view
  FF8258 = 8140
  FF825A = FFFF
  FF86F8..FF86FF = zero at this checkpoint

chunk 12:
  FFA200..FFC9FF, high view
  FFA67E = 3078
  FFB22A = 1239 FFFC 4813...

chunk 16:
  009C00..00B5FF, low view
  00A67E = CA7E 000E
  00B22A = 344E 4E4B 4EB8...

chunk 19:
  FC6000..FC61FF, FC/DPRAM/local view

chunk 5:
  includes low-system-block callback initialization
  V161 img[0x30E2] -> low $00E2/$00E6
  $00E2 = FFF88968
  $00E6 = FFF88554

chunks 5 and 20:
  000944..000B43, low view
  000AE2..000AE5 = 00008B00
```

Important consequences:

```text
FF0AE2 is not written in the clean run
000AE2 is legitimate low-view loader data
$00E2/$00E6 callbacks are unrelated to node type 89A2
```

---

## 11. Channel A remains separate

Channel A service state around:

```text
$14C0
F884FC
F88554
F8857A
```

belongs to a separate serial TX/RX service, plausibly MIDI.

It is not the main Channel B panel-output mechanism.

Do not chase panel ACKs for Channel A traffic such as:

```text
A0 55 00
```

Current safe distinction:

```text
SCN2681 Channel B:
  keypad/display-controller communication
  output ring at $0378..$03B7
  THRB/RHRB at FC4817

SCN2681 Channel A:
  separate serial service
  exact external role still requires independent proof
```

---

## 12. Current environment-gated diagnostics

Current panel response diagnostics include:

```text
ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO
ASR10_DIAG_PANEL_C_PARSER_TRACE
ASR10_EXPERIMENT_PANEL_REPLY_71_FF
ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF
ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING
ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS
```

The response experiments are default-off and mutually exclusive through reset
priority.

`ASR10_DIAG_PANEL_C_PARSER_TRACE` is a trace control, not a competing response
experiment.

The latest later-rings diagnostic currently includes the bounded
final-completion probe that proved:

```text
final THRB 02
→ final RX FF
→ F89A9A with $03BC=0
→ F89ACE
→ $03C5 FF -> 00
→ second ring natural THRB 74
```

Do not promote diagnostic RX `FF` to permanent default behavior.

---

## 13. Next smallest proof steps

### Step 1 — continue the correct completion lifecycle through later rings

The next diagnostic must no longer stop at:

```text
$03BC 01 -> 00
```

For every output transaction it must include:

```text
one completion for every launched THRB byte
plus the final completion that reaches F89A9A with $03BC already zero
and clears $03C5
```

It must continue only through confirmed natural Channel B ring traffic.

For each byte report:

```text
THRB byte
THRB source PC
$03BC before/after launch
$03C5
RX completion
parser branch
F89A9A/F89AB8/F89ACE path
```

For each transaction completion report:

```text
final THRB byte
$03BC 01 -> 00
final RX completion
F89A9A entry with $03BC=00
$03C5 before/after
slot0 state
slot0 head/tail
node 14F4 fields
```

### Step 2 — correlate later transaction completion with slot0

The decisive evidence is a full transaction completion while:

```text
slot0 = 0202
head/tail = 14F4/14F4
node +02 = 89A2
```

Observe:

```text
whether F89AC2 executes
whether slot0 changes 0202 -> 0002
whether dispatcher selects slot0
restored frame PC and registers
first instructions after slot0 resume
whether node 14F4 is read, promoted, changed, released, or retained
whether new panel output is enqueued
```

### Step 3 — reconstruct the complete natural Channel B output stream

Continue logging every clean-run enqueue at:

```text
F89A72/F89A7A/F89A8A
```

For each enqueue record:

```text
caller PC
D2 byte
$03BC before/after
$03B8/$03BA pointers
$03C5
slot0 state
node 14F4 status
nearby display/string/script context if available
```

Goal:

```text
recover the natural panel command stream
separate individual output transactions
identify which transaction overlaps the parked slot0 node
```

### Step 4 — determine authentic panel behavior

The MAME `esqpanel` implementation and EPS/VFX-family behavior remain useful
references, but ASR-10 protocol identity is not proven.

The service manual proves that the keypad/display board is an active controller
with its own self-test behavior when communication with the digital board is
invalid.

The permanent implementation ultimately requires either:

```text
an authentic ASR-10-compatible panel device
```

or:

```text
a narrowly justified ASR-10 panel-controller model
```

Do not infer authentic response bytes solely from the fact that RX `FF` reaches
a completion branch.

### Step 5 — trace node `89A2` only after natural slot0 dispatch

Do not guess its consumer from static address arithmetic.

Once slot0 is naturally dispatched, trace:

```text
restored A1
A2/A5
D2-D7
restored frame PC
first 100-200 instructions
slot0 +10/+12 reads
node 14F4 field accesses
release/promotion operations
new output enqueues
```

---

## 14. Do-not list

- Do not reintroduce the global `FFxxxx <-> 00xxxx` RAM alias.
- Do not treat `CA7E` at `FFA67E` as a clean-run opcode or pseudo-op.
- Do not treat `FC6000/ERROR129` as the current clean-run blocker.
- Do not patch `FC500x` or call it proven FDC based on the alias-contaminated run.
- Do not call or force `F97F10` on node `14F4/89A2`.
- Do not infer `89A2` dispatch from the retracted `FF0AE2` model.
- Do not confuse `$03BC == 0` with complete Channel B transaction idle.
- Do not stop diagnostic completion at `$03BC 01 -> 00`.
- Do not omit the final completion required to clear `$03C5`.
- Do not treat diagnostic RX `FF` as authentic panel protocol evidence.
- Do not model panel TX on Channel A.
- Do not chase panel ACKs for Channel A `A0 55 00`.
- Do not promote env-gated behavior to default behavior.
- Do not make broad memory-map changes without clean chunk-map evidence.
- Do not accumulate further broad diagnostics without updating the handoff and
  current-blocker documentation.

---

## 15. Minimal clean-run work discipline

Use the slim/committed base when practical. Keep diagnostics as small overlays:

```text
slim base
+ one diagnostic patch
+ one baseline run
+ one experiment run
+ one written conclusion
+ stash, commit as checkpoint, or discard the diagnostic scaffold
```

Recommended stash pattern:

```bash
git stash push -u -m "diag-<topic>-YYYY-MM-DD"
```

Current preferred diagnostics:

```text
complete Channel B transaction lifecycle logger
later-ring / slot0 timing correlation
restored slot0 continuation trace
natural panel output-stream recorder
```

Avoid mixing unrelated Channel A, FDC, memory-map, panel-protocol, and scheduler
experiments in one patch.

---

## 16. Current one-screen summary

```text
Clean memory model:
  abs.w sign-extension stands
  global FF/low physical mirror is retracted
  low/high RAM views are separate loader destinations
  low $00E2/$00E6 callback mapping is closed
  $00E2/$00E6 are unrelated to node type 89A2

Clean runtime:
  no CA7E/FC6000/ERROR129 cascade
  dispatcher idle at F87F96/F87F9A

Proven Channel B model:
  $03BC = bytes still waiting in RAM ring
  $03C5 = transmit transaction active/busy

  $03BC 01 -> 00:
    final queued byte has been launched
    transaction is not yet complete
    $03C5 remains FF

  final RX/parser completion:
    F89A9A sees $03BC=00
    F89ACE clears $03C5 FF -> 00
    transmitter becomes truly idle

Proven diagnostic result:
  final THRB 02
  -> final RX FF
  -> F89A9A with $03BC=00
  -> F89ACE
  -> $03C5 FF -> 00
  -> second ring starts naturally with THRB 74

Retracted:
  second ring lacked a firmware idle kick
  $03BC reaching zero alone completes the output transaction
  first-ring diagnostic had fully drained the transaction
  FF is an authentic ASR-10 panel ACK

Current blocker:
  continue correct completion cycles through later panel transactions
  observe a completed transaction while:
    slot0 = 0202
    head/tail = 14F4/14F4
    node +02 = 89A2

  then determine:
    whether F89AC2 clears slot0 bit1
    whether dispatcher runs slot0
    whether node 14F4/89A2 is consumed or advanced

Open:
  authentic ASR-10 panel response protocol
  exact meaning of 74 xx control sequence
  exact consumer and meaning of node type 89A2
```
