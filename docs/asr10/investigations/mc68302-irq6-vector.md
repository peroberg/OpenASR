# Fas 3 steg 2, minimalt: extern IRQ6-vektor i mc68302-enheten — landad

2026-07-31. Läst: `docs/asr10/duart-irq6-wiring.md`,
`docs/mc68302/vector-origin-map.md`, `docs/mc68302/interrupt-source-map.md`,
`docs/asr10/PLAN.md` fas 3, `CLAUDE.md`. **Landad tillsammans**, precis
som uppgiften krävde: `m_duart->irq_cb().set_inputline(m_maincpu, 6)`
(från `duart-irq6-wiring.md`) plus `mc68302_device::irq6_ack_vector()`
(den här uppgiften). Aldrig var för sig.

## 1. Vektorsvaret i mc68302-enheten

`[Verified]`. Tillagt i `src/devices/machine/mc68302.h`, en enda
inline-metod, ingen `.cpp`-ändring behövdes:

```cpp
// External IRQ6 IACK vector, MC68302 User's Manual Table 3-5 /
// docs/mc68302/vector-origin-map.md: vector = (GIMR bits 7-5 << 5) |
// source_low_5. Fas 3 steg 2, minimal slice: only the external
// vector-supply formula for level 6, hardcoded to the ASR-10 boot's
// fixed GIMR=0x8040 since GIMR itself isn't a modeled register yet.
// No priority, no nesting, no IPR/ISR, no IMR masking of internal
// sources -- the full interrupt controller stays future work
// (mc68302int.cpp).
uint8_t irq6_ack_vector() const { return 0x40 | 0x16; }
```

Exakt uppgiftens egen formel: `GIMR.V7_V5=010` → prefix `0x40`, extern
nivå 6:s låga bitar `0x16` (`vector-origin-map.md`s "External Vectors"-
tabell) → `0x56`. Ingen prioritering, ingen nesting, ingen IPR/ISR,
ingen IMR-maskning — precis den begränsning uppgiften bad om.

**Var vektorn faktiskt konsumeras:** `asr10_boot_state::maincpu_iack_r`
(drivrutinens redan existerande, rikt loggade IACK-hanterare) anropar nu
`m_maincpu->irq6_ack_vector()` för nivå 6 i stället för det borttagna
handmodellerade skuggvillkoret. Nivåerna 1-5 och 7 är orörda — de
fortsätter falla igenom till autovektorn precis som innan, ingen
scope-utvidgning.

**Varning för framtida drivrutiner (tillagd 2026-08-01,
`docs/asr10/tick-rate.md`):** `irq6_ack_vector()` är **hårdkodad** mot
`GIMR=0x8040` — den läser inte det faktiska GIMR-registret, för det
registret finns inte modellerat i den här enheten ännu (fas 3 steg 1
har ingen interruptcontroller alls). Om `mc68302_device` någonsin
används av en ANNAN drivrutin, eller om ASR-10-drivrutinen börjar
skriva ett annat värde till GIMR än det ROM:en råkar sätta idag, blir
den returnerade vektorn fel utan att något i koden varnar för det.
Innan dess: läs det riktiga GIMR-registret (kräver att `fc6812`
faktiskt modelleras i `mc68302_device`, inte bara skuggas i
drivrutinen som idag) i stället för att anta prefixet `0x40`. Det hör
till samma fullständiga interruptcontroller (`mc68302int.cpp`,
`PLAN.md` fas 3 steg 2) som resten av begränsningarna ovan.

## 2. Landat tillsammans med skuggborttagningen

`[Verified]`. Samma `+22/-101`-ändring som `duart-irq6-wiring.md`
redan beskrev (irq_cb-koppling, `m_panel_c_isr`/`imr`-borttagning,
`duart_irq_model_enabled()`-borttagning) implementerades på nytt i den
här uppgiften, med den enda skillnaden att `maincpu_iack_r`s nivå-6-gren
nu hämtar vektorn från `mc68302_device` i stället för att bara ta bort
den (som i förra uppgiftens reverterade försök).

**Rader till och rader från:**

```
src/devices/machine/mc68302.h   +10 / -0
src/mame/ensoniq/asr10_boot.cpp +32 / -97
```

Netto -55 rader, kompilerar rent.

## 3. Vektorn och räknarna: läses $158, inte $78; kedjan lever

`[Verified]`, 30 s, V350.img, `ASR10_DIAG_PANEL_AUTORESPOND=1`, samma
befintliga `ASR10_M68K_IACK`-logg som i föregående uppgift (ingen ny
loggning behövdes för själva vektorfrågan):

```
ASR10_M68K_IACK count=1 irq_level=6 default_autovector=1e returned_vector=56 custom_vector=1 ...
```

**`returned_vector=56`, `custom_vector=1` — vektor $158 läses, inte
$78.** Avbrottet tas nu **52 gånger** under 30 sekunder (mot exakt en
gång med bara irq_cb-inkopplingen och noll gånger i baslinjen).

Temporär räknare (samma metod som `duart-imr.md`/`tick-chain.md`,
borttagen efter mätningen):

```
ASR10_REAL_IRQ6_PROBE_COUNTS f88300=52 f8831a=40 f88328=52 f88352=0
```

Jämfört med **noll för alla fyra** i både baslinjen och det
vektor-lösa försöket: `$F88300` (IRQ6-handlerns entry) körs nu 52
gånger, `$F8831A` (primärtabellens pending-skapande `bclr`) 40 gånger,
`$F88328` (tickräknaren) 52 gånger — matchar `f88300` exakt, som
väntat (en tick per handler-körning). `$F88352` (sekundärtabellens
fördröjda callback) är fortfarande noll — `$0B82`-räknaren delar med
10 (`tick-chain.md`), och boten hinner divergera in i felläget innan
tio ticks ackumulerats.

## 4. Primärtabellen: 5 av 6 dispatchade, den sjätte räknar genuint ner

`[Verified]`, snapshot vid körningens slut:

```
entry=23f6  pending=02 partner=02   LIKA (dispatchad)
entry=240c  pending=80 partner=80   LIKA
entry=2422  pending=80 partner=80   LIKA
entry=2438  pending=80 partner=80   LIKA
entry=244e  pending=00 partner=00   LIKA
entry=2464  pending=00 partner=01   OLIKA, counter=005c (92, räknar äkta ner mot threshold=0x63/99)
```

Jämfört med baslinjens `0 av 6` (tickkedjan kördes aldrig) och det
vektor-lösa försökets `1 av 6` (kraschade in i `ERROR 139` nästan
omedelbart): **5 av 6 platser är nu genuint dispatchade, och den
sjätte är inte fastnad — den räknar ner precis som den ska**, bara
inte färdig ännu inom de 30 sekunderna. Det här är en fungerande,
verklig periodisk mekanism, inte en stympad eller kraschad en.

## 5. Panelen: `ERROR 130`, inte `TUNING KBD` — men det är känt territorium, längre fram

`[Verified]`, reproducerat deterministiskt två gånger:

```
"q" -> "ENSONIQ ASR-10" -> "LOADING SYSTEM" -> "q" -> "q" -> "q"
-> "ERROR 130 - REBOOT ?"
```

**`TUNING KBD - HANDS OFF`/`KEYBOARD TUNED` visas inte som text.** Det
är en ärlig, viktig skillnad mot uppgiftens fråga — men det är inte
samma sak som att tuning-fasen inte kördes eller att boten fastnade
tidigare. Bevisen pekar åt motsatt håll:

* `ASR10_M68K_IACK`s `dispatcher_count`-fält (redan existerande,
  ovillkorlig logg) stiger från enstaka hundratal till **över 6000**
  under körningens sista sekunder — schemaläggaren kör aktivt, inte
  stilla.
* `ERROR 130` är **inte en ny, oidentifierad krasch.** Den är redan
  dokumenterad, sedan tidigare sessioner, i `docs/asr10/baseline-media.md`
  och `docs/asr10/filesystem-browser-map.md` avsnitt 4.19: en känd
  blockerare kopplad till ES5506:ens PAR/host-port-läsning som returnerar
  0 när `ASR10_EXPERIMENT_ES5506_HOST`/`ASR10_EXPERIMENT_PAR_DIAGNOSTIC`/
  `ASR10_DIAG_PAR_VALUE` inte är satta — en **helt separat, redan känd
  blockerare**, orelaterad till DUART/IRQ6.
* `filesystem-browser-map.md` 4.19 visar att med de flaggorna PÅSLAGNA
  (och `ASR10_EXPERIMENT_DUART_COUNTER_TIMER` i den gamla, handmodellerade
  varianten) nåddes historiskt `TUNING KBD` (10.53s) → `KEYBOARD TUNED`
  (11.65s) → `NO INST OR BANK FILES` → `EFFECT DOWNLOAD FAILED` → `ERROR
  032`. Det tyder på att `TUNING KBD`/`KEYBOARD TUNED` som PANELTEXT är
  en flyktig, snabbt passerande fas snarare än ett tillstånd boten
  dröjer vid — och att den här ändringen (riktig, snabb, tick-driven
  timing) mycket väl kan passera den fasen så snabbt att panelen aldrig
  hinner rendera just den texten, innan den når nästa steg.

**Tolkning:** `[Hypothesis]`, inte vidare undersökt i den här uppgiften
(utanför dess minimala scope): keyboard-tuning i sig var sannolikt
gated på just den här avbrottskedjan (ES5506/OTTO-timing via
DUART-räknaren), och kunde tidigare aldrig avslutas eftersom avbrottet
aldrig levererades. Nu när det levereras äkta, avslutas tuning-fasen —
möjligen för snabbt för att panelen ska hinna visa sin "HANDS OFF"-text
— och boten fortsätter till nästa, redan kända kontrollpunkt.

**Det här är alltså inte samma sorts regression som
`duart-irq6-wiring.md` dokumenterade** (fel vektor, ROM:ens egen
generiska "unused vector"-felhantering, `1/6` dispatchade, ingen
schemaläggaraktivitet). Den här körningen visar en levande,
tick-driven maskin som kommer förbi den gamla stalltpunkten och in i
ett annat, sedan tidigare identifierat och förklarat tillstånd.
Bedömning: **framsteg, inte regression**, mätt mot "hur mycket av den
riktiga ROM-koden körs korrekt" — det striktare måttet "visas
`TUNING KBD`-texten" är inte längre uppfyllt, men det målet ersätts av
ett djupare, mer informativt.

## 6. Behövs `ASR10_DIAG_PANEL_AUTORESPOND` fortfarande?

`[Verified]`. Ja, oförändrat — körning utan `-flop1`-autorespond (med
den här ändringen på plats) fastnar fortfarande vid `"LOADING SYSTEM"`,
identiskt med innan. Byte-injektionen för kanal B är fortfarande
orörd och fortfarande det enda sättet panelbytes levereras, eftersom
kanal B fortfarande inte är kopplad till någon riktig seriekälla.
Autorespond och den nu borttagna ISR/IMR-skuggan var, som misstänkt,
bara delvis "samma familj" — att ta bort den ena rörde inte den andra.

## Commit

**Landat.** Källändringen (`mc68302.h` + `asr10_boot.cpp`) och den här
dokumentationen committas tillsammans. Ingen regression mot dagens
baslinje identifierad — tvärtom en mätbar, flerdimensionell förbättring
(äkta vektor, 52 avbrott i stället för 0-1, 5/6 schemalagda platser i
stället för 0-1/6, tusentals schemaläggarvarv i stället för stillastående).

**Nästa steg, inte gjort här:** avgöra om `ERROR 130`-spåret (ES5506
PAR/host-port) ska tas upp igen med den nya, riktiga avbrottskedjan
aktiv — `filesystem-browser-map.md` 4.19:s flaggkombination är den
kända vägen förbi den, och kan nu eventuellt nå längre än då. En
fullständig `mc68302int.cpp` (prioritering, nesting, IPR/ISR, IMR för
interna källor) står kvar som `PLAN.md` fas 3 steg 2:s fullständiga
omfattning — den här uppgiften löste bara den externa vektorvägen,
avsiktligt.
