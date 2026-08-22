# Metod — hypoteshantering och evidensstatus

Detta dokument är normerande för hur projektet formulerar, prövar och ändrar
tekniska påståenden. Målet är inte att undvika hypoteser, utan att göra det synligt
vad som är observerat, vad som är den enklaste överlevande förklaringen och vad som
skulle ändra bedömningen.

Metoden gäller oberoende av delsystem: ASR-10-kortet, MC68302, ES5506, ES5510,
firmware, MAME-modellen och framtida reverse-engineeringarbete.

---

## 1. Hypoteskrav

Innan en hypotes används som arbetsgrund ska följande vara dokumenterat:

1. **Påstående.** En avgränsad formulering som inte blandar flera oberoende
   slutsatser.
2. **Förutsägelse.** Vilken observation hypotesen säger att ett experiment ska ge.
3. **Övergivandevillkor.** Vilken observation som skulle motsäga förutsägelsen och
   få hypotesen att nedgraderas eller markeras `[DISPROVEN]`.
4. **Evidensdomän.** Om belägget gäller firmware, emulerad runtime, chipets
   dokumenterade funktion, fysisk kortkoppling eller extern hårdvara.
5. **Konkurrenter.** Vilka andra förklaringar som passar nuvarande data.
6. **Diskriminerande experiment.** Nästa mätning som kan skilja de överlevande
   förklaringarna åt.

En hypotes utan övergivandevillkor är inte testbar och får inte användas som
implementationens grund.

## 2. Grundregler

1. Alla hypoteser ska vara falsifierbara innan de används som arbetsgrund.
2. En statusändring kräver en namngiven observation eller mätning.
3. En hypotes får inte uppgraderas om en enklare förklaring fortfarande förklarar
   samma observationer. Den enklaste förklaring som överlever all data föredras;
   enkelhet ersätter aldrig evidens.
4. Konkurrerande hypoteser hålls separata tills diskriminerande evidens finns.
5. Negativa resultat är evidens och journalförs med rådata eller probe, exakt
   stoppunkt och en levande positiv kontroll för mätinstrumentet.
6. Ingen semantisk etikett införs utan direkt stöd i kod, mätning eller extern
   dokumentation. En adress, bufferstorlek eller samtidighet räcker inte ensam för
   att namnge en funktion.
7. Frånvaro i modellen är inte evidens om fysisk hårdvara. Firmwarebeteende,
   chipkapacitet, fysisk kortkoppling och nuvarande MAME-beteende är skilda
   påståenden.
8. Ett påstående ska delas om dess delar har olika evidensstatus. Exempel:
   "firmware använder SCC1" och "SCC1 bär audio på kortet" är två hypoteser.

## 3. Statusnivåer

### `[Verified]`

Påståendet är direkt och reproducerbart verifierat genom mätning, direkt kodbevis
eller flera oberoende evidenskällor som möts i samma slutsats. Metod, artefakt och
relevant täckning ska anges.

`[Verified]` är alltid begränsat till sin evidensdomän. Vanliga preciseringar är
`[Verified runtime]`, `[Verified static]`, `[Verified firmware]`,
`[Verified device]` och `[Verified silicon spec]`. En verifierad firmwareaccess
verifierar inte fysisk kortkoppling, och en verifierad chipfunktion verifierar inte
att ASR-10 använder den.

### `[Likely]`

Påståendet är den enklaste förklaring som överlevt samtliga relevanta observationer,
men saknar ett avgörande test. Kvarvarande alternativ och det diskriminerande testet
ska anges.

`[Likely]` betyder inte "rimligt" eller "förväntat". Det kräver positiv evidens och
en redovisad jämförelse med enklare eller konkurrerande förklaringar.

### `[OPEN]`

Två eller fler förklaringar överlever samma evidens, eller så saknas ännu en giltig
mätning. Dokumentet ska ange vad som är känt utan att välja etikett åt den okända
mekanismen.

Ett nollresultat utan positiv kontroll lämnar frågan `[OPEN]`; det motbevisar inte
hypotesen.

### `[DISPROVEN]`

Minst en verifierad observation motsäger hypotesens dokumenterade förutsägelse.
Hypotesen och observationen behålls i revisionsspåret så att samma väg inte öppnas
igen utan ny evidens.

`[DISPROVEN]` gäller den testade formuleringen, inte automatiskt närliggande eller
svagare hypoteser. Om endast en del faller ska påståendet delas och delarnas status
bedömas var för sig.

## 4. Evidens och mätbar frånvaro

Rådata inventeras före dokumentclaims. Ett resultat ska kunna spåras till den ROM,
diskbild, körning, probe, disassemblering, manual eller källfil som producerade det.

Ett negativt resultat är giltigt först när följande är känt:

- mätobjektet var aktivt under hela det relevanta fönstret,
- proben levde och kunde ge utslag under hela fönstret,
- en positiv kontroll träffade med samma metod och parametrar,
- experimentet nådde den kod- eller tillståndspunkt där effekten förväntades,
- täckningen är angiven: tid, adressområde, signaler och relevanta tillstånd.

Om något av detta saknas är resultatet "inte mätt", inte "observerat frånvarande".
De konkreta Lua-tap-fällorna och kravet på levande vittne finns i
`methods-static-analysis.md` §8.5-§8.7.

## 5. Occams rakkniv i praktiken

Enkelhet bedöms mot observationerna, inte mot vad som verkar naturligt i en modern
arkitektur. En hypotes är enklare när den kräver färre ej observerade mekanismer,
färre specialfall och färre antaganden över evidensdomäner.

När två hypoteser förklarar samma data:

1. behåll båda som `[OPEN]` om ingen är tydligt enklare eller bättre belagd,
2. använd `[Likely]` endast om en är enklast och ingen observation motsäger den,
3. konstruera ett experiment där hypoteserna ger olika förutsägelser,
4. implementera inte ny hårdvara enbart för att få den mer komplicerade hypotesen
   att passa.

## 6. Revisionsspår

Varje statusändring ska dokumentera:

- hypotesens exakta formulering,
- observationen som utlöste ändringen,
- rådata/probe och reproduktionsvillkor,
- tidigare status,
- ny status,
- varför observationen stöder eller motsäger förutsägelsen,
- vilka alternativa hypoteser som fortfarande överlever,
- vilket framtida experiment som kan diskriminera mellan dem.

Använd följande minimallayout i en investigation eller referensfil:

```text
Hypotes:
Förutsägelse:
Övergivandevillkor:
Evidensdomän:
Observation:
Rådata/probe:
Täckning och positiv kontroll:
Tidigare status:
Ny status:
Överlevande alternativ:
Nästa diskriminerande experiment:
```

## 7. Projektets korrigerande exempel

- **E2:** etiketten "spärr" ersattes när mätning visade ett verkligt beslut. Endast
  den observerade beslutspunkten uppgraderades; den bakomliggande
  högadressmodellen förblev `[OPEN]`.
- **ERROR 45/46:** föll när disassembleringen lästes om. Den verifierade avkodningen
  är ERROR 005/006, "could not synchronize audio input" enligt manualen.
- **"Keyboard buffers":** nedgraderades när etiketten saknade kodbevis. Ringarnas
  adresser och storlek kan vara verifierade utan att deras funktion är det.
- **SCC = keyboard:** förblir `[OPEN]` när fysisk keyboardevidens och
  sample-lägets firmwarebeteende ännu inte identifierar samma bytekälla.
- **SCC = audio:** förblir `[OPEN]` tills full RECORD-sekvens ger diskriminerande
  evidens. Full RECORD/start nådde 2026-08-22 `WAITING` men gav ingen RX-trafik,
  descriptorfyllning, level-4-IACK, sample-RAM-trafik eller 005/006. Den smala
  förutsägelsen "omedelbart 005/006" är `[DISPROVEN]`; den breda identiteten
  förblir `[OPEN]` eftersom ingen insignal passerade tröskeln.

Exemplen är metodhistorik, inte genvägar till nya slutsatser. Varje framtida
statusändring kräver sitt eget reproducerbara revisionsspår.
