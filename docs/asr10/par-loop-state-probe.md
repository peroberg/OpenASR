# PAR loop state probe

2026-08-03. Read: `duart-opr-dynamic.md`, `par-is-an-adc.md`,
`loop840-and-divisor-source.md`, `CLAUDE.md`.

Temporary instrumentation only. The probe was added around the existing
ES5506 host read tap, gated by `ASR10_DIAG_PAR_STATE_PROBE`, and removed
after measurement. No `read_port_cb` was bound, no PAR value was injected,
and source was restored before this note was committed.

Run:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_EXPERIMENT_ES5506_HOST=1 \
  ASR10_DIAG_PAR_STATE_PROBE=1 SDL_VIDEODRIVER=dummy \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

The run reached `ERROR 130 - REBOOT ?`. Access oracles stayed clean:
`ASR10_CS3_ACCESS_SUMMARY ... unknown=0` and
`ASR10_MC68302_ACCESS_SUMMARY ... unknown=0`.

## `[Verified]` `$006868` PAR loop state

The probe logged exactly 16 relevant PAR data bytes: `$FC206D/$FC206F`
for each of the eight `MOVEP.L ($68,A0),D2` reads.

Constant for all 16 logged bytes:

| Field | Value | Last writer |
|---|---:|---:|
| Internal DUART OPR | `$D1` | `$F8E1F4` |
| Physical DUART output | `$2E` | derived from OPR |
| `$0170` OPR table index | `$00` | unchanged |
| MC68302 PADAT | `$1868` | `$F8E1FA` |
| PAR byte at `$FC206D` | `$00` | ES5506 readback |
| PAR byte at `$FC206F` | `$00` | ES5506 readback |

PBDAT readback alternated between `$0017` and `$001F`; the last write PC
remained `$0067EC` throughout. The only changing bit is bit 3. PBDAT
bits 2:0 stayed `$7` for every PAR byte.

Per iteration:

| Iteration | `$FC206D` | `$FC206F` | OPR/out | PADAT | PBDAT |
|---:|---:|---:|---|---:|---:|
| 1 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$0017` |
| 2 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$0017` |
| 3 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$0017` |
| 4 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$001F` |
| 5 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$0017` |
| 6 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$001F` |
| 7 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$0017` |
| 8 | `$00` | `$00` | `$D1` / `$2E` | `$1868` | `$001F` |

## `[Verified]` PAR consumers in this run

Searching the run log for `$FC2069/$FC206B/$FC206D/$FC206F` found only the
eight `pc=$FC60B0` PAR reads from the `$006868` loop. No second PAR
consumer appeared in this V350 path to `ERROR 130`.

## Conclusion

`[Verified]` In the observed V350 path, `$006868` uses a fixed selected
PAR input. There is no dynamic OPR change, no `$0170` index change, no
PADAT change, and no PBDAT bits 2:0 change between the eight reads.

`[Likely]` The PBDAT `$0017/$001F` alternation is unrelated to channel
selection because it is only bit 3, while bits 2:0 remain `$7`.

`[Open]` The physical identity of the fixed selected PAR input is still
unknown. This measurement does not justify an ADC model or any permanent
constant.

## Cleanup

Temporary C++ added and removed:

* `ASR10_DIAG_PAR_STATE_PROBE` gate and ES5506 read-tap log block.
* temporary DUART `outport_cb()` mirror.
* temporary MC68302 debug readback / last-write-PC accessors.

No obvious dead probe block was removed separately; the only clear
cleanup in this pass was the temporary probe itself.
