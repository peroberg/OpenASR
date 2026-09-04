# Panel completion consumer check, V3.50

Scope: V3.50 `FILE 1  TUTORIAL BNK`, runtime receive path, no `-log`, no
`mem_map` change.

## Completion paths to return

### `$FFB43E`

`$FFB43E` does not just store a byte and return. It:

```text
$FFB43E  move.b $0303.w,$0302.w
         save D0 byte, call $FFB41E
         D3.low <- D2
         trap #3
         store D1/D3 into ($4,A5)/($6,A5)
         jsr $F87FD2
         reload D1/D3
         trap #4
         D2 <- D3.low
         jsr $B6C4
         conditionally branch through $B56E, otherwise rts at $FFB486
```

If it branches through `$B56E`, it uses `trap #2`, tests/clears bit 15 of `D1`,
may call `$B596`, may call `trap #4`, or calls `$B55A` and `trap #d`.

### `$FFB488`

`$FFB488` has the same trap/service shape as `$B43E`, but calls `$B73C` and can
set `A1=#2` before branching through `$B56A/$B56E`. It returns directly at
`$FFB4CA` only when the post-service condition says there is nothing to post.

### `$FFB4CC`

`$FFB4CC` calls `$B770`, computes/selects a bit in `D3`, and can:

- call `trap #2`
- call `$B55A`, which writes a six-byte record relative to `A5 + 2`
- call `trap #9` with `A1=#2438`
- call `$B7A0`
- branch through `$B56E`, which can post via `trap #2`/`trap #d`
- return directly at `$FFB558`

`$B55A` is the concrete local record writer:

```asm
ffb55a  movea.w A5,A0
ffb55c  addq.w  #2,A0
ffb55e  move.w  A1,(A0)+
ffb560  clr.b   (A0)+
ffb562  move.b  D2,(A0)+
ffb564  move.b  D1,(A0)+
ffb566  move.b  D3,(A0)
ffb568  rts
```

[Verified static] Completion paths can post work through traps and A5-relative
records. They do not merely store `$03C4` and return.

[OPEN] Which exact trap/record path is active for the injected no-effect frames.
The sampled scheduler slots remained not-ready in the dynamic runs.

## `$03C0-$03CF` FILE 1 block trace

20 s in `FILE 1`, no injection:

```text
total accesses: 475
reads:          439
writes:          36
```

Access distribution:

| PC | direction | address | count |
| --- | --- | --- | --- |
| `$F97662` | read | `$03C8` | 380 |
| `$F89AC8` | read | `$03C4` | 12 |
| `$F89AC8` | write | `$03C4` | 12 |
| `$FFB0DA` | read | `$03C0` | 12 |
| `$FFB3C0` | read | `$03C0` | 12 |
| `$FFB3C0` | write | `$03C2` | 12 |
| `$F89ACE` | read | `$03C4` | 12 |
| `$F89ACE` | write | `$03C4` | 12 |
| `$F89A8E` | read | `$03C4` | 11 |

[Verified dynamic] `$03C4` is read in FILE 1, but the observed readers are
`$F89A8E`, `$F89AC8`, and `$F89ACE` during the immediate channel-B
status/autoresponse path. This does not yet identify a downstream consumer for
decoded panel events.

[Verified dynamic] `$03C8` is polled from `$F97662` in FILE 1. It is the only
periodic block read seen by this method.

[OPEN] Whether `$03C8` is the real runtime signal/flag for decoded input. This
pass identified the periodic reader but did not follow `$F97662`.

Generated data:
`docs/asr10/static/panel-03c0-block-trace-v350-file1.csv`.

## `$40 $40` block diff

Immediately before `$40 $40`:

```text
$03C0-$03CF:
b3 92 b3 92 00 00 00 00 00 00 00 00 00 00 00 00
```

After the frame and 50 ms settle:

```text
$03C0-$03CF:
b3 92 b3 92 24 00 00 00 00 00 00 00 00 00 00 00
```

Byte diff:

```text
$03C4: $00 -> $24
```

[Verified dynamic] No adjacent flag byte, counter, head/tail pointer, or state
inside `$03C0-$03CF` changed for `$40 $40`. Only `$03C4` changed.

[Verified] 192 frames over branches `$00-$3F`, `$40-$7F`, and `$80-$BF` gave
zero display changes, zero ready scheduler slots, and zero ES5506 writes.

[Verified] Frames are decoded: `$03C4` went `$4000 -> $2400` via
`subi.b #$1c`.

[OPEN] What consumes the decoded value.

Caveat: ES5506 silence after `$40 $40` is not evidence against the keyboard
hypothesis. No branch produced downstream effect.

Generated data:
`docs/asr10/static/panel-frame-blockdiff-v350-4040.csv`.
