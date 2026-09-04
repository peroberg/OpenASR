# Arbetsregler för det här trädet

Gäller ASR-10-arbetet i `src/mame/ensoniq/` och `docs/asr10/`.
Läs först `docs/asr10/current-status.md`,
`docs/asr10/DOCUMENTATION-MANIFEST.md` och
`docs/asr10/reference/methods-hypothesis-management.md`. Äldre handoff- och
investigation-filer är provenance, inte automatiskt aktuell teknisk sanning.

## Kontext

Det här är en MAME-fork där ASR-10 drivs framåt. Målet är en ASR-10 som
låter och känns rätt, med ES5506 och ES5510. MAME är destinationen.

Trädet har en historia som reglerna nedan finns till för att bryta:
`asr10_boot.cpp` växte till 10 832 rader och `asr10_boot_codex.cpp` till
5 878, bredvid ett upstream-skelett på 228 rader. Merparten av det är
instrumentering och handmodellerade enheter som redan finns färdiga i
MAME. Hela 68307-familjen — device, SIM, buss, timers — är 1 327 rader.
Det är storleksordningen arbetet ska hålla sig i.

## Regler

1. **Harnesset får inte vara större än det det instrumenterar.**
   Om en ändring skulle göra `asr10_boot.cpp` större än den enhet den
   undersöker: stanna och säg till. Överskridande är ett stopp, inte en
   observation.

2. **Instrumentering raderas när utredningen är klar.**
   Fyndet går in i `docs/asr10/`; koden som producerade det tas bort.
   "Den kan behövas igen" gällde när det var dyrt att skriva om den.
   Det är det inte längre.

3. **C++ endast för det som ändrar maskinens beteende.**
   Allt som bara observerar hör hemma i ett Lua-skript via
   `-autoboot_script`. Lua når `bpset`, `wpset`, minnesutrymmen och
   symboltabeller, och kräver ingen omkompilering.

4. **Färdigställ eller radera innan du bygger nytt.**
   Finns det redan en halvfärdig abstraktion för det du ska göra, gör
   klart den eller ta bort den. Lägg inte en femte variant bredvid fyra
   ofärdiga.

5. **Använd MAME:s enheter i stället för att modellera dem.**
   Kontrollera alltid `src/devices/` innan något modelleras för hand.
   DUART:en (`mc68681.cpp`), FDC:n (`upd72069_device` i `upd765.h`),
   ES5506 och ES5510 finns redan färdiga och testade.

6. **Ett prestandapåstående kräver en siffra. Ett arkitekturpåstående
   kräver ett fall som går sönder.** Skriv inte ner en kausal förklaring
   som fynd utan att ha mätt den.

7. **Varje uppgift ska ha en raderingslista.**
   Rapportera antal rader till och antal rader från. Rena tillägg ska
   motiveras.

8. **Dokumentation skrivs av en part i taget.**
   Kör inga git-kommandon från annat håll under en pågående uppgift.

## Körning

Headless, utan GUI och utan ljud:

**Två körlägen. Blanda dem aldrig.** Den aktuella acceptanssviten använder
V3.50 och `./mame`; äldre `./mess`/V1.61-kommandon i arkivet är historiska.

```sh
# no-media: "PLEASE INSERT DISK" är förväntat
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -video none -sound none -nothrottle -seconds_to_run 30

# med media: aktuell V3.50-baseline
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30
```

Verifiera alltid mot båda. En ändring som är grön i no-media kan ha
brutit den djupa vägen.

Binären heter `mame`, maskinen `asr10booth` (i `asr10_boot.cpp`).
`esqasr.cpp`s `asr10` byggs inte med nuvarande `SOURCES=`-filtrering.
Använd inte `-log`: projektets observation ska vara synlig via Lua `print()`
eller, när maskinbeteende verkligen måste ändras i C++, `osd_printf_error()`.
Ett nollresultat från en Lua-tap är ogiltigt utan ett levande vittne genom
hela mätfönstret.

Styrfil för dynamisk observation, utan omkompilering:

```sh
SDL_VIDEODRIVER=dummy ./mame asr10booth \
  -flop1 floppies/asr10booth/V350.img \
  -autoboot_script docs/asr10/lua/probe.lua \
  -video none -sound none -seconds_to_run 30
```

Aktuell full regression: `docs/asr10/regression-test.sh`.
