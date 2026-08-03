# ASR-10 PAR Lua reproduction

2026-08-03. Current HEAD: `0bf6b05ebd8c`.

## Method

`docs/asr10/lua/asr10_adc.lua` was run in `answer` mode against
`V350.img`. The script supplies synthetic 10-bit PAR values per channel
through a read tap.

The channel was derived from the measurement sequence rather than from
PBDAT writes: eight PAR reads form one measurement, and the expected
measurement order is channel `7,5,0` from
`docs/asr10/par-channel-select.md` section 1.

Reason: the PBDAT write tap currently sees zero accesses in the entire
`$FC6000-$FC6FFF` window. Treat that as an open instrumentation question,
not as evidence that the firmware did not execute the channel-select code.

Flags used:

```text
ASR10_DIAG_PANEL_AUTORESPOND=1
ASR10_EXPERIMENT_DUART_COUNTER_TIMER=1
ASR10_EXPERIMENT_ES5506_HOST=1
ASR10_EXPERIMENT_ES5510_HOST=1
```

Both PAR flags were off:

```text
ASR10_EXPERIMENT_PAR_DIAGNOSTIC unset
ASR10_DIAG_PAR_VALUE unset
```

## Result

`ERROR 130` is passed. The answer-mode run made ten measurements in the
derived `7,5,0` cycle, versus one measurement in observe mode with no PAR
answer. Channel 7 answered `0x2FA`, the divisor became non-zero, and the
`divu.w D2,D0` at `$006800` did not trap.

No error-code `0x82` was written and no `ERROR 130` text appeared on the
panel.

Panel sequence:

```text
ENSONIQ ASR-10 -> LOADING SYSTEM -> silence after five bytes
```

First divergence from real hardware:

```text
TUNING KBD - HANDS OFF
```

The real reference sequence continues:

```text
ENSONIQ ASR-10 -> SCSI INSTALLED -> SEARCHING FOR SCSI DEV
-> PLEASE INSERT DISK -> LOADING SYSTEM -> TUNING KBD - HANDS OFF
-> KEYBOARD TUNED -> FILE 1...
```

`SCSI INSTALLED` and `SEARCHING FOR SCSI DEV` are absent in the emulator
but present on the user's SCSI-equipped machine. That is an expected
configuration difference, not a failure of this PAR reproduction.

## Open instrumentation question

Observe-mode evidence:

```text
fallande_PC=006802
$0DD6-skrivningar: 3
ES5506-fordelning includes FC2068/FC206A/FC206C/FC206E PAR(idx=13) x8 each
$FC-sidcensus (las): FC20xx x1128  FC30xx x15  FC40xx x1875886  FC48xx x3092  FC50xx x2
68302-LAS $FC6000-$FC6FFF: (inga)
68302-SKRIV $FC6000-$FC6FFF: (inga)
```

`ori.b #$07,($00FC6829).l` at `$0067EC` and the matching later channel
select instructions are still the static channel-select evidence, but
Lua passthrough taps did not observe their `$FC68xx` accesses in this
run. Leave this as a low-priority tap/dispatch-layer question.
