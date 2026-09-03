# Generic MAME Fix: NEC uPD72069 Standby Auxcmd ($35/$34) and ASR-10 CD-ROM File Load

## Kontext och problem

När ASR-10 bootades från hårddisk (SCSI ID 0) och lagringsenhet växlades till autentisk Ensoniq CDR-1 CD-ROM (SCSI ID 4), fungerade katalogbläddring och katalogläsning korrekt. Däremot misslyckades inläsning av filer (t.ex. Bank-filen `ORCH STRNGS1`) omedelbart med:

```text
FILE OPERATION ERROR
```

Inga SCSI-kommandon skickades till CD-ROM-enheten under laddningsförsöket trots att katalogen tidigare lästs framgångsrikt via SCSI.

---

## Kausal rotorsak

ASR-10-firmware anropar `prepare_device_for_io` (`$013398`) före varje lagringsoperation. Vid access till SCSI-bussen försätts diskettkontrollern (NEC uPD72069 vid `$FC4000..$FC4003`) i standby-läge genom att skriva auxcmd `$35` (*set standby*) till FDC auxcmd-registret (`$FC4001`).

Därefter kontrollerar firmware kontrollerns Main Status Register (MSR, `$FC4001` read) i en vänteloop (`$FFFB8D1E`):

```m68k
FFFB8D1E: 1038 4001        MOVE.B ($FC4001), D0
FFFB8D22: 0800 0004        BTST #4, D0             ; Test bit 4 (MSR_CB - Command Busy)
FFFB8D26: 6712             BEQ.S $FFFB8D3A         ; Fortsätt om FDC inte är upptagen
FFFB8D28: 5381             SUBQ.L #1, D1           ; Timeout-räknare ($1F40 = 8000 varv)
FFFB8D2A: 66F2             BNE.S $FFFB8D1E
FFFB8D2C: 11FC 000D 049D   MOVE.B #$0D, ($049D)    ; FILE OPERATION ERROR
FFFB8D32: 11FC 0020 04AE   MOVE.B #$20, ($04AE)    ; FDC busy timeout flagga
```

I upstream MAME [`src/devices/machine/upd765.cpp`](file:///Users/paroberg/develop/mame-upstream/src/devices/machine/upd765.cpp) (introducerat i commit `93ed4a942bfa`, 2025-10-25) grupperades `case 0x35:` (*set standby*) och `case 0x34:` (*reset standby*) under en `switch`-gren som satte:

```cpp
main_phase = PHASE_RESULT;
result[0] = ST0_UNK;
result_pos = 1;
```

Detta fick `msr_r()` att permanent rapportera:

$$\text{MSR} = \$D0 = \text{MSR\_RQM} \mid \text{MSR\_DIO} \mid \text{MSR\_CB}$$

Eftersom bit 4 (`MSR_CB`, Command Busy) var satt och FIFO inte lästes, loopade `$FFFB8D1E` tills räknaren nådde 0 och avbröt laddningen med `$049D = $0D` och `$04AE = $20`. FDC anropades aldrig mer och noll SCSI READ-kommandon nådde WD33C93.

---

## Korrigering i `src/devices/machine/upd765.cpp`

Auxcmd `$35` (*set standby*) och `$34` (*reset standby*) returnerar inte något resultat i FIFO enligt NEC:s hårdvaruspecifikation för uPD72065/72069. De ska inte försätta enheten i `PHASE_RESULT`.

I [`upd72069_device::auxcmd_w`](file:///Users/paroberg/develop/mame-upstream/src/devices/machine/upd765.cpp#L3460-L3478) separerades dessa kommandon och delegerades till basklassen:

```diff
--- a/src/devices/machine/upd765.cpp
+++ b/src/devices/machine/upd765.cpp
@@ -3461,12 +3461,14 @@ void upd72069_device::auxcmd_w(uint8_t data)
 		result[0] = ST0_UNK;
 		result_pos = 1;
 		break;
+	case 0x35: // set standby
+	case 0x34: // reset standby
+		upd72065_device::auxcmd_w(data);
+		break;
 	case 0xc3: case 0xd3: case 0xe3: case 0xf3: // precompensation (72069 exclusive)
 	case 0x4f: // select IBM format (select 77 tracks on some other 7206x variants)
 	case 0x5f: // select ECMA/ISO format (select 255 tracks on some other 7206x variants)
-	case 0x35: // set standby
 	case 0x47: // start clock
-	case 0x34: // reset standby
 	case 0x33: // enable external mode
 	case 0x80: // Not a valid auxcmd, but the Akai S3000 sends it and expects an ACK.
 		main_phase = PHASE_RESULT;
```

Ingen ändring gjordes i ASR-10-drivern (`asr10_boot.cpp`).

---

## Verifiering

### 1. Kausal A/B-verifiering

| Steg | Före fix | Efter fix |
| :--- | :--- | :--- |
| **FDC MSR efter `$35`** | `$D0` permanent (`MSR_CB` satt) | **`$80`** (`MSR_CB` = 0, idle) |
| **`prepare_device_for_io`** | Timeout efter 8 000 iterationer | **Passerar omedelbart (0 iterationer)** |
| **Felkoder i RAM** | `$049D=$0D`, `$04AE=$20` | **`$049D=$00`**, **`$04AE=$00`** |
| **Första SCSI READ** | 0 kommandon | **`LBA $0000C1C9`** (49 609) |
| **Läst fil-extent** | Inget | **`$0000C1C9` .. `$0000C58B`** (963 block) |
| **Display** | `FILE OPERATION ERROR` | **`FILE LOADED`** |

### 2. Ljud- och syntesverifiering (`ORCH STRNGS1`)

Fullständigt inläsnings- och uppspelningstest kördes från CD-ROM via SCSI ID 4:
- **Sample-RAM-skrivningar:** 524 290 bytes överförda till vågtabellminne.
- **ES5506-röstprogrammering:** `voice_delta = 4800` registerändringar efter instrumentval (`ORCH STRNGS1 ?OLUME?99`).
- **MIDI Note-On:** Mottogs via DUART RHRA (`rhra_delta = 3`).
- **Ljudcapture (`-wavwrite`):**
  - Toppamplitud: **11 057** / 32 767 (långt över brusgolv).
  - Grundton: **262.3 Hz** (nominellt Middle C 261.6 Hz, avvikelse < 0.3%).

### 3. Full regression

Hela acceptanssviten ([`docs/asr10/regression-test.sh`](file:///Users/paroberg/develop/mame-upstream/docs/asr10/regression-test.sh)) kördes och passerade samtliga 21 deltester utan anmärkning:

```text
PASS boot display="FILE 1  TUT0RIAL BNK  "
PASS display display="FILE 1  TUT0RIAL BNK  "
PASS button display="FILE 2  JM DIGI 5YN   "
PASS button_upper button=23 rhrb_delta=2
PASS nodisk display="  PLEA5E IN5ERT DI5K  "
PASS file_loaded display="FILE L0ADED           " idma_bytes=172544 arms=21
PASS mc68302_guards display="FILE L0ADED           " alarms=0
PASS note_audio rhra=3 voice_writes=2400
PASS note_audio_wav peak=3838 freq=262.3Hz
PASS audio_rate_mode A=00 B=01 A2=00
PASS note_audio_wav peak=10797 freq=262.3Hz
PASS note_audio_wav peak=18369 freq=260.9Hz
PASS note_audio_wav peak=11968 freq=260.9Hz
PASS interrupt_controller iack_4d=1 iack_4b=1 imr=E480->EC80->E480 witness=9595
PASS memory_size size=00200000 base=00600000
PASS stereo_round_trip left_bytes=794 right_bytes=794 iack4b=2 witness=1488039
PASS display_protocol underline_left=17-21 underline_right=17-21 lamp_ok=true
PASS panel_input hold_ok=true primary_wire=05,09,1A nav_mode=0->1
PASS panel_navigation left_and_right_move_underline_correctly
PASS display_field_rewrite bar_roundtrip=true tempo=90->91->92->91->90 anchor=6 underline=6-8 trailing=false
PASS regression
```
