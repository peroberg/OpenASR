# Sequencer Event Mask ($0CDE) och Playback Dispatch i ASR-10 V3.50

---

## 1. Sammanfattning och Fastställd Status

* **`[VERIFIED firmware structure]`:** Dispatchern vid `$FFF90324` (`$0538 $0CDE`) exekverar `BTST D2, $0CDE.w` där $D2 = \text{Event Type} - 1$.
* **`[VERIFIED]`:** `$0CDE` är en dynamisk **Event-Type Filter Mask** för sekvensuppspelning.
* **`[VERIFIED runtime]`:** Autentiskt panel-workflow (Bank Load $\to$ Instrument Load) etablerar masken:
  $$\$00 \ (\text{Boot Idle}) \ \longrightarrow \ \$3C \ (\text{Bank Load}) \ \longrightarrow \ \$3F \ (\text{Bank + Instrument Load})$$
* **`[VERIFIED runtime]`:** NOTE-events ($\text{Type } \$01 \implies D2 = 0$) testar bit 0 i `$0CDE`. Med `$0CDE = $3F` (där bit 0 = 1) passerar noten naturligt till röstallokering.
* **`[DISPROVEN]`:** NOTE-uppspelning kräver `$0CDE` bit 5 (`$20`).
* **`[DISPROVEN]`:** MAME saknar initiering i hårdvaran för sequencern.
* **`[DISPROVEN]`:** Opkoden `$0538` var en statisk `BTST #5`.

---

## 2. Den Fullständiga Uppspelningskedjan

Signal- och kontrolldispatch för sekvenshändelser från disk till ES5506:

```
Track Stream Decoder ($FFB180 -> $B380)
      | (allokerar spårnod via TRAP #3)
Track Event Queue (+$1E(A4))
      | (traverseras av Steppern vid $F902D2)
Delta-Time Countdown (subq.w #1, 2(A5) == 0)
      |
Vector 0 Dispatch ($FF9502 -> $FFF90318)
      |
Opcode $0538: BTST D2, $0CDE.w  (D2 = Event Type - 1)
      | (Bit 0 = 1 när instrument är laddat, $0CDE = $3F)
TRAP #3 (os_alloc_event_node -> allokerar utgående nod A5)
      | (kopierar timing & notdata från A3 till A5, sätter flagga $40)
TRAP #C / TRAP #12 (os_scheduler_post till Sound Queue $00DC)
      |
Sound Scheduler Consumer
      |
ES5506 Voice Allocator & Register Writes ($FC2000-$FC207F)
```

---

## 3. Trap-Primitiver i ASR-10 Kärnan

| Vektor | Funktion | Semantik |
| :--- | :--- | :--- |
| **`TRAP #3`** | `os_alloc_event_node()` | Poppar en ledig 8-bytes nod från `$0B6C` och returnerar i `A5`. |
| **`TRAP #4`** | `os_free_event_node(a5)` | Återlämnar nod `A5` till den fria poolen vid `$0B6C`. |
| **`TRAP #9`** | `os_enqueue_node(a1, a5)` | Länkar in nod `A5` i kö / spår `A1`. |
| **`TRAP #C` (`TRAP #12`)** | `os_scheduler_post(a1, a5)` | Postar nod `A5` till aktiv scheduler-kö `A1` (t.ex. `$00DC`). |
