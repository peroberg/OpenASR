# ASR-10 Current Blocker — Channel B Output Completion and Slot0 Continuation

**Date:** 2026-07-13  
Read `architecture.md`, `panel-protocol.md`, and `asr10-panel-slot0-handoff-2026-07-13.md` first.

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
