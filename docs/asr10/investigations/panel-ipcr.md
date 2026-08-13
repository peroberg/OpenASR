# $FC4809 = IPCR, and what's missing is an external DUART input pin, not a register model

Current note, 2026-08-12: the PC-specific `$FC4809` bit-4 stub and the
`ASR10_DUART_INPUT_CHANGE_STUB` base-value override have been removed from
`asr10_boot.cpp`. The failed Disk Ready trial drove DUART IP0 from floppy
loaded + motor-active state and did not boot V3.50 past repeated
`PLEASE INSERT DISK`. Current code drives DUART IP0 from the uPD72069 index
callback instead. The historical notes below describe why the old PC-specific
stub existed and why it was not a hardware model.

2026-07-30. Rent diagnostiskt. Läst: `PLAN.md` fas 3, `CLAUDE.md`,
`fdc-dumpreg.md`, `duart.md`, `panel-protocol.md`,
`panel-input-display.md`. Ingen kod ändrad, inga stubbar byggda.

## 1. $FC4809 = register 4 = IPCR (läs), ACR (skriv)

`[Verified]`. Direkt källkodsläsning, `src/devices/machine/mc68681.cpp`:

```
case 0x04: /* IPCR */
    r = IPCR;
    ...
    IPCR &= 0x0f;   // change-flaggorna (bit 4-7) rensas vid läsning
```

och `docs/asr10/architecture.md` rad 215 bekräftar oberoende samma
karta från ROM-sidan: `FC4809 = IPCR/ACR`. Udda-byte-mappningen
(`FC4808` som ordadress, `ACCESSING_BITS_0_7` för den låga/udda byten)
matchar exakt samma konvention som resten av DUART-fönstret
(`FC4803`=SRA, `FC4807`=RHRA/THRA, osv). Läsvägen är alltså IPCR, inte
ACR — ACR är skrivsidan av samma registeradress (`mc68681.cpp` har en
separat `write()`-gren för adress 4 som skriver ACR, aldrig konsulterad
av den här läsningen).

## 2. Bit 4 = IP0-ändringsflaggan — bekräftat, och vad ROM:en jämför mot

`[Verified]`. Fyra separata handlers i `mc68681.cpp`
(`ip0_w`/`ip1_w`/`ip2_w`/`ip3_w`) sätter exakt en bit var när
respektive pinnes tillstånd ändras: `ip0_w` sätter `IPCR |= 0x10`
(bit 4), `ip1_w` sätter bit 5, `ip2_w` bit 6, `ip3_w` bit 7. Standard
SCN2681-numrering, precis som uppgiften antog.

ROM:en (`src/mame/ensoniq/asr10_boot.cpp`, PC `$FB7C84`) gör
`BTST #4,$FC4809` — testar exakt IP0:s ändringsflagga, inget annat
bitmönster. Det bekräftar uppgiftens premiss ordagrant.

**En detalj värd att notera:** `IMR = 0x2B` (redan dokumenterat i
`architecture.md`) har bit 7 (`INT_INPUT_PORT_CHANGE = 0x80` i
`mc68681.cpp`) **omaskad av**. ROM:en har alltså **inte** aktiverat
DUART:ens egen avbrottsväg för input-port-change — den pollar IPCR
direkt via BTST, precis som resten av den här utredningen redan visat
för FDC:n (`fdc-dumpreg.md`). Polling, inte interrupt, är det
konsekventa mönstret genom hela den här utredningstråden.

## 3. Den kvarvarande stubben — vad den gör och varför den inte räcker

`[Verified]`, `asr10_boot.cpp` rad 4604-4629. Adress `$FC4808`
(ordadressen för IPCR/ACR) hanteras **helt vid sidan av** den riktiga
`m_duart`-enheten: `raw_data` sätts hårdkodat till
`ASR10_DUART_INPUT_CHANGE_STUB` (`= 0x00`, alltid), och bit 4 OR:as in
**bara om `pc == 0x00fb7c84` exakt**:

```cpp
raw_data = ASR10_DUART_INPUT_CHANGE_STUB;
if (ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 && pc == 0x00fb7c84)
    raw_data |= 0x10;
```

Detta är alltså inte en modell av IP0 — det är en enda hårdkodad PC,
inte en pinne. Två saker gör den otillräcklig:

1. Den riktiga `m_duart->read(word)`-anropet på rad 4588 beräknas
   redan (och skulle returnera IPCR:s *riktiga* värde — alltid `0x00`
   förändringsbitar, eftersom ingenting någonsin anropar `ip0_w`/
   `ip1_w`/`ip2_w`/`ip3_w` någonstans i den här drivrutinen), men det
   värdet kasseras helt för just den här adressen.
2. **Samma register läses från minst två olika PC:** `$FB7C84`
   (loggad roll `semantic_input_change_bit4`, den enda platsen stubben
   täcker) och `$FB7C30` (loggad roll `ack_input_change_latch`,
   redan namngiven i den befintliga koden). `docs/asr10/fdc-dumpreg.md`
   visade att den **faktiska** stallpunkten i dagens körning läser
   registret upprepade gånger från `$FB7C30` (containing_routine
   `$FB9104`, `ASR10CPUCONTEXT hits=1, hits=2, ...`) — en helt annan
   PC än den enda stubben täcker. Från `$FB7C30` är `raw_data`
   alltid `0x00`; stubben kan aldrig hjälpa där, oavsett hur länge
   körningen fortsätter.

## 4. Vad är plausibelt kopplat till IP0?

`[Verified]` att ingenting driver IP0-3 idag: `grep` mot
`asr10_boot.cpp` ger noll träffar för `ip0_w`/`ip1_w`/`ip2_w`/`ip3_w`
— fyra pinnar som per real hårdvara *måste* vara kopplade till något,
men här är helt oanslutna i maskinkonfigurationen.

**Två konkurrerande, var för sig rimliga kandidater** — ingen är
bevisad för ASR-10 specifikt:

### Kandidat A: panelhändelse/status (`panel-input-display.md`s egen slutsats)

`[Likely]`, baserat på `panel-input-display.md`s "Input/event gate"-
avsnitt: bit 4 tolkas där som "input-change/event-available/status
bit" kopplad till samma externa panelkontroller-kandidat
(`ENS5702000102`+80C52) som resten av `panel-protocol.md`s
Kanal B-protokoll redan handlar om. Motivering: ROM:et testar bit 4
**direkt efter** en sekvens som rör panelrelaterad textbyggnad
(`$FB7C2C`/`ASR10_049D_WRITE`), vilket är svagt men konsekvent stöd.
Inget kopplingsschema eller servicemanual citeras för detta i
trädet — se `panel-protocol.md` avsnitt 7.4 ("Panel-controller hardware
evidence [open]").

### Kandidat B: diskettstatus (motor/media) — starkare stöd, oväntat fynd

`[Likely]`, nytt fynd i den här uppgiften, **inte efterfrågat av de
lästa ASR-10-dokumenten men starkt stött av ett syskonverk**:
`src/mame/ensoniq/esq5505.cpp` (samma DUART-familj, snarlik
bootarkitektur) kopplar **precis den här pinnen** till diskettstatus,
inte panelen:

```cpp
// esq5505.cpp rad 387
m_duart->ip0_w(m_floppy_is_active && m_floppy_is_loaded);
// rad 336-338: "On VFX and later, DUART input bit 1 is 0 for cartridge present."
m_duart->ip1_w(state);
```

D.v.s. på ESQ5505-familjen är **IP0 = diskettmotor aktiv OCH media
inlagd**, IP1 = löstagbar-media-närvarande. Panelen på den familjen går
via `b_tx_cb()`/`rx_b_w()` (Kanal B, seriellt), aldrig via IP-pinnarna.

**Tidskorrelationen stöder kandidat B konkret.** `fdc-dumpreg.md`
visade att den sista FDC-relaterade raden i hela körningen är:

```
[:fdc] polled 0 : 1 -> 0
```

— `run_drive_ready_polling()` i `upd765.cpp` (rad 2667) upptäcker att
drivenhet 0:s ready-tillstånd växlar `1 -> 0` (rimligen motorn stängs
av efter att all disklästning redan avslutats, se `disk-read-path.md`).
Det inträffar i loggen i precis samma tidsfönster som den upprepade
`$FB7C30`-pollningen. Om ASR-10:s riktiga kort kopplar samma signal
till IP0 som ESQ5505 gör, är **just den här övergången** den händelse
ROM:en väntar på att se som en IPCR bit-4-ändring — och den missas helt
enkelt eftersom ingenting i den här drivrutinen någonsin ropar
`m_duart->ip0_w(...)`.

**Detta motsäger inte kandidat A** — båda kan vara sanna för olika
IP-pinnar (kortet har fyra: IP0-IP3), eller kandidat B kan vara fel för
just ASR-10 trots syskonprecedensen. Ingen servicemanual eller
kopplingsschema för ASR-10:s DUART-sida citeras i det här trädet för
att avgöra det. Men kandidat B är den enda av de två som har en konkret,
tidsmässigt sammanfallande, redan-emulerad källhändelse (uPD72069:ans
egen ready-polling) att koppla till — kandidat A har ingen motsvarande
konkret källhändelse i den nuvarande körningen.

## 5. Finns en MAME-enhet som kan driva det?

`[Verified]` för ramverket, `[OPEN]` för protokollinnehållet.

**Om kandidat B (diskettstatus) stämmer:** ingen ny enhet behövs alls.
`m_floppy_connector`/`m_fdc` finns redan; det är en enda
devcb-koppling, samma mönster som `esq5505.cpp` rad 387 — sannolikt
`m_duart->ip0_w(floppy && floppy->exists() && !floppy->mon_r())` eller
motsvarande, kopplad via `m_floppy_connector`s befintliga
signalfunktioner. Det här är den billigaste av de två kandidaterna att
testa.

**Om kandidat A (panel) stämmer:** `src/mame/ensoniq/esqpanel.h/cpp`
har en återanvändbar **bas** (`esqpanel_device : device_serial_interface`,
med `write_tx()`/`rx_w()` kopplade till `m_duart->b_tx_cb()`/
`rx_b_w()` exakt som `esq5505.cpp` rad 772-773, 812-813 visar) — den
seriella transportramen (baud, framing, DUART-koppling) är direkt
återanvändbar. Men **protokollinnehållet är det inte**: alla fem
befintliga underklasser (`ESQPANEL1X22`, `ESQPANEL2X40`,
`ESQPANEL2X40_VFX`, `ESQPANEL2X40_SQ1`, `ESQPANEL2X16_SQ1`)
implementerar var sitt fast, produktspecifikt kommandoset via den rena
virituella `send_to_display()`. ASR-10:s protokoll (markör/nyttolast
PATH A, direkttext-PATH B med `0x66`-prefix, deskriptorexpansion,
ring/ACK-kvittens över Kanal B — allt utförligt dokumenterat i
`panel-protocol.md`) matchar ingen av dem. `panel-protocol.md` avsnitt
7 noterar redan att ASR-10:s display "matches ESQPANEL1X22 (1×22 VFD +
fixed LEDs)" formatmässigt, men kommandoinnehållet är bevisligen
annorlunda. **Slutsats: ASR-10 skulle behöva en egen ny underklass av
`esqpanel_device`** (samma bas, eget protokoll), inte återanvändning
av en befintlig variant rakt av.

## 6. PLAN.md fas 3 — rättelse

`[Verified]`-baserad rättelse committad separat: blockeraren vid det
här stallet är en **oansluten DUART-inputpinne** (IP0, sannolikt IP1-3
också), inte interruptcontrollern och inte en polleringsartefakt utan
verklig orsak. `mc68302int.cpp` löser inte det här — det är fortfarande
efter i prioritetsordning. Vilken extern signal som faktiskt hör hemma
på IP0 (panel eller diskett) är **inte** avgjort här; båda kandidaterna
och sina respektive billigaste-först-testa-ordning är noterade ovan.

## Sammanfattning av evidensläge

| Fråga | Svar | Tagg |
|---|---|---|
| $FC4809 = IPCR (läs) | Ja | `[Verified]` |
| Bit 4 = IP0-ändringsflagga | Ja | `[Verified]` |
| ROM testar bit 4 vid $FB7C84 mot detta | Ja | `[Verified]` |
| Stubben räcker | Nej — PC-låst till $FB7C84, verkliga stallet läser från $FB7C30 | `[Verified]` |
| IP0 = panelhändelse | Möjlig, svagare stöd | `[Likely]` |
| IP0 = diskettstatus (motor/media) | Möjlig, starkare stöd (syskonprecedens + tidskorrelation) | `[Likely]` |
| MAME-enhet för panelfallet | `esqpanel_device`-ramverk återanvändbart, protokoll måste vara nytt | `[Verified]`/`[OPEN]` |
| MAME-enhet för diskettfallet | Ingen ny enhet behövs, en devcb-koppling räcker | `[Likely]` |

## 2026-07-31: kandidat B (diskett) testad i tre varianter — ingen fungerar, koden reverterad

`[Verified]`. Tre `m_duart->ip0_w(...)`-villkor testades, alla mot
`./mess asr10booth -flop1 V350.img -video none -sound none -nothrottle
-seconds_to_run 30 -log` med stubben borttagen:

1. **`m_floppy_is_active && m_floppy_is_loaded`** (esq5505.cpp:387,
   ordagrant). Regression: fastnar direkt i upprepad
   `"PLEASE INSERT DISK"`, når aldrig `LOADING SYSTEM`.
2. **`m_floppy_is_loaded` ensamt** (motorobereoende — motiverat av att
   `$FB7C84` kontrolleras innan något motorkommando skickats).
   **Identisk regression**, byte-för-byte samma access-siffror som (1).
3. **`floppy_image_device::dskchg_r()`** (periodisk poll, 1 kHz, in i
   `ip0_w()` varje tick — inget push-callback finns för DSKCHG).
   **Återigen identisk regression.**

## 2026-08-12: korrigering — IP0 är floppy INDEX

`[Verified]`. De tre ovanstående varianterna falsifierar bara sina egna
predikat, inte diskettspåret som helhet. En senare felkodstapp utan
PC-beroende IPCR-stubb visade att den blockerande skrivningen var
`$049D=$05`, inte `$049D=$0D`: IP0-change uteblev, medan FDC-timeouten vid
`$FB8D5E` inte var blockeraren i den körningen.

Koppling testad: uPD72069 behåller sin interna
`floppy_image_device::setup_index_pulse_cb()` och publicerar samma indexnivå via
`idx_wr_callback()`, kopplad parallellt till `scn2681_device::ip0_w()`.
Resultat: V3.50 bootar till `FILE 1  TUTORIAL BNK` utan den gamla
PC-beroende `$FC4809` bit-4-stubben.

**Det verkliga IPCR-värdet ROM:en fick, mätt direkt (stubben borta, de
befintliga `ASR10_DUART_INPUT`-loggraderna), var `0x00` vid **båda**
portarna, i varje varv av (3):**

```
pc=fb7c84 addr=fc4809 value=00 role=semantic_input_change_bit4 branch_taken=1  (väntar på bit4=1, tar fel-grenen)
pc=fb7c30 addr=fc4809 value=00 role=ack_input_change_latch                     (väntar på bit4=1, ser aldrig det)
```

Bit 4 sattes alltså **aldrig**, trots att `m_duart->ip0_w()` verkligen
anropas (verifierat i föregående runda: `real_ipcr=0x11` gick att mäta
direkt efter en `ip0_w`-övergång innan stubben fanns kvar för att
maska den). `[Hypothesis]`, inte verifierat vidare i den här
uppgiften: den mest sannolika förklaringen är att `IPCR`:s
ändringsflagga sätts en gång, tidigt (t.ex. i `machine_start()`, före
`m_duart`s egen `device_reset()`, som sannolikt nollställer `IPCR`
oavsett `IP_last_state`), och att ingen **ytterligare** verklig
nivåändring sker innan `$FB7C84`/`$FB7C30` läser registret — eller att
någon annan, ospårad läsning av register 4 konsumerar flaggan
(`IPCR &= 0x0f` vid varje läsning) innan ROM:et själv hinner se den.
Ingen av dessa möjligheter undersöktes vidare — utanför uppgiftens
scope (`"Bygg ingen panelenhet"`).

**Acceptanskriteriet ("stubben bort utan regression") uppfylldes
inte.** Hela IP0-kopplingen reverterad i sin helhet — noll rader netto
kvar av den (klass-medlemmar, `machine_start()`-koppling,
funktionskroppar, `intrq_wr_callback`, allt borttaget); stubben
återställd till exakt sitt ursprungsskick. Bekräftat identiskt
efterbygge-beteende mot baslinjen (`TUNING KBD - HANDS OFF`, samma
loggrader). Endast CS3-access-oraklet (`docs/asr10/PLAN.md` fas 3,
punkt 0 i föregående uppdrag) kvarstår som en genuin, ocommitterad
addition efter den här uppgiften.

**Kandidat A (panelhändelse) är fortfarande obestriden och otestad.**
Nästa steg, om frågan tas upp igen, bör antingen (a) undersöka VARFÖR
den verkliga `ip0_w`-övergången aldrig syns i `IPCR` vid läspunkterna
(reset-ordning eller en konsumerande läsning — se ovan), eller (b)
testa kandidat A i stället för att fortsätta variera diskettvillkoret.

## 2026-07-31: fördröjd IP0-övergång efter reset — flaggan sätts verkligen, men konsumeras av en tredje, tidigare okänd läsare ($FB7BEE) innan ROM:et hinner se den

`[Verified]`. Föregående avsnitts hypotes — "någon annan, ospårad
läsning av register 4 konsumerar flaggan innan ROM:et själv hinner se
den" — testades direkt genom att flytta IP0-övergången till EFTER
CPU-reset i stället för vid monteringstillfället, och genom att
tillfälligt bredda diagnosen till att logga *varje* läsning av IPCR,
inte bara de två kända grindarna.

**Metod.** En `emu_timer`, startad i `machine_reset()` (garanterat
efter alla enheters `device_start()`/`device_reset()`, till skillnad
från `machine_start()`), fyrade 300 ms in i körningen och anropade då
`m_duart->ip0_w(...)` — en äkta flanke medan CPU:n kör, inte ett
tillstånd satt före DUART:ens egen reset. Stubben
(`ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84`) togs bort
under testet. Två polariteter kördes:

1. **0 → 1** (idle låg, timer sätter `ip0_w(1)`).
2. **1 → 0** (idle hög satt i `machine_reset()`, timer sätter
   `ip0_w(0)`) — provad eftersom riktiga diskettsignaler är aktivt
   låga.

**Resultat: identisk regression i båda fallen** — fastnar i upprepad
`"PLEASE INSERT DISK"`, når aldrig `LOADING SYSTEM`. Men till skillnad
från de tre diskettvillkoren i föregående avsnitt (som aldrig ens
producerade en mätbar nivåändring) bekräftade den breddade loggningen
att DUART:en verkligen registrerar övergången:

```
# 0 -> 1-fallet, timerns egen ip0_w:
ASR10_IPCR_EXPERIMENT event=any_ipcr_read ... pc=00fb7bee value=11   (bit4 satt, rätt nivå)
ASR10_DUART_INPUT pc=fb7c84 addr=fc4809 value=00 role=semantic_input_change_bit4   (flaggan redan borta)
ASR10_DUART_INPUT pc=fb7c30 addr=fc4809 value=00 role=ack_input_change_latch       (flaggan redan borta)

# 1 -> 0-fallet, spegelvänt:
ASR10_IPCR_EXPERIMENT event=any_ipcr_read ... pc=00fb7bee value=10   (bit4 satt, rätt nivå)
ASR10_DUART_INPUT pc=fb7c84 addr=fc4809 value=00 role=semantic_input_change_bit4
ASR10_DUART_INPUT pc=fb7c30 addr=fc4809 value=00 role=ack_input_change_latch
```

**Fyndet:** det finns en tredje, tidigare oidentifierad läsare av
IPCR vid `$FB7BEE`, som körs TIDIGARE i varje omförsöksvarv än de två
kända grindarna — uppmätt kadens: `$FB7BEE` avfyras ungefär var 1,7:e
sekund, `$FB7C84` följer ~0,75 s efter varje `$FB7BEE`. Eftersom
`IPCR`:s ändringsflaggor nollställs vid läsning (`IPCR &= 0x0f`,
`mc68681.cpp`), och `$FB7BEE` kommer först i programordning,
konsumerar `$FB7BEE` alltid en färsk flagga innan `$FB7C84`/`$FB7C30`
någonsin kan se den — **oavsett övergångens riktning**. Detta
bekräftades explicit för båda polariteterna ovan: `$FB7BEE`:s första
läsning efter övergången visar korrekt rått värde med bit4 satt
(`value=11` respektive `value=10`); de två kända grindarnas
efterföljande läsningar visar alltid flaggan redan borttagen.

**Detta är en avgörande, mätt förklaring** till varför ingen variant
av IP0-kopplingen någonsin kunnat få bort stubben — det är inte ett
tidsfrågeproblem eller en polaritetsfråga, det är en tredje
konsument som alltid kommer före. Vad `$FB7BEE` faktiskt gör med
värdet, om den är den "riktiga" avsedda konsumenten (med `$FB7C84`/
`$FB7C30` som sekundära/redundanta kontroller mot en annorlunda
modellerad signal), och om riktig hårdvaras DSKCHG/IP0-koppling är
nivåkänslig snarare än enstaka-flagga (så att alla tre läsarna skulle
se konsekvent tillstånd på riktig kisel) — inget av detta undersöktes
i den här uppgiften (ingen disassemblering av `$FB7BEE`s omgivande
kod gjordes). Kandidat för en FRAMTIDA uppgift, inte åtagen här.

**Acceptanskriteriet uppfylldes återigen inte.** IP0-kopplingen
reverterad i sin helhet igen (klass-medlemmar, `machine_start()`- och
`machine_reset()`-koppling, timer, load/unload-loggning, breddad
IPCR-diagnostik, funktionskroppar) — noll rader netto kvar av den.
Stubben återställd till exakt sitt ursprungsskick. Bekräftat identiskt
efterbygge-beteende mot baslinjen (`TUNING KBD - HANDS OFF`, samma
panelsekvens). CS3-access-oraklet (se nedan) committades separat som
den enda genuina, bestående tillägget från den här uppgiften.
