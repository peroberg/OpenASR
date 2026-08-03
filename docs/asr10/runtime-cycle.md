# Den upprepade mjukvarucykeln: en per-PC instruktionsräknare hittar schemaläggarens idle-scan

2026-07-31. Läst: `PLAN.md`, `CLAUDE.md`. Metod: den befintliga m68000
instruktions-exekveringscallbacken (`commit d36b79addd6`, redan kopplad
i `asr10_boot_state::maincpu_instruction_hook`) användes för att bygga
en temporär, körningslokal per-PC-räknare plus en fromPc→toPc-kant­
räknare, gated bakom en ny miljövariabel `ASR10_DIAG_PC_HISTOGRAM`. All
kod som lades till för detta togs bort igen efter mätningen —
`git diff --stat src/mame/ensoniq/asr10_boot.cpp` visar netto noll
rader efter uppgiften.

**Viktig metodnot om körkonfiguration:** en första mätning gjordes utan
`ASR10_DIAG_PANEL_AUTORESPOND=1` och hittade en helt annan, mycket
mindre intressant hetaste loop ($F89C46, panelens byte-sänd-och-vänta-
på-svar-rutin, som då alltid gav upp efter 100 misslyckade försök
eftersom ingenting matade in ett svar). Det var en artefakt av fel
körläge, inte ett fynd om ROM:et. `docs/asr10/PLAN.md`/`running.md`
kräver autorespond-flaggan för den djupa bootkedjan — **alla siffror
nedan är från en körning MED `ASR10_DIAG_PANEL_AUTORESPOND=1`**, som är
det körläge planen faktiskt föreskriver.

Körkommando:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 ASR10_DIAG_PC_HISTOGRAM=1 \
  ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 30 -log
```

## 1-2. Topp-20 instruktions-PC efter 30 sekunder

`[Verified]`. Adress, träffantal, ROM/RAM, disassemblerad instruktion
(via `unidasm -arch m68000` mot den hi/lo-sammanflätade ROM-avbilden):

| Rank | PC | Antal | Region | Instruktion |
|---|---|---|---|---|
| 1 | `f87f9e` | 2 968 992 | ROM | `eor.b D1,D0` |
| 2 | `f87f96` | 2 968 992 | ROM | `move.b ($2,A2),D0` |
| 3 | `f87f9a` | 2 968 992 | ROM | `move.b ($3,A2),D1` |
| 4 | `f87fa0` | 2 968 992 | ROM | `beq $f87fc2` |
| 5 | `f87fc2` | 2 968 986 | ROM | `adda.w #$16,A2` |
| 6 | `f87fca` | 2 968 985 | ROM | `bcs $f87f96` |
| 7 | `f87fc6` | 2 968 985 | ROM | `cmpa.w $c8.w,A2` |
| 8 | `fb8d6c` | 2 521 142 | ROM | `cmp.l D3,D3` (känd fast fördröjningsloop, se `fdc-msr-wait.md`) |
| 9 | `fb8d74` | 2 521 142 | ROM | `bne $fb8d6c` |
| 10 | `fb8d6e` | 2 521 142 | ROM | `move.l D3,-(A7)` |
| 11 | `fb8d70` | 2 521 142 | ROM | `move.l (A7)+,D3` |
| 12 | `fb8d72` | 2 521 142 | ROM | `subq.l #1,D3` |
| 13 | `fb8aa2` | 1 674 022 | ROM | `move.b $fffc4001.l,D1` (känd FDC data-fas-poll, `evidence-tree.md`) |
| 14 | `fb8aa8` | 1 674 022 | ROM | `bpl $fb8abc` |
| 15 | `fb8abe` | 1 673 990 | ROM | `bne $fb8aa2` |
| 16 | `fb8abc` | 1 673 990 | ROM | `subq.l #1,D0` |
| 17 | `f87dda` | 1 048 064 | ROM | `cmp.w #$400,D1` |
| 18 | `f87dd6` | 1 048 064 | ROM | `move.w D0,(A0)+` |
| 19 | `f87dde` | 1 048 064 | ROM | `bne $f87dd6` |
| 20 | `f87dd8` | 1 048 064 | ROM | `addq.w #2,D1` |

Rank 1-7 (alla i `f87f8c`-`f87fca`) dominerar tillsammans (nästan 21
miljoner instruktionsexekveringar av 30 sekunders körning) och är EN
enda sammanhängande loop, inte sju separata. Rank 8-12 är den redan
kända, oberoende 4-varvs fördröjningsloopen i FDC-koden (dokumenterad i
`fdc-msr-wait.md`). Rank 13-16 är den redan kända FDC-databytesöverförings­
pollen (`evidence-tree.md`). Rank 17-20 är en oidentifierad
512-ords-kopieringsloop (`move.w D0,(A0)+` upprepad 0x400/2=512 gånger)
— sannolikt en buffertinitiering, inte undersökt vidare (utanför
uppgiftens scope: den är inte den hetaste och stannar på ett fixt antal
varv, inte en oändlig cykel).

## 3. Hetaste bakåtkant och minsta upprepade PC-sekvens

`[Verified]`. Kantfrekvenstabell (fromPc→toPc, räknad parallellt med
PC-histogrammet i samma callback):

```
f87f9a -> f87f9e   2 968 992
f87f9e -> f87fa0   2 968 992
f87f96 -> f87f9a   2 968 992
f87fa0 -> f87fc2   2 968 986
f87fc6 -> f87fca   2 968 985
f87fc2 -> f87fc6   2 968 985
f87fca -> f87f96   2 968 985 backward=1   <-- hetaste bakåtkant
```

Den minsta upprepade PC-sekvensen (en full iteration av innerloopen,
"kolla en tabellpost, hoppa till nästa"):

```
f87f96 -> f87f9a -> f87f9e -> f87fa0 -> f87fc2 -> f87fc6 -> f87fca -> (f87f96)
```

Sju instruktioner, upprepas ~2 969 000 gånger. Dessutom finns en YTTRE
självreferens: när `A2` (pekaren som stegas 0x16 åt gången) passerar
gränsen vid lowmem `$c8.w`, faller koden igenom till
`f87fcc: move #$2000,SR` / `f87fd0: bra $f87f92` — vilket laddar om `A2`
från lowmem `$c6.w` och startar hela tabellgenomgången på nytt. Denna
YTTRE omstart mättes separat (se avsnitt 4): **494 835 gånger** på 30
sekunder, dvs den inre 7-instruktionssekvensen kör i snitt sex varv
(en gång per tabellpost, sex poster) varje gång den yttre loopen
startar om.

## 4. Loopens exitvillkor och den mätta tabellen

`[Verified]`, disassemblerat och verifierat med en levande minnesdump
vid start och vid programslut.

```
f87f92: movea.w $c6.w,A2      ; A2 <- tabellbas (lowmem $c6.w)
f87f96: move.b ($2,A2),D0     ; D0 = post.byte2
f87f9a: move.b ($3,A2),D1     ; D1 = post.byte3
f87f9e: eor.b  D1,D0          ; D0 ^= D1
f87fa0: beq    $f87fc2        ; LIKA -> hoppa till nästa post (ingenting att göra)
f87fa2: ...                    ; OLIKA -> full kontext-switch + rte (dispatchar posten)
f87fc2: adda.w #$16,A2        ; nästa post (stride 22 byte)
f87fc6: cmpa.w $c8.w,A2       ; A2 mot tabellslutet (lowmem $c8.w)
f87fca: bcs    $f87f96        ; A2 < slut -> kolla nästa post
f87fcc: move   #$2000,SR
f87fd0: bra    $f87f92        ; A2 >= slut -> börja om från början
```

**Exitvillkoret:** loopen (både inner- och ytterloopen) fortsätter för
evigt så länge `byte(post+2) == byte(post+3)` för VARJE post i tabellen.
Den bryts bara för en enskild post genom att posten dispatchas
(`f87fa2`-kedjan: kontext återställs från posten, `(post+2)` nollställs,
`rte`). Detta är den redan kända sex-slots schemaläggaren från
`current-blocker.md`/`PLAN.md` ("slot=1,3,0,4,5"), nu lokaliserad till
exakt denna PC-adress och detta byte-par.

**Tabellen, mätt direkt (samma körning):**

```
base=23f6 end=247a  (6 poster, stride 0x16 = 22 byte, exakt "sex slots")
```

**TIDIGT varv** (`f87f92` besökt första gången, dvs allra först i
körningen):

```
entry=23f6  byte2=00 byte3=01  OLIKA (pending)
entry=240c  byte2=00 byte3=01  OLIKA
entry=2422  byte2=00 byte3=01  OLIKA
entry=2438  byte2=00 byte3=01  OLIKA
entry=244e  byte2=00 byte3=01  OLIKA
entry=2464  byte2=00 byte3=01  OLIKA
```

Alla sex platser är "pending" vid start — matchar de sex initiala
schemaläggningarna (`slot=1,3,0,4,5` plus en) som redan dokumenterats.

**SENT varv** (vid programslut, efter **494 835** yttre
tabellgenomgångar):

```
entry=23f6  byte2=81 byte3=81  LIKA
entry=240c  byte2=80 byte3=80  LIKA
entry=2422  byte2=80 byte3=80  LIKA
entry=2438  byte2=80 byte3=80  LIKA
entry=244e  byte2=01 byte3=01  LIKA
entry=2464  byte2=01 byte3=01  LIKA
```

Alla sex platser har blivit "lika" (dispatchade/kvitterade) och
**stannar där för resten av körningen.** Loopen (`f87f96`-kedjan)
fortsätter ändå att fysiskt exekvera — den scannar tomt, om och om
igen, 494 835 gånger, utan att någonsin hitta en post att dispatcha.

## 5. fromPc->toPc räckte

`[Verified]`. Exakt PC plus kantparet ovan var tillräckligt för att
lokalisera och förstå hela loopen; ingen ringbuffer eller allmän
blockgraf byggdes (i linje med uppgiftens egen instruktion att bara
lägga till det minimum som behövs).

## Efter punkt 4: skrivwatchpoint på tabellens byte2/byte3-fält

`[Verified]`. En exakt, ej hastighetsbegränsad räknare lades till för
varje skrivning till `byte(post+2)`/`byte(post+3)` för alla sex platser
(motsvarande den redan existerande, men hastighetsbegränsade,
`log_f87f96_queue_write`-loggningen — se den logiken i `asr10_boot.cpp`
för `ASR10_F87F96_QUEUE_WRITE`, en tidigare sessions egen
skrivbevakning av exakt samma tabell).

**Resultat: totalt 138 skrivningar under HELA 30-sekunderskörningen,
och samtliga 138 sker inom de sex FÖRSTA tabellgenomgångarna**
(`restart_count_at_write` går 0 → 6). Efter den sjätte
tabellgenomgången — dvs under **494 829 av 494 835** genomgångar, i
praktiken hela körningens andra halva — **skriver INGENTING till någon
av de sex platserna.**

De 138 skrivningarna kommer från nio olika PC (samtliga redan kända
kodregioner i filen, ingen ny disassemblering krävdes för dem):
`f87e82`, `f87f28`, `f87f2c`, `f87fb0` (nollställer `(post+2)` vid
dispatch — den enda platsen som EGENTLIGEN hör till loopen själv),
`f89ac2`, `f880ce`, `f880d2`, `f880fc`, `f88100`, `f88120`, `f88124`,
`f8816e`, `f8ce3a`, samt `fb8ab6` (FDC-databytet skriver in i samma
minnesregion tidigt under diskladdningen, innan tabellen tas i drift
som schemaläggartabell — ofarlig adressöverlappning under bootens
första sekund, inte en skrivning till en "levande" schemaläggarpost).

**Svaret på "skriver NÅGOT till den under hela körningen?": nej, inte
efter de första sex tabellgenomgångarna.** Det som SKULLE ha skrivit
dit är en ny producenthändelse — ett nytt inkommande panelbyte, en
timer/interrupt som lägger en ny uppgift i kön, eller en sjunde/åttonde
schemaläggningsbegäran. Ingen sådan händelse inträffar i den här
30-sekunderskörningen efter de sex första. Detta bekräftar, nu med en
exakt siffra och en exakt adress, det `PLAN.md` redan misstänkte
kvalitativt: booten fastnar inte för att en väntan misslyckas, utan för
att INGEN NY HÄNDELSE NÅGONSIN triggar en sjunde schemaläggning under
`TUNING KBD`-fasen. Vilken händelse som borde ha producerat den sjunde
posten (och varför den uteblir) är en öppen fråga för en framtida
uppgift — de nio kända producent-PC:erna ovan är kandidaterna att
disassemblera vidare.

## Städning

All instrumentering för den här uppgiften (en `<unordered_map>`-baserad
per-PC/per-kant-räknare bakom `ASR10_DIAG_PC_HISTOGRAM`, en riktad
sampling av `$F89C46` som visade sig vara en felkonfigurerad körnings
artefakt, samt en exakt skrivräknare för schemaläggartabellen) har
tagits bort i sin helhet. `git diff --stat
src/mame/ensoniq/asr10_boot.cpp` visar netto noll rader efter
uppgiften. Ingen kompensation byggd.

---

# Scheduler slot resume layout after all-channel PAR experiment

2026-08-03. Follow-up to `par-channel-synthetic-experiment.md`. Method:
temporary C++ instrumentation in the existing instruction hook, gated by
`ASR10_DIAG_SCHEDULER_SLOT_PROBE`, plus the already documented temporary
all-channel PAR table to avoid the known channel-7 divide-by-zero path.
All probe code and the PAR table were removed after measurement; source
tree state returned to zero `asr10_boot.cpp` diff.

Command used for the corrected vector/count run:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_ES5506_HOST=1 \
ASR10_EXPERIMENT_PAR_CHANNEL_TABLE=1 \
ASR10_DIAG_SCHEDULER_SLOT_PROBE=1 \
SDL_VIDEODRIVER=dummy \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 25 -log
```

## `[Verified]` Slot layout

Static disassembly of ROM `$F87F76-$F87FD0` and live `$F87FC0` logs agree:

| Field | Source |
|---|---|
| table base | lowmem `$00C6` = `$23F6` |
| table end | lowmem `$00C8` = `$247A` |
| stride | `$16` bytes |
| saved PC | slot `+6`, restored by `$F87FA2` then `RTE` at `$F87FC0` |
| saved SR | slot `+A`, saved at `$F87F86`, restored by `$F87FA6` |
| saved A5 | slot `+C`, restored at `$F87FB4` |
| saved USP | slot `+E`, saved at `$F87F82`, restored at `$F87FAA-$F87FAE` |

The six live slots are `$23F6`, `$240C`, `$2422`, `$2438`, `$244E`, and
`$2464`.

## `[Verified]` TRAP #7 and TRAP #8 targets

Live runtime vector read, delayed until the first scheduler/trap hit:

```
vector39=f88108 vector40=f8812c handler39_word0=3478 handler40_word0=007c
```

Static disassembly:

```
f88108: movea.w $b6a.w,A2
f8810c: move.w  D0,($14,A2)
f88110: moveq   #0,D1
f88116: cmp.w   (A2),D0
f8811a/f88120: clear/set bit 0 of slot +2
f88124: set bit 0 of slot +3
f88128: bra     $f87f80

f8812c: ori     #$700,SR
f88130: movea.w $b6a.w,A0
f88134: move.w  D0,(A0)
f88136: rte
```

`[Verified]` TRAP #7 is a scheduler state update plus direct branch into
the context-save path. TRAP #8 writes `D0` to the active slot's word 0 and
returns with `RTE`. Neither trap was classified by name alone.

## `[Verified]` Dispatch counts

The corrected 25-second run made 10,044 `$F87FC0` dispatches and 272,874
idle restarts at `$F87FCC`.

Per slot:

| Slot | A2 | Dispatches |
|---:|---:|---:|
| 0 | `$23F6` | 1 |
| 1 | `$240C` | 1 |
| 2 | `$2422` | 1 |
| 3 | `$2438` | 1 |
| 4 | `$244E` | 75 |
| 5 | `$2464` | 9,965 |

Per resume PC:

| Resume PC | Dispatches |
|---:|---:|
| `$00780C` | 9,964 |
| `$006876` | 40 |
| `$0069BC` | 33 |
| `$0069CC` | 1 |
| `$FFA2A2` | 1 |
| `$FFC8CA` | 1 |
| `$00738E` | 1 |
| `$FF90F4` | 1 |
| `$00689A` | 1 |
| `$0077C2` | 1 |

The dominant combination is slot 5 (`A2=$2464`) resuming at `$00780C`.

## `[Verified]` Dominant resume PC

Disassembly around `$00780C`:

```
0077c2: clr.w   $d0b4.w
0077c6: clr.w   $d0b2.w
0077ca: move.w  #$64,D0
0077ce: trap    #8
0077d0: clr.w   $d0b0.w
0077d4: jsr     $e68e.l
0077da: jsr     $7cf0.l
0077e0: jsr     $7cf0.l
0077e6: cmpi.b  #$1,$ce3.w
0077ec: beq     $77fe
0077ee: movea.w $d0b0.w,A0
0077f2: tst.b   (-$2f3c,A0)
0077f6: beq     $77fe
0077f8: jsr     $7cf0.l
0077fe: movea.w $d0b0.w,A0
007804: move.b  (-$2f4a,A0),D0
00780a: trap    #7
00780c: addq.w  #1,$d0b0.w
007810: cmpi.w  #$b,$d0b0.w
007816: ble     $77d4
007818: jsr     $71e6.w
00781c: jsr     $e63c.l
007822: jsr     $e66e.l
007828: bra     $77ca
```

`[Verified]` `$00780C` is not a hang address. It is the instruction after
the loop's TRAP #7 scheduler handoff. The task repeatedly scans indices
`$D0B0=0..$0B`, conditionally calls `$007CF0`, and hands control back to
the scheduler with `D0` loaded from a table at `$D0B6 + $D0B0`.

`[Likely]` The dominant wait is this slot-5 task cycling through its
12-entry scan and scheduler handoff. The observed condition gates are
`$0CE3 == 1` and per-index bytes at `$D0C4 + $D0B0`; the exact hardware
event or subsystem state behind those bytes remains open.

---

# Scheduler tick/slot timing probe

2026-08-03. Follow-up to the slot resume probe above. Method: temporary
C++ observation in the existing instruction hook, gated by
`ASR10_DIAG_SCHEDULER_TICK_PROBE`, plus the already documented temporary
all-channel PAR table. This was used only to correlate CPU registers,
exception-frame callers, low-memory slot fields, and writing PCs in one
run. All probe code and the PAR table were removed after measurement;
`asr10_boot.cpp` returned to zero diff.

Command:

```sh
ASR10_DIAG_PANEL_AUTORESPOND=1 \
ASR10_EXPERIMENT_ES5506_HOST=1 \
ASR10_EXPERIMENT_PAR_CHANNEL_TABLE=1 \
ASR10_DIAG_SCHEDULER_TICK_PROBE=1 \
SDL_VIDEODRIVER=dummy \
./mess asr10booth -flop1 floppies/asr10booth/V350.img \
  -video none -sound none -nothrottle -seconds_to_run 25 -log
```

End of run:

```
ASR10_CS3_ACCESS_SUMMARY ... unknown=0
ASR10_MC68302_ACCESS_SUMMARY ... unknown=0
Average speed: 64.54% (24 seconds)
```

## `[Verified]` 100 ms slot snapshots

The probe captured 251 snapshots of all six scheduler slots. A stable
late snapshot:

| Slot | Base | Counter | State +2/+3 | Pending | Saved PC | Pre-2 | Parking | Threshold |
|---:|---:|---:|---:|---:|---:|---:|---|---:|
| 0 | `$23F6` | `$0000` | `$02/$02` | 0 | `$F87F66` | `$601A` | other | `$0000` |
| 1 | `$240C` | `$0000` | `$80/$80` | 0 | `$FFC8B0` | n/a | other | `$0000` |
| 2 | `$2422` | `$0000` | `$80/$80` | 0 | `$0073EA` | `$4E46` | other | `$0000` |
| 3 | `$2438` | `$0000` | `$80/$80` | 0 | `$F8F2FA` | `$4E46` | other | `$0000` |
| 4 | `$244E` | `$0000` | `$08/$08` | 0 | `$F87F66` | `$601A` | other | `$0000` |
| 5 | `$2464` | `$0064` | `$01/$01` | 0 | `$00780C` | `$4E47` | TRAP #7 yield | `$0063` |

Slots 0-4 were not parked at a saved PC immediately after TRAP #8
(`$4E48`) with a permanently zero counter. By this test, none of slots
0-4 is unambiguously suspicious in this run.

Static disassembly for the saved PCs:

```
f87f64: bra     $f87f80
f87f66: move    USP,A0

ffc8ac: lea     (A5,D2.l),A0
ffc8b0: move.b  ($47,A0),D2

0073e8: trap    #6
0073ea: bra     $740c

f8f2f8: trap    #6
f8f2fa: jsr     $ffff9650.l
```

## `[Verified]` TRAP #8 callers

The run logged 905 entries at `$F8812C`.

| Caller PC | Count | Notes |
|---:|---:|---|
| `$0077CE` | 831 | slot 5 background poller sleep |
| `$006870` | 40 | PAR/calibration path |
| `$0069B4` | 33 | PAR/calibration path |
| `$0069C4` | 1 | PAR/calibration path |

Representative entries:

```
caller_pc=006870 active_slot=244e slot_index=4 d0=0004 counter_before=0000 counter_after=0004 threshold=0000
caller_pc=0077ce active_slot=2464 slot_index=5 d0=0064 counter_before=0000 counter_after=0064 threshold=0000
caller_pc=0077ce active_slot=2464 slot_index=5 d0=0064 counter_before=0058 counter_after=0064 threshold=0058
```

`[Verified]` TRAP #8 writes `D0` to the active slot counter. For the
stable slot-5 loop, the threshold before the sleep is `$0058`.

## `[Verified]` Slot 5 timing

An early one-shot threshold read captured `$0009`, before the stable
poller state was established. The stable value visible in later TRAP #8
rows is `$0058`.

With the firmware's 100-tick sleep value:

```
wake_after = 100 - threshold = 100 - 88 = 12 ticks
```

The measured interval between `$0077CE` passages averaged 191,994 CPU
cycles over 830 intervals, about 12.0 ms at the 16 MHz main CPU clock.
This matches the stable threshold-derived 12 ms wake interval.

`[Likely]` Slot 5 is an intentional periodic background poller with a
roughly 12 ms steady-state cadence in this boot path.

## `[Verified]` Panel THRB references

Only two of the requested panel THRB reference PCs executed:

| PC | Hits | After PAR calibration |
|---:|---:|---:|
| `$F89AA2` | 0 | 0 |
| `$F89BE0` | 0 | 0 |
| `$F89C46` | 8 | 0 |
| `$F89C86` | 0 | 0 |
| `$F89CAE` | 2 | 0 |
| `$F89CBC` | 0 | 0 |
| `$F89CE8` | 0 | 0 |
| `$F89CF6` | 0 | 0 |
| `$F89D0E` | 0 | 0 |
| `$F89D22` | 0 | 0 |

`[Verified]` In this run, the panel driver did not hit those THRB
reference PCs after OS calibration.

## `[Verified]` Slot write attribution

Every observed slot-field change was attributed to a writing PC. No
spontaneous slot changes were observed.

| Producer class | Writes |
|---|---:|
| IRQ6 / `$F88300` | 20,268 |
| TRAP #8 / `$F8812C` | 905 |
| secondary callback | 0 |
| other writing PC | 40,155 |

Top writing PCs/fields:

| Count | Producer | PC | Field |
|---:|---|---:|---:|
| 10,230 | IRQ6 / `$F88300` | `$F88312` | `+0` |
| 10,044 | other | `$F87FB0` | `+2` |
| 10,039 | other | `$F88124` | `+2` |
| 10,039 | other | `$F88120` | `+2` |
| 10,038 | IRQ6 / `$F88300` | `$F8831A` | `+2` |
| 9,965 | other | `$F8810C` | `+$14` |
| 905 | TRAP #8 / `$F8812C` | `$F88134` | `+0` |

`[Likely]` The large "other" group is scheduler-owned state maintenance:
context save/ready flag handling at `$F87FB0`, TRAP #7 threshold/state
logic at `$F8810C-$F88124`, and early boot table initialization or
overlap such as `$FB8AB6`.

`[Hypothesis]` The missing forward progress after calibration is not a
bad slot-5 sleep interval. The next target should be the event source
that should enqueue or wake another task after calibration completes.
