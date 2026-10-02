# ASR-10 Layout Generator

Kanonisk layoutgenerator i Kotlin för Ensoniq ASR-10 MAME-panellayout (`.lay` XML och SVG preview).

## Syfte

Skriptet [`generate_layout.main.kts`](generate_layout.main.kts) bygger frontpanelens layout deklarativt och relationellt med zoner (`Region`), proportionerliga och absoluta delningar (`Span`) samt en linter (`LayoutValidator`) som verifierar gränskontroller, unika knappbindningar, träffytor och textplacering.

Skriptet genererar två parallella målbilder:
1. **MAME Layout XML** (`default.lay`) för MAME:s interna renderingsmotor (`asr10booth`).
2. **SVG/HTML Förhandsgranskning** (`preview.html`) för visuell verifiering och direkt webbgranskning utan att starta MAME.

## Körning

Skriptet är ett fristående Kotlin-skript (`.main.kts`) och körs antingen via symlänken i repo-roten eller direkt från skriptkatalogen:

```sh
# Från repo-roten via symlänk
./generate_layout.main.kts

# Alternativt direkt i generator-katalogen
cd docs/asr10/generator && ./generate_layout.main.kts
```

Kräver att `kotlin` finns installerat i PATH (Java / Kotlin CLI compiler).

## Output-destinationer

Skriptet skriver automatiskt till följande mål:
* **MAME Layout:** `artwork/asr10booth/default.lay`
* **SVG Förhandsgranskning:** `preview.html` (i aktuell arbetskatalog) och `docs/asr10/generator/preview.html`

## Struktur i katalogen

* [`generate_layout.main.kts`](generate_layout.main.kts) – Kanoniskt produktionsskript (refaktorerat med 100 % paritet).
* [`preview.html`](preview.html) – Senast genererade SVG-förhandsgranskning.
* [`archive/`](archive/) – Tidigare utvecklingsiterationer (`generate.main.kts`, `generate2.main.kts` t.o.m. `generate16.main.kts`) bevarade för provenance och historik.
* [`test.main.kts`](test.main.kts) – Hjälpskript för isolerade geometritester.

## Framtida faser

1. **Manuell validering av matrisbindningar:**
   - Genomlysning och verifiering av samtliga knapp- och indikatorbindningar (`buttons_0`, `buttons_32`, `asr10_annbit*`, `asr10_instlamp*`) mot Ensoniq ASR-10 Musicians Manual och Service Manual / ROM-tabeller.
2. **Analoga kontroller & hjul:**
   - Införande av modulations- och pitchbend-hjul pausas tills vidare, då ADC-drivrutinen för närvarande avkodar viloläget som ett värde större än noll. När ADC-kalibreringen är löst implementeras hjulen i layouten.
