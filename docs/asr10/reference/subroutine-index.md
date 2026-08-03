# ASR-10 rutinindex

Slå upp en adress, förstå vad som händer där. Ingen historik, inga
experiment, inga resonemang — de hör hemma i `investigations/`.

Statusmärkning per post:

- **V** — verifierad mot disassemblering och/eller live-körning
- **L** — sannolik, härledd men inte bekräftad
- **H** — hypotes, får inte byggas vidare på utan mätning

## Konventioner som måste läsas först

**Teckenutvidgning.** `jsr $xxxx.w` och `move $xxxx.w` med bit 15 satt
utvidgas till `$FFxxxx`, inte `$00xxxx`. `$FFFC4803` och `$00FC4803` är
**samma** 24-bitarsadress. OS:et använder båda ändarna av kortformen:
låga celler positivt (`$0CE3`), höga negativt (`$FFD0B0`).

**Diskresident kod.** Överlagringen som innehåller `$0067xx`-`$0078xx`
ligger i `V350.img` med `file_offset = RAM + 0x2600`. Ankare: `$0067F6`
→ `0x8DF6`. Metod i `investigations/os-code-extraction.md`.

**ROM-avbilden.** `asr10.bin`, 256 KB, mappad `$F80000`-`$FBFFFF`, rak
big-endian ordläsning. Offset 0-7 är SSP och PC; kod börjar på offset
`$0C`.

**KÄND BLIND FLÄCK.** Sökning efter absoluta adresser i ROM hittar
**inte** registerrelativa accesser (`($2,A0)` med basen i ett register).
Det har missat fynd fyra gånger: CTU/CTL/ACR i en tabellstyrd init, en
tredje IMR-skrivning, feltabellens `FFF8xxxx`-pekare, och hela
DUART-breakrutinen på `$F8845A`. Hittar du inte en referens du vet ska
finnas — anta registerrelativ form innan du drar en slutsats.

---

## ROM — bootstrap och undantag

| Adress | Rutin | Gör | Status |
|---|---|---|---|
| `$F8000C` | Reset entry | `move.w #$2700,sr` — maskar allt. PC ur vektorn på offset 4. | V |
| `$F88280` | **Gemensam exception-svans** | `ori #$700,SR`; skriver felkoden ur D0 till `($00C0).w`; anropar `$F977B0`, `$FF8D44`, `$F8CF9C`, formateraren `$F89D46`; `move.w #$1600,D7`; `jmp $FB8E3E`. Återvänder aldrig. | V |
| `$F882AA`–`$F882DA` | Exception-stubbtabell | Fyra byte per post i vektorordning: `moveq #kod,D0` / `bra $F88280`. Vektor 2→128, 3→129, 4→131, 5→**130 (divide by zero)**, 6→132, 7→133, 8→134, 9→135, …→137/138/139. Bekräftad mot servicehandboken, elva av elva. | V |
| `$F884F8` | ERROR 145-resning | `moveq #$91,D0` / `trap #0`. Inget CPU-undantag — firmware upptäcker själv "unknown DUART interrupt". | V |
| `$F89D46` | Felmeddelandeformaterare | Läser felkoden ur `($00C1).w`, tre siffror, bygger `ERROR nnn - REBOOT ?` ur `$FFF824AA` + `$FFF824B1`. | V |
| `$FB8E7E` | Avbrotts- och timerinit | GIMR←`$8040` (`$FC6812`), IMR←0 (`$FC6816`), ISR←`$FFFF` (`$FC6818`), IPR←`$FFFF` (`$FC6814`), Timer2 TRR←`$3F01` (`$FC6852`), TMR←`$003B` (`$FC6850`). | V |

## ROM — schemaläggare

Uppgiftskontrollblock, stride `$16`, tabellgränser i `($00C6).w` och
`($00C8).w`. Sex slots. Layout:

```
+$00 raknare      +$02 pending    +$03 vilomonster   +$06 sparad PC (long)
+$0A sparad SR    +$0C sparad A5  +$0E sparad USP    +$14 troskel
```

| Adress | Rutin | Gör | Status |
|---|---|---|---|
| `$F87F40` | TRAP-ingång till växlaren | `movem.l` sparar register, `move SR,(A7)`, patchar återhoppsadressen på stacken med `$FFF87F66`. | V |
| `$F87F80` | Kontextsparning | USP→`+$0E`, `(A7)+`→SR `+$0A`, `(A7)+`→PC `+$06`, maskar avbrott. | V |
| `$F87F92` | Dispatch-skanning | Läser `+$02` mot `+$03`, `eor.b`; skiljer de sig återställs kontexten och `rte` in i uppgiften. Aktuell uppgift skrivs till `($0B6A).w`. | V |
| `$F87FCC` | **Idle-loop** | `move.w #$2000,SR` — enda stället avbrott öppnas helt — sedan `bra $F87F92`. Att stå här är normalt. | V |
| `$F88300` | **IRQ6-handler / pending-producent** | Kvitterar via `move.b ($FFFC481F).l,D0` (Stop Counter). Dekrementerar varje slots `+$00`; när `d1 <= ($14,a0)` görs `bclr` i `+$02` = uppgiften blir redo. **Räknare 0 → slot hoppas över för alltid.** Tickräknare i `($0B82).w`, var tionde tick körs en sekundär tabell ur `($00CA).w`, stride `$1A`, med callback-pekare på `+$16`. | V |
| `$F8812C` | TRAP #8-handler | Skriver D0 till aktiv slots `+$00` = "sov N tick". Faktisk väntetid är `N − tröskel`, inte N. | V |
| `$F88108` | TRAP #7-handler | Schemaläggartillstånd + gren till kontextsparning. Tar D0 som parameter. | V |

## ROM — DUART och panel

DUART-register: bas `$FFFC4801`, stride 2, alltså register *n* på
`$FFFC4801 + 2n`. **Kanal A = MIDI, kanal B = panel** (bevisat via
`esq5505.cpp` rad 771-778 och via paneltext fångad på THRB).

| Adress | Rutin | Gör | Status |
|---|---|---|---|
| `$F884BE` | **DUART-avbrottsdispatcher** | Läser ISR (`$FFFC480B`) och dispatchar: bit 5 RxRDY B → `($00DE).w`; bitar 2\|1 kanal A → `($00E2).w`; bit 0 TxRDY A → `($00E6).w`; bit 3 counter ready → `($8638).w`. Ingen träff → ERROR 145. **Bit 4 (TxRDY B) testas inte.** | V |
| `$F8845A` | Kanal A break-återställning | Läser RHRA, läser SRA, skriver CRA `$40`/`$50`/`$20`/`$01`, och om SRA bit 7 (Received Break) är satt läses RHRA en extra gång för att kasta null-byten. Registerrelativ — osynlig för absolutsökning. | V |
| `$F89AA2`–`$F89D22` | Paneldrivrutin | Tio referenser till THRB `$FFFC4817`. Skriver panelens 22-teckensrader. Felformateraren ligger direkt efter, på `$F89D46`. | V |
| `$F8E1DA` | OPR-skrivare, tabellstyrd | `A0 = $F8E252`, index ur `($0170).w`, `OPR = ~tabell[i]`. Tabell `2E 57 36 00` → OPR `$D1, $A8, $C9, $FF`. Index cyklar 0..3. Följs av `bclr #7,($00FC6823).l` (PIO port A). | V |
| `$F97BC6` | Tabellstyrd DUART-init | (offset, värde)-par, terminator `00FF`: `0A→16` IMR, `10→0E` MR1/2B, `1A→18` OPCR, `1C→14` SetOPR, `1E→0C` ResetOPR. | L |
| `$FB7BEE`, `$FB7C30` | IPCR-läsare | `tst.b IPCR` följt av ovillkorlig `st` — rensar latchen. Korrekt förändringsdetektor som accepterar "ingen förändring". | V |
| `$FB7C84` | IPCR bit 4-test | `btst #4` — enda riktiga testen av IPCR. | V |
| `$FB7CA8` | Input Port bit 2-test | `btst #2,($FFFC481B).l`. | V |

## ROM — analoga ingångar (ES5506 PAR)

| Adress | Rutin | Gör | Status |
|---|---|---|---|
| `$F8DAFE` | PAR-avläsning, rå | `movea.l #$00FC2001,A0` / `jsr $FFFC60B0` / `asl.w #6,D2` / `ori #1,CCR` / `rts`. | V |
| `$F8DB1E` | PAR-avläsning, skalad | Som ovan, `bra $F8DB6E`. | V |
| `$F8DB30` | PAR + filter 1/2 | `add.w ($6,A2),D2` / `roxr.w #1,D2` — carry-bevarande medelvärde. Tillståndscell `(A2+6)`. | V |
| `$F8DB4C` | PAR + filter 3/4 | `0,75 × gammalt + 0,25 × nytt`, samma tillståndscell. | V |
| `$F8DB6E` | Delad svans | Tröskeljämförelse mot `(A2+8)` och `(A2+$A)`, hopptabell på `$F8DB8C` indexerad med `(A2)`. Fem kända mål. | V |
| `$F8DB12`, `$F8DB18` | Parameterblock | Sex byte vardera, **data inte kod**. Laddas i A3. Byte 1 verkar vara offseten till tillståndscellen inom instansblocket. | H |

## ROM — tabeller och dataformat

| Adress | Innehåll | Status |
|---|---|---|
| `$F8050E` | Namngiven feltabell, 29 pekare i `FFF8xxxx`-form → 22-teckensmeddelanden. Koder utanför faller igenom till `ERROR nnn - REBOOT ?`. | V |
| `$FB8F2E`–`$FB9094` | Bootmeddelanden, 22 tecken styck: `DISK NOT FORMATTED`, `PLEASE INSERT DISK`, `ENSONIQ ASR-10`, `LOADING SYSTEM`, `SCSI INSTALLED`, `SEARCHING FOR SCSI DEV.` m.fl. | V |
| `$F81003`–`$F81F87` | Ordfragmentvokabulär, 256 NUL-terminerade fragment. Skärmar byggs av fragmentindex med inbäddade kontrollbyte (`1F xx`, `16 xx`, `13 xx`). Innehåller `ANALOG INPUTS`, `A/D TO D/A`, `DC OFFSET`, `MIDI LOOP`, `GPR MONITOR`, `CALIBRAT`, ` TUNED`. | V |
| — | **Strängtabellformat**: `<pekare.l><bredd.b><antal.b>`. 114 självvaliderande förekomster i ROM. Nyckeln till varje meny och uppräknad parameter. Ex: `$F853DA` → `$F853E0`, 12×3 = `LOW VOLTAGE` / `HIGH VOLTAGE` / `ESP RAM TEST`. | V |

## DPRAM — MOVEP-thunkbibliotek `$FC6028`–`$FC6136`

Alla använder A0 som bas, satt av anroparen. Tjugo thunkar; de tre som
används mot ES5506:

| Adress | Instruktion | Mål med `A0 = $FC2001` |
|---|---|---|
| `$FC60B0` | `movep.l ($68,A0),D2` | `$FC2069/6B/6D/6F` = **PAR** (registerindex 13) |
| `$FC60B6` | `movep.l ($78,A0),D0` | `$FC2079/…` = **PAGE** (index 15) |
| `$FC60BC` | `movep.l ($70,A0),D0` | **IRQV** (index 14) |

**PAR:s byteordning.** Värdet är 10 bitar högerjusterat i en 32-bitars
latch, så de två första MOVEP-byten är **alltid `$00`**. Data finns bara
på `$FC206D` (bit 9:8) och `$FC206F` (bit 7:0). Läs aldrig av PAR på
`$FC2069`.

## Diskresident OS (V350, `file_offset = RAM + 0x2600`)

| Adress | Rutin | Gör | Status |
|---|---|---|---|
| `$006864` | **ADC-mätslinga** | `moveq #7,D7`, åtta varv. Per varv: `trap #8` (D0=4, sov), `trap #7` (yield), `jsr $FFFC60B0` (PAR), `asl.w #6` + `lsr.w #3` (netto `raw<<3`), ackumulera i D6. Returnerar summan i D2. Fullt utslag = `$FFC0`. Väljer ingen kanal. | V |
| `$0067EC` | Kalibrering steg 1 | `ori.b #$07,($00FC6829).l` → **kanal 7 (referens)**, mät, lagra `$0DD6`, `divu.w D2` i `$A3480000` → faktor på `$0DF2`. | V |
| `$006800` | `divu.w D2,D0` | Fäller `ERROR 130` när D2 = 0. Endast noll är dödligt; `bvc` + klamp till `$FFFF` hanterar overflow avsiktligt. | V |
| `$00680C` | Kalibrering steg 2 | `andi.b #$F8` + `ori.b #$05` → **kanal 5**, mät, primar filtercellen `($0DC2+6)`, `A3 = $F8DB12`, `jsr $F8DC2E`. | V |
| `$00683A` | Kalibrering steg 3 | `andi.b #$F8` + `ori.b #$00` → **kanal 0**, mät, `mulu` mot faktorn, `swap`, lagra `centrum+$528` på `$0DDE` och `centrum−$528` på `$0DE0`. | V |
| `$0068C8` | Andra kalibreringsvägen | Kanal 7 igen, instansblock `$0DD0`, `A3 = $F8DB18`, `jsr $F8DB4C`, egen kopia av divisionen. | V |
| `$0077C2`–`$007828` | **Slot 5, bakgrundspollare** | `trap #8` med D0=100 (faktisk väntan = 100 − tröskel `$58` = 12 tick), skannar tolv poster ur `($FFD0B6+index)` med en `trap #7` per post, `bra` tillbaka. **Oändlig med flit** — dess dominans i dispatchstatistik är korrekt beteende. | V |

## RAM-celler

| Adress | Innehåll |
|---|---|
| `$00C0`/`$00C1` | Felkod, läses av formateraren |
| `$00C6` / `$00C8` | Schemaläggartabellens start och slut (`$23F6` / `$247A` i V350) |
| `$00CA` / `$00CC` | Sekundär tabell, stride `$1A` |
| `$00DE`, `$00E2`, `$00E6` | DUART-dispatcherns hanterarpekare |
| `$0170` | OPR-tabellindex, 0..3 |
| `$0B6A` | Senast dispatchade uppgift |
| `$0B82` | Tickräknare |
| `$0CE3` | Testad av slot 5 |
| `$0DC2`, `$0DD0` | Reglageinstansblock, `$0E` isär. `+6` filtertillstånd, `+8`/`+A` trösklar |
| `$0DD6` | Kanal 7:s råsumma |
| `$0DDE` / `$0DE0` | Kanal 0:s dödzon, centrum ± `$528` |
| `$0DF2` | Kalibreringsfaktor, 0.16 fixpunkt |
| `$FFD0B0` | Slot 5:s skanningsindex |

## Enhetsfönster

| Område | Enhet |
|---|---|
| `$FC2000`–`$FC207F` | ES5506, `.umask16(0x00ff)`. PAR idx 13, IRQV 14, PAGE 15 |
| `$FC3000`–`$FC303F` | ES5510 host. `$FC31C1` = host offset `$E0`, "Write select GPR+INSTR" |
| `$FC4801 + 2n` | SCN2681 DUART. Kanal A MIDI, kanal B panel |
| `$FC6000`–`$FC67FF` | MC68302 DPRAM (thunkbiblioteket) |
| `$FC6800`+ | MC68302 SIM-register. GIMR `$6812`, IPR `$6814`, IMR `$6816`, ISR `$6818`, PBDAT `$6828`/`$6829` (bit 2:0 = ADC-kanalval), Timer2 `$6850`/`$6852` |

## Diskformat

Katalogen börjar på filoffset **`0x41E`**, 26 byte per post:

```
+0  typ (word)   +2  namn (12 tecken)   +14 storlek i block (word)
+16 flagga       +18 startblock (long)  +22 reserv
```

Typkoder: `$03` INSTRUMENT, `$1C` SEQUENCE, `$1D` SONG, `$1E` BANK,
`$20` OS, `$21` EFFECT.

Kedjan validerar sig själv: `startblock[n] = startblock[n-1] +
storlek[n-1]`, 17 av 17 i V350. Geometri:
`byte_offset = (track_index*20 + (R-1)) * 512`, `track_index = C*2 + H`.

## Avförda tolkningar — bygg inte vidare på dessa

- **SRA bit 7 (Received Break) orsakar browserregressionen.** Testad
  genom att tvingas hög på `7bc57b8ab45`. Ingen effekt.
- **MAME saknar implicit timerstart (AN414).** Fel. `mc68681.cpp`
  rad 960-982 startar timern vid ACR bit 6-övergång.
- **DUART OPR väljer ADC-kanal.** Fel. OPR är konstant vid PAR-fönstret.
  Kanalvalet är MC68302 PBDAT bit 2:0.
- **`$0DD6` är divisorn vid `$006800`.** Formellt fel — divisorn är D2
  register-direkt. Dataflödet är dock detsamma.
- **ES5506 PAR läses på `$FC2069`.** Den byten är strukturellt alltid
  `$00`. Se MOVEP-avsnittet.
