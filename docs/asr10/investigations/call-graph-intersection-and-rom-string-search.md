# The call graph as a search tool; the ROM string table found and its readers still open

Two static techniques, zero emulator runs: use `call-graph-edges.csv` as a
set-intersection search tool (transport handler = panel-reachable ∩
sequencer-reaching), and search the OS artifacts directly for the strings
the manual promises, then trace their readers.

## Del 1 — the call graph as a set operation

**Method.** `call-graph-edges.csv` has 5243 edges: 3839 `direct` (resolved
via `effective_address`) plus 1404 `mirror-hypothesis` (resolved via
`logical_target`, the theorized low-RAM alias of a high-RAM operand).
Built a directed graph from both interpretations, computed forward
reachability from the verified panel classification chain (`$FFB0BC` →
the `$0003C0` indirect-dispatch pointer → `$FFB392`/`$FFB20A` → `$FFB43E`)
and backward reachability (predecessors) to Slot 3's task (`$F8F2FA`).

**Result: the intersection is empty — 0 nodes — identically with and
without the 1404 mirror-hypothesis edges, and identically whether or not
known bridge points are added.** Full accounting below; this is a strong
result, but not for the reason it might look like at first.

### Why the graph itself is nearly blind on both sides

None of the panel-chain roots (`$FFB0BC`, `$FFB392`, `$FFB20A`, `$FFB43E`)
appear as a `from_address` anywhere in the CSV — checked directly, exact
match on all four. Widening to the entire `$FFB000-$FFB800` cluster (every
address any panel-protocol document places in this family) still finds
**zero** `from_address` values in range. `$F8F2FA` itself does not appear
in the CSV **at all** — not as source, not as any kind of target (checked
against `from_address`, `effective_address`, and `logical_target` across
all 5243 rows).

This is not "no path was found by searching" — it is that the graph's own
construction method (methods-static-analysis.md §1: domain-filtered
counting of `4EB9`/`4EF9`/`4EB8`/`4EF8` absolute jsr/jmp operands) cannot
see either side of this question, for two distinct, already-documented
reasons:

1. **The panel cluster lives entirely in `$FFB0xx-$FFB7xx`, and its own
   internal control flow uses only `bsr`/`bra` (PC-relative) and `jmp
   (A0)` (register-indirect)** — confirmed by reading the disassembly
   already on record: `$FFB0BC`'s body (`memory-map.md` §4) is
   `move.b`/`and.b`/`beq`/`bsr $ffb092`/`bra $ffb2d6`, and `$FFB0D4`
   itself ends in `movea.w ($03c0).w,A0` / `jmp (A0)` — the exact
   `$0003C0` indirection the task names. `$FFB43E`'s body
   (`panel-completion-consumer-v350.md`) is `trap #3`/`jsr
   $F87FD2`/`trap #4`/`jsr $B6C4` — one real absolute jsr (`$F87FD2`,
   landing in ROM) and everything else either a trap or a call to
   another `$FFBxxx` address. None of `bsr`, `bra`, `jmp (An)`, or `trap
   #n` is one of the four opcodes the graph's own construction method
   scans for (§1). This is the same blind spot §7 already names for
   `(d,An)` addressing, now confirmed to extend to relative branches and
   register-indirect jumps too — not a new mechanism, but a new
   *instance* of the documented one, worth naming because it explains a
   concrete zero-result rather than a hypothetical one.

2. **`$F8F2FA` is dispatched via `TRAP #9`'s slot-pending install
   (`$F88138`), not via any absolute jsr/jmp** — already established in
   `trap-c-and-the-real-note-path.md`/`slot5-connects-notes-to-voice-programming.md`.
   A trap dispatch has no static call-site operand naming the handler at
   all (the CPU vector table supplies it); there is nothing for a
   jsr/jmp-operand scan to find, by construction.

**Bridging with already-documented (not newly reverse-engineered) data**
narrows this further without changing the answer. `$FFB43E`'s one real
absolute jsr (`$F87FD2`) and its two trap targets (`TRAP #3` → `$F88078`,
`TRAP #4` → `$F880A2`, both confirmed via the CSV's own `vector`-mechanism
rows, which do cover the exception vector table) are legitimate graph
entry points. `$FFB4CC`'s two more trap targets (`TRAP #2` → `$F88066`,
`TRAP #9` → `$F88138`) are the same family. All five
(`$F87FD2`/`$F88078`/`$F880A2`/`$F88066`/`$F88138`) were checked for their
*own* outgoing edges: **zero, all five, in either mapping_basis.** They
are leaves. Forward reachability terminates at 9 nodes total (4 roots + 5
bridges); none is `$F8F2FA` or a predecessor of it.

```
DIRECT-ONLY (3839 edges):
  forward (roots only):     4 nodes  {$FFB0BC $FFB20A $FFB392 $FFB43E}
  forward (+ known bridges): 9 nodes {+ $F87FD2 $F88066 $F88078 $F880A2 $F88138}
  backward (from $F8F2FA):   1 node  {$F8F2FA}
  intersection:              0

DIRECT+MIRROR (5243 edges):
  forward (roots only):     4 nodes  (identical)
  forward (+ known bridges): 9 nodes (identical)
  backward (from $F8F2FA):   1 node  (identical)
  intersection:              0
```

The 1404 mirror-hypothesis edges make no difference to either reachable
set in this search — they extend coverage elsewhere in the graph, not
along any path touching the panel cluster or `$F8F2FA`.

**What this does and doesn't say.** It does not say a transport handler
doesn't exist, or that the panel can't reach the sequencer. It says: *if*
such a path exists, it is not visible to absolute-jsr/jmp graph search,
because both ends of the question sit in address territory (and use
addressing modes) this technique cannot see. This is one of the cases
methods-static-analysis.md §7 already warns about: "inga referenser
hittade" means "inga absoluta referenser hittade," never "används inte."
A live, PC-correlated tap on the panel cluster during an actual transport
attempt remains the only technique that could settle this — out of scope
here (no emulator runs this task).

## Del 2 — the strings, found; their readers, structurally open

**Method.** Reconstructed `asr10.bin` (interleaved hi-then-lo from
`roms/asr10booth/asr-65e0-hi-1.5b.bin` / `asr-648c-lo-1.5b.bin`) and
verified its SHA-256 against `DOCUMENTATION-MANIFEST.md`'s recorded hash
(`fe290ea4e5...`) — exact match, so this is the same artifact prior
rounds worked from, not a fresh guess at the interleave order. Searched it
plus both floppy images (`V350.img`, `V161.img`) for literal ASCII.

**Locations, all in the 256KB ROM image, all plain ASCII, null-terminated:**

| String / fragment | ROM address |
|---|---|
| `GPR MONITOR` | `$F8101C` |
| `INSTRUCTION MONITOR` | `$F81028` |
| `SOFTWARE INFORMATION` | `$F813E7` |
| `ESP TESTS` | `$F814F4` |
| `A/D TO D/A` | `$F81046` |
| `DC OFFSET` | `$F81051` |
| `MIDI LOOP` | `$F8105B` |
| `KEYBOARD` (fragment) | `$F81F0D` |
| `EXAMINE ` (fragment, trailing space) | `$F81F4F` |
| `" BARS - KEEP TRACK?"` (fragment, leading space) | `$F81C70` |
| `TEMPO` | `$F802EB` |
| `CLICK` | `$F802F7` |

None of these are in either floppy OS image — the diagnostic/menu/transport
text lives entirely in the boot ROM, independent of which OS version is
loaded. This matches the diagnostic menu being reachable in principle
without a floppy at all (untested this task — out of scope, no emulator
runs).

**The manual's `XXX BARS - KEEP TRACK?` is real and located precisely**:
the fragment is `" BARS - KEEP TRACK?\0"` at `$F81C70`, with a leading
space consistent with being appended after a runtime-rendered number —
matching the manual's `XXX` placeholder exactly. `TEMPO` and `CLICK` are
real, standalone fragments (`$F802EB`/`$F802F7`), sitting in the same
small table as `CLOCK`, `COUNTOFF`, `BAR` and other Seq•Song-page words.
No literal `CLICK=` — the `=` is not part of this fragment; whether it is
drawn separately by the display formatter was not traced (see below).

**Structural finding: this is a fragment table, not a table of complete
menu lines.** Dumping the regions around each hit shows short, reusable,
null-terminated words and word-groups packed contiguously — `KEYBOARD`,
`COMMAND`, `EXAMINE `, `CREATE `, `CANCEL`, `ENTER`, `DIRECTORY`, etc. all
appear as **separate, independently-terminated fragments**, not as parts
of one long literal string. `CALIBRATE KEYBOARD`'s two halves are not
adjacent in ROM (`KEYBOARD` at `$F81F0D`; no `CALIBRATE` found anywhere in
either image, plain-ASCII or digit-substituted) — the full menu line is
assembled at display time from separately-stored pieces, not stored whole.
This also resolves an apparent inconsistency: `GPR MONITOR` is stored with
a real `O`; the live display renders it `GPR M0NIT0R`. The `O`→`0`/`S`→`5`
substitution is therefore a **display/transcription-layer effect** (the
VFD's own font, or this project's decoder), not a ROM encoding choice —
ROM strings use real ASCII letters throughout every location checked.

**Who reads them: structurally open, not merely unsearched.** Searched
both ROM and both floppy images for any absolute 32-bit reference (the
same domain-filtered technique as the call graph, methods-static-analysis.md
§1) to each of the 12 addresses above, individually — **zero hits, on
every one, in all three images.** No pointer table containing any of
these addresses exists anywhere in the scanned corpus either (same
search, same result). Given the fragment-table structure just established,
this is the expected outcome, not a surprise: a compact fragment table is
normally read by an **index-based walker** — a generic "print fragment
#N" routine that receives a small integer and counts N null terminators
from a table base, rather than by code carrying each fragment's literal
address. That indexing mechanism is invisible to absolute-reference
scanning by construction (the caller's own instruction holds a small
integer immediate, not an address), the same blind-spot family as Del 1's
panel cluster. Finding the actual walker routine and each fragment's
ordinal would need either live disassembly around the table or a runtime
tap — both out of scope for a no-emulator-runs task.

**Cross-reference to `$00A304` (prior round, not reopened here):**
`$00A304`'s bounds check compares against the 16-bit immediate `$101C`.
`$8258` MONITOR's real ROM address is `$00F8101C` — its **low word is
exactly `$101C`**. This sharpens, but does not resolve, the "coincidental
address" caution that investigation already raised: the bounds-check
constant is not an arbitrary number that happens to equal something —
it is bit-for-bit the low 16 bits of this exact string's real address.
Whether that is deliberate (a genuine index/offset scheme keyed off the
low word) or coincidental (a 16-bit comparison that would collide with
any ROM address ending in `$101C`) is not resolved by this observation
alone and is not re-litigated here — `$00A304`'s own investigation is
closed for this round; this is filed as a precise cross-reference only.

## Del 3 — prefetch audit (§8.10)

Redone directly by the main agent this task (the previous attempt at this
audit ran inside the reverted fork commit and was rolled back with it).
Audited every positive execution claim project-wide for documented
PC-correlation.

**Downgraded to `[OPEN, prefetch-osäkert]`, both edited directly into
`current-status.md`:**

- `$00E66E`'s `~83Hz` clock-division-stage figure and `$0073A8`'s
  positive-execution claim (`tempo-clock-consumer-chain.md`) — zero
  `PC`/`CURPC` references anywhere in that file, checked directly.
- Type `$0E`'s dispatch-table `jmp (a0)` landing near `$006014` (`~144Hz`,
  `execution-traced-clock-and-sequencer-stepper.md`) — no PC value shown
  for that specific tap, unlike this same file's other 144Hz/1000Hz
  claims (`$00F902D8`, `$F88366`), which do show explicit PCs. The
  sibling claim — `$F8F2FA` executing at 144Hz — stays `[Verified]`: it
  is trap-based (register capture on `TRAP #9`/`#C`), inherently
  execution-confirmed and not exposed to prefetch false positives, since
  a TRAP exception only fires if the TRAP instruction itself executes.

**Not touched, per the rule:** `$F8C588` and `$00A304`'s existing
zero-result findings — both live-witnessed negatives, not positive
execution claims. Prefetch produces false positives only, never false
negatives; a zero-result tap is unaffected by this class of error by
construction.

## What Del 1 and Del 2 could not answer

- **Del 1** cannot say whether a static jsr/jmp/vector path exists between
  the panel and the sequencer, because the graph's construction method
  cannot see the panel cluster's own addressing modes (`bsr`/`bra`/`jmp
  (An)`) or the scheduler's trap-based dispatch. This is a coverage gap in
  the technique, not a measured absence. A live, PC-correlated tap during
  an actual transport-button attempt is still the only method that could
  answer it.
- **Del 1** could not test "reaches the sequencer state" via `$000B70`/
  `$000B6E` at all — both are confirmed pure data addresses (tempo/step
  values), never a jsr/jmp/vector target anywhere in the CSV (checked:
  zero occurrences as any field). A call graph cannot express "reaches"
  for a data address; only `$F8F2FA` (a real code address) was a valid
  sink for this technique. The "sekvensobjektet" target named in the task
  has no established address in the currently-trusted tree (its earlier
  discovery was part of the commit reverted this round) and was excluded
  from the search rather than guessed.
- **Del 2** located every string/fragment asked for, but could not name
  the code that reads and displays any of them, or the caller/condition
  above that — the indexed fragment-table-walker mechanism is structurally
  invisible to the same static techniques that found the strings
  themselves. No code or variable is named here without measured use, per
  the project's own rule; none of this section names one.

## Deletion accounting

No C++ changed. No repository Lua added (the reconstruction/search scripts
used this task lived in the session scratchpad, per the
instrumentation-is-deleted-when-done rule — nothing exploratory was added
to the tree, so nothing to delete from it). Documentation additions: this
file, two `current-status.md` `[OPEN, prefetch-osäkert]` blocks, and a
`DOCUMENTATION-MANIFEST.md` update.
