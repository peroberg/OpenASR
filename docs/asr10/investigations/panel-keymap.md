# ASR-10 panel keymap

Status: pilotmappning for utforskning. Intern identitet ar fortfarande
`BTN_00`...`BTN_3F`; inga funktionsnamn ar fastslagna har.

Klickbar panel:

- Alla 64 koder finns som klickbara knappar i ASR-panelens layout.
- Knapparna ligger i ett 8x8-rutnat och ar markta med hexkoden `00`...`3F`.
- Endast de verifierade markorknapparna har tangentbordskoder.

Det tidigare Shift-bankforsoket ar borttaget. Flera `PORT_CODE` pa samma
ioport-falt ar alternativ, inte kombinationer, och kunde darfor inte gora
`$20-$3F` tillgangliga via Shift.

| Kod | Tangent | Kod | Tangent |
| --- | --- | --- | --- |
| `$00` | klick | `$20` | klick |
| `$01` | klick | `$21` | klick |
| `$02` | klick | `$22` | klick |
| `$03` | klick | `$23` | klick |
| `$04` | klick | `$24` | klick |
| `$05` | klick | `$25` | klick |
| `$06` | klick | `$26` | klick |
| `$07` | klick | `$27` | klick |
| `$08` | klick | `$28` | klick |
| `$09` | klick | `$29` | klick |
| `$0A` | `Up`, klick | `$2A` | klick |
| `$0B` | `Down`, klick | `$2B` | klick |
| `$0C` | klick | `$2C` | klick |
| `$0D` | klick | `$2D` | klick |
| `$0E` | klick | `$2E` | klick |
| `$0F` | klick | `$2F` | klick |
| `$10` | `Left`, klick | `$30` | klick |
| `$11` | `Right`, klick | `$31` | klick |
| `$12` | klick | `$32` | klick |
| `$13` | klick | `$33` | klick |
| `$14` | klick | `$34` | klick |
| `$15` | klick | `$35` | klick |
| `$16` | klick | `$36` | klick |
| `$17` | klick | `$37` | klick |
| `$18` | klick | `$38` | klick |
| `$19` | klick | `$39` | klick |
| `$1A` | klick | `$3A` | klick |
| `$1B` | klick | `$3B` | klick |
| `$1C` | klick | `$3C` | klick |
| `$1D` | klick | `$3D` | klick |
| `$1E` | klick | `$3E` | klick |
| `$1F` | klick | `$3F` | klick |

Valda bort fran pilotmappningen:

- `Tab`: MAME:s UI-meny.
- `Esc`: UI Cancel/exit.
- `Enter`/`Return`: UI Select och host-beroende standardkommando.
- `Space`: vanlig UI/host-genvag och latt att trycka av misstag.
- `P`: MAME pause.
- funktionstangenter: MAME UI/debug/video-kommandon.
- `Backspace`, `Delete`, `Insert`, `Home`, `End`, `Page Up`, `Page Down`:
  vanliga UI-/navigeringsgenvagar.
- `Scroll Lock`: anvands for keyboard capture/passthrough i MAME.
- backtick/tilde: MAME on-screen display.

Ovriga tangentbordskoder ar inte bundna i pilotmappningen. De 62 oidentifierade
knapparna ska anvandas via layoutklick tills funktionerna ar verifierade.

**Rattat, 2026-08-24** (`panel-button-and-transport-map.md`): `$0C`/`$0D`
hade tidigare `Left`/`Right` som tangentbordsgenvag. Matt direkt: fran
`REC SRC`-skarmen navigerar dessa till en helt orelaterad meny
(`COPY`/`ERASE`/`FILTER`/`SHIFT AUDIO TRACK` vid upprepade tryck), inte en
markorflytt. Etiketten var fel, inte bara overifierad, sa
tangentbordsbindningen togs bort helt (`src/mame/ensoniq/esqpanel.cpp`) --
en felaktig genvag ar varre an ingen genvag.

**Rattat igen, samma dag** (`partial-update-position-probe.md`): de
verkliga Left/Right Arrow-koderna ar nu funna och verifierade --
`$10`=Left, `$11`=Right, matt mot displayens egen understrykning som
facit (REC SRC Falt 2 <-> Falt 1, badstal i badda riktningar). Samtidigt
uppmattes att `$0A`/`$0B` (Up/Down) hade fel tangentbordsgenvag inbordes:
effekten (VOLUME-skarmen, en nedrakning fran den knapp som kallades
"Up") visade att `$0A` ar verkligt Up (tak vid 99, ingen synlig andring)
och `$0B` ar verkligt Down (99->98). Bada rattade i
`src/mame/ensoniq/esqpanel.cpp`; tabellen ovan speglar nu den uppmatta,
inte den ursprungliga, tilldelningen.
