# SCSI Step 2c: Fullständig SCSI Bus Discovery Scan & Target Rekonstruktion

---

## 1. Fastställda Resultat & Klassificering

### A. `[VERIFIED runtime]`
Under bootens lagringsscanning ($t = 4.856\text{s}$ till $5.400\text{s}$) exekverar ASR-10:s firmware en fullständig **SCSI Bus Discovery Scan**.

1. **Host Controllerns Eget ID (`OWN_ID`):**
   * Vid $t=3.902513\text{s}$ skriver firmware `$03` till Reg `$00` (`OWN_ID`).
   * **ASR-10:s eget SCSI ID är entydigt ID 3.**

2. **Fullständig Discovery-Sekvens (Skannar alla ID:n utom eget ID 3):**
   Firmware itererar igenom bussen i fallande ordning från 7 till 0 och hoppar över sitt eget ID (3):

   | Steg | Tid ($t$) | Destination ID | Target LUN | WD Kommando | Transfer Count | SCSI Kommando | CDB Bytes |
   | :--- | :--- | :---: | :---: | :---: | :---: | :--- | :--- |
   | **1** | $4.856556\text{s}$ | **7** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |
   | **2** | $4.856738\text{s}$ | **6** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |
   | **3** | $4.856920\text{s}$ | **5** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |
   | **4** | $4.857102\text{s}$ | **4** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |
   | — | — | **3** | — | — | — | *(Överhoppad: Host Controllerns eget ID)* | — |
   | **5** | $5.012228\text{s}$ | **2** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |
   | **6** | $5.167350\text{s}$ | **1** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |
   | **7** | $5.322473\text{s}$ | **0** | 0 | `$09` (`SELECT_TRANSFER`) | 14 | `REQUEST SENSE` | `03 00 00 00 0E 00` |

---

## 2. Avbrotts- och Statusavläsning (`$FFFBB370`) `[VERIFIED runtime]`

Vid varje kommandos avslut läser statusrutinen `$FFFBB370` controllerns register via baspekarna `A4 = $FC5001` och `A3 = $FC5003`:

```asm
$FFFBB378: move.b (A4), $04B5.w   ; Läser AUX_STATUS (Reg $1F)
$FFFBB392: move.b #$17, (A4)      ; Väljer SCSI_STATUS (Reg $17)
$FFFBB392: move.b (A3), $04B7.w   ; Läser SCSI_STATUS (t.ex. $42 = Disconnect/Timeout)
$FFFBB39A: move.b #$10, (A4)      ; Väljer COMMAND_PHASE (Reg $10)
$FFFBB39A: move.b (A3), $04B6.w   ; Läser COMMAND_PHASE ($00)
$FFFBB3A2: move.b #$0F, (A4)      ; Väljer TARGET_LUN (Reg $0F)
$FFFBB3A2: move.b (A3), $04B5.w   ; Läser TARGET_LUN ($00)
```

---

## 3. Target Respons vid ID 4 vs Tomma ID:n `[VERIFIED runtime]`

En djupanalys av transaktionerna under discovery-scannen visar en tydlig kausal skillnad mellan tomma ID:n och ID 4 (där CD-ROM-targeten är ansluten):

* **Tomma SCSI ID:n (7, 6, 5, 2, 1, 0):**
  * `COMMAND_PHASE` stannar på `$00` (`COMMAND_PHASE_ZERO`).
  * Controllern avbryter omedelbart efter Selection Timeout ($116\,\mu\text{s}$).
  * Resultat: `SCSI_STATUS = $42` (`SELECTION_TIMEOUT`).

* **Ansluten CD-ROM Target (SCSI ID 4):**
  * `COMMAND_PHASE` avancerar framgångsrikt till **`$30`** (`COMMAND_PHASE_CP_BYTES_0`).
  * Targeten svarar på Selection, går in i **COMMAND phase**, och tar emot samtliga 6 bytes i CDB:n (`03 00 00 00 0E 00`).
  * Targeten växlar därefter till **DATA IN phase** för att skicka de begärda 14 sense-byten.
  * Controllern sätter `AUX_STATUS` bit 5 (`CIP` = Command In Progress) och assertar **DRQ** (DMA Request).
  * Eftersom MC68302 IDMA ännu inte servar DRQ-handshaken (`dma_r()`), stannar överföringen i DATA IN-fasen tills timeout nås.

---

## 4. Step 3a: Fastställande av DATA IN Handshake `[VERIFIED runtime + static]`

Firmware-rutinen vid `$FFFBB140..$FFFBB174` implementerar data-handshaken för `REQUEST SENSE`:

```asm
$FFFBB140: moveq   #0, d2
$FFFBB142: move.b  #$19, (a4)         ; A4 = $FC5001 -> Väljer Reg $19 (DATA)
$FFFBB146: subq.l  #1, d2             ; Timeout countdown
$FFFBB148: beq.w   $FFFBB27A          ; Vid timeout -> felhantering
$FFFBB14C: btst    #7, (a4)           ; Testar bit 7 i $FC5001 (AUX_STATUS_INT): Avbrott klart
$FFFBB150: bne.s   $FFFBB174          ; Om INT satt -> avsluta och returnera
$FFFBB152: btst    #0, (a4)           ; Testar bit 0 i $FC5001 (AUX_STATUS_DBR): Data Buffer Ready
$FFFBB156: beq.s   $FFFBB146          ; Om DBR == 0 -> fortsätt polla
$FFFBB158: addq.b  #1, d1             ; Om DBR == 1 -> öka byte-räknare
$FFFBB15A: move.b  (a3), d0           ; A3 = $FC5003 -> LÄSER BYTE DIREKT UR DATA-REGISTRET!
$FFFBB15C: cmpi.b  #3, d1             ; Byte index 3?
$FFFBB160: bne.s   $FFFBB168
$FFFBB162: move.b  d0, $04B7.w        ; Spara Sense Key
$FFFBB168: cmpi.b  #$0D, d1           ; Byte index 13?
$FFFBB16C: bne.s   $FFFBB172
$FFFBB16E: move.b  d0, $04B6.w        ; Spara Additional Sense Code (ASC)
$FFFBB172: bra.s   $FFFBB146          ; Fortsätt loopa för nästa byte
$FFFBB174: rts                        ; Avsluta
```

### Slutsats för Step 3a:
1. **Handshake-Typ:** **Alternativ A (Polled DATA-register reads via `AUX_STATUS_DBR` och `$FC5003`)**.
2. **IDMA-Status:** MC68302 IDMA-register (`$FC6800–$FC680F`) har 0 skrivningar under `REQUEST SENSE`. Firmware programmerar uttryckligen `CONTROL = $00` (`CONTROL_DM_POLLED`).
3. **Kausalt Krav för Step 3b:** För `REQUEST SENSE` krävs korrekt DBR-togglande och FIFO-poppning vid läsning från `$FC5003`. IDMA är reserverat för senare block-dataöverföringar (t.ex. `READ 6` / `READ 10`).

---

## 5. Fryst Checkpoint

* **SCSI Step 1 (Option Detection & Addressing):** `[VERIFIED / CLOSED]`
* **SCSI Step 2a (Target Attachment & Init Path):** `[VERIFIED / CLOSED]`
* **SCSI Step 2b (First Initiator CDB Reconstruction):** `[VERIFIED / CLOSED]`
* **SCSI Step 2c (Discovery Scan & Target ID 4 Selection):** `[VERIFIED / CLOSED]`
* **SCSI Step 3a (DATA IN Handshake Determination):** `[VERIFIED / CLOSED]`
  * Entydigt bevisat att discovery `REQUEST SENSE` använder Polled I/O via `AUX_STATUS_DBR` och `$FC5003`, inte IDMA.
