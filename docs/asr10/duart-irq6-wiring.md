# Att koppla in den riktiga avbrottet regredierar boten — vektorn är fel, inte kopplingen

2026-07-31. Läst: `docs/asr10/duart-imr.md`, `docs/asr10/tick-chain.md`,
`CLAUDE.md`. **Ändringen implementerades, kördes, verifierades regrediera
boten mot den etablerade baslinjen, och reverterades i sin helhet.**
`git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader
efter uppgiften. Ingenting committat i `asr10_boot.cpp`.

## 1. Kopplingen: exakt vad som gjordes

`[Verified]`. Precis kabeldragning mot en befintlig enhet, samma mönster
som `esq5505.cpp` redan använder för sin egen SCN2681:

```cpp
SCN2681(config, m_duart, XTAL(16'000'000) / 4);
m_duart->irq_cb().set_inputline(m_maincpu, 6);
```

Borttaget i samma ändring, allt beroende av skuggan:

* Medlemmarna `m_panel_c_isr`, `m_panel_c_imr`, `m_panel_c_irq6_asserted`.
* Funktionerna `panel_c_update_irq6()` och `duart_irq_model_enabled()`
  (den senare hade inga andra användare kvar när dess fyra anropsställen
  försvann).
* ISR-skuggans override av läsningar på `$FC480A` i
  `duart_panel_asr_candidate_r` — riktiga läsningar går nu alltid rakt
  igenom till `m_duart->read(5)`.
* IMR-skuggans spegling av skrivningar till `$FC480B` i
  `duart_panel_asr_candidate_w` — skrivningen till den riktiga enheten
  (`m_duart->write(...)`) fanns redan och var ovillkorlig; bara
  skuggkopian och dess `panel_c_update_irq6()`-anrop försvinner.
* Den anpassade IACK-vektorgrenen (`vector = 0x56` när
  `m_panel_c_irq6_asserted`) i `maincpu_iack_r` — död kod sedan sin enda
  källvariabel togs bort.

Kvar, orört, eftersom det är en helt separat mekanism (den fejkade
byte-injektionen för kanal B, som fortfarande inte är kopplad till
någon riktig seriekälla): `m_panel_c_rx_valid`, `m_panel_c_rx_byte`,
`m_panel_c_srb`, `panel_c_queue_rx()`, och alla `panel_reply_experiment_enabled()`-grindade SRB/RHRB-overrides.

**Rader till och rader från (den isolerade kopplingsändringen, mätt
innan reverteringen): +22 / -101, netto -79 rader.**

## 2-3. Räknat och verifierat: vektorn är fel

`[Verified]`, 30 s, V350.img, `ASR10_DIAG_PANEL_AUTORESPOND=1`, kört med
den inkopplade ändringen på plats:

```
ASR10_REAL_IRQ6_PROBE_COUNTS f88300=0 f8831a=0 f88328=0 f88352=0
```

**Fortfarande noll — precis som innan kopplingen.** Men avbrottet TAS nu
faktiskt, exakt en gång under hela körningen (mot noll gånger tidigare):

```
ASR10_M68K_IACK count=1 irq_level=6 default_autovector=1e returned_vector=1e
custom_vector=0 pc=f87fc0 sr=2600 sr_mask=6 ...
```

**Vektor 0x1E (30) — autovektor, adress $78 — exakt som uppgiften
förutsåg.** Ingen IV6-logik finns (ingen interruptcontroller, ingen
anpassad vektorleverantör kopplad till nivå 6:s IACK), så 68000-kärnan
föll tillbaka på standardautovektorn i stället för $158.

**Detta stämmer exakt mot redan existerande, mycket äldre dokumentation**
(`docs/asr10/architecture.md`, `docs/asr10/boot-flow.md`, från tidigare
sessioners direkta ROM-disassemblering, inte från den här uppgiften):

```
architecture.md:  IRQ6 -> vector 0x56 -> f884be   (den RIKTIGA handlern)
boot-flow.md:     autovectors 0x19..0x1f -> ERROR 139 unused vector
```

Vektor 0x1E ligger inom `0x19..0x1f`. ROM:ens EGEN vektortabell pekar
alltså autovektorn mot en generisk "unused vector"-stubb, inte mot den
riktiga IRQ6-handlaren på `$F884BE`. Det är inte ett fel i kopplingen —
avbrottsbegäran levereras nu genuint och korrekt till CPU:n — det är att
rätt VEKTOR aldrig levereras utan `mc68302int.cpp` (eller åtminstone en
enhetsstyrd vektorleverantör för nivå 6:s IACK, t.ex.
`mc68681_device::get_irq_vector()`, som `esq5505.cpp` redan använder för
just detta syfte).

## 4. Primärtabellen: regression, inte framsteg

`[Verified]`. Jämfört med `runtime-cycle.md`s etablerade "sent varv"
(alla sex platser dispatchade, `pending==partner` för samtliga):

```
entry=23f6  pending=00 partner=00   LIKA  (dispatchad)
entry=240c  pending=00 partner=01   OLIKA (fortfarande väntande)
entry=2422  pending=00 partner=01   OLIKA
entry=2438  pending=00 partner=01   OLIKA
entry=244e  pending=00 partner=01   OLIKA
entry=2464  pending=00 partner=01   OLIKA
```

**Bara 1 av 6 platser hann dispatchas, mot 6 av 6 i baslinjen.** Boten
avviker in i felhanteringen tidigare i schemaläggningen än den
tidigare, ohindrade idle-scanen ens hann komma igång.

## 5. Panelen: ERROR 139 — REBOOT ?, inte TUNING KBD

`[Verified]`, reproducerat två gånger, deterministiskt:

```
"q" -> "ENSONIQ ASR-10" -> "LOADING SYSTEM" -> "q" -> "q" -> "q"
-> "ERROR 139 - REBOOT ?"
```

Jämfört med den etablerade baslinjen (`"q" -> "ENSONIQ ASR-10" ->
"LOADING SYSTEM" -> "q" -> "q" -> "TUNING KBD - HANDS OFF" -> "q"`, och
sedan stilla för resten av körningen): **`TUNING KBD - HANDS OFF` nås
aldrig.** `KEYBOARD TUNED` (uppgiftens fråga) är alltså ännu mer
otillgängligt än förut — boten kraschar in i ett explicit felmeddelande
tidigare än den gamla, ofarliga men oändliga idle-scanen någonsin gjorde.

**Det här är en regression, inte framsteg, mätt mot "hur långt kommer
boten."** Den är samtidigt en extremt värdefull, exakt förutsagd
regression: den bekräftar konkret och otvetydigt att exakt en sak
saknas (rätt vektor för nivå 6), inte flera. `ERROR 139` är ROM:ens EGEN,
redan existerande generiska "oväntat avbrott"-hantering — inte ett
emulatorkrasch, utan legitimt ROM-beteende som triggas av att en riktig
elektrisk signal (som verkligen ska finnas på riktig hårdvara) nu
levereras till en CPU-kärna som saknar den krets (MC68302:ans
interruptcontroller) som på riktig hårdvara skulle ha försett den med
rätt vektor.

## 6. Behövs `ASR10_DIAG_PANEL_AUTORESPOND` fortfarande?

`[Verified]`. Ja, oförändrat. Körning utan `-flop1`-autorespond (samma
inkopplade ändring, annars) fastnar vid `"LOADING SYSTEM"` precis som
innan — autorespond och IRQ6-skuggan är, som uppgiften själv
misstänkte, "samma familj" bara i den bemärkelsen att båda är
handmodellerade DUART-substitut, men de täcker helt olika register
(SRB/RHRB byte-leverans kontra ISR/IMR-driven avbrottsleverans). Att ta
bort den ena påverkar inte den andras nödvändighet. Byte-injektionen
(`m_panel_c_rx_valid`/`srb`/`rx_byte`, oberörd av den här ändringen)
förblir det enda sättet ROM:ens panelbyte-polling någonsin ser data,
eftersom kanal B fortfarande inte är kopplad till någon riktig
seriekälla.

## Slutsats och rekommendation

**Reverterat i sin helhet.** Kopplingen i sig är korrekt (verifierad,
`+22/-101`-diffen var ren och kompilerade rent), men den gör
emulatorns beteende sämre utan en efterföljande, mindre men nödvändig
del: en vektorleverantör för nivå 6:s IACK. Två vägar framåt, ingen
byggd här:

* **Minsta möjliga fix**, om bara detta specifika problem ska lösas:
  koppla `m_duart`s egen `get_irq_vector()`-metod (redan använd av
  `esq5505.cpp:457`) till `cpu_space_map`s nivå-6-post i stället för
  att förlita sig på autovektorn. Det är fortfarande "kabeldragning mot
  en befintlig enhet", inte en ny modell, och skulle sannolikt räcka
  för att nå `$F884BE` (den riktiga handlern) i stället för `ERROR
  139`-stubben.
* **Den fullständiga vägen**, redan planerad i `PLAN.md` fas 3 steg 2:
  `mc68302int.cpp`, som uppgiftens egen TOLKNING pekade mot.

Kopplingen bör landas SAMTIDIGT som en av dessa två — aldrig ensam,
eftersom den ensam är bevisligen sämre än dagens (kompensationslösa,
men ofarliga) tillstånd.

## Städning

`git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar netto noll
rader. Inget committat i `asr10_boot.cpp` — regressionen gör att
CLAUDE.md/uppgiftens eget villkor ("Committa bara om ingen regression")
inte är uppfyllt.
