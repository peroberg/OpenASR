# Floppy Media-Change Semantics Investigation

## 1. Executive Summary

This investigation resolves the central technical question:
> **Does OpenASR fail to expose a firmware-visible floppy media-change indication that a real ASR-10 would have observed?**

Through static silicon/driver tracing, M68000 ROM/OS disassembly, and three controlled empirical runtime experiments (**Experiment A: Prompted Swap**, **Experiment B: Unprompted Swap**, and **Experiment C: Explicit Rescan**), this investigation establishes:

1. `[VERIFIED — silicon & driver architecture]`
   The Ensoniq ASR-10 floppy subsystem uses a NEC uPD72069 controller and an SCN2681 DUART. In the MAME implementation, the uPD72069 lacks a Digital Input Register (DIR) or any host-accessible DSKCHG register. In OpenASR driver wiring, DUART input pin IP0 is connected to floppy INDEX pulses (`idx_wr_callback()`), which pulse only when the spindle motor is actively spinning. Neither the controller nor the DUART provides a persistent unprompted media-change interrupt or latch to the CPU while the drive motor is stationary.

2. `[VERIFIED — V3.50 firmware consumer path]`
   The error string `DISK HAS BEEN CHANGED` (ROM `$F82338`, error code `$049D = 09`) exists in the V3.50 error table, but disassembly of `$FBB2A4..$FBB2E8` reveals that `$049D = 09` is **exclusively a SCSI removable-media path** (`$04B0 != 0`), triggered when a SCSI target returns `CHECK CONDITION` with Sense Key `0x06` (`UNIT ATTENTION`) and ASC `$28` (`MEDIUM MAY HAVE CHANGED`). For floppy devices (`$04B0 == 0`), firmware never sets `$049D = 09` in this routine; it evaluates only rotation via DUART IP0/INDEX (`$FB7C84`), mapping absent pulses to `$049D = 05` (`PLEASE INSERT DISK`).

3. `[VERIFIED — firmware behavior]`
   ASR-10 V3.50 maintains an in-RAM directory cache populated at boot or via `CHANGE STORAGE DEVICE`. It does not poll the floppy drive while idle in menus. An unprompted media replacement is not autonomously detected; firmware continues to browse cached directory entries until the user explicitly invokes `COMMAND -> SYSTEM -> CHANGE STORAGE DEVICE -> FLOPPY` or an operation attempts a volume-validated access.

**Verdict:** **OUTCOME D — [VERIFIED — firmware policy, exercised path]**

- `[VERIFIED — exercised firmware paths]`
  No emulator semantic divergence was observed in the prompted-swap, unprompted-swap, or explicit-rescan workloads.
- `[VERIFIED — firmware behavior]`
  V3.50 does not autonomously invalidate its cached floppy directory in the exercised unprompted media-replacement path. Explicit rescan and prompted multi-volume BANK exchange cause fresh media reads.
- `[CONSISTENT — functional hardware model]`
  The current OpenASR/MAME floppy model is consistent with all firmware-visible behavior exercised here.
- `[OPEN — physical board precision]`
  Exact physical ASR-10 board wiring for any media-change-related signal beyond the established INDEX path is not proven by this investigation.

No production code changes are required or justified.

---

## 2. Baseline

- Committed baseline: `e2e837248bdb33a89823ac356abc6ec9a88b2b8f`
- Commit subject: `docs/asr10: document authentic factory BANK acceptance`
- Working tree state: clean, 0 production code modifications.

---

## 3. Floppy Subsystem Wiring & Hardware Model

The ASR-10 floppy architecture is modeled in `src/mame/ensoniq/asr10_boot.cpp`:

```text
+-----------------------+
|  floppy_image_device  |
|  (:fdc:0:35hd)        |
+-----------+-----------+
            |
            | index_pulse_cb
            v
+-----------+-----------+
|  upd72069_device      |  idx_wr_callback()  +------------------------+
|  (:fdc)               |-------------------->| SCN2681 DUART (:duart) |
|                       |                     | IP0: Input Port 0      |
|  intrq_wr_callback    |                     | IPCR: Register 4       |
+-----------+-----------+                     +-----------+------------+
            |                                             |
            v                                             v
    MC68302 IRQ1 (Vector $51)                     Polled at ROM $FB7C84
```

### Key Hardware & Emulation Properties:
1. **NEC uPD72069 Controller**:
   - Mapped at CPU addresses `$FC4000..$FC4003`:
     - `$FC4001`: Main Status Register (MSR, read) / Auxiliary Command (write).
     - `$FC4003`: FIFO Data Register (read/write).
   - `[VERIFIED — MAME implementation]` In MAME (`src/devices/machine/upd765.cpp`), `upd72065_device` and `upd72069_device` expose only these two registers. There is **no Digital Input Register (DIR)** mapped.
   - Base class method `do_dir_r()` (`return fi.dev->dskchg_r() ? 0x00 : 0x80;`) is mapped exclusively in PC-compatible controller subclasses (`smc37c78_device`, PC FDC interface). It is absent from the uPD72069 silicon model and address map.
2. **Floppy Ready Line**:
   - `m_fdc->set_ready_line_connected(false)` in `asr10_boot.cpp:755`.
   - Per `docs/asr10/investigations/ready-line-artifact-probe.md`, the ready line is tied active to eliminate synthetic polled ready-change interrupts (`ST0=$C8`) generated by MAME's internal 1.024ms timer when the drive motor stops.
3. **DUART IP0 / Index Pulse**:
   - `[VERIFIED — OpenASR wiring]` `m_fdc->idx_wr_callback().set(m_duart, FUNC(scn2681_device::ip0_w))` (`asr10_boot.cpp:750`).
   - When the drive motor is spinning, passing index holes trigger `ip0_w()`.
   - Each transition on IP0 sets bit 4 of DUART IPCR (`$FC4809`). Reading `$FC4809` clears the change flag.
   - When the motor is stationary, no index pulses occur, and IPCR bit 4 remains 0.

---

## 4. V3.50 Firmware Consumers & Control Flow

### A. The Rotation / Presence Check (`$FB7C7A..$FB7C9E`)
Firmware verifies media readiness by testing whether index pulses are actively toggling DUART IP0:

```m68k
$FB7C7A: move.b  $04EE.w, d0
$FB7C7E: tst.b   d0
$FB7C80: bpl.s   $FB7C9C
$FB7C82: clr.b   d0
$FB7C84: btst    #4, ($FFFC4809).l    ; Test DUART IPCR bit 4 (IP0 change flag)
$FB7C8C: beq.s   $FB7C92              ; If 0 (no pulses), branch
$FB7C8E: move.b  #1, d0               ; If 1 (pulses detected), set d0 = 1
$FB7C92: move.b  d0, -(sp)
$FB7C94: bsr     $FB8D78
$FB7C98: move.b  (sp)+, $04EE.w
$FB7C9C: bne.s   $FB7CA4
$FB7C9E: move.b  #$05, $049D.w        ; Error $05: DISK DRIVE NOT READY -> PLEASE INSERT DISK
```

### B. The Error Table at ROM `$F80510`
The firmware disk error dispatcher indexes 32-bit entries relative to `$F80000`:

| Error Code (`$049D`) | Table Entry Address | String Address | String Text |
|---|---|---|---|
| `$00` | `$F80510` | `$F8227E` | `DISK COMMAND COMPLETED` |
| `$01` | `$F80514` | `$F82296` | `DISK NOT FORMATTED` |
| `$02` | `$F80518` | `$F822AA` | `DISK DATA CORRUPTED` |
| `$03` | `$F8051C` | `$F822BE` | `DISK WRITE PROTECTED` |
| `$04` | `$F80520` | `$F822D4` | `DISK ERROR - LOST DATA` |
| `$05` | `$F80524` | `$F822EC` | `DISK DRIVE NOT READY` (`PLEASE INSERT DISK`) |
| `$06` | `$F80528` | `$F82302` | `DISKETTE BAD` |
| `$07` | `$F8052C` | `$F82310` | `DISK SOFTWARE ERROR` |
| `$08` | `$F80530` | `$F82324` | `DISK NOT RESPONDING` |
| **`$09`** | **`$F80534`** | **`$F82338`** | **`DISK HAS BEEN CHANGED`** |
| `$0A`..`$0C` | `$F80538..` | `$F8234E..` | `DISK TYPE ERROR` |
| `$0D` | `$F80544` | `$F8237E` | `FILE OPERATION ERROR` |

### C. Attribution of `DISK HAS BEEN CHANGED` (`$FBB2A4..$FBB2E8`)
Static disassembly of the only site in the entire ROM that emits error `$09`:

```m68k
$FBB2A4: tst.b   ($04B0).w            ; Test active storage device: 0 = Floppy, != 0 = SCSI
$FBB2A8: bne.s   $FBB2B6              ; If SCSI, branch to $FBB2B6!
$FBB2AA: bsr     $FB7C7A              ; Floppy path: check IP0/INDEX rotation
$FBB2AE: tst.b   ($049D).w
$FBB2B2: bne.s   $FBB2DE              ; If rotation missing, branch to set $049D = $05
$FBB2B4: bra.s   $FBB2E8              ; Floppy path complete, return clean

; --- SCSI Removable Media Branch ---
$FBB2B6: moveq   #0, d3
$FBB2B8: clr.l   ($0406).w
$FBB2BC: bsr     $FBAEFA              ; Execute SCSI TEST UNIT READY / REQUEST SENSE
$FBB2C0: tst.b   ($049D).w
$FBB2C4: beq.s   $FBB2E8              ; Success -> return
$FBB2C6: cmpi.b  #$06, ($04B7).w      ; SCSI Sense Key == $06 (UNIT ATTENTION)?
$FBB2CC: bne.s   $FBB2DE
$FBB2CE: cmpi.b  #$28, ($04B6).w      ; SCSI ASC == $28 (MEDIUM MAY HAVE CHANGED)?
$FBB2D4: bne.s   $FBB2DC
$FBB2D6: move.b  #$09, ($049D).w      ; >>> WRITE $049D = $09: DISK HAS BEEN CHANGED! <<<
$FBB2DC: bra.s   $FBB2E4
$FBB2DE: move.b  #$05, ($049D).w      ; $049D = $05: DISK DRIVE NOT READY
$FBB2E4: bsr     $F8816A
$FBB2E8: rts
```

- `[VERIFIED — V3.50 firmware consumer path]`
  The identified `$049D = $09` / `DISK HAS BEEN CHANGED` path is the SCSI removable-media path using Sense Key `$06` and ASC `$28`.
- The exercised floppy branch instead performs the INDEX/rotation presence check and does not set `$049D = $09` in this routine.
- This finding specifically bounds the identified firmware consumer path; it is not a claim that no floppy media-change signal could ever exist in any other firmware revision or hardware context.

---

## 5. Controlled Experimental Results

### Experiment A: Prompted Multi-Disk Swap (Known-Good Control)
- **Fixture:** `floppies/essential/ASR10Demo-Disk1.eda` & `ASR10Demo-Disk2.eda` -> `REZO ASR-BNK`.
- **Trace target:** State transitions during prompted media replacement.

| Phase | Time | Event | Floppy Exists | FDC MSR | DUART IPCR | Display |
|---|---:|---|:---:|:---:|:---:|---|
| **Phase 1** | `t=70.51s` | Firmware prompts for Disk 2 | `true` | `$80` | `$00` | `[INSERT ASR-002 - ENTER]` |
| **Phase 2** | `t=70.51s` | `flop:unload()` executed | `false` | `$80` | `$00` | `[INSERT ASR-002 - ENTER]` |
| **Phase 2B** | `t=71.51s` | 1.0s elapsed with drive empty | `false` | `$80` | `$00` | `[INSERT ASR-002 - ENTER]` |
| **Phase 3** | `t=71.51s` | `flop:load("Disk2")` executed | `true` | `$80` | `$00` | `[INSERT ASR-002 - ENTER]` |
| **Phase 4** | `t=72.51s` | 1.0s elapsed with Disk 2 loaded | `true` | `$80` | `$00` | `[INSERT ASR-002 - ENTER]` |
| **Phase 5** | `t=72.79s` | User presses `ENTER` (`$23`) | `true` | `$D0` | `$10` | Motor ON, reads Block 2 (`ASR-002ID`) |
| **Phase 6** | `t=103.79s` | Remaining dependencies loaded | `true` | `$80` | `$00` | `[FILE LOADED           ]` |

[OBSERVED — Exp A]:
1. While waiting at `INSERT ASR-002 - ENTER`, the drive motor is off (`MSR=$80`, `IPCR=$00`).
2. Unloading Disk 1 and loading Disk 2 produces **zero firmware activity** and zero register changes while idle.
3. Media detection is triggered strictly by user confirmation (`ENTER`), which commands motor-on, verifies volume Block 2 at `$F89852`, and resumes IDMA.

---

### Experiment B: Unprompted Swap (Spontaneous Media Replacement)
- **Fixture:** Boot `V350.img` -> mount `AD-003.img` -> initialize directory -> unprompted swap to `AD-008.img`.
- **Trace target:** Spontaneous firmware reactions and directory cache invalidation.

| Phase | Time | Event | Floppy Exists | FDC MSR | DUART IPCR | Display |
|---|---:|---|:---:|:---:|:---:|---|
| **Initial** | `t=23.05s` | Stable LOAD mode on AD-003 | `true` | `$80` | `$00` | `[FILE 1  ROADS 2 BANK  ]` |
| **Unload** | `t=23.05s` | Unprompted `flop:unload()` | `false` | `$80` | `$00` | `[FILE 1  ROADS 2 BANK  ]` |
| **Empty Idle** | `t=23.55..25.05s` | 2.0s wait with no disk | `false` | `$80` | `$00` | `[FILE 1  ROADS 2 BANK  ]` (0 polls) |
| **Load** | `t=25.05s` | Unprompted `flop:load("AD-008")` | `true` | `$80` | `$00` | `[FILE 1  ROADS 2 BANK  ]` |
| **Loaded Idle** | `t=25.55..27.05s` | 2.0s wait with new disk | `true` | `$80` | `$00` | `[FILE 1  ROADS 2 BANK  ]` (0 auto-detect) |
| **Browse** | `t=27.63s` | User presses Down Arrow (`$0A`) | `true` | `$80` | `$00` | `[FILE 2  CLN GTR BANK  ]` |
| **Load Attempt** | `t=28.59..31.59s` | User presses `ENTER` twice | `true` | `$D0` | `$10` | Reads Block 27 on AD-008 -> `[BANK LOAD COMPLETED]` |

[OBSERVED — Exp B]:
1. Media removal and insertion while in LOAD mode produces **zero automatic reaction** in firmware. The display remains frozen on `[FILE 1  ROADS 2 BANK  ]`.
2. Pressing Down Arrow advances to `[FILE 2  CLN GTR BANK  ]`, proving that firmware continues to browse its in-RAM cached directory from `AD-003`.
3. Pressing `ENTER` commands the FDC to read start block 27 directly from the newly inserted disk (`AD-008.img`). Since block 27 on AD-008 is an effect block rather than a bank header, 0 slots are populated and firmware immediately exits to `[BANK LOAD COMPLETED]`.
4. Firmware has no autonomous directory cache invalidation mechanism for floppy media.

---

### Experiment C: Explicit Rescan (`CHANGE STORAGE DEVICE`)
- **Fixture:** Same starting state as Experiment B (swapped from `AD-003.img` to `AD-008.img`).
- **Trace target:** Verified user procedure to force storage re-enumeration.

| Step | Button Input | Display | Operational Effect |
|---|---|---|---|
| **Step 1** | `COMMAND` (`$06`) | `[CHANGE STORAGE DEVICE ]` | Enters Command menu |
| **Step 2** | `ENTER` (`$23`) | `[LOAD DEVICE=FLOPPY    ]` | Selects Change Storage Device |
| **Step 3** | `ENTER` (`$23`) | `[DISK COMMAND COMPLETED]` | Executes re-enumeration: spins up motor, reads Blocks 0..N, updates in-RAM directory |
| **Step 4** | `LOAD` (`$1A`) | `[FILE 1  AD-008 BANK   ]` | Browses newly loaded in-RAM directory of `AD-008` |

[OBSERVED — Exp C]:
1. Performing `CHANGE STORAGE DEVICE -> FLOPPY` forces firmware to execute its full disk re-scan routine.
2. The in-RAM directory cache is flushed and repopulated with `AD-008`'s entries.
3. Subsequent LOAD mode entry displays `[FILE 1  AD-008 BANK   ]`.

---

## 6. Comparison Table (Experiments A, B, C)

| Feature / Transition | Experiment A (Prompted Swap) | Experiment B (Unprompted Swap) | Experiment C (Forced Rescan) |
|---|:---:|:---:|:---:|
| **Physical image unload** | Observed | Observed | Observed |
| **Physical image load** | Observed | Observed | Observed |
| **Spontaneous FDC status change** | Not observed (`MSR=$80`) | Not observed (`MSR=$80`) | Not observed (`MSR=$80`) |
| **Spontaneous DUART IPCR change** | Not observed (`IPCR=$00`) | Not observed (`IPCR=$00`) | Not observed (`IPCR=$00`) |
| **Autonomous firmware polling** | Not observed | Not observed | Not observed |
| **Spontaneous directory invalidation** | Not observed | Not observed | Not observed (remains stale) |
| **Trigger mechanism** | `ENTER` on prompt | None (stale browse) | `CHANGE STORAGE DEVICE` |
| **FDC commands issued** | Motor ON, Read Block 2 | Motor ON, Read Block 27 | Motor ON, Read Blocks 0..N |
| **Volume verification at `$F89852`** | Executed (matched `ASR-002`) | Not executed (blind load) | Executed (read new root) |
| **Resulting display** | `[FILE LOADED]` | `[BANK LOAD COMPLETED]` | `[FILE 1  AD-008 BANK   ]` |

---

## 7. First Semantic Divergence Analysis

[VERIFIED — exercised paths]
No firmware-visible emulator semantic divergence was observed across the prompted swap, unprompted swap, and explicit rescan experiments.

Specifically:
- Unprompted replacement leaves the cached floppy directory intact.
- Subsequent browsing continues using cached directory metadata.
- Direct file access may therefore use old block metadata against the newly inserted medium (as observed in Experiment B when reading Block 27).
- `COMMAND -> SYSTEM -> CHANGE STORAGE DEVICE -> FLOPPY` causes a fresh media and directory scan.
- Prompted multi-volume BANK exchange is a separate firmware path: `ENTER` causes a fresh volume read and identity validation.

The retention of stale cached directory metadata during unprompted media replacement is a firmware behavioral characteristic, not an emulator defect.

---

## 8. Physical Evidence & Board Architecture

1. **NEC uPD72069 Register Interface**:
   - `[VERIFIED — MAME implementation]` The MAME `upd72069_device` / `upd72065_device` models only MSR (`$FC4001`) and FIFO (`$FC4003`), without a Digital Input Register (DIR) or host-visible DSKCHG bit.
2. **DUART SCN2681 Wiring**:
   - `[VERIFIED — OpenASR wiring]` DUART IP0 receives the modeled floppy INDEX callback (`idx_wr_callback()`), which pulses only during active spindle rotation.
   - `[OPEN — physical wiring]` Exact physical ASR-10 board netlist wiring for any media-change-related signal beyond the established INDEX path is not proven by this investigation.
3. **Firmware Contract**:
   - `[VERIFIED — V3.50 firmware consumer path]` The Musician's Manual warning `DISK HAS BEEN CHANGED` corresponds to error code `$049D = $09`, which is triggered exclusively by SCSI Sense Key `$06` / ASC `$28` in the analyzed ROM routine, not by the floppy drive.

---

## 9. Causal Conclusion

OUTCOME D — [VERIFIED — firmware policy, exercised path]

- V3.50 retains its in-RAM floppy directory across the exercised unprompted media replacement and does not autonomously rescan the newly inserted floppy.
- Explicit `CHANGE STORAGE DEVICE` initiates a fresh directory/media scan.
- During prompted multi-volume BANK loading, firmware already knows that another volume is required; user confirmation (`ENTER`) triggers a new media read and volume-identity validation.
- No emulator semantic defect was observed in these exercised paths.

[OPEN — physical precision]
This investigation does not establish every possible physical media-change signal or board-level wiring mechanism of a real ASR-10.

---

## 10. Emulator Code Changes

**NONE.** No emulator code changes were made or are justified.

---

## 11. Regression Verification

Because no production code was modified, the established baseline remains unaffected:
- Regression suite: 20/20 test checkpoints passed (`docs/asr10/regression-test.sh`).
- Authentic factory BANK acceptance: intact and frozen (`docs/asr10/investigations/authentic-factory-floppy-bank-load.md`).

---

## 12. Freeze & Remaining Open Questions

### Closure Statement
Floppy media-change semantics — **CLOSED** for the exercised V3.50 prompted-swap, unprompted-swap, and explicit-rescan workloads.

No production emulator change is warranted by this investigation.

Reopen only if:
- a new authentic workload shows a firmware-visible media-change failure,
- new physical board evidence contradicts the current functional model, or
- another firmware path exposes a distinct media-change semantic.

This is not a claim of complete physical board reconstruction.

### Remaining Open Points
- `[OPEN — physical board precision]` Exact physical board-level routing of floppy interface pin 34 (DSKCHG) on the physical ASR-10 mainboard remains unmeasured; however, V3.50 firmware exercises no consumer for such a signal in the evaluated floppy paths.

---

## 13. Git Status & Process Hygiene

- Untracked report file created: `docs/asr10/investigations/floppy-media-change-semantics.md`.
- No modified tracked files.
- Process hygiene: `pgrep -fl mame` clean; 0 orphan MAME processes.
