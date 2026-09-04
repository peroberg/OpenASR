# ES5506 host-port kopplades in korrekt — och stannade ändå vid ERROR 130

2026-08-03. Läst: `docs/asr10/divisor-zero.md`, `docs/asr10/
device-wiring-audit.md`, `docs/asr10/es5506-chain-verification.md`,
`docs/asr10/movep-library.md`, `docs/asr10/os-code-extraction.md`,
`CLAUDE.md`. Metod: source-läsning (ingen instrumentering krävdes för
inventeringen, se `device-wiring-audit.md`), en riktig, permanent
kodändring (inte ett flaggat experiment) som gjordes, byggdes,
regressionstestades i båda lägena per `running.md`, och sedan
**reverterades i sin helhet** eftersom acceptanskriteriet inte
uppfylldes och ett nytt, orelaterat sidoeffekts-problem upptäcktes.
`git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar netto noll
rader efter uppgiften.

## 0. Lägeskontroll (uppgiftens punkt 0)

Vid uppgiftens start: `git status --short` visade en ocommittad
diff i `src/mame/ensoniq/asr10_boot.cpp` (43 tillägg / 59 borttagna
rader — host-port-mappningen redan genomförd men inte testad än),
plus `docs/asr10/device-wiring-audit.md` skriven men orapporterad.
Binären (`./mess`, `./mame`) var byggd från exakt den ocommittade
källan (matchande tidsstämplar). `git log --oneline -3`:

```
0a8c5cdb109 docs/asr10: D2=0 traces to a genuine device read, not a memory write -- and the "healthy" comparison never held
024be1cb560 docs/asr10: ERROR 130 is a genuine divide-by-zero, not a masking artifact
64cfa01df32 docs/asr10: no tick-rate bug -- DUART ticks correctly, CPU permanently stops listening
```

Inget avbrott hade skett i den mening frågan antog — arbetet var
mitt i mappnings-/testfasen, inte förlorat. Det ocommittade
mappnings­försöket testades klart (se nedan) och reverterades sedan
enligt uppgiftens egen regel: **aldrig halv inkoppling committad.**

## 1. Vad inventeringen hittade (uppgiftens punkt 1)

Full redovisning i `docs/asr10/device-wiring-audit.md`. Kort
sammanfattning per enhet:

| Enhet | Mappad? | Callbacks kopplade? | Handskrivet svar kvar? |
|---|---|---|---|
| `m_maincpu` (mc68302) | Ja, äger hela mem_map | IRQ6 real; IP0-3 aldrig drivna | Nej (CPU:n är destinationen) |
| `m_duart` (SCN2681) | Ja, registerfil → riktig enhet | `irq_cb` real; kanal B/IP0-3 ej | Ja: `$FC4808`-stub, kanal B SRB/RHRB |
| `m_fdc` (uPD72069) | Ja, riktig enhet | Inga (korrekt — polled I/O, bevisat i `PLAN.md` Steg B) | Nej |
| `m_floppy_connector` | Ja (barn till FDC, MAME-konvention) | Ingen väg till DUART IP0-3 | Nej |
| `m_es5506_host` | **Nej i standardbygget** — instansiering + mappning satt bakom `ASR10_EXPERIMENT_ES5506_HOST` | Ingen (`irq_cb`, `read_port_cb` obundna) | Ja: plain `.ram()` |
| `m_es5510_host` | Samma mönster, men mer komplett genomförd bakom egen flagga (utanför scope) | Inga behövs, korrekt | Ja, när flaggan är av (utanför scope) |

**Ingen fjärde instansierad-men-inte-inkopplad enhet hittades utöver
de tre uppgiften redan kände till** (DUART `irq_cb`, MC68302:s
GIMR-skugga/`irq6_ack_vector()`, och ES5506:s host-port). En mindre,
redan tidigare dokumenterad lucka noterades men lämnades orörd
(DUART:ens IP0-IP3, permanent flytande — `PLAN.md` "Steg C"/
`panel-ipcr.md`, två obevisade kandidatkällor).

## 2. Mappningen: adressmatematiken stämmer exakt, live bekräftat

`[Verified]`. `map(0xfc2000, 0xfc207f).rw(m_es5506_host,
FUNC(es5506_device::read), FUNC(es5506_device::write))
.umask16(0x00ff);` installerades ovillkorligt (samma `.umask16(0x00ff)`-
konvention som `esqkt.cpp`/`macrossp.cpp`/`ssv.cpp`), och
`ES5506(config, m_es5506_host, XTAL(16'000'000))` instansierades
ovillkorligt i stället för bakom `ASR10_EXPERIMENT_ES5506_HOST`. Den
handskrivna stand-in:en (plain `.ram()` för hela `$FC2000-$FC2FFF`
när flaggan var av) revs för den delen av fönstret.

Körd mot `V350.img` med `ASR10_DIAG_PANEL_AUTORESPOND=1` (den
existerande, redan byggda `ASR10_ES5506_HOST event=host_read`-
diagnostiken, gated bakom `m_es5506_host_enabled` som nu alltid är
sann eftersom enheten alltid finns):

```
event=host_read pc=fc60b0 ... address=fc2069 adapter_disp=68
logical_offset=34 case_index=13 register=PAR(all banks) data=00
mem_mask=00ff
```

**`case_index=13` / `register=PAR` är exakt vad
`es5506-chain-verification.md`s kedja förutspådde**
(`+0x68 → device_offset=0x34 → case 0x34/4=13 → PAR`) —
**det enda tidigare obevisade länken i den kedjan (kortnivå-
routningen) är nu stängd med en riktig, levande körning, inte bara
algebra.** Läsningen på `$FC2069` går nu genom en genuin
`es5506_device::read()`, inte omodellerat RAM. Samtliga 8 observerade
läsningar av `address=fc2069` under körningen visar `data=00`, utan
undantag.

## 3. OTTO:s klocka — rörd inte, öppen fråga (uppgiftens punkt 3)

`[Hypothesis]`, avsiktligt olöst denna omgång. `XTAL(16'000'000)`
lämnades oförändrad. Anteckning för framtiden: `PLAN.md`s
kristalltabell listar `Y2 = 30,476180 MHz` som "OTTO / ES5506", men
det är en ljudkvalitetsfråga (sampelfrekvens/tonhöjd), inte en
bootkedjefråga — utan ljud att jämföra mot (`-sound none` i alla
körningar denna session) går den inte att validera nu. Observerat i
förbigående, INTE agerat på: `esq5506.cpp` finns inte, men
`esqasr.cpp` — den riktiga (för närvarande obyggda)
ASR-10-drivrutinen — instansierar samma krets med samma
`XTAL(16'000'000)`, liksom `esqmr.cpp` och `esqkt.cpp`; `es5506.cpp`s
egen headerkommentar anger "up to 16MHz operation" som chipets
specificerade gräns. Att lägga `Y2` orört på kretsen skulle alltså
gå emot den enda konkreta klockreferens som finns för just den här
kretsfamiljen i det här källträdet. Kvarstår som öppen fråga tills
ljud faktiskt kan jämföras.

## 4. Regressionstestet — båda lägena (uppgiftens punkt 4)

### No-media (`-video none -sound none -nothrottle -seconds_to_run 30`, ingen `-flop1`)

`[Verified]`. Samma hang som baslinjen, på samma plats: en generisk
buffertfyllningsloop vid `$F87DD6`-området (redan identifierad i
`divisor-zero.md` avsnitt 5 som en ORELATERAD rutin) kör tills den
träffar `poll_address=ffffffff`, `poll_count=4000001`,
`ASR10HANG reason=max_poll_count`, i **båda** byggena. Ingen skillnad
i den funktionella slutpunkten. Skillnad i loggvolym: baslinje 9 831
rader; med mappningen 1 887 996 rader, varav 1 874 940
`[:es5506_host] unmapped bank0 memory read`-rader (se avsnitt 5).

### Med media (`-flop1 floppies/asr10booth/V350.img`, `ASR10_DIAG_PANEL_AUTORESPOND=1`)

`[Verified]`. Båda byggena:

```
"   ENSONIQ  ASR-10    " -> "    LOADING SYSTEM    " -> "ERROR 130 - REBOOT ?"
```

**Identisk slutlig panel-text i båda byggena.** Baslinje: `ERROR 130`
på rad 820 881 av 823 474. Med mappningen: `ERROR 130` på rad
1 977 082 av 2 782 174 (fler rader p.g.a. `unmapped bank0`-spam och
den nya `ASR10_ES5506_HOST`-diagnostiken, inte p.g.a. fler
bootsteg).

**Acceptanskriteriet, punkt för punkt:**

a) **Delvis.** Läsningen på `$FC2069` besvaras nu av en genuin
   enhet (`register=PAR`, bevisat ovan) — inte längre omodellerat
   RAM. Men enhetens svar är **fortfarande `0x00`** i alla 8
   observerade fall.

b) **Nej.** D2 blir fortfarande noll. `es5506.cpp` rad 1498-1501
   (`reg_read_low`) och rad 1521-1524 (`reg_read_test`), PAR-fallet:

   ```cpp
   case 0x68/8:    // PAR
       if (!m_read_port_cb.isunset())
           result = m_read_port_cb(0) & 0x3ff; // 10 bit, 9:0
       break;
   ```

   `result` initieras till `0` och uppdateras **bara** om
   `read_port_cb()` är bunden. Vår `read_port_cb()` lämnas obunden i
   standardfallet (bara bunden under den separata, uttryckligen
   icke-auktoritativa `ASR10_EXPERIMENT_PAR_DIAGNOSTIC`+
   `ASR10_DIAG_PAR_VALUE`-kombinationen). **Det här är precis
   uppgiftens eget förutspådda utfall: "om D2 fortfarande är noll
   trots korrekt mappning... registret kräver ett ES5506-tillstånd
   som inte finns."** PAR är chipets externa parallell-/analogingångs-
   register (jämför `esq5505.cpp`s `m_otis->read_port_cb().set(FUNC(
   esq5505_state::analog_r))` — en riktig ADC-avläsningsfunktion i
   den syskonmaskinen). Vad `$FC2069` "borde" returnera på riktig
   ASR-10-hårdvara är **inte belagt i det här trädet** och rapporteras
   därför inte som en gissning.

c) **Nej.** `ERROR 130 - REBOOT ?` visas i båda byggena, på samma
   ställe i bootkedjan.

**Ingen kompensation gjordes** för (b)/(c) — ingen fast PAR-injektion,
ingen gissad "korrekt" viloläge.

## 5. En orelaterad regression hittades: enhetens egen sampelmotor vaknar

`[Verified]`. Så fort en riktig `es5506_device` finns och firmware
gör en riktig registerskrivning som rensar en röstas
`CONTROL_STOPMASK`-bitar (`es5506.cpp` rad 490:
`m_voice[j].control = CONTROL_STOPMASK;` vid reset — verkligt
default-stoppat, men fastware-koden avstannar tydligen minst en röst
under normal boot), börjar enhetens interna `generate_samples()`-väg
hämta wavetable-data från `bank0`-adressrymden (`m_bank0_config`,
`es5506.cpp` rad 185). Ingen `set_region0()`/`set_region1()` är
kopplad i den här drivrutinen (till skillnad från `esq5505.cpp`s
`m_otis->set_region0("waverom")`), så varje sådan hämtning loggar
`[:es5506_host] (no context): unmapped bank0 memory read from
XXXXXX & FFFF` — **1 875 158 rader** i den 30-sekunders V350-körningen
(67% av hela loggfilen), och en mätbar hastighetssänkning
(399,50% → 283,12% simuleringshastighet med media,
1410,81% → 594,41% utan media — omkring 30-58% långsammare beroende
på läge). Detta är **helt orelaterat till `$FC2069`/PAR** — det är en
sidoeffekt av att över huvud taget ha en riktig, aktiv ljudenhet
instansierad utan dess wavetable-minne modellerat, och exakt den typ
av regression uppgiftens egen varning förutspådde ("kan regressa på
ställen som inte har med `$FC2069` att göra").

En mindre, sekundär observation: de exakta engångs-loggtaggarna som
träffas i felhanteringsdispatchern (`ASR10_DISPATCHER_RTE_CANDIDATE_
F8D020` i baslinjen mot `ASR10_ERROR_WATCH` x3 i den mappade
körningen) skiljer sig åt — men **slutpunkten (panel-texten) är
identisk i båda**, så detta tolkas som att andra, redan-riktiga
registerläsningar (t.ex. `PAGE`, som nu returnerar `m_current_page`
i stället för RAM-skräp) marginellt förskjuter vilken av flera
näraliggande, redan instrumenterade PC-punkter som råkar logga
först — inte ett nytt eller annorlunda felläge.

## 6. Beslut: reverterat i sin helhet

Per uppgiftens egen regel ("Committa aldrig halv inkoppling"):
mappningen **håller** rent tekniskt (adressmatematiken är nu
live-bevisad, inte bara algebra), men den **löser inte** `ERROR 130`
(kriterium b/c ej uppfyllda) och **inför en mätbar, orelaterad
regression** (avsnitt 5). Att committa den skulle permanent förstora
harnesset (CLAUDE.md regel 1) utan att uppgiften den motiverades av
faktiskt löstes, och skulle kräva ytterligare, ospecificerat arbete
(wavetable-minne, ev. `read_port_cb`-modell) för att inte permanent
förorena loggarna.

```sh
git checkout -- src/mame/ensoniq/asr10_boot.cpp
```

`git diff --stat src/mame/ensoniq/asr10_boot.cpp` efter revert: inga
rader. Binären ombyggd från den reverterade källan och verifierad
identisk med det committade trädet innan uppgiften avslutades.

**Rader till/från under uppgiften:** den testade (och sedan
reverterade) ändringen var **43 tillägg / 59 borttagna rader** i
`src/mame/ensoniq/asr10_boot.cpp` (nettominskning, eftersom
`if (flag) {...} else {...}`-grenarna för både instansiering och
adressinstallation kollapsades till en ovillkorlig sökväg) — men
eftersom ändringen reverterades är **nettoeffekten på committad kod
noll**. Den enda bestående förändringen från den här uppgiften är
`docs/asr10/device-wiring-audit.md` (ny fil) och den här filen.

## 7. Vad som skulle krävas för att faktiskt komma vidare

Inte agerat på i den här uppgiften (utanför scope, kräver nya
belägg):

1. **Vad ska `read_port_cb()` returnera?** Kräver antingen
   ASR-10-specifik hårdvarudokumentation för vad chipets
   parallell-/analogingång är kopplad till på just det här kortet,
   eller ett bevis för att OS-koden faktiskt förväntar sig en
   specifik konstant (går inte att avgöra utan schemat eller en
   fungerande referens-ASR-10 att jämföra mot).
2. **Wavetable-minnet** (`bank0`/`bank1`, `set_region0`/
   `set_region1`) måste modelleras eller enheten hållas i ett
   tillstånd där dess röster aldrig avstannas, innan enheten kan
   vara permanent instansierad utan loggspam.
3. Först därefter är det meningsfullt att återuppta punkt 3 (klockan)
   med faktiskt ljud att jämföra mot.
