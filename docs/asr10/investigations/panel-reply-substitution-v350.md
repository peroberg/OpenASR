# Panel reply substitution, V3.50

Scope: V3.50 boot to `FILE 1  TUTORIAL BNK`, channel B harness replies, no
`-log`, no `mem_map` change.

## TX sequence classification

[Verified] Channel B is a request/response link in the current harness run. The
logged reset-to-`FILE 1` conversation contains 198 THRB writes and 196 RHRB
reads. Every logged RHRB value was `$FF`.

The complete byte sequence is in
`docs/asr10/static/panel-channel-b-conversation-v350.csv`. Classified as
protocol traffic, the TX side is:

```text
000-002  e7 71 66
003-024  display text: "   ENSONIQ  ASR-10    "
025      66
026-047  display text: "    LOADING SYSTEM    "

048-054  e7 71 e7 71 71 7e fc
055-086  scan-like sequence:
         74 07  74 06  74 05  74 04  74 03  74 02  74 01  74 00
         74 0f  74 0e  74 0d  74 0c  74 0b  74 0a  74 09  74 08
087      66
088-109  display text: "TUNING KBD - HANDS OFF"
110-112  7f ff fd
113-116  e7 71 d5 66
117-134  display text: "    KEYBOARD TUNED"

135-138  78 0f 79 0f
139-170  scan-like sequence:
         74 07  74 06  74 05  74 04  74 03  74 02  74 01  74 00
         74 0f  74 0e  74 0d  74 0c  74 0b  74 0a  74 09  74 08
171-176  78 0f 79 0f 77 0e
177      66
178-197  display text: "FILE 1  TUTORIAL BNK"
```

[Verified] The recurring scan-like command is `74 nn`, where `nn` traverses
`07..00,0f..08`. It appears twice in the boot conversation. Each TX byte in
the scan-like bursts was followed by an `$FF` RHRB read after about 24 us.

[Likely] `74 nn` is panel scanning or row/column selection. The evidence is the
full repeated 16-position traversal and immediate response cadence. The exact
panel-side meaning of `nn` remains open.

[Verified] Display text is sent byte-by-byte on THRB. The final visible mode
text is transmitted as ASCII bytes after command/control byte `$66`.

## Reply substitution

Harness change: when `ASR10_PANEL_REPLY_SUBSTITUTE=1` is set, the autoresponder
can replace exactly one `$FF` response with `ASR10_PANEL_REPLY_SUBSTITUTE_VALUE`.
This pass used `ASR10_PANEL_REPLY_SUBSTITUTE_TX=0x74` and
`ASR10_PANEL_REPLY_SUBSTITUTE_OCCURRENCE=1`, i.e. the first response to the
first observed `74` command.

[Verified] The substitution point was reached at:

```text
seq=56 tx=$74 occurrence=1 pc=$F89AA4 time=0015.037,757,000
$03C0 before = $B392
$03C4 before = $0000
display_before = "q"
```

The full `$00-$FF` substitution sweep was not completed in this pass. The
one-process-per-value method reaches the substitution point only after about
15 s of emulated boot wall time, so a complete 256-value sweep is too expensive
for this turn.

Generated partial results are in
`docs/asr10/static/panel-reply-substitution-v350.csv`.

| value | result |
| --- | --- |
| `$C0` | substituted, did not reach `FILE 1` within 45 s |
| `$C1` | substituted, did not reach `FILE 1` within 45 s |
| `$C2` | substituted, did not reach `FILE 1` within 45 s |
| `$C3` | substituted, did not reach `FILE 1` within 45 s |
| `$C4` | substituted, did not reach `FILE 1` within 45 s |
| `$FF` | substituted, reached `FILE 1  TUTORIAL BNK` |
| `$00` | substituted, did not reach `FILE 1` within 45 s |
| `$7F` | substituted, did not reach `FILE 1` within 45 s |
| `$80` | substituted, did not reach `FILE 1` within 45 s |
| `$BF` | substituted, did not reach `FILE 1` within 45 s |
| `$74` | substituted, did not reach `FILE 1` within 45 s |

[Verified] `$FF` is the only tested value that preserved the boot path to
`FILE 1`. Non-`$FF` pilot substitutions changed system behavior before the file
browser state was reached.

[Verified] Substitution of the harness `$FF` reply at the FIRST TX `$74` stops
the boot for all tested values (`$00`, `$7F`, `$80`, `$BF`, `$C0-$C4`, `$74`).
Only `$FF` reaches `FILE 1`.

[Likely] `$FF` is an obligatory empty reply during the boot tuning phase.

Caveat: the substitution point was before `FILE 1`. The pilot says nothing
about panel input during runtime.

[OPEN] Whether any non-`$FF` reply produces a useful browser-visible key event.
This pass substituted the first boot scan response, not a known steady-state
file-browser poll.
