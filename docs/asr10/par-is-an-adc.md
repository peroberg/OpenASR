# PAR är en analogingång — belagt ur ASR-10:s EGEN firmware, och mätningen "data=00" var strukturellt tvingad

2026-08-03. Statisk analys av ROM-avbilden (`asr10.bin`, `$F80000-$FBFFFF`,
rak big-endian ordläsning) plus källäsning av `src/devices/sound/es5506.cpp`
och `src/mame/ensoniq/esq5505.cpp` / `esqasr.cpp`. Ingen MAME-körning, ingen
instrumentering, noll rader C++ ändrade.

Den här filen gör två saker: den **ogiltigförklarar en mätning** som
`es5506-hostport.md` avsnitt 2/4a redovisade, och den **stänger den länk**
som `es5506-chain-verification.md` avsnitt 6 lämnade som `OPEN`.

---

## 1. `[Verified]` Byten på `$FC2069` är ALLTID `0x00`. Oavsett `read_port_cb`.

`es5506.cpp:1537-1563`:

```cpp
u8 es5506_device::read(offs_t offset)
{
    int shift = 8 * (offset & 3);
    if (shift != 0)
        return m_read_latch >> (24 - shift);
    ...
    m_read_latch = reg_read_low(voice, offset / 4);   // eller _high / _test
    ...
    return m_read_latch >> 24;                        // <-- HÖGSTA byten
}
```

och PAR-fallet i alla tre `reg_read_*`:

```cpp
case 0x68/8:    // PAR
    if (!m_read_port_cb.isunset())
        result = m_read_port_cb(0) & 0x3ff;   // 10 bitar, 9:0
    break;
```

PAR-värdet ligger i **bit 9:0 av en 32-bitars latch**. Bit 31:24 är per
konstruktion noll. `$FC2069` är `device_offset=52`, `52 & 3 == 0`, alltså
den byte som returnerar `m_read_latch >> 24`.

**Slutsats:** `data=00` på `$FC2069` säger ingenting om huruvida
`read_port_cb` är bunden eller vad den returnerar. Det är samma nolla i
båda fallen. `es5506-hostport.md`s formulering "Samtliga 8 observerade
läsningar av `address=fc2069` under körningen visar `data=00`, utan
undantag" är korrekt observerad men **noll-informativ** — den kan inte
skilja en obunden callback från en bunden.

De 10 bitarna dyker upp först i de två SISTA MOVEP-byten:

| CPU-adress | `device_offset` | `shift` | Returnerar | Innehåller PAR-bitar |
|---|---|---|---|---|
| `$FC2069` | 52 | 0  | `latch >> 24` | **inga — alltid 0x00** |
| `$FC206B` | 53 | 8  | `latch >> 16` | **inga — alltid 0x00** |
| `$FC206D` | 54 | 16 | `latch >> 8`  | PAR bit 9:8 |
| `$FC206F` | 55 | 24 | `latch >> 0`  | PAR bit 7:0 |

**Varje framtida PAR-experiment måste avläsas på `$FC206D`/`$FC206F`.**
Ett experiment som injicerar ett värde och sedan kontrollerar `$FC2069`
kommer alltid att se `00` och felaktigt rapporteras som "injektionen
verkade inte". Samma fälla gäller `ASR10_DIAG_PAR_VALUE` som den ser ut nu.

Detta är också konsistent med riktig kisel: PAR är 10 bitar i ett
32-bitars registerfönster, så toppbyten är noll även på hårdvara.

---

## 2. `[Verified]` Fyra ROM-callbacks skalar svaret med `asl.w #6` — och två av dem filtrerar det

`movep-library.md` lokaliserade fyra statiska anropare av `$FC60B0`
(`movep.l ($68,A0),D2`) i ROM. Deras exakta bytes, ur `asr10.bin`
(första blocket börjar på `$F8DAFE`, inte `$F8DB00` — mindre rättelse
mot `movep-library.md`):

```
Block A  F8DAFE  207C 00FC 2001   movea.l #$00FC2001,A0
         F8DB04  4EB9 FFFC 60B0   jsr     $FFFC60B0
         F8DB0A  ED42             asl.w   #6,D2
         F8DB0C  003C 0001        ori.b   #1,CCR          ; sätter C = "giltig"
         F8DB10  4E75             rts

Block B  F8DB1E  207C 00FC 2001   movea.l #$00FC2001,A0
         F8DB24  4EB9 FFFC 60B0   jsr     $FFFC60B0
         F8DB2A  ED42             asl.w   #6,D2
         F8DB2C  6000 0040        bra.w   $F8DB6E         ; delad svans

Block C  F8DB30  207C 00FC 2001   movea.l #$00FC2001,A0
         F8DB36  4EB9 FFFC 60B0   jsr     $FFFC60B0
         F8DB3C  ED42             asl.w   #6,D2
         F8DB3E  D46A 0006        add.w   ($6,A2),D2      ; + gammalt värde
         F8DB42  E252             roxr.w  #1,D2           ; /2 MED carry
         F8DB44  3542 0006        move.w  D2,($6,A2)
         F8DB48  6000 0024        bra.w   $F8DB6E

Block D  F8DB4C  207C 00FC 2001   movea.l #$00FC2001,A0
         F8DB52  4EB9 FFFC 60B0   jsr     $FFFC60B0
         F8DB58  ED42             asl.w   #6,D2
         F8DB5A  302A 0006        move.w  ($6,A2),D0
         F8DB5E  E248             lsr.w   #1,D0
         F8DB60  D06A 0006        add.w   ($6,A2),D0
         F8DB64  E250             roxr.w  #1,D0           ; D0 = 0,75 * gammalt
         F8DB66  E44A             lsr.w   #2,D2           ; D2 = 0,25 * nytt
         F8DB68  D440             add.w   D0,D2
         F8DB6A  3542 0006        move.w  D2,($6,A2)
                                                          ; faller igenom till svansen

Svans    F8DB6E  227C FFF8 DB8C   movea.l #$FFF8DB8C,A1   ; hopptabell
         F8DB74  D2D2             adda.w  (A2),A1         ; per-instans index
         F8DB76  B46A 000A        cmp.w   ($A,A2),D2      ; övre tröskel
         F8DB7A  630C             bls.s   $F8DB88
         F8DB7C  B46A 0008        cmp.w   ($8,A2),D2      ; undre tröskel
         F8DB80  6406             bcc.s   $F8DB88
         F8DB82  2069 0004        movea.l ($4,A1),A0
         F8DB86  6002             bra.s   $F8DB8A
         F8DB88  2051             movea.l (A1),A0
         F8DB8A  4ED0             jmp     (A0)

         F8DB8C  FFF8DBAC FFF8DBBC FFF8DBC0 FFF8DBE2 FFF8DBF2   (hopptabell)
```

`ED42` = `ASL.w #6,D2` verifierat mot 68000-kodningen
(`1110 110 1 01 0 00 010`). `E252`/`E250` är `ROXR`, inte `LSR` —
`movep-library.md` skrev `lsr.w #1,D2` för `$F8DB42`; det är en
`roxr.w #1,D2`. Skillnaden är inte kosmetisk: `roxr` efter `add.w` roterar
in carry-biten i bit 15, dvs det är ett **16-bitars medelvärde utan
overflow**, precis den kod man skriver för att medelvärdesbilda två
osignerade mätvärden. En ren `lsr` hade tappat översta biten.

### Vad koden alltså gör med svaret

1. **`asl.w #6`** flyttar ett högerjusterat 10-bitarsvärde (bit 9:0) till
   bit 15:6. Det är EXAKT den 16-bitarsjustering som resten av
   Ensoniq-familjen använder: `es5505_device`s egen PAR maskar
   `& 0xffc0` (bit 15:6), och `esq5505.cpp`s analogtabell är skriven i
   samma skala (`0x7fc0`, `0xffc0`, `0xccc0`, `0x5540`). ASR-10:s ROM gör
   i mjukvara exakt den normalisering som ES5505 gör i kisel — vilket
   bara är meningsfullt om det som anländer är ett **högerjusterat
   10-bitars mätvärde**, dvs `es5506_device`s PAR-format.
2. **Block C och D är två exponentiella glidande medelvärden med olika
   tidskonstant** (1/2 respektive 3/4 gammalt värde), var och en mot en
   egen tillståndscell `(A2+6)`.
3. **Svansen jämför det filtrerade värdet mot ett par trösklar**
   `(A2+8)` / `(A2+0xA)` och dispatchar till olika hanterare.

Man skriver inte carry-bevarande glidande medelvärden med två olika
tidskonstanter och hysteres-trösklar mot ett digitalt statusregister.
Man skriver det mot en **brusig analog mätning**.

---

## 3. `[Verified som slutsats]` Länken som var `OPEN` är stängd — från ROM-sidan

`es5506-chain-verification.md` avsnitt 6: *"The chain closes ... contingent
on exactly one unproven link: that ASR-10's actual FC2001 wiring uses the
same odd-lane-on-16-bit-bus convention ... The missing evidence is
board-level, not chip-level."*

Beviset som saknades behöver inte vara ett kopplingsschema. Avsnitt 2 ovan
är ASR-10:s egen firmware som behandlar svaret på `$FC2001+0x68` som ett
högerjusterat 10-bitars analogvärde, normaliserar det till ES5505/ES5506-
familjens 16-bitarsskala, lågpassfiltrerar det och tröskeljämför det.

`$FC2001+0x68` **är** PAR, och PAR **är** en analogingång på det här kortet.
Det är belagt av konsumenten, inte antaget av producenten.

Kvarstående osäkerhet är inte längre *om* det är PAR, utan **vad som är
kopplat till stiftet**.

---

## 4. `[Likely]` Vad som driver stiftet: en analogmultiplexer

Precedens i det här trädet, `esq5505.cpp`:

```cpp
m_otis->read_port_cb().set(FUNC(esq5505_state::analog_r));  // ADC
...
u16 esq5505_state::analog_r() { return m_analog_values[m_duart_io & 7]; }
void esq5505_state::duart_output(u8 data) { m_duart_io = data; }
```

Åtta kanaler, kanalval från DUART:ens utgångsport. ASR-10:s komponentlista
innehåller `MC74HC4051` (U55) — en 8-kanals analogmultiplexer — samt
`MC74HC4053` (U30, U33). Den enda observerade PAR-anroparen vid körning är
`$006868`-loopen med `moveq #7,D7` / `dbra`, dvs **exakt åtta varv**
(`movep-library.md`: "32 events = 8 samples x 4 MOVEP bytes").

Åtta kanaler i hårdvaran, åtta varv i mjukvaran, samma mönster som
syskondrivrutinen. Det är `[Likely]`, inte `[Verified]` — se avsnitt 5.

`esqasr.cpp` (MAME:s egen ASR-10-skelettdrivrutin) binder redan
`read_port_cb()`, men till `esq5506_read_adc()` som returnerar `0`. Den är
en stubb utan belägg och duger inte som facit.

**Skalvarning:** `es5505_device`s PAR maskar `& 0xffc0` (vänsterjusterat),
`es5506_device`s PAR maskar `& 0x3ff` (högerjusterat). `esq5505.cpp`s
konstanter är skrivna för ES5505. Återanvänds de rakt av mot en ES5506 blir
resultatet **64 gånger fel** — och fortfarande skenbart "rimligt", vilket är
värre än noll. Alla värden måste skiftas ner 6 steg.

---

## 5. `[OPEN]` Det som faktiskt måste mätas härnäst

1. **Väljer `$006868`-loopen kanal?** Disassemblera `$006840-$006894` ur
   det körande RAM:et. `divisor-zero.md` fångade bara från `$006872` och
   flaggade själv att `$006872-$006878` är svansen på en annan rutin —
   loopkroppen `$006868-$006872` är alltså **inte disassemblerad**. Om det
   finns en skrivning där (DUART OPR `$FC480E`/`$FC480F`, MC68302 PIO
   port B, eller ES5506 PAGE) är muxen identifierad och åtta kanaler
   bevisade. Finns ingen skrivning är de åtta varven översampling av EN
   kanal och hela mux-hypotesen faller.
2. **Är summan i D6 verkligen divisorn?** `error-130.md`/`divisor-zero.md`
   följer `D2 -> $0DD6` men **divisorns härkomst vid själva
   `divu`-instruktionen är inte spårad**. Innan det är gjort är
   "PAR=0 orsakar ERROR 130" en hypotes, inte en slutsats. Det här är
   dessutom det billigaste steget: det kräver ingen inkoppling alls, och
   det avgör om PAR över huvud taget är på den kritiska vägen.
3. **Wavetable-minnet.** `es5506_device` exponerar `device_memory_interface`
   med ett adressutrymme (`m_bank0_config`, 16 bitar data / 21 bitar adress,
   `es5506.cpp:185`). ASR-10 är en SAMPLER — dess vågminne är SIMM-DRAM,
   inte ROM. Rätt modell är därför `set_addrmap(0, ...)` med `.ram()`, inte
   `set_region0("waverom")`. Det är samtidigt den enda kända fixen för de
   1 875 158 `unmapped bank0`-raderna i `es5506-hostport.md` avsnitt 5.

## 6. Vad den här filen INTE påstår

- Inte att ERROR 130 orsakas av PAR. Se punkt 5.2.
- Inte vilka fysiska reglage de åtta kanalerna är. `esq5505.cpp`s
  etiketter ("pitch mod", "mod wheel", "battery voltage", "vRef") gäller
  VFX-familjen, inte ASR-10.
- Inte något värde som PAR "borde" returnera. Ingen konstant föreslås.
