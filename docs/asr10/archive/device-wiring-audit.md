# Enhetsinkoppling: inventering av samtliga sex enheter i machine_config

2026-08-03. Läst: `docs/asr10/divisor-zero.md`, `docs/asr10/
os-code-extraction.md`, `docs/asr10/es5506-chain-verification.md`,
`docs/asr10/movep-library.md`, `CLAUDE.md`, `docs/asr10/PLAN.md`
(avsnitt om FDC/DUART-blockeraren, "Steg A/B/C"). Metod: läsning av
källkoden i `src/mame/ensoniq/asr10_boot.cpp` (ingen körning krävd för
den här delen) — `asr10_boot()` (rad 10014), `mem_map()` (rad 2180),
samt varje handskriven handler-funktion som svarar för respektive
enhets adressfönster. Ingen instrumentering, ingen kompilering.

För varje enhet: (a) är läs/skrivvägen mappad i `address_map` eller en
handler? (b) är utgående callbacks (irq/drq/tx/ready) kopplade? (c)
eller svarar handskriven kod för adressfönstret i stället?

## 1. `m_maincpu` (`mc68302_device`)

**a) Mappad.** Äger hela adressrymden: `set_addrmap(AS_PROGRAM,
mem_map)`, `set_addrmap(AS_CPU_SPACE, cpu_space_map)`. Det interna
4KB-fönstret (`FC6000`-`FC6FFF`) installeras dynamiskt av enheten
själv (`mc68302_device::install_internal_window()`) när ROM:et
skriver BAR — inte hand-mappat i drivrutinen.

**b) Delvis kopplat.**
- `set_instruction_execute_callback` / `set_rte_callback` — riktiga
  diagnostikkrokar, ändrar inte beteende.
- `set_pb_input(3, ...)` — Port B pinne 3 (LRCLK) drivs av en riktig
  timer, ett genuint kopplat inläge.
- **IRQ6-vägen är den enda riktiga interrupt-vägen just nu**: `m_duart
  ->irq_cb().set_inputline(m_maincpu, 6)` (se avsnitt 2). Vektorn
  hämtas via `m_maincpu->irq6_ack_vector()`, men den metoden är
  **hårdkodad** (`0x40 | 0x16`) — läser INTE enhetens egna GIMR-
  register. Redan dokumenterad brist, se `mc68302.h`s egen kommentar.
- **IP0-IP3 (externa interruptingångar) drivs aldrig av något.**
  Ingen anropare av `m_duart`s eller någon annan källas motsvarande
  utgång finns kopplad till `m_maincpu->ip0_w()`/`ip1_w()`/`ip2_w()`/
  `ip3_w()` — grep i hela filen ger noll träffar. Redan känt och
  dokumenterat: `PLAN.md` "Steg C" (`panel-ipcr.md`) konstaterar att
  DUART:ens IPCR bit 4 (IP0:s ändringsflagga) permanent läser 0 av
  exakt detta skäl, med två obevisade kandidater för vad som borde
  driva den (panelhändelse, eller diskettmotor/media-status — som
  esq5505.cpp kopplar just den pinnen till).
- Den syntetiska timer-IRQ-injektorn (`ASR10_EXPERIMENT_SYNTH_
  68302_TIMER_IRQ`, rad 108) är `static constexpr bool ... = false` —
  avstängd på kompileringsnivå, ingen körtidspåverkan i normal build.

**c) Inget handskrivet svar för CPU:n själv** — dess eget
adressutrymme ÄR maskinens adressutrymme per definition.

## 2. `m_duart` (`scn2681_device`, SCN2681 DUART)

**a) Mappad via handler.** `duart_panel_asr_candidate_r/w`
(`0xfc4800`-`0xfc481f`) vidarebefordrar registerfilet (ACR,
CTU/CTL-preload, start/stop-räknarkommandon, MR/CR/SR, RHR/THR) rakt
igenom till `m_duart->read(word)`/`m_duart->write(word, data)` — en
riktig enhet, inte en modell.

**b) IRQ kopplad, IP0-3 inte.** `irq_cb().set_inputline(m_maincpu,
6)` är en riktig pin-till-pin-koppling (landad tidigare session,
`docs/asr10/duart-irq6-wiring.md`). Men de fyra externa
inputpinnarna (IP0-IP3) har ingen avsändare någonstans i
drivrutinen — se avsnitt 1b. Kanal B (frontpanelen) är inte kopplad
till någon riktig seriekälla över huvud taget; ingen
`m_duart->rx_b_w()`/motsvarande anrops finns.

**c) Handskrivet svar kvarstår på två ställen inom samma
adressfönster:**
1. `$FC4808` (IPCR): `raw_data = ASR10_DUART_INPUT_CHANGE_STUB` —
   en fast, handskriven konstant i stället för enhetens egna IPCR-
   bitar, eftersom IP0-3 aldrig drivs (se ovan). En experimentflagga
   (`ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84`) kan
   sätta bit 4 vid en specifik PC — ett riktat testinjektion, inte en
   modell.
2. Kanal B SRB/RHRB (`$FC4812`/`$FC4816`) när
   `panel_reply_experiment_enabled()`: byten injiceras direkt i
   `raw_data` från `m_panel_c_srb`/`m_panel_c_rx_byte` i stället för
   att gå via enhetens egen seriemottagningsväg.

## 3. `m_fdc` (`upd72069_device`)

**a) Mappad via handler.** `upd72069_fdc_r/w`
(`0xfc4000`-`0xfc4003`) vidarebefordrar rakt av till
`m_fdc->msr_r()`/`fifo_r()`/`fifo_w()`/`auxcmd_w()`/`tc_w()`/
`set_rate()` — en riktig enhet, programmerad I/O, byte-exakt, redan
etablerat i `disk-read-path.md`.

**b) Inga utgående callbacks kopplade (`intrq`/`drq`) — men det är
inte ett fynd, det är den bevisade, korrekta modellen.** Grep ger noll
träffar för `intrq_cb`/`drq_cb`. `PLAN.md` "Steg B"
(`docs/asr10/fdc-dumpreg.md`) visar redan att den riktiga firmware-
koden pollar statusregistret (`BTST #7,$FC4001`=RQM,
`BTST #6,$FC4001`=DIO) i en spin-loop och aldrig lämnar den vägen för
en interrupt- eller DMA-baserad överföring — `"polled 0 : 1 -> 0"` är
bevisligen den sista FDC-relaterade raden i hela körloggen. Att koppla
`drq_cb` skulle bara vara meningsfullt om firmware använde DMA-stil
överföring, vilket den inte gör.

**c) Inget handskrivet svar** — handlern loggar bara, den ersätter
ingen enhetsfunktion.

## 4. `m_floppy_connector` (`floppy_connector` / `floppy_image_device`)

**a) Inte del av `address_map` direkt** (korrekt — MAME:s konvention:
disketten är barn till FDC:n, adresseras aldrig direkt av CPU:n).
Nås genom `m_floppy_connector->get_device()` följt av riktiga
metodanrop (`ready_r()`, `mon_r()`, `exists()`, `get_cyl()`,
`ss_r()`, `floppy_is_hd()` etc.) — inga fejkade returvärden, all
status kommer från den riktiga enheten.

**b) Ingen callback kopplad UT mot något annat.** Diskettens
statuspinnar (t.ex. en verklig ready-övergång) matar aldrig in i
DUART:ens IP0-3 (se avsnitt 1b/2b) — det är precis den obevisade
kandidat B som `panel-ipcr.md` redan flaggat, med tidsmässig
korrelation men ingen bekräftad koppling.

**c) Inget handskrivet svar** — statusen som returneras är alltid
den riktiga enhetens.

## 5. `m_es5506_host` (`es5506_device`, ES5506/OTTO)

**a) Mappad, men bara bakom en experimentflagga — INTE i
standardbygget.** Både instansieringen (`asr10_boot()`, rad
10059-10082) och adress-installationen (`mem_map()`, rad 2202-2222)
är villkorade på `ASR10_EXPERIMENT_ES5506_HOST`. **Utan flaggan
(dagens default, inklusive alla tidigare körningar i den här
sessionen) instansieras enheten aldrig** (`optional_device`,
`.found() == false`) **och `0xfc2000-0xfc2fff` är rent, omodellerat
`.ram()`.** Det är exakt fyndet i `divisor-zero.md`: `$FC2069`
returnerar `0x0000` eftersom ingen enhet någonsin svarar där.

Mappningen som FINNS bakom flaggan är dock redan korrekt utförd
enligt den bevisade `.umask16(0x00ff)`-konventionen (samma som
`esqkt.cpp`/`macrossp.cpp`/`ssv.cpp` använder för samma krets):
`map(0xfc2000, 0xfc207f).rw(m_es5506_host, FUNC(es5506_device::read),
FUNC(es5506_device::write)).umask16(0x00ff);` — se punkt 2 nedan för
om denna ska göras ovillkorlig.

**b) Ingen callback kopplad ens när flaggan är på.** `irq_cb()`
lämnas obunden helt. `read_port_cb()` binds ENDAST under en
ytterligare, snävare kombination (`ASR10_EXPERIMENT_PAR_DIAGNOSTIC` +
`ASR10_DIAG_PAR_VALUE`) och binder då till
`es5506_host_read_par_diag()` — en uttryckligen märkt engångs-
diagnostik ("NOT an analog model"), inte en modell av någon riktig
inpinne. Ingen ljudutgång konsumeras (`-sound none` i alla körningar
denna session).

**c) När flaggan är av (default): ja, plain `.ram()` svarar för hela
fönstret** — det handskrivna "svaret" är helt enkelt frånvaron av
någon modell, vilket är precis vad uppgiftens LÄGE beskriver.

## 6. `m_es5510_host` (`es5510_device`, ESP/ES5510 DSP)

**a) Mappad, men bara bakom en egen experimentflagga
(`ASR10_EXPERIMENT_ES5510_HOST`) — samma mönster som ES5506, men mer
komplett genomförd.** När flaggan är på routas `FC3000-303F` samt
fyra enskilda register (`FC3101`/`FC3141`/`FC3181`/`FC31C1`, motsvarar
host-offset `0x80`/`0xa0`/`0xc0`/`0xe0`) genom fasta wrapper-handlers
till `host_r()`/`host_w()`, eftersom MAME ger `offset=0` för
enordsfönster och den absoluta host-offseten annars går förlorad.
Resten av `FC3000-31FF`-fönstret förblir `.ram()` — en medvetet
avgränsad, redan bevisad delmängd (`filesystem-browser-map.md`
4.22-4.28), inte en gissning.

**b) Inga callbacks behövs eller är kopplade** — ES5510 har ingen
interruptutgång relevant här; `set_disable()` håller enheten utanför
schemaläggarens exekveringslista (`host_r`/`host_w` är ren register-/
latch-/GPR-manipulation, ingen `execute_run()`-väg), vilket är
korrekt och redan dokumenterat i koden.

**c) När flaggan är av (default): plain `.ram()`, samma mönster som
ES5506** — men detta ligger UTANFÖR den här uppgiftens uttryckliga
scope (punkt 2/3 gäller bara ES5506). Ingen ändring görs här.

## Sammanfattning: var är "resten" av de tre tidigare instansierat-
men-inte-inkopplat-fallen?

Två är redan kända och åtgärdade (DUART `irq_cb` →
`duart-irq6-wiring.md`; `irq6_ack_vector()` som ersatte en tidigare
hårdkodning). **Den tredje, tidigare odokumenterade luckan den här
inventeringen hittar är ES5506 (avsnitt 5)** — enheten existerar i
`machine_config`-koden men är, i varje körning som någonsin gjorts
den här sessionen, aldrig faktiskt instansierad, eftersom
instansieringen SJÄLV (inte bara mappningen) sitter bakom en
avstängd flagga. Det är strängare än "instansierad men inte
inkopplad" — det är "kodad men aldrig instansierad" i standardbygget.

**Ytterligare en, mindre lucka noteras men lämnas orörd i den här
uppgiften:** DUART:ens IP0-IP3 (avsnitt 1b/2b) är en riktig enhets
riktiga pinnar, permanent flytande vid 0, med två obevisade
kandidatkällor redan dokumenterade i `PLAN.md`. Utanför scope för
punkt 2/3 nedan (som gäller ES5506), men värd att åtgärda i en
framtida uppgift.

## Nästa steg (denna uppgifts punkt 2-3) — utfall

Se `docs/asr10/es5506-hostport.md`: mappningen testades ovillkorlig
(rev `ASR10_EXPERIMENT_ES5506_HOST`-grinden borttagen för både
instansiering och adress-installation), regressionstestad i båda
körlägena, och **reverterad i sin helhet** — adressmatematiken
stämmer exakt (`$FC2069` → PAR, precis som `es5506-chain-verification.md`
förutsade), men `ERROR 130` kvarstår identiskt (PAR:s riktiga
viloläge är `0` utan en bunden `read_port_cb`), och enheten drar
igång sin egen interna sampelmotor så fort en riktig röst avstannas
(`CONTROL_STOPMASK` rensas av firmware), vilket spammar loggen med
~1,9 miljoner "unmapped bank0"-rader och sänker simuleringshastigheten
~30%. Se den filen för fullständiga siffror och beslutet.
