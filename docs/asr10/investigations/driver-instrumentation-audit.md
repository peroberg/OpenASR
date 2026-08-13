# ASR-10 driver instrumentation audit

Date: 2026-08-12 inventory; updated 2026-08-13 after Category A/B/C cleanup.

This is the authoritative cleanup record for ASR-10 driver instrumentation in
`src/mame/ensoniq/asr10_boot.cpp`. It distinguishes the intended cleanup order
from what the final squashed cleanup commit contains.

## Historical baseline

Baseline before cleanup:

- Commit before cleanup: parent of `f5e189eb39d`.
- `src/mame/ensoniq/asr10_boot.cpp`: 6916 lines.
- The driver mixed behavior-changing fabrication, observation-only diagnostics,
  and completed one-shot experiment/sweep controls.
- `ASR10_*` strings that are only log record names are not counted as controls.

Intended cleanup order:

1. Category C first: completed experiments/sweeps whose results were already
   captured in `docs/asr10/`.
2. Category B next: observation-only diagnostics and logging.
3. Category A last: behavior-changing fabrication, because removing it can
   change machine behavior and must be checked separately.

Git result:

- `f5e189eb39d asr10: remove instrumentation categories A, B and C` ultimately
  contains removals from C, B and A in one commit.
- `8b45f56d22a asr10: finalize instrumentation cleanup` removes one final
  dead Category C crumb and adds cleanup status to `current-status.md`.

Line-count checkpoints:

| checkpoint | lines in `asr10_boot.cpp` |
|---|---:|
| historical baseline | 6916 |
| after `f5e189eb39d` | 3583 |
| after `8b45f56d22a` | 3580 |
| after structural cleanup | 952 |

## Category C - experiment/sweeps

Definition: one-shot or historical experiment controls whose results are
journaled in `docs/asr10/`.

Final status:

- No known Category C controls remain in `asr10_boot.cpp`.
- The final leftover C crumb,
  `m_fc6816_service_2400_clear_experiment_done`, was removed by
  `8b45f56d22a`.
- Historical C examples removed by the cleanup include GPIO trace, download
  trace, disk-signature trace, FC3000 verify trace, tuning-stall trace, panel
  conversation/window/sweep/frame controls, Step 0 runtime trace and RX-event
  trace.

## Category B - observation/diagnostics

Definition: logging, counters, summaries and read-only diagnostics that do not
change guest-visible behavior.

Final status:

- No `std::getenv("ASR10_...")` controls remain in `asr10_boot.cpp`.
- No trace, profile, summary, poller or diagnostic logging system remains in
  `asr10_boot.cpp`.

## Category A - behavior-changing fabrication

Definition: code that synthesizes hardware responses, stubs status, injects
data or interrupts, substitutes device results, or otherwise changes
guest-visible behavior.

Final Category A table:

| family | final status | evidence | rationale |
|---|---|---|---|
| `ASR10_FAKE_SCSI_INSTALLED` | ALREADY_REMOVED | Present in `f5e189eb39d^`; no HEAD hits. Removed by `f5e189eb39d`. | Fake SCSI installed/status/data path. |
| `$FC4809` base stub `ASR10_DUART_INPUT_CHANGE_STUB` | ALREADY_REMOVED | Present in `f5e189eb39d^`; current DUART path reads `m_duart->read()`. Removed by `f5e189eb39d`. | Substituted DUART IPCR value. |
| `$FC4809` bit-4 PC stub | ALREADY_REMOVED | `ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84` removed by `f5e189eb39d`; current code wires `m_fdc->idx_wr_callback()` to DUART IP0. | PC-specific synthetic input-change bit. |
| `ASR10_MISSING_FDC_RATE_SOURCE` | KEEP | Present in HEAD; `upd72069_fdc_w()` still forces `m_fdc->set_rate(500000)` for aux `$88`. | Known missing-source workaround; ASR-10's real 500 kbit/s rate source is not identified. |
| synthetic MC68302 timer IRQ | ALREADY_REMOVED | `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ*`, timer, callback and `set_input_line()` path removed by `f5e189eb39d`. | Synthetic interrupt assertion. |
| synthetic MC68302 IACK vector | ALREADY_REMOVED | `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_*` and shadow source manipulation removed by `f5e189eb39d`. | Synthetic vector/source delivery. |
| panel reboot-confirm raw `$21` | ALREADY_REMOVED | `ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21` and `m_panel_reboot_confirm_injected` removed by `f5e189eb39d`. | Synthetic ROM receive-path byte. |
| FDC `$1E` result stub | ALREADY_REMOVED | `ASR10_EXPERIMENT_STUB_CMD1E_RESULTS` and result-byte constants removed by `f5e189eb39d`. | Synthetic FDC command result bytes. |
| FDC `$0E` result stub | ALREADY_REMOVED | `ASR10_EXPERIMENT_STUB_CMD0E_RESULT` and result-byte constant removed by `f5e189eb39d`. | Synthetic FDC command result byte. |
| synthetic FDC terminal count | ALREADY_REMOVED | `ASR10_EXPERIMENT_FDC_SYNTH_TC`, `m_fdc_synth_tc_*` and `tc_w()` pulse removed by `f5e189eb39d`. | Synthetic terminal-count pulse from observed host byte count. |
| panel legacy autorespond | ALREADY_REMOVED | `ASR10_PANEL_LEGACY_AUTORESPOND`, timer and injection state removed by `f5e189eb39d`. | Synthetic `$FF` channel-B response after THRB writes. |
| panel reply substitute | ALREADY_REMOVED | `ASR10_PANEL_REPLY_SUBSTITUTE*` and related state/logging removed by `f5e189eb39d`. | One substituted legacy panel response. |
| fixed ES5506 PAR workaround | ALREADY_REMOVED | `es5506_host_read_par_diag()` replaced by `analog_r()` in `f5e189eb39d`. | Fixed synthetic PAR value replaced by panel analog route. |

The ASR panel `$FF` response is deliberately tracked outside the driver cleanup:
`asr10panel_device::rcv_complete()` in `src/mame/ensoniq/esqpanel.cpp` sends
`$FF` unless `ASR10_PANEL_DISABLE_ECHO` is set. It is not part of
`asr10_boot.cpp` cleanup and requires a separate ASR panel model decision.

## Final state

- `src/mame/ensoniq/asr10_boot.cpp` was reduced from 3580 to 952 lines by the
  structural cleanup that removed obsolete runtime instrumentation and kept the
  driver to the current machine model, adapters and documented workaround.
- `std::getenv` in `asr10_boot.cpp`: 0.
- Remaining `ASR10_*` driver controls are:
  `ASR10_MISSING_FDC_RATE_SOURCE` and `ASR10_DISPLAY_LENGTH`.
- Category A remaining in driver: `ASR10_MISSING_FDC_RATE_SOURCE` only.
- Category B remaining in driver: none known.
- Category C remaining in driver: none known.
- Normal boot requires no experiment flags.
- Verification after structural cleanup: `make SUBTARGET=mame -j4`,
  `docs/asr10/regression-test.sh` (5/5), `git diff --check`, and a normal
  no-Lua launch of `./mame asr10booth -flop1 floppies/asr10booth/V350.img`
  all remained working. The regression harness verified `FILE 1  TUTORIAL BNK`
  and panel button navigation to `FILE 2`.

Verification command, 2026-08-13:

```sh
rg 'getenv|ASR10_EXPERIMENT_|ASR10_STUB_|ASR10_FAKE_|ASR10_SYNTH_' \
  src/mame/ensoniq/asr10_boot.cpp
```

Result: no matches.

Method: `ASR10_*` controls were collected from `std::getenv("ASR10_...")`
and `static constexpr ASR10_...` declarations in `src/mame/ensoniq/asr10_boot.cpp`
and `src/mame/ensoniq/esqpanel.cpp`. `ASR10_...` strings that are only log
record names are not counted as flags.

Categories:

- A. FABRICERAR: synthesizes hardware responses, stubs status, injects data or
  interrupts, or otherwise changes guest-visible behavior.
- B. OBSERVERAR: logging, counters, summaries, read-only diagnostics.
- C. EXPERIMENT: one-shot or historical experiment controls whose results are
  journaled in `docs/asr10/`.
- D. AKTIV: currently useful controls/instrumentation that are neither pure
  observation nor a completed experiment.

## Historical pre-cleanup inventory - all identified controls

The table below is historical. It records controls identified before cleanup and
must not be read as current-code status unless a row is also listed in
`Final state`.

| flag | category | default | effect |
|---|---|---|---|
| `ASR10_FAKE_SCSI_INSTALLED` | A | off, constexpr `false` | Forces SCSI status/data paths to look installed. |
| `ASR10_LOG_FDC_ACCESS` | B | off, constexpr `false` | Extra FDC access logging. |
| `ASR10_LOG_FDC_04B0_CONTEXT` | B | off, constexpr `false` | Extra low-RAM `$04B0` context logging. |
| `ASR10_LOG_PANEL_BYTES` | B | off, constexpr `false` | Extra legacy panel byte logging. |
| `ASR10_MISSING_FDC_RATE_SOURCE` | A | on, constexpr `true` | Workaround for the missing ASR-10 FDC data-rate source; forces command `$88` to `set_rate(500000)`. |
| `ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B` | C | off, constexpr `false`; no active use found | Historical MC68302 internal-shadow experiment constant. |
| `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480` | C | off, constexpr `false`; no active use found | Historical MC68302 internal-shadow experiment constant. |
| `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER` | C | off, constexpr `false`; no active use found | Historical MC68302 internal-shadow experiment constant. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ` | A | off, constexpr `false` | Enables synthetic MC68302 timer IRQ pulses. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ_LEVEL` | A | inactive parameter, constexpr `1` | IRQ level used by synthetic timer IRQ mode. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR` | A | off, constexpr `false` | Enables synthetic timer IRQ plus custom IACK vector path. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_IRQ_LEVEL` | A | inactive parameter, constexpr `1` | IRQ level for synthetic IACK-vector mode. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE` | A | inactive parameter, constexpr `$40` | Vector byte returned by synthetic IACK mode. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK` | A | inactive parameter, constexpr `$2400` | Internal interrupt source bits ORed into the MC68302 shadow. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT` | A | inactive parameter, constexpr `false` | Limits synthetic IACK firing when vector mode is enabled. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR` | A | inactive parameter, constexpr `false` | Gates synthetic firing on service-bit clear when vector mode is enabled. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP` | A | inactive parameter, constexpr `1024` | Minimum synthetic timer callback gap. |
| `ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21` | A | off, constexpr `false` | Injects raw panel reboot-confirm response `$21` on ROM receive path. |
| `ASR10_EXPERIMENT_STUB_CMD1E_RESULTS` | A | off, constexpr `false` | Overrides FDC aux command `$1E` result bytes. |
| `ASR10_STUB_CMD1E_RESULT_BYTE0` | A | inactive parameter, constexpr `$00` | First synthetic `$1E` result byte. |
| `ASR10_STUB_CMD1E_RESULT_BYTE1` | A | inactive parameter, constexpr `$00` | Second synthetic `$1E` result byte. |
| `ASR10_EXPERIMENT_STUB_CMD0E_RESULT` | A | off, constexpr `false` | Overrides FDC aux command `$0E` result byte. |
| `ASR10_STUB_CMD0E_RESULT_BYTE` | A | inactive parameter, constexpr `$00` | Synthetic `$0E` result byte. |
| `ASR10_DIAG_PANEL_B` | B | on, constexpr `true` | Panel channel-B diagnostic logging and first-hit markers. |
| `ASR10_DIAG_PANEL_SUBMISSIONS` | B | off unless env set | Logs panel descriptor/ring submission diagnostics. |
| `ASR10_DIAG_ROOT_DIRECTORY` | B | off unless env set | Logs root-directory descriptor/table diagnostics. |
| `ASR10_EXPERIMENT_MC68302_GPIO_TRACE` | C | off unless env set | Historical GPIO trace. Observes only. |
| `ASR10_EXPERIMENT_DOWNLOAD_TRACE` | C | off unless env set | Historical ESP/effect download trace. Observes only. |
| `ASR10_EXPERIMENT_FDC_SYNTH_TC` | A | off unless env set | Pulses FDC `tc_w()` from host byte count. |
| `ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE` | C | off unless env set | Historical disk-signature trace. Observes only. |
| `ASR10_EXPERIMENT_FC3000_VERIFY_TRACE` | C | off unless env set | Historical ES5510 host-window verification trace. Observes only. |
| `ASR10_EXPERIMENT_TUNING_STALL_TRACE` | C | off unless env set | Historical tuning-stall scheduler trace. Observes only. |
| `ASR10_PANEL_LEGACY_AUTORESPOND` | A | off unless env set | Restores old harness response: queues `$FF` on channel B after THRB writes. |
| `ASR10_PANEL_B_CONVERSATION` | C | off unless env set | Logs channel-B TX/RX conversation from reset to FILE 1. |
| `ASR10_PANEL_REPLY_SUBSTITUTE` | A | off unless env set | Substitutes one legacy autoresponse value. Requires legacy autorespond path. |
| `ASR10_PANEL_REPLY_SUBSTITUTE_VALUE` | A | inactive parameter, default `$FF` | Replacement response byte. |
| `ASR10_PANEL_REPLY_SUBSTITUTE_TX` | A | inactive parameter, default `$74` | TX byte that arms substitution. |
| `ASR10_PANEL_REPLY_SUBSTITUTE_OCCURRENCE` | A | inactive parameter, default `1` | Which matching TX occurrence gets substituted. |
| `ASR10_PANEL_FILE1_TX_WINDOW` | C | off unless env set | 20 s FILE 1 TX observation window. |
| `ASR10_PANEL_03C0_BLOCK_TRACE` | C | off unless env set | 20 s low-memory `$03C0-$03CF` block trace. |
| `ASR10_PANEL_FRAME_DELAY_US` | C | inactive parameter, default `24` | Inter-byte delay for frame injection experiments. |
| `ASR10_PANEL_FRAME_SETTLE_MS` | C | inactive parameter, default `20` | Post-frame settle delay for frame/button experiments. |
| `ASR10_PANEL_ES5506_AFTER_FRAME` | C | off unless env set | Counts ES5506 host writes after a panel frame experiment. |
| `ASR10_PANEL_FRONTPANEL_SWEEP` | C | off unless env set | Injects two-byte front-panel candidate frames. |
| `ASR10_PANEL_FRONTPANEL_START` | C | inactive parameter, default `$00` | Start byte for front-panel sweep. |
| `ASR10_PANEL_FRONTPANEL_END` | C | inactive parameter, default `$3F` | End byte for front-panel sweep. |
| `ASR10_PANEL_BUTTON_SWEEP` | C | off unless env set | Runs button-frame or panel-device button sweep. |
| `ASR10_PANEL_BUTTON_SWEEP_DEVICE` | C | inactive parameter, default off | Uses `set_button()` instead of direct frame injection during button sweep. |
| `ASR10_PANEL_BUTTON_SWEEP_START` | C | inactive parameter, default `$00` | First button ID in sweep. |
| `ASR10_PANEL_BUTTON_SWEEP_END` | C | inactive parameter, default `$3F` | Last button ID in sweep. |
| `ASR10_PANEL_FRAME_SEQUENCE` | C | off unless env set | Injects an explicit panel byte sequence. |
| `ASR10_PANEL_SWEEP_RAW` | C | off unless env set | Historical async single-byte injection. |
| `ASR10_PANEL_SWEEP_ALL` | C | off unless env set | Historical async raw-byte sweep. |
| `ASR10_PANEL_SWEEP_START` | C | inactive parameter, default `$00` | Start byte for async sweep. |
| `ASR10_PANEL_SWEEP_END` | C | inactive parameter, default `$FF` | End byte for async sweep. |
| `ASR10_DIAG_PANEL_C_PARSER_TRACE` | C | off unless env set | Directed parser trace from earlier panel-state-machine work. |
| `ASR10_STEP0_RUNTIME_TRACE` | C | off unless env set | Step-0 PC/IRQ/handoff profiling. |
| `ASR10_RX_EVENT_TRACE` | C | off unless env set | RX-event chain tracing and scheduler slot dumps. |
| `ASR10_PANEL_DEVICE_BYTE_TRACE` | B | off unless env set | Logs ASR panel-device RX/TX/display byte events. |
| `ASR10_PANEL_DISABLE_ECHO` | D | off unless env set | Disables the ASR panel device's default `$FF` reply; behavior-affecting negative control. |

Non-flag constants with `ASR10_` names: `ASR10_DISPLAY_LENGTH` and
`ASR10_PANEL_DESCRIPTOR_TRACE_LIMIT` are sizes/limits, not behavior flags.

## Historical pre-cleanup inventory - Category A controls

The table below is historical. The authoritative final Category A status is the
`Final Category A table` above.

| control | fabricates | active by default? | observations to re-check if active |
|---|---|---:|---|
| `ASR10_FAKE_SCSI_INSTALLED` | SCSI installed/status/data responses. | no | SCSI probe, boot priority, any "PLEASE INSERT DISK" or SCSI absence conclusion. |
| `ASR10_MISSING_FDC_RATE_SOURCE` | Forces FDC aux command `$88` to `set_rate(500000)`. | yes | Disk-read timing/rate conclusions and any result depending on `$88` using firmware-selected rate. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ` | Synthetic interrupt line assertion from host timer. | no | IRQ source attribution, scheduler/runtime progress, PB/timer conclusions. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR` | Synthetic interrupt plus custom IACK vector and MC68302 shadow source bits. | no | IRQ6/vector, internal interrupt source, runtime-dispatch conclusions. |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_*` parameters | Vector, level, source mask, one-shot/wait/gap policy for synthetic IACK mode. | inactive unless vector mode compiled on | Same as synthetic IACK vector mode. |
| `ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21` | Synthetic raw `$21` response in the ROM receive path. | no | Panel reboot-confirm path and ROM receive path behavior. |
| `ASR10_EXPERIMENT_STUB_CMD1E_RESULTS` / `ASR10_STUB_CMD1E_RESULT_BYTE0/1` | Synthetic FDC command `$1E` result bytes. | no | FDC command `$1E` and any path depending on those result bytes. |
| `ASR10_EXPERIMENT_STUB_CMD0E_RESULT` / `ASR10_STUB_CMD0E_RESULT_BYTE` | Synthetic FDC command `$0E` result byte. | no | FDC command `$0E`, mount/status decisions. |
| `ASR10_EXPERIMENT_FDC_SYNTH_TC` | Synthetic FDC terminal-count pulse from observed host byte count. | no | FDC DMA/PIO/TC conclusions. |
| `ASR10_PANEL_LEGACY_AUTORESPOND` | Legacy `$FF` channel-B RX response after THRB writes. | no | Panel protocol, RX FIFO, IRQ, and FILE browser input observations. |
| `ASR10_PANEL_REPLY_SUBSTITUTE*` | One substituted legacy panel response. | no | Boot tuning/substitution pilot conclusions. |

One category A control is active in a normal build after the cleanup:
`ASR10_MISSING_FDC_RATE_SOURCE`. The `$FC4809` base-value stub, the PC-specific
`$FC4809` bit-4 stub, the ES5506 fixed-PAR workaround, fake SCSI path,
synthetic MC68302 interrupt experiments, FDC command-result stubs, synthetic
FDC terminal count, legacy panel autorespond and reply-substitution paths were
removed from `asr10_boot.cpp`.

## Always-on non-flag behavior fabrications

These are not `ASR10_*` flags, but they are still driver behavior that does not
come directly from a fully modeled ASR-10 board:

- `asr10panel_device::rcv_complete()` sends `$FF` after each host byte unless
  `ASR10_PANEL_DISABLE_ECHO` is set. This is now part of the ASR panel device
  model, but remains a synthetic idle response until tied to service data.

## Boot Dependency Trial, 2026-08-12

Method: one V3.50 boot from reset per fabrication, no `-log`, no `mem_map`
change, and no removal. Each trial changed only the tested fabrication and was
restored afterward.

| fabrication disabled | result | dominant PC / wait point | classification |
|---|---|---|---|
| `$FC4809` base value `ASR10_DUART_INPUT_CHANGE_STUB = $00` | Boot still reached `FILE 1  TUTORIAL BNK`. | FILE1 scheduler profile remained active; no new hardware wait was observed. | obsolet for V3.50 boot-to-FILE1 |
| `$FC4809` bit 4 at `$FB7C84` | Boot stayed in repeated `PLEASE INSERT DISK`. | Tight ROM cycle `$FB8D6C/$FB8D6E/$FB8D70/$FB8D72/$FB8D74`; phase also reads `$FC4809` and FDC status. | workaround |
| FDC command `$88` forced to 500 kbit/s | Boot stayed in repeated `PLEASE INSERT DISK`. | Dominant PC `$FB8AA8`; 4,612,215 FDC reads in the 20 s loading window, dominated by `$FC4000`. | workaround |
| ES5506 PAR fixed value `$0200` changed to `$0000` for the trial | Boot reached `ERROR 130 - REBOOT ?`, not FILE1. | Error path after PAR channel-7 sum becomes zero; existing documentation identifies `$006800 divu.w D2,D0` as the trap point when `D2 = 0`. Later display/error loop is dominated by `$F89CD4`. | workaround |

## Disk Ready Cleanup Trial, 2026-08-12

`ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84` and
`ASR10_DUART_INPUT_CHANGE_STUB` were removed from the driver. ASR-10 now drives
DUART IP0 from floppy media-loaded plus motor-active state, following
`esq5505_state::update_floppy_inputs()`'s Disk Ready precedent.

Result: V3.50 does not reach `FILE 1`. It stays in repeated
`PLEASE INSERT DISK`; the A_FDC_LOAD profile is the known tight ROM cycle
`$FB8D6C/$FB8D6E/$FB8D70/$FB8D72/$FB8D74`, with `$FC4816`, `$FC4000`,
`$FC4002`, `$FC4808`, and `$FC4812` as top CS3 accesses. The old PC-dependent
stub was not reintroduced.

The even DUART addresses in that access summary are diagnostic word bases, not a
different register stride: `$FC4808`, `$FC4812`, and `$FC4816` are the low-byte
accesses to IPCR `$FC4809`, SRB `$FC4813`, and RHRB `$FC4817`.

[DISPROVEN] IP0 = floppy loaded && motor active. V3.50 bootar inte till
`FILE 1` med den kopplingen; stannar i `PLEASE INSERT DISK` med samma
`$FB8D6C`-cykel som utan någon användbar IP0-drivning alls. The ESQ/VFX-family
Disk Ready precedent remains relevant, but this ASR-10 predicate is not the
complete hardware source firmware expects.

Error-code trace without the PC-dependent IPCR stub:

- `$049D=$05` from `$FB7C9E`: 13 writes.
- `$049D=$05` from `$FB7C2C`: 13 writes.
- `$049D=$05` from `$FB91D4`: 13 writes.
- `$049D=$0D` from `$FB8D5E`: 0 writes identified in the 25 s run.

[Verified] `$049D` is an error-code variable. `$05` is written when the IP0
change path does not produce the expected state (`$FB7C9E` path); `$0D` is the
FDC timeout code at `$FB8D5E`, but that timeout was not the blocker in this
run.

## IP0 Index Trial, 2026-08-12

[Verified] IP0 = floppy INDEX on ASR-10. Implementation route tested:
uPD72069 owns `floppy_image_device::setup_index_pulse_cb()` internally, then
publishes the same index level through `idx_wr_callback()`, which is wired to
`scn2681_device::ip0_w()`. This keeps the FDC's own index handling intact and
adds a parallel DUART IP0 observer. Result: V3.50 reaches
`FILE 1  TUTORIAL BNK` without the old PC-dependent `$FC4809` bit-4 stub.

Negative control: with the same index wiring but no mounted disk image, V3.50
does not reach `FILE 1`; it stays in `PLEASE INSERT DISK` and writes
`$049D=$05` from `$FB7C9E` 13 times in 25 s, with matching `$05` writes from
`$FB7C2C` and `$FB91D4`. No `$049D=$0D` from `$FB8D5E` was identified in that
run. This is the control that raises IP0=index from likely to verified:
without rotation there is no useful IP0-change event.

[Verified] `$FB8D6C-$FB8D74` is a delay loop, not the signal wait. The real
wait reads FDC status at `$FB8D40` (`btst #7,$FFFC4001.l`) with timeout
countdown in `($0476).w`; on timeout it writes `$049D=$0D` and `$04AE=$21`.

Disk-loop disassembly:

```asm
FB8D40  0839 0007 FFFC4001  btst    #7,$FFFC4001.l
FB8D48  670E                beq.s   $FB8D58
FB8D4A  7604                moveq   #$04,D3
FB8D4C  611E                bsr.s   $FB8D6C
FB8D4E  0839 0006 FFFC4001  btst    #6,$FFFC4001.l
FB8D56  6712                beq.s   $FB8D6A
FB8D58  53B8 0476           subq.l  #1,($0476).w
FB8D5C  6AE2                bpl.s   $FB8D40
FB8D5E  11FC 000D 049D      move.b  #$0D,($049D).w
FB8D64  11FC 0021 04AE      move.b  #$21,($04AE).w
FB8D6A  4E75                rts
FB8D6C  B683                cmp.l   D3,D3
FB8D6E  2F03                move.l  D3,-(A7)
FB8D70  261F                move.l  (A7)+,D3
FB8D72  5383                subq.l  #1,D3
FB8D74  66F6                bne.s   $FB8D6C
FB8D76  4E75                rts
```

The wait loop reads FDC status at `$FC4001`, tests bit 7 first and bit 6 after a
short delay, exits successfully at `$FB8D6A` when bit 6 is clear, and times out
through `$049D=$0D` / `$04AE=$21`.

IPCR gate disassembly:

```asm
FB7C7A  1038 04EE           move.b  ($04EE).w,D0
FB7C7E  4A00                tst.b   D0
FB7C80  6A1A                bpl.s   $FB7C9C
FB7C82  4200                clr.b   D0
FB7C84  0839 0004 FFFC4809  btst    #4,$FFFC4809.l
FB7C8C  6704                beq.s   $FB7C92
FB7C8E  103C 0001           move.b  #$01,D0
FB7C92  1F00                move.b  D0,-(A7)
FB7C94  6100 10E2           bsr     $FB8D78
FB7C98  11DF 04EE           move.b  (A7)+,($04EE).w
FB7C9C  6606                bne.s   $FB7CA4
FB7C9E  11FC 0005 049D      move.b  #$05,($049D).w
FB7CA4  4E75                rts
```

When IPCR bit 4 is set, firmware records `D0=1`, calls the `$FB8D78` FDC wait
variant, stores `1` back to `$04EE`, and returns without writing
`$049D=$05`. When bit 4 is clear, the same wait is called with `D0=0`; if it
returns with no pending state, firmware writes `$049D=$05`.

IPCR-specific static context: `$FC4809` is DUART IPCR on reads. Bits 0-3 are
current IP0-IP3 levels and bits 4-7 are their change flags; MAME's `mc68681`
clears the change flags when IPCR is read. Documented ROM reads are
`$FB7BEE`, `$FB7C30`, `$FB7C84`, and `$FB8154`; `$FB7BEE` performs a `tst.b`
style read that acknowledges/clears the latch, and `$FB7C84` tests bit 4
explicitly. A direct hex search of the OS images for absolute `$FC4809` forms
found two V3.50 hits and no V1.61 hits by this method:

| artifact | offset | bytes | decoded operation |
|---|---:|---|---|
| `V350.img` | `0x013B8C` | `08 39 00 05 FF FC 48 09` | `btst #5,$FFFC4809.l` |
| `V350.img` | `0x01BD4E` | `08 39 00 05 FF FC 48 09` | `btst #5,$FFFC4809.l` |
| `V161.img` | - | - | no direct absolute `$FC4809` hit identified by this method |

This supports only "IP0/IP1 change-flag consumers exist"; it does not by itself
name the physical signal wired to either input. Candidate names such as disk
change, write protect, SCSI presence, or pedal remain unassigned until firmware
behavior ties a bit to a specific subsystem.

## Remaining Missing-Source Workarounds

[OPEN] `ASR10_MISSING_FDC_RATE_SOURCE`: ASR-10's true 500 kbit/s FDC rate source
is not identified. MAME's uPD72069 aux command table decodes `$88` as
250 kbit/s (`data & $70 == $00`; `$98`/`$C8` select 500 kbit/s), while the
current ASR-10 boot path still needs `set_rate(500000)` to read the V3.50 disk.
Either ASR-10 uses a different command coding than the current model implements,
or `$88` means something else in this board context. The current driver has no
identified separate latch write in the measured failing window; once Disk Ready
reaches the `$88` phase without a PC stub, the write window immediately before
`$88` should be remeasured.

Retest after IP0=index: disabling `ASR10_MISSING_FDC_RATE_SOURCE` still prevents
V3.50 boot-to-FILE1. The run stays in `PLEASE INSERT DISK`, dominated by FDC
status reads at `$FC4000`; `$049D=$0D` is repeatedly written from `$FB8AE0`
with `$04AE=$28`, and `$049D=$0D` is also observed from `$FB7C2C` after the
IPCR gate path. The FDC-rate workaround therefore remains an active `[OPEN]`
missing-source workaround, independent of the fixed IP0/index path.

Failure-site disassembly for the rate-off run:

```asm
FB8AA2  1239 FFFC4001       move.b  $FFFC4001.l,D1
FB8AA8  6A12                bpl.s   $FB8ABC
FB8AAA  0801 0005           btst    #5,D1
FB8AAE  6712                beq.s   $FB8AC2
FB8AB0  0801 0006           btst    #6,D1
FB8AB4  670C                beq.s   $FB8AC2
FB8AB6  12F9 FFFC4003       move.b  $FFFC4003.l,(A1)+
FB8ABC  5380                subq.l  #1,D0
FB8ABE  66E2                bne.s   $FB8AA2
FB8AC0  601E                bra.s   $FB8AE0
FB8AC2  6100 02B4           bsr     $FB8D78
FB8AC6  4A38 049D           tst.b   ($049D).w
FB8ACA  6600 0020           bne     $FB8AEC
FB8ACE  1438 04C6           move.b  ($04C6).w,D2
FB8AD2  0202 00C0           andi.b  #$C0,D2
FB8AD6  6714                beq.s   $FB8AEC
FB8AD8  0838 0007 04C7      btst    #7,($04C7).w
FB8ADE  660C                bne.s   $FB8AEC
FB8AE0  11FC 000D 049D      move.b  #$0D,($049D).w
FB8AE6  11FC 0028 04AE      move.b  #$28,($04AE).w
FB8AEC  4E75                rts
```

This is not the `$FB8D40` status wait. It is a data-transfer loop over
`$FC4001/$FC4003`: status bit 7 must be set, bits 5/6 decide whether to branch
to result handling, and data bytes are copied from `$FC4003` while the loop
counter in `D0` lasts. `$04AE=$28` marks this data-transfer timeout; `$04AE=$21`
is the separate `$FB8D40` status-wait timeout.

Rate-off FDC command dialogue before the repeated timeout:

```text
aux 36
aux 0B
aux 4F
aux 1E
fifo 04 00
fifo 04 00
aux 88
aux F3
fifo 03 E1 09 07 00 08
fifo 46 00 00 00 01 02 01 1B FF
```

The repeated failing transfer is the `46 00 00 00 01/02 02/01 1B FF` family.
MAME decodes `$46` as Read Data with MFM set. The command is issued after aux
`$88` and aux `$F3`.

MAME model context: `upd765_family_device::set_rate()` directly writes
`cur_rate`, and read/write live timing uses
`cur_live.pll.set_clock(attotime::from_hz(mfm ? 2*cur_rate : cur_rate))`.
The device's construction clock is therefore not the source of the read data
rate in this path. `upd72069_device::auxcmd_w($88)` matches the
72069-exclusive data-rate command group and decodes `data & $70 == $00` as
250 kbit/s; `$98` or `$C8` would select 500 kbit/s in the current model. ASR-10
sends `$88`, so either this uPD72069 variant/board uses different coding, or
another as-yet-unidentified source overrides the rate on real hardware.

Rate-table calibration, 2026-08-12:

[OPEN] MAME's `upd72069_device` rate table is not calibrated by any other
locally inspectable firmware in this checkout. Other drivers instantiate the
same device (`mpc2000`, `mpc3000`, `s3000`), but their ROMs are not present
under local `roms/`, so their aux command streams could not be extracted by the
static method used here. No identified non-ASR firmware in this tree was shown
to send `$98` or `$C8` for 500 kbit/s.

`upd72069_device::auxcmd_w()` handles each aux byte as a standalone command. It
does not implement `(command, parameter)` pairs. The relevant groups are:

```text
$36             software reset
$0E..$FE / xE   enable motors
$0B..$FB / xB   control internal mode; data & $C0 -> 250k, 500k, 600k, 300k
$88..$F8 / x8   control data transfer rate; data & $70 selects rate
$C3..$F3 / x3   precompensation
$4F/$5F         format select/track-count variant acknowledgement
```

For the rate group, current MAME decodes:

```text
data & $70 = $00 -> 250 kbit/s
data & $70 = $10 -> 500 kbit/s
data & $70 = $20 -> 600 kbit/s
data & $70 = $30 -> 300 kbit/s
data & $70 = $40 -> 500 kbit/s
data & $70 = $50 -> 1000 kbit/s
data & $70 = $60 -> 1250 kbit/s
data & $70 = $70 -> 600 kbit/s
```

No datasheet reference is cited next to this table in `upd765.cpp`; the code
comments name the aux command groups but do not state their source. The class
does contain 72069-specific aux behavior rather than being only a renamed
`upd765a`: motor enable, internal-mode rate selection, data-rate selection,
precompensation, format-select acknowledgement and the Akai `$80` ACK quirk are
implemented in `upd72069_device::auxcmd_w()`.

[Verified] ASR-10 disk format is 80 tracks x 2 sides x 20 sectors x 512 bytes =
1,638,400 bytes. Twenty 512-byte sectors per track requires 500 kbit/s MFM.
Firmware sends Read Data command `$46` with `N=$02`.

[Verified] The command dialogue before the rate-off failure is `SPECIFY`,
`RECALIBRATE`, `SENSE INTERRUPT STATUS`, `READ DATA`, preceded by aux `$36`,
`$0B`, `$4F`, `$1E`, `$88`, `$F3`.

[OPEN] Which aux write sets the data rate on real µPD72069 hardware. In current
MAME, `$0B` would select 250 kbit/s, `$88` would select 250 kbit/s, `$F3` is
precompensation, `$36` is reset, `$4F` is format/variant acknowledgement, and
`$1E` is motor enable. The ASR-10 observation therefore calibrates neither the
current rate table nor a replacement table by itself.

[Verified] The fixed ES5506 PAR workaround has been replaced by the panel analog
path. ASR-10 panel controls call `esqpanel_device::set_analog_value()`, the
panel's `write_analog()` callback updates `asr10_boot_state::m_analog_values`,
and `ES5506::read_port_cb()` now reads `analog_r()` selected by the DUART output
latch. V3.50 still boots to `FILE 1  TUTORIAL BNK`; observed PAR reads returned
raw `$0200` from channel 6 (`left_aligned=$8000`), so the old `$0200` constant
was a plausible centered 10-bit value rather than a max value.

## ES5506 / ES5510 status at current HEAD

[Verified] Current `asr10_boot.cpp` does instantiate `ES5506(config,
`m_es5506_host`, `XTAL(16'000'000))` and maps `$FC2000-$FC207F` to the device's
host registers. It is a host-interface/diagnostic integration, not a complete
audio path: no speaker/pump routing is configured, bank 1-3 wavetable regions
are intentionally unpopulated, and PAR is now supplied by the ASR-10 panel
analog route rather than a fixed diagnostic value.

[Verified] Current `asr10_boot.cpp` does instantiate `ES5510(config,
`m_es5510_host`, `XTAL(10'000'000))` and maps the evidenced `$FC3000-$FC303F`,
`$FC3101`, `$FC3141`, `$FC3181`, and `$FC31C1` host paths. The device is
`set_disable()` and used as a host-interface register bank, not as an executing
ESP audio processor.

[DISPROVEN] The older statement "neither ES5506 nor ES5510 is configured" is no
longer true for current HEAD. Therefore `ERROR DOWNLOADING EFFECT` cannot be
explained merely by absence of ES5510 from `machine_config`; any current failure
must be re-read against the partial ES5510 host integration and its remaining
gaps.

[Likely] `$FC3001` is ES5510 host latch/register offset `$00`. It is inside CS2
(`$FC2000-$FC3FFF`), in the same host window as `$FC3101/$FC3141/$FC3181`, and
`$F97662` performs write/read with retry through `$FFFC3001`. `$03C8` is the
inhibit flag for that routine.

[Verified] `es5510_device::host_r()` offset `$12` (Host Control) returns
hard-coded `0`. Bit 2 can therefore never be set by the current stock device
host read path.

[Verified] `esq5505.cpp` calls `m_esp->set_disable()` for the ES5510 in the
VFX/SD-family configurations. Those drivers use the host register bank without
running the ESP core, which explains why a Host Control bit-2 stub has not been
exercised there.

[Verified] The `$F97662` loopback tolerates missing Host Control bit 2: a V3.50
boot-to-FILE1 run observed 491 of 491 calls succeed on the first attempt via
latch readback, with 0 retry-exhausted calls and 0 compare mismatches.

[OPEN] Whether the real effect download requires Host Control bit 2, or fails
for another reason later than the `$F97662` latch loopback.

[Verified] `ASR10_ESP_DOWNLOAD_TRACE` is a bounded runtime trace for manual
effect-download captures. When enabled, it logs every access in
`$FC3000-$FC303F` with direction, CPU byte address, ES5510 host offset, value,
PC and timestamp, plus writes to `$049D` and `$04AE`. It defaults to 200,000
events and reports `count`, `dropped`, `limit` and `cap_hit` at machine exit.

## FDC Interrupt Path Audit, 2026-08-12

[Verified static] Current `asr10_boot.cpp` does not connect
`m_fdc->intrq_wr_callback()` and does not connect `m_fdc->drq_wr_callback()`.
The only FDC callback wired in machine configuration is
`m_fdc->idx_wr_callback().set(m_duart, FUNC(scn2681_device::ip0_w))`, so FDC
index is visible as DUART IP0 but FDC completion/DRQ has no corresponding CPU
path.

[Verified static] DUART interrupts do have a complete path: SCN2681
`irq_cb().set_inputline(m_maincpu, 6)` asserts CPU level 6; CPU-space IACK for
level 6 calls `maincpu_iack_r(6)`; that supplies
`mc68302_device::irq6_ack_vector() == $56`, whose vector-table target is
`$F884BE`.

[Verified static] No analogous FDC interrupt path exists in the driver. There
is no FDC callback to a CPU input line, no FDC-specific IACK vector supplier,
and no driver-side bridge from FDC `INTRQ`/`DRQ` into the scheduler.

[Verified static] The current `mc68302_device` is still a plumbing model for
BAR/SCR, Port B and chip-select decode. Its header states that there is no
interrupt controller, timer, IDMA or communications processor in this step, and
`mc68302.cpp::classify_offset()` classifies the interrupt controller block
`0x0812-0x0819` as `known_unimplemented`.

Firmware-vector search:

- ROM reset initialization at `$F87E60` copies the vector table from `$F82000`
  into low RAM `$000000-$00015F`.
- The initial vector table includes DUART IRQ6 vector `$56 -> $FFF884BE`.
- No initial vector entry points to the known ROM FDC poll/data routines
  (`$FB7Dxx`, `$FB8Axx`, `$FB8Dxx`, `$FB99xx`).
- Direct long-address searches found FDC register users in ROM and duplicated
  FDC snippets in V1.61/V3.50, but not as an identified vector target.

Scheduler-producer search:

- The method searched `asr10.bin`, `V161.img` and `V350.img` for short absolute
  references to `$00C6`/`$00C8` using `movea.w`, `move.w` and `cmpa.w`
  instruction forms.
- Identified ROM hits were `$F87E7E/$F87E84` (slot-table clear),
  `$F87F16` (slot initialization), `$F87F92/$F87FC6` (scheduler scan) and
  `$F88306/$F88322` (DUART counter-ready service).
- No V1.61 or V3.50 hit was identified by this method.
- No identified scheduler producer both touches `$FC4000-$FC4003` and posts an
  FDC completion task. Register-relative and indirect paths remain a known
  blind spot, so this is not proof that no such producer exists.

[Verified dynamic] Under instrumentinläsning slutar maskinen skicka på panelen
(`thrb_w +0`) och slutar polla FDC:n (`fc4001 +0`), men tar emot knapptryck
(`RHRB 8a/00` och `0a/00`) och kör schedulern normalt. Uppgiften är suspenderad,
inte fastnad i en loop.

[Likely] Disktasken väntar på FDC:ns avslutningsavbrott, som aldrig kommer.
Static code reading verifies that the current driver has no FDC `INTRQ`/`DRQ`
path to the CPU; the exact firmware vector/producer remains `[OPEN]` because
the direct static search did not identify it.
