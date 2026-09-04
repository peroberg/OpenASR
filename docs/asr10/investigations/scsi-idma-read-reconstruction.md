# CD-ROM Fillsystem, Bankladdning, Sample-RAM och ES5506 Röstkedja: Slutrapport

---

## 1. Sammanfattning av den Fullständiga Kedjan `[FUNCTIONALLY CLOSED]`

Hela den autentiska lagrings- och uppspelningskedjan från CD-ROM till hörbart ljud via ES5506 OTIS är bevisad och verifierad end-to-end:

* **CD-ROM (CDR-1):**
  * Katalogtraversering: `Root` (`$03`) $\to$ `STRINGED` (`$100D`) $\to$ `STRING SECTN` (`$1025`)
* **Katalogpost (Typ $03 Bank):**
  * Bank-Header vid Start-LBA (`$C1C9` för `ORCH STRNGS1`, `$12214` för `TRMLO STRNGS`)
* **Parameterblock (Lager- och Wavesample-deskriptorer):**
  * Avläsning av multizoner, ord-offsetter, tangentbordsgränser och röstparametrar
* **Sample Payload (16-bitars PCM):**
  * `READ(10)` via polled WD33C93 DATA-register (`$FC5003`)
* **Sample-RAM (`$00600000..`):**
  * Rak, linjär byte-bevarande överföring
* **ES5506 Röstregister (`$00FC2000..`):**
  * Deskriptorns ord-offsetter skrivs till `START`/`END`/`FC` i DRAM Bank 0
* **ES5506 Röstmotor & Hörbart Ljud:**
  * Autentisk Sample-RAM-data renderas av ES5506-rösten till icke-tyst ljud verifierat via MAME `-wavwrite`.

---

## 2. Oberoende Bank-Validering (`TRMLO STRNGS`) `[VERIFIED / CLOSED]`

Strukturen och överföringsmodellen bekräftades generellt på en andra, oberoende Bank från en annan del av CDR-1 med distinkt storlek och layout:

* **Strukturjämförelse:**
  * `ORCH STRNGS1`: Totalt 963 block (11 parameterblock + 952 sample-block, start-LBA `$C1C9`)
  * `TRMLO STRNGS`: Totalt 537 block (17 parameterblock + 520 sample-block, start-LBA `$12214`)
* **TRMLO STRNGS Exakta Fält:**
  * Start-LBA: `$00012214` (537 block, `$0219`)
  * Bank-Header: Block 0 vid LBA `$00012214`
  * Parameterblock: 17 block (LBA `$00012214..$00012224`)
  * Sample Payload: 520 block (`$0208`, LBA `$00012225..$0001242C`)
    * *Första 16 byte vid LBA `$00012225`:* `0B 00 0B 00 07 00 07 00 01 00 F7 00 EE 00 EC 00`

---

## 3. SCSI CD Bank Transport & IDMA/DACK Status

* **SCSI CD Bank Transport `[VERIFIED / CLOSED]`:**
  * Den observerade autentiska CD Bank-laddningen och sampleöverföringen sker deterministiskt via CPU-polled WD33C93 DATA-register (`$FC5003`).
* **MC68302 SCSI IDMA/DACK Körstatus `[OPEN utanför denna väg]`:**
  * Andra SCSI-operationer (t.ex. specifik streaming till/från hårddisk eller sampling) kan använda firmwarens strukturellt identifierade IDMA/DACK-väg (`$FC5801`).

---

## 4. OpenASR Övergripande Statuskarta & Frysta Baselines

| Delområde | Status | Detalj |
|---|---|---|
| **ES5510 Functional Post-Effect Audio** | `FROZEN` | Ljudbearbetning och effektkedja |
| **Audio-Rate Policy** | `FROZEN` | Rate switching A/B/A2 |
| **MC68302 Required Functionality** | `CLOSED` | SIM, timers, IRQ-kontroller, SCC |
| **Sequencer Playback Chain** | `FROZEN` | Sequencer-klocka, transport, eventuppspelning |
| **CD-ROM Transport** | `CLOSED` | SCSI-2 READ(10), sense/inquiry, TOC-access |
| **CD Filesystem Hierarchy** | `CLOSED` | 26-bytes poster, `$0416` $\to$ `$041E` traversering |
| **Bank Loading** | `CLOSED` | Indexmodell, `$04B0` SCSI-kontext, `$0486` extent bounds |
| **Bank $\to$ Sample-RAM** | `CLOSED` | Byte-exakt payloadöverföring till `$00600000..` |
| **Wavesample Addressing** | `CLOSED` | Deskriptorers delintervall (subranges) inom payload |
| **Bank $\to$ ES5506 Voice** | `CLOSED` | `START`/`END`/`FC` registrering via DRAM Bank 0 |
| **CD Bank $\to$ Sample-RAM $\to$ ES5506 $\to$ Audio** | `FROZEN` | Verifierad icke-tyst ljudsignal via `-wavwrite` |
| **SCSI IDMA/DACK (andra vägar)** | `OPEN` | Identifierad i hårdvara/firmware men ej krävd för CD Bank |
| **Writable SCSI HDD** | `FUTURE` | Formatering, filallokering, skriv-CDBs |
| **SAVE / Persistens** | `FUTURE` | Skrivning av Bank/Instrument till media |
| **Physical Board Glue** | `OPEN` | Specifika hårdvarubeteenden vid behov |

---

## 5. Slutgiltiga Checkpoints

* **[VERIFIED CD DIRECTORY RECORD CANONICAL FIELD LAYOUT / CLOSED]**
* **[VERIFIED CD CURRENT-DIRECTORY STATE MODEL / CLOSED]**
* **[VERIFIED CD CHILD-DIRECTORY STATE TRANSITION MECHANISM / CLOSED]**
* **[VERIFIED BANK SELECTION INDEX MODEL / CLOSED]**
* **[VERIFIED $0486 EXTENT BLOCK BOUNDS CHECK / CLOSED]**
* **[VERIFIED $04B0 8-BIT STORAGE CONTEXT MODEL / CLOSED]**
* **[VERIFIED CD BANK FIRST-BLOCK TRANSFER / CLOSED]**
* **[VERIFIED BANK SAMPLE PAYLOAD RANGE / CLOSED]**
* **[VERIFIED WAVESAMPLE DESCRIPTOR -> SAMPLE-RAM RANGE / CLOSED]**
* **[VERIFIED WAVESAMPLE -> ES5506 VOICE ADDRESS CHAIN / CLOSED]**
* **[VERIFIED AUTHENTIC CD BANK PRODUCES AUDIO / CLOSED]**
* **[VERIFIED / FUNCTIONALLY CLOSED] Authentic Ensoniq CD-ROM Bank loading end-to-end.**
