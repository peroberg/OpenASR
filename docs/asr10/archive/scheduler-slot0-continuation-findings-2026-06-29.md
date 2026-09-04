# ASR-10 scheduler / slot0 continuation findings

## Current blocker

Trap #9 at f8f2ec/f88138 enqueues node 0014f4 to slot0, but the node is not executed.

Baseline:
- $00d8 = 0023d4 = slot0
- slot0 +02/+03 = 0202
- f8f2e6 writes node+2 = 89a2
- f8f2ec loads A1=$00d8
- trap #9 enqueues A5=0014f4 to slot0 +10/+12
- trap #9 uses bit7, so slot0 bit1-lane remains equalized
- after trap #9, $0b6a active slot is slot3, not slot0

## Verified experiments

### Forced slot0 bit1 arm

Forced:
- slot0 +02/+03: 0202 -> 0002

Result:
- dispatcher selected slot0
- RTE went to f87f66
- node 0014f4 was not consumed
- ff89a2 was not reached

Conclusion:
- bit1 arm is necessary to dispatch slot0, but not sufficient to consume the node.

### Forced direct-active node

Forced:
- slot0 +0c = 14f4
- slot0 +10/+12 = 0000
- slot0 +02 = 0002

Result:
- dispatcher selected slot0
- f87fb4 read slot0 +0c = 14f4
- f87fb8 cleared slot0 +0c
- RTE still went to f87f66
- node+2=89a2 was not read by firmware
- ff89a2 was not reached

Conclusion:
- slot +0c is dispatcher context loaded into D5, not a direct PC.
- The real consumer is after f87f66 restore and f87f74 jmp (A1).

## Important routines

### Dispatcher

f87fb4:
- reads slot +0c into D5
- clears +0c
- does not read slot +10 directly

### Context restore

f87f66..f87f74:
- restores context from USP
- restores A1
- jumps through A1

Important next question:
- What is restored A1 at f87f74?
- Is D5=14f4 preserved into the target?
- Where does node 0014f4 or node+2=89a2 first get read?

### Trap #9

f88138..f88172:
- enqueues A5 node to target slot A1
- has a direct-active path to +0c only for bit7 semantics
- otherwise appends to +10/+12
- hardcodes D0=7

### Trap #6 / promotion

f880d6..f88104:
- uses $0b6a active slot
- promotes active slot +10 -> +0c
- after trap #9, $0b6a is slot3, not slot0

## Next prompt when Codex quota resets

Continue on branch asr10-codex-resume.

Goal:
Find the real consumer of dispatcher D5=14f4 after slot0 dispatch resumes through f87f66/f87f74.

Diagnostics only. Do not change emulator behavior. Do not commit.

Known:
- trap #9 enqueues node 0014f4 with node+2=89a2.
- A diagnostic experiment set slot0 +0c=14f4 and made slot0 pending.
- Dispatcher read slot0 +0c=14f4 at f87fb4 and cleared it at f87fb8.
- RTE frame PC was f87f66.
- f87f66 restores context from USP and then:
  f87f74 jmp (A1)
- Execution did not reach ff89a2 directly.
- Therefore D5=14f4 is likely a context/message pointer consumed after f87f66 restores registers and jumps through A1.

Tasks:
1. In a temporary diagnostic-only run, after the same direct-active experiment:
    - trace f87f66..f87f74
    - log restored A1 at f87f74
    - log D5 after restore
    - log A5 after restore
    - log USP before/after restore if cheap
2. Trace the first 50-100 executed instructions after f87f74 jmp (A1), only for this slot0 dispatch.
3. Stop when any of these happens:
    - D5 is read/used as an address
    - A5 is assigned from D5
    - memory 0014f4 or 0014f6 is read
    - node+2=89a2 is read
    - execution reaches ff89a2
    - execution returns to dispatcher/idle
4. Disassemble the target reached by f87f74 jmp (A1).
5. Search static ROM for instructions using D5 as a node pointer near that target:
    - movea.w D5,A5/A0/A1
    - move.w 2(A5),...
    - move.w 2(A0),...
    - jmp/call through value loaded from node+2
6. Remove all temporary diagnostics and verify build passes.

End with:
- What is restored A1 at f87f74?
- What code runs after jmp (A1)?
- Is D5=14f4 preserved into that code?
- Where is node 0014f4 or node+2=89a2 first read?
- Why does ff89a2 not execute yet?
