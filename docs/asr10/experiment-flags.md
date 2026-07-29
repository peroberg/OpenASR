# Inventering av ASR10_EXPERIMENT_*/ASR10_DIAG_*-flaggor

2026-07-29. Ren läsning — inget kört, inget byggt, ingen kod ändrad.
Läst: `PLAN.md` (avsnitt 5, fas 3), `CLAUDE.md`, `disk-read-path.md`,
`fdc-map.md`. Källa: `src/mame/ensoniq/asr10_boot.cpp` på commit
`7bc57b8ab45` (efter DUART-omskrivningen).

## Metod och en avvikelse från uppdraget

`[Verified]`. Uppdraget angav 58 distinkta flaggor. Jag hittade **45**
faktiskt deklarerade flaggor/konstanter (22 miljövariabelstyrda via
`std::getenv("ASR10_...")`, 23 kompileringstids-`static constexpr`).
Skillnaden: en textmönstersökning efter `ASR10_(EXPERIMENT|DIAG)_[A-Z0-9_]+`
i hela filen ger 55 träffar, men **10 av dem är bara loggtaggsträngar**
inuti `logerror(...)`-anrop — händelsenamn som råkar dela prefix med en
riktig flagga (t.ex. `"ASR10_EXPERIMENT_FC6860_CLEAR_BUSY bit0 1->0 ..."`
är loggtexten för `ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE`,
inte en egen flagga). Dessa tio är: `FC6814_ACK`, `FC6816_CLEAR_SERVICE`,
`FC6860_CLEAR_BUSY`, `LRCLK_CLOCK`, `PANEL_REBOOT_CONFIRM`,
`SYNTH_68302_TIMER_START`, `SYNTH_68302_TIMER_IACK_ARMED`, `_DELAY`,
`_FIRED`, `_SKIP`. 55 − 10 = 45, verifierat genom att korsreferensera
varje `static constexpr`-deklaration och varje `std::getenv`-anrop mot
den fullständiga träfflistan (`comm -23`, inga orediovisade rader kvar).
Jag redovisar de 45 verkliga flaggorna nedan snarare än att pressa in
tio loggtaggar som om de vore inställningsbara flaggor. Om uppdragets
58 räknades på ett annat sätt (t.ex. inklusive dessa tio, eller
inklusive `asr10_boot_defs.*` som inte efterfrågades) är det inte
verifierat här.

## Permanent döda — definition

`[Verified]`. Två skilda mekanismer ger "aldrig satt":

1. **Kompileringstid, `static constexpr bool ... = false`.** Dessa kan
   inte sättas via miljövariabel överhuvudtaget — koden själv avgör
   värdet, permanent, tills källkoden ändras. Otvetydigt döda.
2. **Körtid (miljövariabelstyrd) men noll träffar i `docs/asr10/*.md`.**
   Tekniskt påslagbar, men aldrig historiskt körd/dokumenterad såvitt
   `docs/asr10/` visar. Svagare klassning än (1) — flaggad separat
   nedan, inte i samma "död"-kategori, eftersom mekanismen själv
   fungerar om man sätter miljövariabeln.

En `static constexpr ... = true` är motsatsen till död: den är
**permanent på**, inte flagg-styrd alls — det finns ingen miljövariabel
som stänger av den. De listas ändå nedan eftersom de bär namnet och
utgör en del av vad som faktiskt körs i varje session.

---

## A. Miljövariabelstyrda flaggor (22), default av om inget annat sägs

| Flagga | Default | Vad koden gör | Klass |
|---|---|---|---|
| `ASR10_DIAG_PANEL_AUTORESPOND` | av | Efter varje THRB-skrivning (byte sänt till panelen) schemalägger en timer som injicerar ett syntetiskt `0xFF`-svar i den handskrivna panel-C RX/IRQ6-mekanismen — får boten att tro att varje panelöverföring kvitterades. | (b)* |
| `ASR10_DIAG_PANEL_C_PARSER_TRACE` | av (tvingas även på av vilken `PANEL_REPLY_71_*`-flagga som helst) | Loggar parser-tillståndsövergångar efter varje RHRB-pop. | (d) |
| `ASR10_DIAG_PANEL_SUBMISSIONS` | av | Loggar panel-sändningshändelser (start/slut för ring-kontroll/direkt-text/seriell klassificering). | (d) |
| `ASR10_DIAG_PAR_VALUE` | `0x200` internt, men verkningslös utan `PAR_DIAGNOSTIC` | Talvärde (hex/dec-sträng) som blir det fasta värdet `es5506_host_read_par_diag()` returnerar för ES5506:s PAR-register. | (b) |
| `ASR10_DIAG_ROOT_DIRECTORY` | av | Ren dump: skrivningar till `$544`-katalogtabellen, deskriptor-anropskedjan, direkt-text-spårning, sluttillståndsdump av alla 40 poster. | (d) |
| `ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE` | av | Loggar lågminne `$49c`/`$4ae`/`$944`/`$954`/`$4b8` (diskvaliditets-/signaturfält). | (d) |
| `ASR10_EXPERIMENT_DOWNLOAD_TRACE` | av | Loggar sex lågminnesadresser (`$0e7e` m.fl.) under nedladdnings-omförsöksloopen. | (d) |
| `ASR10_EXPERIMENT_DUART_COUNTER_TIMER` | av | **Ändrad betydelse sedan `duart.md`:** styr numera bara ett antal orelaterade FC2xxx/FC3xxx-schemaläggardiagnostiker (döpta efter den gamla handmodellen, se `duart.md`) samt om `duart_irq_model_enabled()` returnerar sant utan någon panel-svarsexperiment — men eftersom `m_panel_c_isr`s räknarbit inte längre sätts av något (den handskrivna timern är borttagen) ger detta i praktiken inget nytt beteende, bara extra loggrader. | (d)** |
| `ASR10_EXPERIMENT_ES5506_HOST` | av | **Konfigurationstid**, inte bara loggning: instansierar `ES5506(config, m_es5506_host, ...)` överhuvudtaget — utan flaggan finns enheten inte i maskinen alls. | (b) |
| `ASR10_EXPERIMENT_ES5510_HOST` | av | Konfigurationstid: instansierar en avstängd (`set_disable()`) `es5510_device` som ren registerbank bakom FC3000-FC31FF. | (b) |
| `ASR10_EXPERIMENT_FC3000_VERIFY_TRACE` | av | Loggar ES5510-värdregistrets FC3000-verifieringshandskakning. | (d) |
| `ASR10_EXPERIMENT_FDC_SYNTH_TC` | av | Sänder en syntetisk terminal-count-puls (`m_fdc->tc_w()`) baserat på värdsidans egen FIFO-bytezräkning, inte på någon verklig hårdvarusignal. | (a)*** |
| `ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE` | av | Ren spårning: räknar/loggar anrop till `fb82a4`/`fb846a`/`fb895a`/`fb8c6e`/`f894a4`/FDC-kommandoutfärdande. | (d) |
| `ASR10_EXPERIMENT_MC68302_GPIO_TRACE` | av | Loggar GPIO-stage1 gate-pass/fail för Port B-pinnarna (PBCNT/PBDDR/PBDAT). | (d) |
| `ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING` | av (uteslutande mot övriga panel-svarsflaggor) | Injicerar syntetiska `0xFF`-kvittenser specifikt för den första kända THRB-ringens byte. | (b)* |
| `ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS` | av (uteslutande) | Samma som ovan men för senare ringar. | (b)* |
| `ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF` | av (uteslutande) | Injicerar `0xFF` efter byte `0x71`, sedan igen efter `0x7E`. | (b)* |
| `ASR10_EXPERIMENT_PANEL_REPLY_71_FF` | av (uteslutande) | Injicerar `0xFF` specifikt efter byte `0x71`. | (b)* |
| `ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO` | av (uteslutande, lägst prioritet) | Injicerar `0x00` efter byte `0x71`. | (b)* |
| `ASR10_EXPERIMENT_PAR_DIAGNOSTIC` | av | Portvakt: måste vara satt OCH `ASR10_DIAG_PAR_VALUE` måste ha ett värde för att PAR-läshaken ska bindas alls. | (b) |
| `ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE` | av | Ren spårning av indirekta anrop/fällor efter tuning-fasen (`pti_state`). | (d) |
| `ASR10_EXPERIMENT_TUNING_STALL_TRACE` | av | Ren spårning: schemaläggar-dispatcher, TRAP #7/#8-räknare, kodutskrifter, spar-sond. | (d) |

`*` Klass (b) är en tänjning: dessa flaggor kompenserar inte för ett
fel i DUART:ens (nu riktiga) registermodell, utan för att **ingen
motpartsenhet finns på kanal B** — den fysiska ASR-10-panelen/
knappsatskontrollern är inte modellerad som någon MAME-enhet
(`esqpanel`-liknande eller egen). Detta passar inte perfekt i (b)/(c)
men ligger närmast (b): "saknad enhetsmodell", bara att enheten som
saknas är panelen, inte DUART/FDC/ES5506/ES5510.

`**` Innan `duart.md` var detta otvetydigt (b) — den drev hela den
handskrivna räknar/timer-modellen. Efter omskrivningen är
huvudfunktionen borttagen; kvar är bara sidoeffekter på orelaterad
diagnostik. Redovisas som (d) med denna brasklapp snarare än att
tyst ärva sin gamla klassificering.

`***` Se `evidence-tree.md` rad 412-433: statisk disassemblering visar
att ingen verklig TC-signal någonsin behövs (programmerad I/O, inte
IDMA) — flaggan är en vederlagd hypotes, inte en bekräftad
kompensation. Klassad (a) för vad den SKULLE kompensera för om den
behövdes, inte för att den faktiskt gör det.

### Aldrig exercerad i `docs/asr10/` (körtidsflagga, 0 dokumentträffar)

`[Verified]`, grep mot alla `docs/asr10/*.md`:

* `ASR10_DIAG_PANEL_SUBMISSIONS` — 0 träffar.

Samtliga övriga 21 körtidsflaggor har minst en träff i `docs/asr10/`.

---

## B. Kompileringstidskonstanter (23), `static constexpr` i klassen

### Permanent PÅ (inget sätt att stänga av utan källkodsändring)

| Konstant | Värde | Vad koden gör | Klass |
|---|---|---|---|
| `ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84` | `true` | Vid `pc==0xfb7c84`, tvingar IPCR-läsning (`0xfc4808`/`09`) att visa bit 4 satt — en extern DUART-insignal som ingenting driver. | (c) |
| `ASR10_EXPERIMENT_CMD88_RATE_500K` | `true` | Vid FDC aux-kommando `0x88`, tvingar `m_fdc->set_rate(500000)` oavsett vad ROM:ets egna hastighetsväljarbitar bad om. | (c) |
| `ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE` | `true` | Låtsas att bit 0 (busy) vid `0xfc6860` (i "m68302 internal candidate"-blocket) självrensar efter en kort läsfördröjning. | (a) |
| `ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_READ_DELAY` | `2` | Parametern (antal läsningar) till ovanstående. | (a) |
| `ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3` | `true` | Syntetiserar en växlande klockbit (bit 3) vid läsning av `0xfc6828` (Port B-data) — en LRCLK-liknande signal ingenting driver. | (a) |
| `ASR10_EXPERIMENT_68302_LRCLK_CLOCK_PHASE_READS` | `8` | Parameter: antal läsningar per fasväxling ovan. | (a) |
| `ASR10_EXPERIMENT_68302_LRCLK_CLOCK_MAX_LOGS` | `64` | Parameter: tak på antal loggrader för fasövergångar. | (d) |
| `ASR10_DIAG_PANEL_B` | `true` | Ren diagnostikutskrift (`log_panel_b_rhrb`) vid varje RHRB-läsning. | (d) |

### Permanent döda (`= false`, ingen väg att slå på utan källkodsändring)

| Konstant | Vad den skulle göra om den vore på | Klass |
|---|---|---|
| `ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B` | Rensar bitmönster `0x000B` i den skuggade "interrupt pending"-registret (`0xfc6814`) direkt efter varje dispatcher-RTE — manuell IACK/ack-emulering. | (a) |
| `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480` | Rensar bitmönster `0x2480` i "interrupt in service"-registret (`0xfc6816`) efter dispatcher-RTE. | (a) |
| `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER` | Villkorad variant: rensar bit `0x2400` i samma register efter en specifik tidigare-observerad sättare/läsarsekvens. | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ` | Genererar en periodisk hårdvaru-timeravbrott (`set_input_line(irq_level, HOLD_LINE)`) helt syntetiskt, utan någon riktig timerenhet bakom. | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ_LEVEL` | Parameter (IRQ-nivå) till ovanstående. | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR` | Utökar samma syntetiska avbrott med en fullständig IACK-vektorleverans (villkorad på dispatcher-kontext, SR-mask, m.fl. villkor). | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_IRQ_LEVEL` | Parameter. | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE` | Parameter (vektorbyte `0x40`). | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK` | Parameter (källmask `0x2400`). | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT` | Parameter: begränsar ovanstående till en enda avfyrning. | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR` | Parameter: väntar på att källmasken rensats innan nästa avfyrning. | (a) |
| `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP` | Parameter: minsta callback-mellanrum. | (a) |
| `ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21` | Injicerar rått `0x21` vid en specifik RHRB-läsning efter att "ERROR 009 - REBOOT ?" observerats. | (b)* |
| `ASR10_EXPERIMENT_STUB_CMD1E_RESULTS` | Stubbar FDC-kommando `0x1E`:s två resultatbyte till fasta värden. | (b) |
| `ASR10_STUB_CMD1E_RESULT_BYTE0`/`BYTE1` | Parametrar (båda `0x00`) till ovanstående. | (b) |
| `ASR10_EXPERIMENT_STUB_CMD0E_RESULT` | Stubbar FDC-kommando `0x0E`:s resultatbyte till ett fast värde. | (b) |
| `ASR10_STUB_CMD0E_RESULT_BYTE` | Parameter (`0x00`). | (b) |

Övriga `static constexpr`-konstanter i samma block
(`ASR10_FAKE_SCSI_INSTALLED=false`, `ASR10_LOG_FDC_ACCESS=false`,
`ASR10_LOG_FDC_04B0_CONTEXT=false`, `ASR10_LOG_PANEL_BYTES=false`,
`ASR10_DUART_INPUT_CHANGE_STUB=0x00`,
`ASR10_PANEL_L_MAX_REPLIES/ZERO_CROSSINGS/CYCLES...`,
`ASR10_DISPLAY_LENGTH`, `ASR10_PANEL_DESCRIPTOR_TRACE_LIMIT`) matchar
inte `ASR10_EXPERIMENT_*`/`ASR10_DIAG_*`-mönstret uppdraget bad om
(förutom att de delar `ASR10_`-prefixet) och räknas inte in i de 45.

---

## 3-4. KRAVLISTA FAS 3 — alla klass (a)

`[Verified]`/`[Likely]` (se enskilda rader ovan för säkerhetsgrad).
Detta är alla flaggor vars kod kompenserar för en **saknad
MC68302-funktion** — de skulle bli överflödiga (eller kunna tas bort
och ersättas med enhetens egna register) av en riktig
`MC68302`-MAME-enhet enligt `PLAN.md` fas 3. Grupperat efter vilken
SIB-delfunktion de pekar på:

**Interruptcontroller (IPR/IMR/ISR vid `0xfc6814`/`0xfc6816`,
manuell ack/service-rensning):**
- `ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B` (permanent död)
- `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480` (permanent död)
- `ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER` (permanent död)

**Timer + IACK (helt syntetiskt avbrott och vektorleverans):**
- `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ` + `_IRQ_LEVEL` (permanent döda)
- `ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR` + `_IRQ_LEVEL` +
  `_VECTOR_BYTE` + `_SOURCE_MASK` + `_ONESHOT` +
  `_WAIT_FOR_SERVICE_CLEAR` + `_MIN_CALLBACK_GAP` (alla permanent döda)

**PIO / Port B (GPIO-signaler in på kortet som ingenting driver):**
- `ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3` + `_PHASE_READS` (permanent
  PÅ idag — en riktig enhet med rätt Port B-koppling gör denna
  gissning onödig)

**Chip-select/statusregister i det ännu ej modellerade interna
fönstret (`0xfc6800`-blocket i stort, exemplifierat av `0xfc6860`):**
- `ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE` +
  `_READ_DELAY` (permanent PÅ idag)

**Syntetisk TC (kompensation för en signal som visat sig onödig, men
klassificeringsmässigt hör hemma här om frågan återuppstår):**
- `ASR10_EXPERIMENT_FDC_SYNTH_TC` (körtidsflagga, av som standard,
  bevisat kontraproduktiv — se fotnot ovan)

Detta är i sak samma lista som `PLAN.md`s egen fas 3-beskrivning
("GIMR/IPR/IMR/ISR, IPL-generering, vektor vid IACK",
"Timer 1, Timer 2, watchdog", "Port A/B PIO") — den här inventeringen
ger den konkreta, körbara motsvarigheten i dagens harness för varje
punkt i den planen.

---

## RADERINGSLISTA — alla klass (d) och alla permanent döda

`[Verified]`. Två grupper, per `CLAUDE.md` regel 2
("instrumentering raderas när utredningen är klar") och regel 7
(radera hellre än att lämna dött).

**Klass (d), ren diagnostik utan beteendepåverkan (kan flyttas till
Lua enligt `CLAUDE.md` regel 3, eller tas bort om utredningen den
skapades för är avslutad):**

```
ASR10_DIAG_PANEL_C_PARSER_TRACE
ASR10_DIAG_PANEL_SUBMISSIONS
ASR10_DIAG_ROOT_DIRECTORY
ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE
ASR10_EXPERIMENT_DOWNLOAD_TRACE
ASR10_EXPERIMENT_DUART_COUNTER_TIMER          (huvudfunktion redan
                                                 borta sedan duart.md;
                                                 se fotnot **)
ASR10_EXPERIMENT_FC3000_VERIFY_TRACE
ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE
ASR10_EXPERIMENT_MC68302_GPIO_TRACE
ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE
ASR10_EXPERIMENT_TUNING_STALL_TRACE
ASR10_EXPERIMENT_68302_LRCLK_CLOCK_MAX_LOGS   (loggtaksparameter)
ASR10_DIAG_PANEL_B                             (permanent på, ren logg)
```

**Permanent döda (`static constexpr ... = false`, ingen väg att slå på
utan källkodsändring — raderas i sin helhet, inte bara flaggan utan
den `if constexpr`-grenade koden bakom den):**

```
ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B
ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480
ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ (+ _IRQ_LEVEL)
ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR (+ _IRQ_LEVEL,
    _VECTOR_BYTE, _SOURCE_MASK, _ONESHOT, _WAIT_FOR_SERVICE_CLEAR,
    _MIN_CALLBACK_GAP)
ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21
ASR10_EXPERIMENT_STUB_CMD1E_RESULTS (+ _RESULT_BYTE0, _RESULT_BYTE1)
ASR10_EXPERIMENT_STUB_CMD0E_RESULT (+ _RESULT_BYTE)
```

**Observera:** raderingslistan för klass-(a)-flaggorna som är
permanent döda (`FC6814_ACK_PENDING_000B`,
`FC6816_CLEAR_SERVICE_2480/2400_AFTER_SETTER`,
`SYNTH_68302_TIMER_IRQ`-familjen) är samma poster som förekommer i
fas 3-kravlistan ovan. Det är inte en motsägelse: de är döda **i
harnesset** (ingen väg att slå på dem utan källkodsändring, och även
om de sloges på skulle de bara syntetisera vad en riktig enhet ska
göra på riktigt) — kravlistan säger att en riktig `MC68302`-enhet gör
just det de försökte gissa sig till, permanent och korrekt, varefter
raderingslistan säger att gissningskoden då kan tas bort helt. Fas 3
kommer före raderingen av dessa specifika poster; klass-(d)-posterna
och de rent parameterlösa döda `(b)`-posterna (panel-reboot-stub,
CMD1E/CMD0E-stubbar) kan raderas oberoende av fas 3, redan nu.
