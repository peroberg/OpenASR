# Diskvägen: FDC-karta, monteringsläge, "NO INST OR BANK FILES" och slot0

2026-07-29. Rent kartläggningsarbete — inga kodändringar, inga nya
stubbar. Läst: `PLAN.md`, `CLAUDE.md`, `baseline.md`, `duart.md`,
`current-blocker.md`, `scheduler-slot0-continuation-findings-2026-06-29.md`.
Kodreferenser mot arbetsträdet efter `docs/asr10/duart.md`-committen
(`7bc57b8ab45`).

## 1. Vilka FDC-accesser går genom `m_fdc`, vilka är handskrivna?

`[Verified]`, läst direkt ur `upd72069_fdc_r`/`upd72069_fdc_w`
(`src/mame/ensoniq/asr10_boot.cpp:4333` resp. `:4482`), adressmappat
vid `:2271`:

```
map(0xfc4000, 0xfc4003).rw(upd72069_fdc_r, upd72069_fdc_w)
map(0xfc4004, 0xfc47ff).ram()   -- oadresserat, ren RAM, ingen enhet, ingen hand-modell
```

**Rättelse av `baseline.md`s tidigare påstående.** `baseline.md` (skriven
innan koden faktiskt lästes rad för rad) säger "Handmodellerad
FDC-tillståndsmaskin ... fortfarande handkodad — inte
`upd72069_device`". Det stämmer inte för själva registerprotokollet.
Alla fyra kärnoperationerna går redan genom den riktiga enheten:

| Byteadress | Register | Anrop | Rad |
|---|---|---|---|
| 0xFC4001 (läs) | MSR | `m_fdc->msr_r()` | 4345 |
| 0xFC4003 (läs) | FIFO | `m_fdc->fifo_r()` | 4386 |
| 0xFC4001 (skriv) | Aux command | `m_fdc->auxcmd_w(...)` | 4565 |
| 0xFC4003 (skriv) | FIFO | `m_fdc->fifo_w(...)` | 4678 |
| (internt, CMD46-läge) | Terminal count | `m_fdc->tc_w(false); m_fdc->tc_w(true);` | 4403-4404 |
| (internt, CMD88) | Data rate workaround | `m_fdc->set_rate(500000)` | current `ASR10_MISSING_FDC_RATE_SOURCE` block |

Historical pre-cleanup inventory: det som då var handskrivet runt dessa anrop,
i turordning av allvarlighetsgrad:

1. **`ASR10_MISSING_FDC_RATE_SOURCE = true`** — en
   `constexpr bool`, INTE miljövariabelstyrd, alltid på. Vid aux-kommando
   `0x88` tvingas `m_fdc->set_rate(500000)` oavsett vilken hastighet
   ROM:ets egen databit i samma kommandobyte faktiskt bad om (rad
   4566-4570). Det är en genuin avvikelse från rent passthrough, aktiv
   som standard i varje körning. `m_fdc_data_rate` (rad 185) är en egen
   skuggvariabel som bara loggar vad koden *tror* att hastigheten är —
   den styr aldrig den riktiga enheten, den är ren bokföring parallellt
   med `set_rate()`.
2. **Syntetisk TC-puls** (rad 4395-4409 i dåvarande träd), gated bakom
   `ASR10_EXPERIMENT_FDC_SYNTH_TC` (miljövariabel, default av). När på:
   härledde den en förväntad sektorstorlek ur CMD46:s egna kommandobytes
   (`m_fdc_cmd46_write_bytes[5]`) och triggade `tc_w()` själv när precis så
   många FIFO-byte lästs. Detta ersatte vad som på riktig hårdvara vore en
   räknare/DMA-signal.
3. **`ASR10_EXPERIMENT_STUB_CMD1E_RESULTS`/`ASR10_EXPERIMENT_STUB_CMD0E_RESULT`**
   (rad 120, 123) — båda `constexpr bool ... = false`, dvs död kod som
   inte går att slå på ens med miljövariabel (måste ändra källkoden).
   Ren kvarleva, ingen effekt i något körbart läge idag.
4. **Allt annat** i de två funktionerna (`m_fdc_transaction`,
   `m_fdc_command_ring`, `m_fdc_cmd46_*`, `m_fdc_os_cmd_*`,
   `m_fdc_txn_*`) är ren observation/bokföring för loggning — de
   modellerar inget hårdvarubeteende, de bara sparar vad den riktiga
   enheten redan svarat med för senare loggutskrift.

**`ASR10FDCSTATE`/`ASR10FDCSTATE_READ`-taggarna är INTE FDC-register.**
De kommer från `low_rom_or_lowmem_r`/`lowmem_w` (rad 2384 resp. 3921),
dvs vanlig `.ram()`-läsning/skrivning på nio lågminnesadresser som
ROM:et självt använder som sin egen mediaprobe-bokföring:

```
0x04a6 0x04ae 0x04b0 0x04b4 0x04b6 0x04c4 0x04c6 0x04d6 0x04e6
```

(`is_fdc_state_field`/`fdc_state_field_name`,
`src/mame/ensoniq/asr10_boot_defs.cpp:168-202`.) Ren observation, noll
sidoeffekt — värdena skrivs helt av ROM:et, vi injicerar inget.

**Slutsats:** FDC-registerprotokollet är redan `upd72069_device`, inte
en handmodell. Den enda på-som-standard avvikelsen är den tvingade
500 kbps-hastigheten för CMD88, nu namngiven
`ASR10_MISSING_FDC_RATE_SOURCE`. `PLAN.md`s "riv ut FDC-approximationen"
(avsnitt 5) är alltså till större delen redan gjort för
registerprotokollet. Efter Category A/B/C-cleanupen återstår i aktuell driver
att identifiera och modellera den riktiga ASR-10-källan för 500 kbit/s och
därefter ta bort `ASR10_MISSING_FDC_RATE_SOURCE`.

## 2. Monteras någon diskavbild i `asr10booth`? Är "PLEASE INSERT DISK" fel?

`[Verified]`. `ROM_START(asr10booth)` (rad 10637-10641) innehåller
bara CPU-ROM:en, ingen diskregion. `FLOPPY_CONNECTOR(config,
m_floppy_connector, ..., "35hd", floppy_formats, true)` (rad 10561) sätter
ingen standardavbild. Ingenting i maskinkonfigurationen monterar en
diskett automatiskt.

De två testavbilderna som använts i historiska sessioner
(`floppies/asr10booth/V161.img`, `V350.img`) finns lokalt på disk men är
**inte** en del av det här git-trädet (`git status` visar dem inte alls
— varken spårade eller "??"; de ligger utanför repot filosofiskt sett,
bara lokala testresurser) och måste monteras explicit med `-flop1
<path>`.

`docs/asr10/baseline.md` och `docs/asr10/duart.md` kördes **utan**
`-flop1` (bekräftat av deras egna kommandoradsrader). Det är alltså
konsekvent, förväntat att de körningarna fastnar i
`"  PLEASE INSERT DISK  "` — **`[Verified]` det är korrekt beteende för
den konfigurationen, inte ett fel eller en regression.** (`baseline.md`
har redan fått en rättelseruta överst som bekräftar exakt detta.)

Med `-flop1 floppies/asr10booth/V161.img` monterad kommer bootkedjan
längre (se avsnitt 3-4 nedan): `"   ENSONIQ  ASR-10    "` →
`"    LOADING SYSTEM    "` → (kräver `ASR10_DIAG_PANEL_AUTORESPOND=1`
för att komma vidare, se avsnitt 4) → `"TUNING KBD - HANDS OFF"` →
`"    KEYBOARD TUNED"`. Verifierat empiriskt i den här uppgiften mot
dagens byggda `./mess`.

## 3. Var kommer "NO INST OR BANK FILES" ifrån?

`[Verified]`, flera oberoende bevislinjer:

* **Vår egen kod innehåller aldrig textsträngen som data.** Enda
  träffen för `"NO INST OR BANK FILES"` i hela `asr10_boot.cpp` är rad
  5082: `strstr(m_panel_text, "NO INST OR BANK FILES")` — ett
  **jämförelsevillkor** mot den ackumulerade panelbufferten, bakom
  `m_root_directory_trace_enabled` (miljövariabelstyrd diagnostik,
  default av). Koden letar efter strängen när den redan finns i
  bufferten; den skriver aldrig in den.
* **Strängen finns inte som sammanhängande ASCII i boot-ROM:et.**
  Sökt i båda ROM-filerna sammanflätade (`asr-648c-lo-1.5b.bin`
  low-byte, `asr-65e0-hi-1.5b.bin` high-byte, offset 0/1 exakt som
  `ROM_LOAD16_BYTE` anger): `"ENSONIQ"` hittas (offset 233407),
  `"PLEASE INSERT DISK"` hittas (offset 233336), men `"NO INST"` och
  `"BANK FILES"` hittas inte, i någon byteordning.
* **Strängen finns inte heller rått i `V161.img`.** Sökt direkt i
  disksektordumpen (1 638 400 byte) — ingen träff för någon av
  delsträngarna.
* **Men ROM:et innehåller ett delordsbibliotek som kan bygga den.**
  Samma sammanflätade ROM-sökning hittar `"NO "`, `"INST "`, `"BANK "`,
  `"FILES "` som separata nollterminerade strängar i vad som ser ut som
  en delad ord-tabell för att bygga sammansatta meddelanden (samma
  mönster som den redan dokumenterade kategori-menytabellen
  `"FACTORY SNDS"`/`"MY SOUNDS"` etc. i `current-blocker.md` rad
  160-162). Det här är alltså en space-sparande teknik: meddelandet
  byggs ihop av ROM-koden vid körning från återanvända ordfragment,
  inte lagrat som en enda sträng någonstans.

**Slutsats:** `[Verified]` texten kommer från ROM-kod som bygger
meddelandet ord-för-ord vid körning och sänder det byte för byte över
kanal B (DUART THRB), inte från vår egen kod. Vår kod är en passiv
mottagare (`panel_receive_byte`/`flush_panel_text`) som råkar kunna
mönstermatcha mot resultatet efteråt.

**Empirisk återskapning denna session:** `[Verified]`, men ofullständig
inom given tidsram. Med `-flop1 floppies/asr10booth/V161.img
ASR10_EXPERIMENT_ES5510_HOST=1 ASR10_DIAG_PANEL_AUTORESPOND=1
-seconds_to_run 150` nådde dagens bygge `"TUNING KBD - HANDS OFF"` och
`"    KEYBOARD TUNED"` (se avsnitt 4), men inte `"NO INST OR BANK
FILES"` inom de 150 emulerade sekunderna som testades här. Utan
`ASR10_DIAG_PANEL_AUTORESPOND=1` (bara `-flop1` + `ES5510_HOST`, testat
upp till 150 s) fastnar körningen i upprepad "qqqq"-skräptext direkt
efter `"LOADING SYSTEM"` och når aldrig `TUNING KBD` — konsekvent med
`current-blocker.md`s egen modell (kanal B:s sista RX-kvittens uteblir
utan en panel-svarsexperiment, se avsnitt 4). `current-blocker.md`s
2026-07-21-uppdatering anger att `NO INST OR BANK FILES` nåddes i en
"~114,5-emulerad-sekunders" körning under "den fullt etablerade
diagnostiska baslinjen" — en annan, troligen längre/mer specifik
miljövariabelkombination än de fyra som testades här. Att exakt
återskapa den specifika baslinjen låg utanför den här uppgiftens
tidsbudget; `[Hypothesis]` att den skulle nås med samma flaggor plus
längre körtid eller ytterligare en av de redan existerande
`ASR10_EXPERIMENT_PANEL_*`-flaggorna.

## 4. Stämmer `current-blocker.md`/`scheduler-slot0-continuation-findings-2026-06-29.md` med koden idag?

`[Verified]` för den del som testades; `[OPEN]` kvarstår som dokumenterat.

**`scheduler-slot0-continuation-findings-2026-06-29.md`** är en
uttryckligen avbruten, ofullständig tråd ("Next prompt when Codex quota
resets... Diagnostics only. Do not change emulator behavior. Do not
commit."). Den ställer frågan: vem konsumerar nod `0014f4` (fält `+02 =
89A2`) efter att `trap #9` köar den till slot0? Den lämnar frågan
uttryckligen öppen.

**`current-blocker.md`** tar upp samma tråd senare (2026-07-13 till
2026-07-16) och bevisar (avsnitt 4, "[PROVEN]") hela kanal
B-completion-livscykeln: `$03BC`/`$03C5`-tillståndsmaskinen, att sista
THRB-byten kräver en EXTRA RX-kvittens innan `$03C5` verkligen
nollställs, och att den andra ringen startar naturligt efteråt. Den
lämnar frågan om nod `89A2` fortfarande öppen i sina egna ord
(avsnitt 9: "the consumer and meaning of node type 89A2 remain
unknown"), men konstaterar att sex schemaläggarslots dispatchas i
ordningen 1,3,0,4,5 efter `KEYBOARD TUNED`.

**Dagens kod, verifierat i den här uppgiften:** samma körning som i
avsnitt 3 (`-flop1 V161.img ASR10_EXPERIMENT_ES5510_HOST=1
ASR10_DIAG_PANEL_AUTORESPOND=1`) reproducerar exakt detta:

```
ASR10_GEN_TRACKING GEN=78 THRB_HEX=44 panel="    KEYBOARD TUNED"
rte_count=2 slot=1   rte_count=3 slot=3   rte_count=4 slot=0
rte_count=5 slot=4   rte_count=6 slot=5
```

— identisk slotordning (1,3,0,4,5) som `current-blocker.md` rad 132
dokumenterar. `lowmem_0b6c=14f4` syns fortfarande i loggen vid det här
laget, dvs nod `14f4` är fortfarande köad/närvarande precis som
dokumentet beskriver. Ingen logglinje i den här körningen visar att
noden konsumeras eller att `F89AC2` kör — frågan är alltså **fortfarande
lika öppen i dagens kod som när den skrevs**, inte tyst löst av någon av
de efterföljande ändringarna (DUART-bytet i `duart.md` rörde inte denna
kod alls).

**Viktig nyans:** `current-blocker.md`s 2026-07-21-uppdatering (den
nyaste, "boot reaches NO INST OR BANK FILES") löste den återstående
bootblockeraren via ES5510-host-adaptern (`FC31C1`-routing för
effektnedladdning) — en **helt annan** delsystem än
panelschemaläggningen/nod-`89A2`-frågan. Inget i dokumentationen eller
i koden idag visar att nod-`89A2`-konsumtionsfrågan någonsin besvarades;
det verkar snarare ha blivit ovidkommande — boot-kedjan kom förbi
`TUNING KBD`/vidare via ett orelaterat fix, inte genom att lösa vem som
läser noden. **`[Hypothesis]`**: nod-`89A2`-spåret var en felaktig
huvudmisstanke för den ursprungliga "TUNING KBD stannar"-bufflocken;
den verkliga boot-blockeraren låg i ES5510-nedladdningen hela tiden, och
schemaläggarnoden konsumeras kanske aldrig alls under normal drift (det
skulle i så fall vara en godartad, aldrig-avbockad kö-post, inte ett
tecken på fel) — inte bekräftat i endera riktningen här.

**Sammanfattning av vad som är stängt kontra öppet:**

| Fråga | Status |
|---|---|
| Kanal B completion-livscykel ($03BC/$03C5, extra RX-kvittens) | `[Verified]`, reproducerad idag |
| Sex-slots dispatch-ordning 1,3,0,4,5 efter KEYBOARD TUNED | `[Verified]`, reproducerad idag, identisk ordning |
| Vem konsumerar nod 14F4/typ 89A2 | `[OPEN]`, olöst 2026-06-29, olöst idag |
| Om 89A2-frågan är relevant för att nå NO INST OR BANK FILES | `[Hypothesis]`, troligen inte — boten kom förbi via ES5510-fixet, inte via nodkonsumtion |
