# ES5570 GLU och ES5701 SuperGLU
## Arkitektur och betydelse för ASR-10 reverse engineering

# Syfte

Detta dokument sammanfattar vad ES5570 GLU respektive ES5701 SuperGLU faktiskt gör och, minst lika viktigt, vad de **inte** gör.

Båda kretsarna används i flera Ensoniq-maskiner men har helt olika ansvar.

---

# Översikt

## ES5570 GLU

Ansvar:

- systemets adressdekoder
- busskontroll
- DTACK-generering
- interrupt acknowledge
- chip-selects
- RAM/ROM-dekodning
- FDC-chipselect
- DUART-chipselect
- DOC/OTIS-chipselect
- DMAC-chipselect

Den är i praktiken systemets nordbrygga.

Den avgör vilken hårdvara CPU:n pratar med.

Den implementerar däremot **ingen funktionalitet** för dessa enheter.

---

## ES5701 SuperGLU

Ansvar:

- bussöversättning mellan CPU och ESP
- bussöversättning mellan CPU och OTIS/OTTO
- latchning av OTIS-adresser
- databusmaskning för 8/12/13/16-bitars samples
- klockdelning
- DTACK till ESP

Den arbetar nästan uteslutande med ljudsubsystemet.

Den har ingen minnesdekoder.

Den genererar inga chip-selects.

Den känner inte ens till floppykontrollern.

---

# ES5570

ES5570 definierar hela systemets minneskarta.

Exempel:

```
20xxxx DOC

24xxxx DMAC

28xxxx DUART

2Cxxxx FDC

30xxxx External

40xxxx Sample RAM

C0xxxx ROM
```

När 68000 adresserar 0x2Cxxxx aktiveras:

```
cs_fdc
```

Det är allt.

GLU vet inte vilken FDC som sitter där.

Den bara väljer enheten.

---

# ES5701

ES5701 arbetar istället mellan CPU:n och ljudkretsarna.

Exempel:

CPU

↓

ES5701

↓

OTIS / OTTO

↓

Sample RAM

Den implementerar:

- multiplexad adress/databuss
- nibble mode
- sampleupplösningar
- adresslatch
- ESP:s multiplexade A/D-buss

Det finns inga FDC-register.

Inga DMA-register.

Ingen disklogik.

---

# Vad säger detta om floppyarkitekturen?

Det visar tydligt att floppykontrollern är en **fristående perifer enhet**.

CPU → ES5570 → cs_fdc → FDC
↘ eventuell DMAC → RAM

SuperGLU är inte inblandad.

---

# FDC i GLU

ES5570 innehåller endast:

```
cs_fdc
```

och

```
fdc dtack delay
```

Det innebär endast att:

- FDC får en chip select
- FDC får rätt busstiming

All diskfunktion ligger utanför GLU.

---

# ES5701 och OTIS

ES5701 visar däremot mycket om ljudarkitekturen.

Den implementerar bland annat:

- 8-bitars samples
- 12-bitars samples
- 13-bitars samples
- 16-bitars samples

via en enkel databussmask.

Det visar att Ensoniq valde att hålla ljudformatshanteringen utanför OTIS själv.

---

# Viktig slutsats

Ingen av dessa kretsar implementerar floppylogik.

ES5570 väljer endast FDC:n.

ES5701 känner inte till att FDC:n existerar.

Det betyder att hela den återstående bootkedjan sannolikt ser ut ungefär så här:

68000

↓

GLU

↓

FDC

↓

DMA

↓

RAM

↓

Scheduler

↓

Voice allocation

↓

System Loaded

---

# Konsekvenser för ASR-10 reverse engineering

Det innebär att FDC/DMA-delen fortfarande är en möjlig stor okänd komponent, men den ska behandlas som hypotes tills firmwarevägen bevisar att den saknade scheduler-producern beror på disk/DMA/completion-status.

[Verified] Schedulern ser ut att fungera.

[Verified] Slot-systemet fungerar.

[Verified] Callback-systemet fungerar.

[Verified] Trap #6 dispatcher-save bevarar return-PC korrekt:

```
slot2 +06 = 007364
slot2 +0A = 0008
```

[Verified] Slot2 blir ändå inte pending eftersom:

```
slot2 +02 = 8080
slot2 +10 = 0000
```

[Likely] Den aktuella blockeraren är därför inte en dispatcher-, RTE- eller Trap #6-save-bugg, utan en saknad upstream producer/re-arm av continuation- eller pending-state.

[Hypothesis] Den saknade producern kan i sin tur bero på att en sektor aldrig levereras från floppykontrollern till RAM, eller att en DMA/interrupt-completion aldrig sker. Detta är ännu inte bevisat.

---

# Sammanfattning

| Komponent | Ansvar |
|-----------|---------|
| ES5570 GLU | Minnesdekodning, chip-selects, busskontroll |
| ES5701 SuperGLU | Ljudbuss, OTIS/OTTO, ESP |
| FDC | Diskläsning |
| DMAC | Minnesöverföring |
| OTIS | Samplingsuppspelning |
| ESP | Effekter/DSP |

Den viktigaste insikten är att både ES5570 och ES5701 är **infrastruktur**.

De utför nästan ingen högre funktionalitet själva utan kopplar ihop övriga kretsar. [Likely] Den återstående buggen ligger inte i någon GLU-funktion. [Hypothesis] Den kan ligga i samspelet mellan ROM-koden, FDC:n och DMA, men den aktuella verifierade blockern är först och främst saknad slot2 continuation/pending-produktion.

## Current assessment 2026-06-29

[Verified] SuperGLU är inte en floppy-controller och bör inte vara primärt spår för diskboot-blockern.

[Verified] Scheduler, callback, dispatcher och Trap #6-save har nu bevisade fungerande delar.

[Verified] Trap #6 sparar inline return-PC `007364` i slot2, men slot2 återupptas inte eftersom den förblir equalized (`8080`) och `+10` är `0000`.

[Likely] Nästa saknade beteende är en upstream producer av slot2 continuation/pending state.

[Hypothesis] FDC/DMA/completion är en möjlig källa till den producern, men ska inte behandlas som bevisad förrän en firmwarebranch visar att utebliven hardware-status avgör om slot2 re-armas.

## Next investigation

[Verified target] Spåra alla writers och candidate producers av slot2 `002410/002412` från callback `007308` till Trap #6-handlerns `F880E0`.

[Likely target] Identifiera första branch/call som borde skapa continuation eller pending men inte gör det.

[Hypothesis target] Om den branch/call-kedjan läser FDC/DMA/interrupt-status, använd det som första hårdvaruspecifika experiment. Tills dess ska FDC/DMA vara sekundärt till continuation-producer-spåret.
