# ASR-10 — anropsmodell

Detta dokument beskriver **modellen**, inte en fullständig graf. Grafen byggs ut
successivt; modellen ska etableras nu så att framtida fynd har någonstans att hamna.

**Coverage correction, 2026-08-25:** `static/call-graph-edges.csv` är inte en
komplett firmware-callgraph. Den nuvarande extractionen ser huvudsakligen
absoluta JSR/JMP-operander och missar BSR, BRA/Bcc, PC-relativa effective
addresses, registerindirekta JMP/JSR samt TRAP/callback-dispatch. Det uppmätta
nollsnittet mellan panel-forward och sequencer-backward är därför ett
coverage-/instrumentproblem, inte evidens för att ingen väg finns. Se
`methods-static-analysis.md` och
`../investigations/call-graph-intersection-and-rom-string-search.md`.

Notation: `[V]` verifierat, `[L]` sannolikt, `[?]` okänt.

---

## 1. Översikt

```
                          RESET
                            │
              ┌─────────────▼─────────────┐
              │  ROM bootstrap $F8000C    │ [V]
              │  SR=$2700, CS0-CS3        │
              └─────────────┬─────────────┘
                            │  kopiera 14 byte
                            ▼
              ┌───────────────────────────┐
              │  DPRAM-brygga $FC6200     │ [V]
              │  BR0=$1F01, ROM→$F80000   │
              └─────────────┬─────────────┘
                            ▼
              ┌───────────────────────────┐
              │  MC68302 init $FB8E06     │ [V]
              │  PIO → GIMR/IPR/IMR/ISR   │
              │  → Timer2, WRR, CP, SCC   │
              └─────────────┬─────────────┘
                            ▼
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
   ┌─────────┐        ┌──────────┐        ┌──────────┐
   │  DUART  │ [V]    │   FDC    │ [V]    │ ES5506/  │ [V]
   │ A=MIDI  │        │ µPD72069 │        │  ES5510  │
   │ B=panel │        │ $FC4000  │        │ $FC2000/ │
   │ $FC4801 │        │          │        │ $FC3000  │
   └────┬────┘        └────┬─────┘        └──────────┘
        │                  │
        │                  ▼
        │        ┌──────────────────────┐
        │        │  filsystem           │ [V]
        │        │  katalog @ 0x600     │
        │        │  "ASR-10 OS" blk 24  │
        │        └──────────┬───────────┘
        │                   ▼
        │        ┌──────────────────────┐
        │        │  OS-laddning         │ [V regler, L gränser]
        │        │  Seg1 RAM=off+0xA00  │
        │        │  Seg2 RAM=off-0x5A00 │
        │        └──────────┬───────────┘
        │                   ▼
        │        ┌──────────────────────┐
        │        │  vektorinstallation  │ [?]  ← E1
        │        │  →$000000-$0003FF    │
        │        └──────────┬───────────┘
        │                   ▼
        │        ┌──────────────────────────────────┐
        │        │  BINDNINGSTABELLEN blir giltig   │ [V struktur]
        │        │  $00801E-$009FF6, 723 slots      │
        │        └──────────┬───────────────────────┘
        │                   ▼
        │        ┌──────────────────────┐
        │        │  ÖVERLÄMNING         │ [?]  ← E1, sannolikt slot $801E.w
        │        └──────────┬───────────┘
        │                   ▼
        └───────►┌══════════════════════════════════┐
                 ║   ROM  ⇄  OS   (resten av tiden) ║
                 ╚══════════════════════════════════╝
                    │            │             │
              schemaläggare   TRAP-dispatch  avbrott
                  [V]            [L]           [V]
```

---

## 2. Kanterna i grafen — kopplingsmekanismerna

**Fyra verifierade eller starkt etablerade kopplingsmekanismer har hittills
identifierats.** Listan är inte bevisat uttömmande. Varje nytt fynd bör klassificeras
som en av dem — eller som en femte.

### 2.1 Bindningstabellen — ROM → (ROM eller OS)  [V]

```
ROM-kod    jsr $8030.w        (effektiv adress $FF8030)
             │
             ▼
tabellslot RAM $008030:  4EF9 FFF97662
             │
             ▼
mål        ROM $F97662       — eller OS-adress, versionsberoende
```

342 slots anropas av ROM, från 1254 platser. **Detta är huvudmekanismen.**
Se `rom-os-abi.md` §2 och `static/os-binding-table.csv`.

De hetaste kanterna:

| slot | anrop | mål V1.61 | mål V3.50 |
|---|---|---|---|
| `$8030.w` | 203 | `$F97662` | `$F97662` |
| `$9818.w` | 40 | `$F92478` | `$F92478` |
| `$8042.w` | 35 | `$FB7788` | `$FB7788` |
| `$8C0C.w` | 26 | `$F8B2E2` | `$F8B2E2` |
| `$8644.w` | 17 | `$009D60` *(OS)* | `$00B008` *(OS)* |

### 2.2 Direkta ROM-anrop — OS → ROM  [V]

```
OS-kod     jsr $FFF976EC     32-bitars absolut, direkt in i ROM
```

895 entrypoints gemensamma för båda OS-versionerna. Ingen indirektion, ingen
patchmöjlighet. Se `static/rom-abi-entrypoints.csv`.

Mest anropade: `$F95EAA` (1 → 33 anrop mellan versionerna, **oidentifierad**),
`$FB84DA`, `$FBA0D6`, `$F8CAB4`, `$F97662`, `$F976CA`.

### 2.3 Vektortabellen — hårdvara → (ROM eller DPRAM)  [V]

OS-bildens vektortabell dirigerar det mesta tillbaka till ROM. Se `vector-map.md`.

```
exception  →  vektor i RAM $0000xx  →  ROM-stubb $F882xx  →  svans $F88280  →  ERROR nnn
vektor 4   →  DPRAM $FC6000
vektor 11  →  DPRAM $FC6014
vektor 33  →  ROM $F87F76   (schemaläggaren)
```

### 2.4 DPRAM-hoppbordet — (ROM eller OS) → ?  [L]

```
anrop      jsr $FFFC60B0
             │
             ▼
DPRAM-slot $FC60B0   (stride-6-rutnät från $FC6014, ~30 poster)
             │
             ▼
mål        okänt — DPRAM fylls vid körning
```

Se `memory-map.md` §2.1. Att dumpa DPRAM efter boot är den billigaste öppna åtgärden
i projektet.

### 2.5 Indirekta kontrollöverföringar  [OPEN]

Registerindirekta hopp (`jmp (An)`, `jsr (An)`), stackade returadresser och
funktionspekare i RAM. **Ingen systematisk kartläggning finns.** Den statiska analysen
ser dem inte, och de är den mest sannolika platsen för en femte mekanism — inklusive
själva ROM→OS-överlämningen.

Belagt att mekanismen används: SCC-avbrottshanterarna laddar registerbaser
(`movea.l #$00FC6880,A1`) och adresserar sedan hela registerblocket relativt A1.

---

## 3. Delgrafer som är etablerade

### 3.1 Reset-kedjan  [V]

```
$F8000C  move.w #$2700,SR
   ├─ $F80010  ($00F2).w ← $0FC6          (syfte okänt)
   ├─ $F80016  ($00F4).w ← $0F200000      (syfte okänt)
   ├─ $F8001E..$F8005D  OR0/BR0 … OR3/BR3
   ├─ $F8005E..$F80070  kopiera $F80078..$F80085 → $FC6200
   └─ $F80072  jmp $FC6200
                 ├─ BR0 ← $1F01
                 └─ jmp $FFFB8E06
                        └─ $FB8E06  PIO-init
                              └─ $FB8E7E  GIMR/WRR/IMR/ISR/IPR/TRR2/TMR2
```

### 3.2 Exception-kedjan  [V]

```
CPU-exception
  └─ vektor i RAM $000008-$0000BF
       └─ ROM-stubb $F882AA-$F882DA     moveq #kod,D0
            └─ $F88280  gemensam svans
                 └─ panelutskrift "ERROR nnn"
```

Kända stubbar: vektor 2 → `$F882AA`, 3 → `$F882AE`, 5 → `$F882B6` (division med noll,
felkod 130), 9 → `$F882C6`, 10 → `$F882CA`, 15 och 25–31 → `$F882DA` (catch-all),
24 → `$F882D6`.

### 3.3 Schemaläggaren  [V]

```
$F87F80  spara kontext        TCB-stride $16
$F87F92  skanna dispatch      +0 räknare, +2 pending, +3 idle-mönster,
$F87FCC  idle-loop            +6 PC, +A SR, +C A5, +E USP, +$14 tröskel
($0B6A).w = aktuell uppgift
```

### 3.4 Kritiska sektioner  [V]

```
$F976EC  enter    move.w SR,D0 / ($0E82).w ← D0 / or.w #$0700,D0 / A000
$F976FA  restore  D0 ← ($0E82).w / A000
```

Exponerade som slot `$8030.w` (→ `$F97662`) och `$8036.w` (→ `$F976CA`).
Sambandet mellan `$F97662`/`$F976CA` och `$F976EC`/`$F976FA` är **inte fastställt** —
de ligger nära varandra men är olika adresser. [?]

### 3.5 Panel-RX-kedjan  [V]

```
panelbyte → DUART kanal B RX-FIFO → RxRDY → ISR bit 5 → IRQ
   → DUART-dispatcher $F884BE
       → om ingen igenkänd källa: $F884F8  moveq #$91,D0 / trap #0  ⇒ ERROR 145
```

### 3.6 SCC-mottagarkedjan  [V för firmware, Open för hårdvara]

```
PB3 (LRCLK) pollas hög
  └─ dbra-fördröjning → SCM2 ← $703B (ENR)
       └─ dbra-fördröjning → SCM1 ← $703B (ENR)
            └─ jsr $FFFF8ECA (abs.l; förväntat alias till slot $008ECA)
                 →  set_sr  →  IMR |= $2400
                 └─ [hårdvara ska nu leverera mottagna tecken]   <-- OBEVISAT
                      ├─ SCC1-avbrott → ISR $008D56 → jsr $00643C → EOI ISR ← $2000 → rte
                      └─ SCC2-avbrott → ISR $008D92 → jsr $00643C → EOI ISR ← $0400 → rte
                           └─ ($016F).w / ($0D04).w grindar → jmp $0064BA
```

Firmwarekedjan är komplett i varje led som går att se statiskt. Den observerade
runtime-körningen är en **V1.61-körning** ur en historisk blockerutredning; att ett
förväntat SCC-avbrott faktiskt uteblev är inte dynamiskt visat. Se
`mc68302-status.md` §3.

### 3.7 PAR / analog selector  [V firmware; physical mux Likely]

```
OS RAM $0067EC  andi.b #$F8,($FC6829)   ; nollställ PB2-PB0
                ori.b  #$07,($FC6829)   ; välj kanal 7
        $006864 ackumulera 8 sampel     ; trap #8 / trap #7 runt mätningen
                jsr $FFFC60B0           ; DPRAM-hoppbordet
                asl.w #6 / lsr.w #3
        $006800 divu.w D2,D0            ; D2=0 ⇒ vektor 5 ⇒ ERROR 130
```

`$FC6829` = PBDAT låg byte. Se `memory-map.md` §2.2.

---

## 4. Delgrafer som saknas

Skriv in dem här när de etableras.

| delgraf | status | ingång |
|---|---|---|
| TRAP-dispatch (TRAP 0–15, vektor 32–47) | [?] | `$F88280`, `$F87F76`, `$F8812C`, `$F88056` |
| Line-A (`$A000`) | [L] `set_sr(D0)`, motsägelse kvarstår | `vector-map.md` |
| Avbrottsvägar per källa | delvis | DUART verifierad, 68302-källor OPEN |
| Filsystem → OS-laddare | [?] | E3 |
| Effektnedladdning till ES5510 | delvis | `$FC31C1` posttyp 1 |
| Sequencer / bank-laddning | [?] | — |

---

## 5. Arbetsregel för utbyggnad

Varje ny kant som läggs till bör ange:

```
från-adress  →  till-adress
mekanism:    bindningsslot | direkt ROM-anrop | vektor | DPRAM-slot | relativ (bsr)
evidens:     [V] / [L] / [?]  + hur den fastställdes
versioner:   V1.61 / V3.50 / ROM
```

Kanter utan mekanism är inte färdiga fynd. Den vanligaste källan till fel i det här
projektet har varit att blanda ihop *instansierad* och *inkopplad* — samma sak gäller
grafen: att en adress refereras betyder inte att kanten någonsin traverseras.

---

## 6. Maskinläsbar graf — `static/call-graph.csv` + `static/routines.csv`

Dokumentet ovan är den läsbara arkitekturmodellen. Den maskinläsbara grafen ligger i två
filer och genereras om vid behov.

**Vad detta är och inte är:** en statisk kontrollflödesdatabas på **anropsställenivå** —
ett register över möjliga och observerade kanter. Det är ännu inte en semantisk
funktionsgraf. `routines.csv` är början på nivå 2.

### `call-graph-edges.csv`

```
edge_id
from_address, from_file_offset, from_address_status, from_space
encoded_operand, effective_address, logical_target, to_space, mapping_basis
mechanism, version, evidence, executed, execution_source, count, comment
from_routine, to_routine
```

**`edge_id`** är stabil och greppbar: `<mech>:<version>:<from>:<effective>`, t.ex.
`bsc:ROM:F88434:FF863E`. Kollisioner får suffix `#2`. Referera kanter härifrån i
dokumentation och runtime-loggar — aldrig genom radinnehåll.

**Tre adresskoordinater hålls isär**, eftersom de inte är utbytbara:

| kolumn | betydelse |
|---|---|
| `encoded_operand` | operanden som den står i instruktionen (`$863E`, `$FFF97662`) |
| `effective_address` | 24-bitars effektiv adress efter teckenutvidgning (`$FF863E`) |
| `logical_target` | normaliserat mål **under en hypotes** (`$00863E`) |
| `mapping_basis` | vilken hypotes: `direct` eller `mirror-hypothesis` |

`to_space` för `$FF8000-$FF9FFF` är **`HIGH-RAM-ALIAS-CANDIDATE`**, inte `SLOT`. Den
logiska kopplingen till bindningstabellen är statiskt mycket stark, men speglingen är
fortfarande [OPEN]. E2 2026-08-10 observerade 566229 data reads i `$FF8000-$FFFFFF`
under V3.50-boot och en mismatch mot `$00xxxx`, men avgjorde inte om fönstret var
avkodat, om värdet var open bus, eller om opcode-fetcher speglar. Det ursprungliga
V1.61-testfallet `$00BF0E -> FFFF8ECA -> $00BF14` är inte kört i denna runda.

`static/call-graph-edges.csv` har fortfarande exakt 1404 rader med
`mapping_basis=mirror-hypothesis`, verifierat direkt mot filen (`scc-hardware-gap.md`
Del 1.1), inte mot denna sammanfattning. **Rättelse:** den tidigare hashen här,
`b8bfb32053274a66...`, matchade inte den aktuella filen och matchade aldrig
`static/README.md` (som alltid hade `472373eea0fac895...`) — en stale referens till en
äldre, 5240-kantersgeneration, ospårad tills `scc-hardware-gap.md` Del 1.1 räknade om
den direkt. Korrekt, aktuell hash:
`472373eea0fac895f07d115c8168cc4551957a9a2b1c6f3a1e6909541c5715de`. Kanterna får inte
se verifierade ut och ska inte ändras manuellt i den genererade CSV:n.

**RAM-adress och filoffset är separata kolumner.** För OS-kod utanför segment 1 är
`from_address` tom och `from_address_status` = `unmapped-segment`; koordinaten finns kvar
i `from_file_offset`. När E3 avgör segmentgränsen fylls `from_address` i utan att
originalkoordinaten går förlorad. 833 av 5243 kanter är i det läget.

**`executed`** har fyra värden, inte tre:

```
unknown                       ingen information
observed                      direkt observerad i en namngiven körning
inferred_from_trace           måste ha traverserats för att observerat tillstånd ska nås
not_reached_in_bounded_run    giltig kant som inte traverserades i den körning som mättes
```

`not_reached_in_bounded_run` är **inte** `no`. En kant kan vara helt korrekt och ändå
aldrig tas i just den mätta körningen.

**`execution_source`** namnger körningen:
`runtime-v350-boot-file1` · `runtime-v161-historical-scc` ·
`runtime-v350-historical-error130`

### Nuvarande innehåll

```
5243 kanter = 5229 statiskt härledda + 14 runtime-/arkitekturbelagda

  2286  direct-abs.l          OS -> ROM
  1490  binding-slot-target   745 slots per version
  1362  binding-slot-call     ROM -> slot
    93  vector
    12  övriga

executed:  observed 7 · inferred_from_trace 3 · not_reached_in_bounded_run 4 · unknown 5229
```

**Kantantalet gick från 5240 till 5243** mellan två generationer av filen. Skillnaden är
inte ett räknefel: den statiska basen är oförändrad på **5229** kanter i båda
generationerna, och de tillagda tre är runtime-/arkitekturbelagda rader (11 → 14).
Kontrollera alltid `evidence`-fördelningen, inte totalen, vid jämförelse mellan
generationer.

**Verifierat maskinellt:** `edge_id` är globalt unikt (5243 av 5243), varje kant har sin
`version` i identifieraren, och alla 745 versionsberoende slotmål har separata
identifierare per version — t.ex.
`bst:V161:00801E:007144` mot `bst:V350:00801E:0071C6`.

**Sju observerade av 5243.** Siffran betyder inte att bara sju kanter fungerar — den
befintliga booten har med säkerhet traverserat hundratals. Den betyder att bara sju är
*knutna till en namngiven observation*. Första dynamiska arbetet bör därför lika mycket
**backfilla redan kända körningar** som skapa nya spår: reset → DPRAM-bryggan →
ROM-init, DUART-avbrottet, FDC-läsningen, OS-laddningen, PAR-rutinen,
division-by-zero-vägen, och vägen till `KEYBOARD TUNED` respektive `FILE 1`.

### `routines.csv` — nivå 2

```
routine_id, logical_name, name, canonical_status,
start, end, space, version,
boundary_evidence, confidence, source_document, comment
```

25 rutiner. `confidence`: 11 `verified` (start **och** slut belagt), 13
`start-verified` (entrypoint känd, slutet inte), 1 `likely`.
`canonical_status`: 20 `resolved`, 2 `partially_resolved`,
**3 `unresolved_high_priority`** — `$F97662` (203 anropsställen), `$F95EAA`
(1 → 33 anrop mellan versionerna) och `$00643C` (vad SCC-mottagningen producerar).
**`$00643C` är disassemblerad och strukturellt förstådd sedan `scc-hardware-gap.md`
Del 3** — `routines.csv`s egen `canonical_status`-kolumn är oförändrad här (regeln mot
handredigering av `static/*.csv` gäller), så fältet säger fortfarande
`unresolved_high_priority` trots att rutinen inte längre är oidentifierad; antalet är
därför 2 i sak, 3 i den ouppdaterade CSV:n. Nästa CSV-regenerering bör sätta
`partially_resolved` eller `resolved` här.

Statusen bärs av `canonical_status`, inte av ett tomt `name`. Tomma fält är dålig
signalering.

**`routine_id` identifierar en konkret adress i en konkret version.** Där samma logiska
rutin ligger på olika adresser i olika versioner finns **två rader** som delar
`logical_name`:

```
os_v161_00bee2   scc_receiver_enable   $00BEE2-$00BF27   V161   verified
os_v350_00e48a   scc_receiver_enable   $00E48A-$00E4D5   V350   likely
```

V3.50-raden är `likely` eftersom adressen är härledd via segment 2-regeln och inte
runtime-observerad. `version = V161;V350` används **endast** där adressen är verifierat
identisk i båda avbildningarna, vilket gäller de två SCC-avbrottshanterarna.

`source_document` anger var kunskapen om rutinen bor, eller `static-analysis` när den
ännu inte har ett hem.

### Regel som inte får brytas

```
Edges are call-site-level observations or static candidates.
Routines are independently curated entities.
No routine ownership is inferred solely from nearest preceding address.
```

`from_routine` och `to_routine` i `call-graph-edges.csv` är tomma tills gränserna räcker
till.
**Fyll dem inte automatiskt från terminatorheuristiken.** Den bryter mot tail calls,
fall-through mellan hjälprutiner, gemensamma epiloger, hopptabeller, exceptionstubbar,
bindningstabeller, små thunkar, kod som bara nås via funktionspekare, data inuti
kodregioner, och rutiner som börjar direkt efter en villkorlig gren. Varje rad i
`routines.csv` ska ha explicit `boundary_evidence`.

Hoppa inte till nivå 3 (subsystem) innan nivå 2 är tillräckligt tät.

### Regenerering

`call-graph.csv` är maskingenererad ur `asr10.bin`, `V161.img` och `V350.img`.
Redigera den inte för hand. Runtime-observationer hör hemma i en separat fil som slås
ihop vid generering, så att en omgenerering inte förstör dem. `routines.csv` är däremot
handkurerad och ska växa en rad i taget.
