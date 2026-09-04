# Instrumentinläsning V3.50

Det här dokumentet avslutar FDC-/instrumentinläsningsspåret i nuvarande
omfattning. Frågan är inte löst, men den är avgränsad: flera hypoteser är
motbevisade och den kvarvarande blockeraren är smalare än när spåret började.

Reproduktion:

```text
image:      floppies/asr10booth/V350.img
startläge:  FILE 1  TUTORIAL BNK
sekvens:    $0A, $23, $02
resultat:   LOADING JM DIGI SYN stannar kvar
```

`$0A` flyttar till `FILE 2  JM DIGI SYN`; `$23` bekräftar; `$02` väljer
instrument-slot enligt den då dokumenterade panelkartan.

## Vad Som Händer

[Verified dynamic] Från knappsekvensen `$0A,$23,$02` till stoppet sker detta:

```text
17.790000  displayen visar LOADING JM DIGI SYN
18.305873  FDC-status läses från $FC4001: $D0, därefter $80
18.305915  ($04EE).w <- $01 från $FB7C9C
18.305956  $FB8CF2 skriver $04 till $FC4003
18.305980  $FB8CF2 skriver $00 till $FC4003
18.306076  $FB8D14 skriver aux/control $88 till $FC4001
18.306162  $FB8D14 skriver aux/control $F3 till $FC4001
18.306249  $FB8CF2 skriver $03 till $FC4003
18.306273  $FB8CF2 skriver $E1 till $FC4003
18.306297  $FB8CF2 skriver $08 till $FC4003
18.306365  $FB8CF2 skriver $07 till $FC4003
18.306389  $FB8CF2 skriver $00 till $FC4003
18.311400  FDC-fasen är tyst: 45 läsningar, 9 skrivningar
```

[Verified dynamic] Inget READ DATA-kommandot `$46 ...` utfärdas under
`LOADING JM DIGI SYN`. Det finns ingen `$FB8AB6/$FB8ABC`-datafas i
instrumentinläsningsfönstret.

[Verified dynamic] FDC-pollningen upphör efter den korta kommandofasen.
Panel-TX upphör också. Däremot fortsätter schedulern och DUART-tickan:
counter-ready fortsätter, scheduler-tabellen läses, och panel-RX tar emot
ytterligare knappframes.

[Verified dynamic] Ingen felkod skrivs till `$049D` eller `$04AE` i
mätfönstret. `$04EE` skrivs till `$01` en gång.

[Verified dynamic] Displayen ändras aldrig från `LOADING JM DIGI SYN`.

## Det Avgörande Fyndet

[Verified dynamic] SPECIFY-kommandots tredje byte skiljer mellan faserna:

```text
boot:                03 E1 09   ND=1, non-DMA
instrumentinläsning: 03 E1 08   ND=0, DMA
```

[Verified dynamic] Bootvägen med `$09` är pollad: CPU:n läser `$FC4003` själv
via `$FB8AB6 move.b $FFFC4003.l,(A1)+`, och cirka 200000 byte överförs korrekt
under OS-laddningen. Lua memory-tap rapporterar PC efter accessinstruktionen,
så `$FB8ABC` i tap-loggen betyder att föregående instruktion `$FB8AB6` gjorde
accessen.

[Verified dynamic] Instrumentinläsningen med `$08` startar ingen sådan CPU-pollad
datafas. Bootvägen följer dessutom RECALIBRATE med SENSE INTERRUPT STATUS;
instrumentinläsningsvägen gör inte det.

[Likely] Inläsningsvägen är avbrottsdriven där bootvägen är pollad. När DMA-läget
väljs förväntar sig firmware en fullbordanssignal i stället för att CPU:n ska
driva hela byteöverföringen i en tät poll-loop.

## Vad Som Är Motbevisat

[DISPROVEN] IDMA används som programmerad DMA-kontroller i den observerade fasen.
Lua-tap under hela inläsningen gav noll skrivningar i:

```text
$FC6800-$FC68FF  MC68302 SIB / board-control candidate
$FC6800-$FC6811  MC68302 IDMA enligt mc68302.cpp::classify_offset()
$FC0000-$FC1FFF  okartlagt CS-fönster
$FC5020-$FC5FFF  okartlagt CS-fönster
```

[DISPROVEN] Inläsningen skriver över OS-koden. Efter `FILE 1` sågs noll
skrivningar i `$FFB0B0-$FFB0F0`. De 30 skrivningarna i `$FF8000-$FF8100`
kom från ROM/OS och är normal bindningstabell-/displayunderhåll, inte
överskrivning av runtime receive path.

[DISPROVEN] Minnet tar slut. Nuvarande `mem_map` erbjuder rikligt RAM för den
observerade körningen, och firmware har separata användarmeddelanden för verklig
minnesbrist. Stoppet sker utan sådan feltext och utan `$049D/$04AE`-felkod.

[DISPROVEN] Systemtickan dör. DUART counter-ready fortsätter med tickdominans
både i vila och under stallet; i load-mätningen sågs 123004 ISR-läsningar och
122720 counter-ready-träffar. IMR maskar inte bort tickan och countern stoppas
inte i mätfönstret.

[DISPROVEN] Maskinen hänger i en enkel loop. PC-profilen under stall visar
fortsatt scheduleraktivitet och många distinkta PC; schedulern dispatchar och
panel-RX tar emot knappframes.

[DISPROVEN] FDC-raten är orsaken till just detta stall. `ASR10_MISSING_FDC_RATE_SOURCE`
behövs fortfarande för bootens READ DATA-väg, men den separata rate-failen slår
till vid `$FB8AE0` med `$049D=$0D/$04AE=$28`. Instrumentinläsningsstallet här
kommer inte ens till READ DATA.

[DISPROVEN] Terminal count saknas som ensam förklaring. Den tidigare
`ASR10_EXPERIMENT_FDC_SYNTH_TC=1`-mätningen ändrade inte instrumentinläsningens
utfall.

[RETRACTED] Att 200828 byte överfördes under instrumentinläsningen. De hörde
till OS-laddningen vid boot. Felet uppstod för att A1-tappen inte var filtrerad
på kalibrerad tap-PC; A1 är destinationspekare bara när callback-PC `$FB8ABC`
representerar föregående instruktion `$FB8AB6`.

## Vad Som Står Kvar

[Verified static] Varken `intrq_wr_callback()` eller `drq_wr_callback()` är
kopplad i `src/mame/ensoniq/asr10_boot.cpp`. En snäv sökning efter dessa två
callbacks i ASR-10-drivrutinen ger noll träffar. Andra Ensoniq-drivrutiner
kopplar motsvarande callbacks, till exempel `enmirage.cpp` och `esq1.cpp`.

[Verified static] Enda FDC-signalen som i dag går ut till något annat i
ASR-10-drivrutinen är `idx_wr_callback()` till DUART IP0.

[Verified dynamic] Ingen OS-installerad vektor som pekar på kod som direkt rör
`$FC4000-$FC4003` identifierades. Kontrollen jämförde både ROM-kopian
`$F82000` och den levande tabellen `$000000-$0003FF`; 180 av 256 longword-poster
skilde sig. Varje avvikande live-mål skannades i de första 1024 bytena efter
direkta `$FC400x`-accessmönster utan träff.

[Likely] Fullbordanssignalen går via MC68302:s egen interruptcontroller, som
genererar vektorn ur GIMR-basen plus källnummer. Det blocket är
`known_unimplemented` i `src/devices/machine/mc68302.cpp`, så vägen finns inte
i modellen oavsett vilken vektor firmware förväntar sig.

Blockerare i en mening:

```text
Instrumentinläsningen väljer FDC DMA-/avbrottsvägen, men ASR-10-modellen har
ingen kopplad FDC intrq/drq-väg och ingen implementerad MC68302-intern
interruptcontroller som kan leverera fullbordanssignalen.
```

## Metodgränser Och Nästa Mätning

En fullbordanshanterare behöver inte röra FDC:n. Den kan enbart posta arbete
till schedulern; registerläsningen sker sedan i den återupptagna uppgiften.
Att skanna vektormål efter `$FC400x`-mönster letar därför efter fel egenskap
för en sådan arkitektur.

Registerrelativ adressering syns inte i absoluta sökningar. "Noll referenser"
betyder alltid "noll identifierade referenser inom den använda metoden".

Lua memory-taps rapporterar PC efter accessinstruktionen i de här mätningarna.
Filtrera därför på nästa instruktions adress, och räkna A1 som postinkrementerad
efter `move.b $FFFC4003.l,(A1)+`.

Den enda mätning som återstår i det här spåret:

```text
Sök bland de 180 avvikande vektorposterna efter en handler som skriver till
schedulerns slottabell: movea.w $c6.w,An följt av en skrivning till +2 eller
+3 relativt en slot.
```

Det är vad "posta arbete" ser ut som i den här arkitekturen, och det är
signaturen en FDC-fullbordanshanterare faktiskt skulle ha. Hittas en sådan
handler säger dess vektornummer vilken avbrottsnivå firmware förväntar sig.
Då är arbetet avgränsat: koppla FDC:ns `intrq` och implementera den del av
MC68302:s interruptcontroller som behövs.
