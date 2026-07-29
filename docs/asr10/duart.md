# DUART: hand-modell ersatt med MAME:s scn2681_device

2026-07-29. Fas 1 av `PLAN.md`, DUART-delen (inte FDC:n). Commit på
`asr10-architecture-cleanup`, byggd på `80ee114be1a` (baslinjen i
`docs/asr10/baseline.md`).

## Radantal

`[Verified]`, `wc -l` och `git diff --stat` mot `80ee114be1a`:

```
src/mame/ensoniq/asr10_boot.cpp       10832 -> 10647   (-185)
src/mame/ensoniq/asr10_boot_defs.cpp    206 -> 204     (-2)
src/mame/ensoniq/asr10_boot_defs.h       48 -> 47      (-1)
                                                total -188

git diff --stat:
 asr10_boot.cpp       | 271 ++++++-----------------------------
 asr10_boot_defs.cpp  |   2 -
 asr10_boot_defs.h    |   1 -
 3 files changed, 43 insertions(+), 231 deletions(-)
```

Rent tillägg: 43 rader (device-registrering, klockkommentar, en
delegeringsrad per läs/skriv). Allt annat i diffen är radering. Ingen
ny instrumentering.

## Vad som togs bort (den handskrivna SCN2681-modellen)

`[Verified]`, grep mot arbetsträdet visar noll träffar kvar för:

* `m_duart_counter_timer` (emu_timer), `m_duart_counter_running`
* `m_duart_ctu_preload`, `m_duart_ctl_preload`, `m_duart_acr`
* `m_duart_counter_start_count`, `m_duart_counter_fire_count`,
  `m_duart_counter_stop_count`
* `m_duart_panel_asr_shadow[0x10]` (registerskuggan för hela
  0xFC4800-0xFC481F-blocket)
* Funktionerna `duart_counter_start`, `duart_counter_stop`,
  `duart_counter_arm_periodic`, `duart_counter_check_implicit_start`,
  `TIMER_CALLBACK_MEMBER(duart_counter_terminal_count)`
* `m_duart_counter_boundary_tap` (diagnostisk läs-tap på 0xFC4820,
  ett ord bortom det handmodellerade blockets antagna gräns -- omöjlig
  att motivera efter att en riktig, exakt 32-byte-stor enhet finns)
* `m_duart_vfx_shadow[0x10]` och hela `duart_vfx_candidate_r/w`-paret
  på 0x280000 (den `[DISPROVEN]` ES5570-VFX-kandidaten för just DUART;
  träffades aldrig, ingår i raderingslistan i `PLAN.md` avsnitt 5) plus
  motsvarande `trace_region::DUART_VFX_CANDIDATE`-post i
  `asr10_boot_defs.h`/`.cpp`

**Kvar, medvetet:** `m_duart_counter_timer_enabled` (bool-flaggan,
`ASR10_EXPERIMENT_DUART_COUNTER_TIMER`) finns fortfarande -- den styr
numera bara ett par sedan tidigare befintliga, orelaterade FC2xxx/
FC3xxx-schemaläggardiagnostiker (`m_hook_fc2068_tap`, klustertraceloggarna
kring FC2D40/FC3000, `log_duart_counter_watched_pc`,
`log_divzero_exception_frame`, `log_primary_slot_snapshot_once`,
`log_timer_secondary_callback`) som råkar dela namn med DUART-experimentet
men inte modellerar DUART-register. Att riva ut den flaggan hade krävt
att också riva ut de orelaterade diagnostikerna, vilket är utanför den
här uppgiften (”inte FDC:n” -- och inte ES5510/schemaläggarspåren
heller). `ASR10_DUART_INPUT_CHANGE_STUB` och
`ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84` finns också
kvar: de modellerar en extern insignal (IPCR bit 4 vid pc=fb7c84) som
inte kommer från något register i DUART-chippet självt och som det inte
finns någon verklig ersättning för i det här varvet -- de blev alltså
inte överflödiga, vilket var villkoret för att ta bort dem.

## Vad som ersatte den

`[Verified]`. `src/devices/machine/mc68681.h` inkluderad;
`required_device<scn2681_device> m_duart` tillagd; instansierad i
`asr10_boot()`:

```cpp
SCN2681(config, m_duart, XTAL(16'000'000) / 4);
```

Adressen 0xFC4800-0xFC481F är oförändrad (redan `[Verified]` i
`PLAN.md` som U20:s fönster) och pekar fortfarande på
`duart_panel_asr_candidate_r/w` -- men de funktionerna delegerar nu
registerläsning/skrivning till `m_duart->read(word)` /
`m_duart->write(word, u8(data))` för alla adresser, och lämnar bara
kvar det som INTE är registermodellering:

* Bytet som skrivs till THRB (0xFC4817) matas fortfarande direkt in i
  `panel_receive_byte()` -- det finns ingen `esqpanel`-liknande enhet i
  det här harnesset som tar emot äkta seriebitar, så panelens
  textbygge (som redan fanns, orört av den här uppgiften) fortsätter
  att tappa av bytet direkt vid CPU-skrivningen i stället för via
  `m_duart`s `b_tx_cb()`. **Detta är den återstående luckan**: kanal B
  är inte seriellt kopplad till något; den riktiga enhetens eget
  TX/RX-tillstånd (SRB, ISR) är äkta, men ingenting matar dess RX-FIFO
  och ingenting lyssnar på dess TX-bitström.
* `panel_reply_experiment_enabled()`-spåret (`m_panel_c_isr/srb/imr/
  rx_valid`, av-som-standard experiment för panelsvar) och
  IPCR-bit4-stubben ovan ligger kvar oförändrade, som overrides ovanpå
  den riktiga enhetens värde -- exakt samma villkor som innan.

## Kanal B: fortfarande inte kopplad till m_duart

`[Verified]`. Uppgiften bad om ett stegvis flytt av panelvägen till
`m_duart` med körning efter varje steg. Efter kartläggningen visade
det sig att det inte finns någon mottagande enhet (ingen `esqpanel_*`
i det här harnesset) att koppla `m_duart->b_tx_cb()` till, och ingen
sändande enhet att koppla `m_duart->rx_b_w()` från -- panelprotokollet
lever helt i C++-metoder i `asr10_boot_state` själv
(`panel_receive_byte`, `flush_panel_text`, m.fl.), inte i en separat
MAME-enhet med serieinterface. Att koppla in detta på riktigt är ett
annat, större arbete (bygga eller återanvända en byte-till-bit-adapter,
eller portera in `esqpanel`-mönstret) och inte en ren
"flytta registeraccess till m_duart"-övning som resten av den här
uppgiften var. Det är därför kvar som en explicit lucka snarare än
genomfört i små steg -- det fanns inget mellanläge att testa mellan.

## Tick-period, X1 = 4,000 MHz

`[Verified]` via källkodsläsning av `src/devices/machine/mc68681.cpp`:
`get_ct_rate()` case ACR[6:4]==`0b11_0` (timer-läge, klockval 2 =
X1/CLK) sätter `rate = clock()` rakt av -- ingen intern delning.
`duart_timer_callback()` togglar `half_period` vid varje terminal
count och sätter `INT_COUNTER_READY` bara varannan gång
(`if (!half_period) set_ISR_bits(...)`), vilket ger exakt samma
`2*N/X1`-formel som den gamla handmodellen redan antog (samma kod
citerade denna formel i sin kommentar).

Med `SCN2681(config, m_duart, XTAL(16'000'000) / 4)` är
`clock() == 4 000 000 Hz` istället för det tidigare antagna
3 686 400 Hz. Vid CTUR:CTLR = 0x07D0 = 2000 (samma exempel som
`PLAN.md` avsnitt 3):

```
gammal modell   2 x 2000 / 3 686 400 Hz = 1,0851 ms
ny (riktig enhet) 2 x 2000 / 4 000 000 Hz = 1,0000 ms
```

`[Hypothesis]` fortfarande i meningen att ROM:et i den här 30-sekunders
körningen aldrig programmerar ACR/CTUR/CTLR -- boten kommer inte
längre än "PLEASE INSERT DISK" i någon av körningarna (se nedan), så
1,0000 ms är räknat från chippets bekräftade formel och den valda
klockan, inte observerat vid körning ännu.

## Körning jämfört med baslinjen

`[Verified]`. `./mess asr10booth -video none -sound none -nothrottle
-seconds_to_run 30 -log`, exit 0 båda gångerna (efter borttagningen av
registermodellen och igen efter borttagningen av boundary-tappen):

```
"q"                        -- samma skräptext som baslinjen
"   ENSONIQ  ASR-10    "   -- samma startskärm
"  PLEASE INSERT DISK  "   -- samma upprepning, samma fas-markör
ASR10PHASE phase=post_insert_disk_prompt pc=f89cb0  -- samma landmärke
```

Displayen når **exakt lika långt** som `docs/asr10/baseline.md`: samma
tre textlägen, samma `post_insert_disk_prompt`-fasmarkör. Ingen
regression från att byta ut registermodellen mot `m_duart`, och ingen
regression från att ta bort boundary-tappen (väntat -- den var
skrivskyddad diagnostik utan sidoeffekt på emulerat tillstånd).

Absolut poll-count till hängdetektorn skiljer sig något mellan
körningar (fb8d6e i en körning, f87dda i en annan, olika `poll_count`)
-- `[Likely]` brus från exakt instruktionstiming kring den riktiga
enhetens interna räknare, inte en funktionell skillnad, eftersom
sluttexten och fasmarkören är identiska i alla körningar.

## Vad som återstår

1. **Kanal B-transport.** Se ovan -- ingen riktig seriekoppling finns
   ännu; `panel_receive_byte`-tappet vid THRB-skrivning är kvar som
   överbryggning. Kräver antingen en `esqpanel`-liknande mottagarenhet
   eller ett medvetet beslut att fortsätta byte-tappa tills en sådan
   enhet byggs (utanför den här uppgiften).
2. **`m_duart_counter_timer_enabled`-namnet** pekar inte längre på
   något DUART-register -- det är nu bara en verbositetsflagga för
   FC2xxx/FC3xxx-spår. Missvisande namn, men att döpa om det rör kod
   utanför DUART-scopet.
3. Tick-perioden (1,000 ms) är inte ännu bekräftad vid körning --
   kräver att ROM:et kommer förbi "PLEASE INSERT DISK" till
   DUART-timerinitieringen, vilket är precis vad resten av `PLAN.md`
   fas 1 (FDC/DUART-mappning tillsammans) siktar på.
