# Panel frontpanel branch sweep, V3.50

Scope: V3.50 `FILE 1  TUTORIAL BNK`, runtime receive path, no `-log`, no
`mem_map` change.

## `$00-$3F, $40`

64 two-byte frames were injected from a clean FILE 1 state:

```text
first byte:  $00-$3F
second byte: $40
interbyte:   24 us
settle:      20 ms per frame
```

[Verified dynamic] No first byte in `$00-$3F` produced a display change.

[Verified dynamic] No sampled scheduler slot had `+2 != +3` after any frame.

[OPEN] Direct PC observation of `$FFB488`. The state effects are consistent
with the `$B0E0 -> $B488` branch, but the completion-PC marker did not fire in
this run.

Generated data:
`docs/asr10/static/panel-frontpanel-branch-sweep-v350-003f.csv`.

## `$80-$BF, $40`

Because the `$00-$3F` sweep produced no display change, the instructed secondary
sweep was run:

```text
first byte:  $80-$BF
second byte: $40
interbyte:   24 us
settle:      20 ms per frame
```

[Verified dynamic] No first byte in `$80-$BF` produced a display change.

[Verified dynamic] No sampled scheduler slot had `+2 != +3` after any frame.

Generated data:
`docs/asr10/static/panel-frontpanel-branch-sweep-v350-80bf.csv`.

## ES5506 effect test

Frame sent:

```text
$40 $40
```

The ES5506 host-register write tap was active for the frame and the following
50 ms.

[Verified dynamic] No ES5506 host-register writes were observed in the 50 ms
window after `$40 $40`.

[Verified dynamic] `$40 $40` left the display unchanged and sampled zero ready
scheduler slots.

[Verified] `$B4CC`-vägen gör `subi.b #$1c` på första byten. `$40 -> $24`.

[Likely] `$40-$7F` är musikklaviaturen. `$40-$7C` minus `$1C` ger 36-96,
vilket är exakt MIDI-omfånget för en 61-tangenters klaviatur C2-C7. Andra
byten är sannolikt anslagsstyrka.

[OPEN] Effektsidan bekräftade inte hypotesen i denna körning. Frånvaro av
ES5506-skrivningar kan betyda att `$40 $40` inte representerar ett aktivt
note-on i detta läge, att effekten går via en annan schemalagd väg, eller att
den behöver ytterligare tillstånd.

Generated data:
`docs/asr10/static/panel-frame-injection-v350-4040-es5506.csv`.
