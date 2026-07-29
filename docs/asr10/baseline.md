> **RÄTTELSE 2026-07-29.** Denna baslinje togs UTAN `-flop` och är därmed
> `running.md`s *no-media regression run*. "PLEASE INSERT DISK" är det
> dokumenterat förväntade utfallet i det läget — ingenting hade regresserat.
> Den djupa bootkedjan (LOADING SYSTEM -> TUNING KBD HANDS-OFF ->
> KEYBOARD TUNED -> NO INST OR BANK FILES) kräver
> `-flop floppies/asr10booth/V161.img`. En med-media-baslinje saknas och
> måste tas. Felet var i uppgiftsformuleringen, inte i körningen.

# ASR-10 baseline, pre-ombyggnad

2026-07-29. Återgångspunkt innan städningen i `PLAN.md` fas 1 påbörjas.
Commit `80ee114be1a` på branchen `asr10-architecture-cleanup`
("asr10: add root directory descriptor/table diagnostic tracing").
Ingen logik ändrad, ingen instrumentering tillagd eller borttagen i den
här uppgiften — enbart mätning av nuläget.

## Radantal

`[Verified]`, `wc -l`:

```
10832  src/mame/ensoniq/asr10_boot.cpp
 5878  src/mame/ensoniq/asr10_boot_codex.cpp
  228  src/mame/ensoniq/esqasr.cpp   (upstream-skelettet, oförändrat)
```

Matchar siffrorna i `PLAN.md` avsnitt 2.

## Byggtid

`[Verified]`:

```
make -j12 SUBTARGET=mess SOURCES=src/mame/ensoniq/asr10_boot.cpp
2,72s user 1,32s system 101% cpu 3,974 total
```

Detta är en **inkrementell** länkning, inte en kallbygge: objektfilen
för `asr10_boot.cpp` var redan aktuell från en tidigare session (samma
arbetsträdsändring låg redan i working tree innan den här uppgiften
startade). Bygget kompilerade om `formats/all.cpp`, `version.cpp` och
drivlistan, sedan länkade om binären `mess`. En kallbygge av hela
`mess`-målet tar väsentligt längre — den siffran är inte mätt här och
ska inte förväxlas med 3,974 s.

Byggd binär: `./mess` (81 908 936 byte, 2026-07-29 17:36). Byggsystemet
för det här trädet producerar en binär vid namn `mess`, inte `mame` —
se körningsavsnittet nedan för rätt kommandorad.

## Drivernamn

`[Verified]`. Det körbara namnet för denna experimentharness är
**`asr10booth`**, inte `asr10`:

```
src/mame/ensoniq/asr10_boot.cpp:10832
CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state,
     empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", 0)
```

`asr10` (utan "booth") är upstream-maskinen i `esqasr.cpp`, byggd med
en naken `M68000` (se `PLAN.md` avsnitt 1) — den kompileras inte in när
`SOURCES=` filtreras till `asr10_boot.cpp`. De två får inte blandas
ihop i kommandon eller dokumentation framöver.

Kommandot i uppgiftsbeskrivningen (`mame asr10 ...`) körs alltså här
som:

```
./mess asr10booth -video none -sound none -nothrottle -seconds_to_run 30
```

## Headless-körning

`[Verified]`. Exit code 0, ingen krasch:

```
Average speed: 1877.48% (29 seconds)
```

`-nothrottle` gör att 30 simulerade sekunder körs på en bråkdel av
väggtiden (drygt 1,5 s reell tid vid ~1877 % hastighet).

## Vad displayen skriver

`[Verified]`, ur `logerror`-taggen `ASR10PANEL` (kräver `-log`, se
nedan) i en körning **utan** monterad diskett:

```
"q"                        — initial skräptext direkt efter reset
"   ENSONIQ  ASR-10    "   — startskärm
"  PLEASE INSERT DISK  "   — upprepas (13 gånger under 30 s), väntar på media
```

Ingen "NO INST OR BANK FILES" observerades i den här körningen — det
textmeddelandet nås först efter att en diskett monterats och FDC-koden
kommit förbi insert-disk-väntan, vilket kräver ett `-flop1`-argument
som inte gavs här. `ASR10PHASE phase=post_insert_disk_prompt` (rad
1049 i `error.log`) är den enda fasövergången som nås; körningen
kommer aldrig till `post_loading_system_panel`.

Efter tre repetitioner av insert-disk-loopen slår harnessets egen
hängdetektor till:

```
ASR10HANG reason=max_poll_count pc=fb8d6e ... poll_count=4000001
```

`[Likely]`: detta är en instrumenterad varning om en poll-loop som körs
väntat länge (väntar på diskett som aldrig kommer i den här körningen),
inte ett tecken på att emulatorn har fastnat — körningen fortsätter
till `-seconds_to_run 30` och avslutar normalt med exit 0.

## `-debuglog`

`[Verified]`. `-debuglog` skriver bara `debug.log` om `-debug` också är
satt (`src/emu/machine.cpp:300`: `if (options().debug() &&
options().debuglog())`). Utan `-debug` skapas ingen fil alls — det
förklarar varför en körning med enbart `-debuglog` inte gav något att
rapportera.

Med `-debug -debuglog` öppnas MAME:s interaktiva debuggerkonsol, som
blockerar processen även med `-video none`; den svarar inte på
`-seconds_to_run` och måste dödas manuellt (testat: fortfarande igång
efter 12 s, `kill -9` krävdes). `debug.log` innehöll då enbart
bannern:

```
MAME debugger version 0.288 (mame0288-270-g80ee114be1a)
Currently targeting asr10booth (ASR-10 boot harness (experiment))
```

`[Verified]`: `-debug`/`-debuglog` är alltså inte kompatibelt med
headless-automation i det här trädet utan ett skript som matar
debuggerkommandon (t.ex. `-autoboot_script` eller ett externt
kommando-script). Ordinär diagnostik (fasövergångar, panel-text,
FDC/DUART-tillstånd) går redan ut via vanlig `logerror()` och fångas
med `-log` (skriver `error.log`) utan att röra debuggerläget — det är
vägen som användes för avsnittet ovan.

## Stubbade enheter

`[Verified]`, från taggfrekvens i en 30-sekunders körning med `-log`
(7 070 loggrader):

| Tagg (frekvens) | Vad den visar |
|---|---|
| `ASR10FDCSTATE` / `ASR10FDCSTATE_READ` (1 775 + 912) | Handmodellerad FDC-tillståndsmaskin på `0xfc4000` (`upd72069_fdc_candidate` i kommentarerna, men fortfarande handkodad — inte `upd72069_device`) |
| `ASR10_DUART_INPUT` / `ASR10_DUART_INPUT_STUB` (26 + 13) | Handmodellerad DUART-insignal, inte `mc68681`/`scn2681` |
| `ASR10STATE049D`, `ASR10_049D_WRITE`, `ASR10_04C6`, `ASR10LOWMEM04EE` m.fl. | Handrullade skuggregister för lågminnesfält som ROM:en pollar under insert-disk-väntan |

Detta bekräftar `PLAN.md` avsnitt 5 (raderingslistan): DUART:en och
FDC:n är fortfarande handmodellerade i `asr10_boot.cpp` och inte routade
mot `mc68681.cpp` / `upd72069_device`, vilket är precis vad fas 1 ska
ersätta.

## Slutsats: fungerar headless-körningen?

**Ja, `[Verified]`.** `./mess asr10booth -video none -sound none
-nothrottle -seconds_to_run 30` kör hela vägen till `exit 0` utan
krasch, med rimlig hastighet (~1877 % realtid) och begriplig
diagnostik via `-log`. Detta är den enda kombinationen som testades och
fungerade; `-debug -debuglog` fungerar inte headless och kräver ett
separat angreppssätt (Lua-autoboot-skript eller
debugger-kommandofil) om debuggerloggning behövs senare. Resten av
planen i `PLAN.md`, som förutsätter upprepade headless-körningar för
att mäta bootframsteg, vilar på ett giltigt fundament.
