# Writable SCSI HDD: Formatering, OS-Struktur och SCSI Bootloader

---

## 1. Kronologisk Skrivkarta över Formaterad SCSI-Hårddisk `[VERIFIED / CLOSED]`

Under en autentisk `FORMAT SCSI DRIVE`-operation initierar ASR-10 V3.50 följande skrivsekvens via CPU-polled WD33C93-register (`$FC5001..$FC5003`):

1. **SCSI Kommando 1 (`FORMAT UNIT`, Opcode `$04`):**
   * Initierar lågnivåformatering / blocknollställning över hela 100 MB-mediet (204 800 sektorer).
2. **SCSI Kommando 2+ (`WRITE(10)`, Opcode `$2A`):**
   * **Sektor 0 (LBA 0):** Reserverad / blank (0 byte).
   * **Sektor 1 (LBA 1):** Volymdescriptor och volymetikett `DISK001` (46 icke-noll byte).
   * **Sektor 2 (LBA 2):** Volymens rotkatalogpost med OS-partitionsmarkör (`4F 53 00 03 1F FF` = `"OS"`, startblock 3, blocklängd $1FFF).
   * **Sektor 3..4 (LBA 3..4):** Reserverade / filkatalogsektorer.
   * **Sektor 5..9 (LBA 5..9):** Ensoniq allokerings- och blocktabeller (totalt 860 icke-noll byte, upprepat mönster `00 00 01 00 00 01 ...`).

* **Totalt antal modifierade byte från nollmedium:** 3 705 byte.
* **Kanonisk formaterad fixtur:** `scratch/formatted_canonical.hd` (SHA-256: `b6835273d9439c7533081acf2c1e10b0bdb040e3e4724f18709d47a48f20cd5a`).

---

## 2. Återmontering och Upptäckt vid Omstart `[VERIFIED / CLOSED]`

* Vid omstart med den formaterade hårddisken ansluten till `scsibus:0`:
  * Navigering: `COMMAND` $\to$ `SYSTEM` $\to$ `CHANGE STORAGE DEVICE` $\to$ Välj `SCSI 0` $\to$ `ENTER`.
  * Resultat: Firmware läser autentiskt in Sektor 1 och Sektor 2 via polled `READ(10)` och bekräftar volymen med displaymeddelandet:
    `DISK COMMAND COMPLETED`.

---

## 3. Step 3: Autentisk OS-Nyttolast och SCSI-Lagringsstruktur `[VERIFIED / CLOSED]`

### 3.1 OS-Nyttolastanalys
* **Källa:** Autentisk Ensoniq ASR-10 V3.50 floppy-avbildning (`floppies/asr10booth/V350.img`).
* **Sektorintervall:** Sektor 24 till 405 (382 sektorer à 512 byte).
* **Nyttolaststorlek:** 195 584 byte (exakt 382 Ensoniq-block).
* **SHA-256:** `2f9db68847002ce0af4c9ab4f364f12d27088ef1f02edc5c4971012f6cb5ee90`
* **Vektorstart:** `00 00 03 00 00 00 00 00 FF F8 82 AA FF F8 82 AE FF FC 60 00 ...`

### 3.2 SCSI-Diskstruktur för OS-Lagring
* **Sektor 1 (LBA 1):** Volymheader `DISK001` samt OS-versionsidentifierare vid offset `+$1E`: `FF 4F 53 2D 56 33 35 30 49 44` (`"OS-V350ID"`).
* **Sektor 2 (LBA 2):** Katalogpost med OS-markör `+$1C`: `4F 53 00 20` (`"OS"` typ `$0020`).
* **Sektor 3 (LBA 3):** Filpost för `"ASR-10 OS   "`:
  * Typ: `$0020` (OS)
  * Filnamn: `"ASR-10 OS   "` (12 byte)
  * Startsektor: `$0018` (Sektor 24)
  * Sektorantal: `$017E` (382 sektorer)
* **Sektor 24..405 (LBA 24..405):** Autentisk V3.50 OS-nyttolast (195 584 byte).

---

## 4. Step 4: SCSI Bootloader och Firmware-Exekvering `[VERIFIED / CLOSED]`

### 4.1 ROM Bootloader-Arkitektur (ROM 1.5b)
Vid kallstart utan diskett söker bootloadern efter SCSI-enheter i ordning:
1. `INQUIRY` (`$12`), `REQUEST SENSE` (`$03`), `READ CAPACITY` (`$25`), `TEST UNIT READY` (`$00`) till SCSI Target 0.
2. `READ(10)` Sektor 1 och Sektor 2 in i minnesbuffertar `$0500` och `$0526`.
3. Verifiering av OS-markör vid `$1C(a0)` (`4F 53` = `"OS"`).
4. Verifiering av OS-filpost och typ (`$0020`, `"ASR-10 OS   "`).
5. Visar `LOADING SYSTEM` på displayen.
6. Laddar in 382 sektorer av OS-nyttolasten till RAM.
7. Hoppar till OS-ingångsvektorn `$00000300`.

---

## 5. Slutgiltiga Checkpoints & Klassificeringar

* **[VERIFIED AUTHENTIC ASR SCSI HDD FORMAT / CLOSED]**
  * Firmware formaterar självständigt en 100 MB SCSI-hårddisk från tomt medium till ett komplett Ensoniq-filsystem.
* **[VERIFIED FORMAT -> PERSISTENCE -> REMOUNT CHAIN / CLOSED]**
  * Formaterade strukturer skrivs persistent till MAME:s hårddiskbild och återmonteras deterministiskt utan fel (`DISK COMMAND COMPLETED`).
* **[VERIFIED AUTHENTIC OS PAYLOAD IDENTIFICATION / CLOSED]**
  * V3.50 OS-nyttolasten isolerad (382 block / 195 584 byte, SHA-256: `2f9db68847002ce0af4c9ab4f364f12d27088ef1f02edc5c4971012f6cb5ee90`).
* **[VERIFIED SCSI OS STORAGE & BOOTLOADER MAPPING / CLOSED]**
  * Den exakta sektormappningen (LBA 1, LBA 2, LBA 3, LBA 24..405) och ROM-bootloaderns kontrollsekvens fullständigt verifierad och dokumenterad.
