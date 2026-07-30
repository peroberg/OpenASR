# docs/mc68302/ — flyttad kunskap från ~/develop/mc68302

2026-07-30. Källa: `~/develop/mc68302/docs/mc68302/*.md` (14 filer,
1996 rader). Flyttat inför `mc68302.cpp`/`mc68302sim.cpp`, per
`docs/asr10/PLAN.md` fas 3. Triage nedan; se `CLAUDE.md` regel 2 och 6
för varför bestående fakta och "fungerar nu"-prosa hanteras olika.

## Klassificering

**Bestående hårdvarufakta, flyttade i huvudsak oförändrade** (endast
källhänvisningar till sidoprojektets interna klassnamn borttagna, och
en kort "Status in this tree"-notis tillagd där det var otvetydigt):

| Fil | Innehåll |
|---|---|
| `sib-register-map.md` | Hela SIB-registerkartan: offset, bredd, reset, block, per register. |
| `scr-spec.md` | SCR-bitkarta, W1C-semantik, ASR-10:s faktiska bootstrap-skrivning och slutlig readback. Redan i specifikationsform i originalet — inget att skriva om. |
| `interrupt-source-map.md` | INRQ-prioritet, källbitar, vektor-lågbitar per källa, EXRQ-nivåer. |
| `vector-origin-map.md` | Vektorformeln `(GIMR.V7_V5<<5)\|source_low_5`, tabellerna för interna och externa vektorer. |
| `pin-function-map.md` | Port A/B-pinfunktioner, dedikerad kontra GPIO. **Används direkt i fas 3 steg 1** för Port B. |
| `timer2-interrupt-spec.md` | Timer 2-register, bitlayout, vektor, ASR-10:s observerade `TRR2`/`TMR2`-skrivning — bekräftat identisk med vad den här trädets egna loggar visar. |
| `watchdog-spec.md` | WRR/WCN, räknesemantik, PB7/WDOG. |
| `idma-spec.md` | IDMA-registerkarta och driftsgränser. Statusavsnittet omskrivet: den här trädets egna bevis (disassemblerad programmerad I/O, se `docs/asr10/evidence-tree.md`) är starkare än sidoprojektets "inte observerat i ett begränsat spår". |
| `communications-block-map.md` | SCC/SMC/SCP-registerkartor, parameter-RAM-intervall. Relevant för fas 2:s CP-fråga. |

**Omskrivna från "fungerar nu" till specifikationsform** (per
uppdragets triageregel — implementationsstatus av sidoprojektets EGEN
kod kan inte bara flyttas, den beskriver inte den här MAME-enheten):

| Fil | Vad som ändrades |
|---|---|
| `observed-access-coverage.md` | Var ett "diagnostic guard"-testresultat för sidoprojektets verktyg. Omskrivet till en ren, verktygsoberoende lista över ASR-10-ROM:ets faktiska bootstrap-registerskrivningar i ordning — det är BESTÅENDE fakta (vad ROM:et skriver), bara inramningen (vilket testverktyg som råkade observera det) som var tillfällig. |
| `semantic-coverage.md` | Var en per-register `FullyImplemented`/`MappedNotImplemented`-katalog över sidoprojektets kod. Behållet: bara klassificeringsIDÉN (sex nivåer), omskriven till att visa hur den mappar mot fas 3:s enklare `known`/`known-unimplemented`/`unknown`. Sidoprojektets faktiska per-register-omdömen är inte flyttade — de gäller inte MAME. |

**Sparad som framtida designreferens, inte fas 3 steg 1** (redan i
designform, inget att skriva om, men uttryckligen märkt som senare fas):

| Fil | Varför den sparas ändå |
|---|---|
| `sib-observer-design.md` | `PLAN.md` fas 3 säger explicit att Semantic Guard/SIB Observer ska **flyttas, inte byggas om**. Det här ÄR den designen. Fas 3 steg 1 bygger bara en enkel 3-vägs räknare (se `semantic-coverage.md`), inte hela den här observatören — men designen ska finnas kvar för när den behövs. |

## Lämnat kvar (inaktuellt, kopierades inte)

| Fil | Varför |
|---|---|
| `timer2-implementation-blockers.md` | Historisk logg över samma utredning som `timer2-interrupt-spec.md` redan redovisar i löst form ("blockers... now resolved"). Allt sakligt innehåll som fortfarande gäller finns redan i den flyttade filen. Ren processhistorik för sidoprojektet, ingen ny fakta. |
| `execution-monitor.md` (385 rader) | Beskriver sidoprojektets egen `TraceRecorder`/"Execution Monitor"-spårningsarkitektur (`Mc68302Bus`, `Mc68302SystemIntegration`, Moira-CPU-adaptrar) — klasser som inte finns i MAME och en spårningsdesign för en helt annan kodbas. Innehåller noll MC68302-hårdvarufakta (verifierat: `grep -n "ASR-10\|MC68302 register\|hardware"` gav en enda träff, ordet "hardware" i en rubrikpunkt). `CLAUDE.md` regel 3 säger redan att observation hör hemma i Lua i det här trädet — den här filens hela ämne (hur man bygger en spårare i C++) är inte tillämpligt. |

## Vad det här INTE är

Ingen av dessa filer definierar MAME-enhetens beteende. De är
källmaterial för `src/devices/machine/mc68302.cpp`/`mc68302sim.cpp` —
implementationen är det som styr, inte dokumentationen. Där ett
dokument ovan säger vad ASR-10 "observerats göra", är det en not om
källan (ett annat träds ROM-spår), inte ett påstående om vad den här
MAME-enheten redan gör.
