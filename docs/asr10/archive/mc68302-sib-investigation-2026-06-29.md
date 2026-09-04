# ASR-10 MC68302 / SIB Investigation Notes

## Purpose
Track evidence and hypotheses around the ASR-10 $FC68xx register block.

## Confidence policy
- verified: observed runtime behavior + supported by reference
- likely: observed structured runtime behavior, reference match not complete
- speculative: plausible but not proven
- rejected: disproven by trace

## Observed structured FC68xx writes

### Chip-select candidates
...

### Port A/B candidates
...

### Interrupt candidates
...

### Timer/control candidates
...

### Unknown/control candidates
...

## Current interpretation
MC68302/SIB is likely involved in configuration, port/status, interrupt and timer behavior.

## Not yet proven
- DMA/IDMA transfer
- FDC data movement
- scheduler wakeup source
- exact vector ownership

## Key risk
Do not treat ASR-10 as plain 68000. $FC68xx is active system state.

## Next experiments
1. Compare FC68xx offsets against MC68302 manual.
2. Trace FDC command -> FC68xx writes -> IACK/vector.
3. Determine CPU FIFO vs MC68302 DMA ownership.