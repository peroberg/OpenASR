# Kommando 0x0E: inte DUMP REGISTERS, inte dödläge

2026-07-30. Läst: `PLAN.md` fas 3, `CLAUDE.md`, `fdc-map.md`. Metod:
slog på MAME:s egen `upd765.cpp`-loggning (`VERBOSE = LOG_GENERAL |
LOG_WARN | LOG_COMMAND | LOG_FIFO`, rad 23) plus en tillfällig
`LOGFIFO`-rad i `fifo_r()`s `PHASE_RESULT`-gren (den grenen loggar annars
ingenting alls). Båda ändringarna reverterade innan commit — se
`git diff` mot `src/devices/machine/upd765.cpp` (tomt).

## Sammanfattning

`[Verified]`. Föregående sessions diagnos var fel på en avgörande
punkt: `upd72069_device` (den enhet drivrutinen faktiskt instansierar)
hanterar **aldrig** kommando `0x0E` som NEC765-familjens
`DUMP REGISTERS`. Den koden (`check_command()` rad 2985,
`i82072_device::execute_command()` rad 3040 med `result_pos=10`) hör
till `i82072_device` — en helt annan, aldrig instansierad
härledd klass i samma fil. `upd72069_device::auxcmd_w()` (rad 3416) är
en **separat** skrivväg (all skrivning till `$FC4001` går genom den, inte
genom `check_command`/`execute_command`), och där hanteras `0x0E` som
en av 16 "enable motors"-varianter (`0x0e, 0x1e, 0x2e, ..., 0xfe` — låg
nibble `0xE`): `result[0] = ST0_UNK (0x80); result_pos = 1;`.

**Kommandosekvensen före 0x0E** (`ASR10FDC_CMD0E_SUMMARY`/`recent_commands`,
befintlig drivrutinsdiagnostik): `36 (soft reset), 0b (control internal
mode), 4f (select IBM format), 1e (enable motors), 88 (data rate
select), f3 (precompensation), 0e (enable motors)`.

**Resultatbytes lästa efter 0x0E:** exakt **1**, värde `0x80`, vid
`pc=fb8db2` — direkt bekräftat av både min tillfälliga `LOGFIFO`-rad
(`fifo_r in result phase, result_pos=1 value=80`) och den redan
befintliga `ASR10FDC_CMD0E_SUMMARY`-raden
(`result_count=1 fifo_bytes="80" fifo_read_pcs="fb8db2"`). Det är
**exakt** `result_pos=1` som `auxcmd_w` satte — ingen avvikelse, inget
kortfall, ingen gissning.

## Svar på frågorna

1. **Kommandosekvens och antal resultatbytes:** se ovan. ROM:en läser
   varken färre än tio (fråga 3) eller noll (fråga 4) — den läser precis
   det enda byte som var förväntat, och gör det korrekt.

2. **Är avvikelsen i uPD72065:ans DUMP REGISTERS-layout (82077/PS2 kontra
   verklig)?** `[Verified]` Nej — frågan är moot. `0x0E` är inte
   DUMP REGISTERS i den här vägen alls; layoutjämförelsen mot
   82077/PS2 gäller `i82072_device`, som aldrig körs för ASR-10.

3. **Är CB (busy) fastlåst efter 0x0E, i väntan på nio bytes till?**
   `[Verified]` Nej. MSR går tillbaka till overksam (`CB` rensas) direkt
   efter den enda läsningen (`main_phase = PHASE_CMD` när `result_pos`
   når 0, `upd765.cpp` rad 646-647). Loggen bekräftar detta: MSR-spåren
   omedelbart efter (`ASR10TRACE ... addr=fc4001 ... data=0080`) visar
   `RQM=1, DIO=0, CB=0` — overksam, inte upptagen.

## Den verkliga följden efter 0x0E — inte ett dödläge, utan fortsatt körning

`[Verified]`. Efter den lyckade enda-byte-läsningen fortsätter körningen
**normalt**, in i ett helt annat kodområde:

```
pc=fb7c2c   ASR10_049D_WRITE (last_command=0e last_fifo_read=80,
            recent_commands="36,0b,4f,1e,88,f3,0e")
pc=fb7c30   läser $FC4809 (DUART "ack_input_change_latch", befintlig
            rolltaggning i drivrutinen), återkommer flera gånger
            (ASR10CPUCONTEXT hits=1, hits=2, ...)
            -> return=fb90fe, callsite=fb9184, containing_routine=fb9104
pc=f882de   ASR10_ERROR_ENTRY_STUB (befintlig drivrutinstagg)
[:fdc]      "polled 0 : 1 -> 0"  -- SISTA FDC-aktiviteten i hela körningen
pc=f87ed0/8 skriver $FC481D/$FC481F (DUART)
            -> in i panel-/DUART-dispatchkoden (ASR10_DIAG_PANEL_AUTORESPOND
            event=imr_write, PANEL_RHRB-sekvensen) som redan är
            dokumenterad som den slutgiltiga TUNING-KBD-stalltillståndet
            i tidigare sessioners fynd.
```

**Detta motsäger föregående sessions slutsats rakt av.** FDC-
handskakningen är inte dödläget. `0x0E`s resultatfas löses korrekt,
körningen lämnar FDC-koden helt och hållet (`"polled 0 : 1 -> 0"` är
bevisligen den sista FDC-relaterade raden i hela loggen), och fortsätter
in i en DUART-/panelrelaterad kodväg (rotad runt `$FB9104`, med
upprepade anrop till `$FB7C30`s DUART-inputkontroll) som är samma
stalltillstånd `PLAN.md` fas 3 redan beskriver — men vägen dit går
**inte** via FDC:n.

`[Hypothesis]`: den återstående utredningen bör flytta fokus till
`$FB9104`/`$FB7C30`-området och DUART-inputförändringslatchen
(`$FC4809`), inte FDC:n. Inte bekräftat i den här uppgiften.

## Ingen kompensation byggd

Per uppdraget: eftersom `upd72069_device`s modell matchar ROM:ets
förväntan exakt (ett byte begärt, ett byte satt, ett byte läst, CB
rensad korrekt), finns ingen avvikelse i `upd765.cpp` att fixa. Inget
ändrat i något committat läge — `VERBOSE` och den tillfälliga
`LOGFIFO`-raden är reverterade (`git diff` mot filen är tomt).
