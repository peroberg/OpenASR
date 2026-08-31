# Ensoniq ASR-10 / AGG / OTIS / ESP — MAME Upstream Code Review & Audit
**Datum:** 2026-08-31  
**Status:** Audit genomförd på branch `asr10-architecture-code-review` (utgångspunkt: `8e6369b40c8`)  
**Omfattning:** `src/mame/ensoniq/asr10_boot.cpp`, `src/mame/ensoniq/esqasr.cpp`, `src/mame/ensoniq/esqpanel.*`, `src/devices/sound/esqpump.*`, `src/devices/sound/es5506.*`, `src/devices/cpu/es5510/*`

---

## Sammanfattning

Detta dokument utgör den reviderade tekniska granskningen och audit-rapporten för **Ensoniq ASR-10**-emuleringen i MAME. Ursprungliga hypotetiska granskningspunkter har prövats mot projektets dokumenterade evidens (`docs/asr10/`), MAME-enheternas faktiska implementationer samt regressionssviten (`docs/asr10/regression-test.sh`).

Rapporten skiljer strikt mellan:
- Verifierade och åtgärdade defekter (**[CONFIRMED / FIXED]**)
- Funktionella interim-modeller och känd arkitektonisk skuld (**[VALID DEBT / INTERIM MODEL]**)
- Falsifierade eller trasiga förslag (**[REJECTED]**)
- Oavgjorda hårdvarufrågor (**[OPEN]**)

---

## 1. Status per Granskningsområde

### 1.1 Savestate-hygien i ljud- och panelenheter
- **`esq_5505_5510_pump_device` (`src/devices/sound/esqpump.cpp`):**
  - **Status:** **[CONFIRMED / FIXED]**
  - **Analys:** Medlemsvariablerna `m_esp_halted` och `m_serial_route` ändras dynamiskt under körning men saknade tidigare `save_item`-registrering. Vid savestate-laddning kunde detta leda till desynk i ESP-exekveringen och felaktig ljudrouting.
  - **Åtgärd:** `save_item(NAME(m_esp_halted))` och `save_item(NAME(m_serial_route))` har lagts till i `device_start()`.
- **`esqpanel_device` (`src/mame/ensoniq/esqpanel.cpp`):**
  - **Status:** **[CONFIRMED / FIXED]**
  - **Analys:** Basklassen `esqpanel_device` sparade inte sin sändningskö eller interna protokollflaggor (`m_xmitring`, `m_xmit_read`, `m_xmit_write`, `m_tx_busy`, `m_light_states`, `m_expect_calibration_second_byte`, `m_expect_light_second_byte`, `m_xmit_overflow_count`).
  - **Åtgärd:** Samtliga fält har registrerats med `save_item` i `esqpanel_device::device_start()`.

---

### 1.2 Hot-Path Profiling och Debug-utskrifter i Ljudloopen
- **Status:** **[CONFIRMED / FIXED]**
- **Källkod:** `src/devices/sound/esqpump.cpp:101-109`
- **Analys:** `sound_stream_update` anropade tidigare `osd_ticks()` två gånger *per sample* runt `m_esp->run_once()` även i normal drift, vilket genererade tiotusentals onödiga systemanrop per sekund under aktiv ljudgenerering.
- **Åtgärd:** Profilingen har kapslats in bakom det befintliga makrot `#if PUMP_TRACK_SAMPLES` (som är satt till 0 som standard), vilket eliminerar overhead i normal drift utan att ta bort mätmöjligheten vid utveckling.

---

### 1.3 Klockval, ACTV och Samplingsfrekvensväxling
- **Status:**
  - `$0CE2` runtime rate-policy: **[VALID DEBT / INTERIM MODEL]**
  - ACTV som ensam direkt MAME clock-switch-policy: **[REJECTED]**
  - Fysisk ASR-10 klockrutning & Super-GLU/ES5701 clock-mux: **[OPEN]**
- **Analys & Epistemisk korrigering:**
  - Firmware i ASR-10 skriver till ES5506:s **ACTV-register (`$FC205E/5F`)** med `$1F` (32 röstslottar, läge 0 / ROM HALL) och `$17` (24 röstslottar, läge 1 / 44LUSH). ACTV styr ES5506:s active-slot count och därmed den generiska ES5506-modellens stream rate.
  - Experimenten i `docs/asr10/investigations/audio-rate-y2-y3-synthetic-policy-v350.md` och `actv-source-and-rate-control-v350.md` falsifierade dock ACTV som ensam direkt MAME clock-switch-policy för kretsens masterklocka.
  - Den nuvarande `$0CE2`-drivna policyn (`apply_effect_audio_rate_policy`) är en fungerande funktionell interim-modell som klarar den verifierade A/B/A-acceptansen (`262.3 -> 260.9 -> 260.9 Hz`).
  - **Regel för ersättning:** Interim-modellen kräver inte absolut fysisk PCB-verifikation för att ersättas i framtiden. En bättre funktionell modell med stark firmware/runtime-evidens och bibehållen regression får ersätta den, men den får inte ersättas av en redan motbevisad ACTV-hypotes.

---

### 1.4 ESP Quiescence, Program Gate & HALT-styrning
- **Status:**
  - 10 ms quiescence & instruktionsgate i `pump_release()`: **[VALID DEBT / INTERIM MODEL]**
  - Omedelbar radering av säkerhetsgaten: **[REJECTED]**
  - Fysisk HALT/RUN-signalering & timing: **[OPEN]**
- **Analys:**
  - 10 ms post-upload quiescence och instruktionskontrollen i `pump_release()` utgör en nuvarande fungerande funktionell säkerhetsbarriär – **inte** verifierad eller nödvändig hårdvarutiming. Den förhindrar att `run_once()` körs med ofullständig mikrokod under pågående uppladdning.
  - Att radera säkerhetsgaten utan en etablerad alternativ livscykelmodell gör ESP-exekveringen instabil.
  - Gaten kvarstår som interim-modell tills en verifierad livscykel- och signaleringsmodell (med fullt godkänd regression) kan ersätta den.

---

### 1.5 Minnesavbildning och Spegling (`mem_map`)
- **Status:**
  - Nuvarande `system_ram_alias_r/w` modulo-dispatch: **[VALID DEBT / INTERIM MODEL]**
  - Föreslaget byte till `.mirror(0xc00000)`: **[REJECTED]**
- **Analys:**
  - Den föreslagna `.mirror(0xc00000)`-ändringen var defekt: den speglade enbart `:asr10_sample_ram` och missade den hybridiserade `m_lowmem_shadow` (1 MB lowmem) som firmware kräver enligt `memory-size-belief-analysis.md`. Dessutom skapade den odefinierade krockar med CS0–CS3.
  - Den nuvarande modulo-hanteringen bevarar systemets faktiska wrap- och aliasing-beteende deterministiskt.

---

### 1.6 SCSI-kontroller (WD33C93 / AM33C93A) & DMA
- **Status:**
  - Nuvarande `$FC5000–$FC501F` stub: **[VALID DEBT / INTERIM MODEL]**
  - Spekulativ hårdkodning av IDMA- och IRQ-linjer: **[REJECTED]**
  - Fysisk SP-3 SCSI-topologi & koppling: **[OPEN]**
- **Analys:**
  - SCSI-fönstret är identifierat på `$FC5001` (SAS) och `$FC5003` (SCMD) i CS3.
  - Spekulativ koppling av IDMA-kanaler och specifika avbrottslinjer utan empirisk runtime-evidens är avvisad. SCSI utvecklas som en separat funktionell enhet när evidensunderlag föreligger.

---

### 1.7 Miljövariabler och C++ Ägandestruktur i Panelen
- **Status:** **[VALID DEBT / INTERIM MODEL]**
- **Analys:**
  - `std::getenv("ASR10_PANEL_DISABLE_ECHO")` i `asr10panel_device::device_reset()` är en utvecklingsflagga som ska fasas ut när panelprotokollet är fullständigt avkodat.
  - Rå pekare för `m_external_panel_server` i `esqpanel_device` är ärvd upstream-skuld som inte rörs i detta steg för att inte introducera regressioner i delade Ensoniq-drivrutiner.

---

## 2. Sammanfattande Klassificeringstabell

| Område | Källkodsplats | Status | Motivering / Åtgärd |
|---|---|---|---|
| **Pump Savestates** | `esqpump.cpp` | **[CONFIRMED / FIXED]** | `m_esp_halted` och `m_serial_route` sparade via `save_item`. |
| **Panel Savestates** | `esqpanel.cpp` | **[CONFIRMED / FIXED]** | Sändningskö och flaggor sparade via `save_item`. |
| **Ljudprofiling** | `esqpump.cpp` | **[CONFIRMED / FIXED]** | `osd_ticks()` gated bakom `#if PUMP_TRACK_SAMPLES`. |
| **$0CE2 Rate-Policy** | `asr10_boot.cpp` | **[VALID DEBT / INTERIM MODEL]** | Bevaras tills en bättre verifierad modell finns. |
| **ESP Program Gate** | `asr10_boot.cpp` | **[VALID DEBT / INTERIM MODEL]** | Fungerande säkerhetsbarriär under mikrokodsuppladdning. |
| **RAM Modulo-Wrap** | `asr10_boot.cpp` | **[VALID DEBT / INTERIM MODEL]** | Bevaras; `.mirror()`-förslaget var felaktigt. |
| **SCSI Stub** | `asr10_boot.cpp` | **[VALID DEBT / INTERIM MODEL]** | Bevaras; spekulativ wiring avvisad. |
| **Panel getenv** | `esqpanel.cpp` | **[VALID DEBT / INTERIM MODEL]** | Utvecklingsflagga under protokollutredning. |
| **ACTV Clock-Proxy** | `asr10_boot.cpp` | **[REJECTED]** | Falsifierat som ensam direkt klockväljare. |
| **Fysisk Klockmux** | Maskinarkitektur | **[OPEN]** | Fysisk Y2/Y3-rutning till ES5506 är öppen. |
| **Fysisk ESP HALT** | Maskinarkitektur | **[OPEN]** | Fysisk hårdvarusignal för ESP HALT är öppen. |
| **SCSI DMA/IRQ** | SP-3 Option | **[OPEN]** | SP-3 busskoppling är öppen. |

---

## 3. Regressionsstatus

Körning av den fullständiga acceptanstestsviten ([`docs/asr10/regression-test.sh`](file:///Users/paroberg/develop/mame-upstream/docs/asr10/regression-test.sh)) efter genomförda fixar:

- **Resultat:** `PASS regression`
- Samtliga ingående deltester (boot, display, panelinput, nodisk, instrumentladdning, MC68302-guards, audio-generering, A/B/A rate-mode, avbrottshanterare, minnesstorleksdetektering, stereo round-trip och displayprotokoll) exekverar och passerar utan fel eller prestandaförluster.
