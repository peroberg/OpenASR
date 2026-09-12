# CDR-04 9FT-BALDWIN: controlled 16 MiB experiment

Date: 2026-09-12.  This is an uncommitted harness experiment, not an
expanded-memory implementation.  The temporary harness made the existing
16 MiB CPU address range distinguishable while retaining the two existing
1 MiB low/sample backing regions.

## Result

[VERIFIED — firmware] V3.50 naturally selected base `$00000000`, size
`$00f80000` when its ordinary RAM probe saw independent storage.  It was not
given a forced allocator size.  After ordinary firmware reservations, the
allocator reported base `$0002a000`, size `$00f47000`, and free/largest-free
`$00f45400`.

[OBSERVED — experiment] V3.50 loaded CDR-04 `9FT-BALDWIN` successfully into
one contiguous `$380800`-byte allocation at `$02b600..$3abe00`.  Its 26
terminal PCM owners span emulator byte-address MBs 0--3.  No compaction or
relocation was observed.

[VERIFIED — emulator] A synthetic MIDI note `$36` selected WaveSample/owner
42 in MB2.  Firmware programmed that voice's CS1 entries as `[2,3,4,5]`.
At logical ES5506 word offset `$01171b`, the current model fetched `$18e4`
from byte address `$222e36`:

```
physical = (CS1[(logical >> 19) & 3] << 20) | ((logical & $7ffff) << 1)
         = (2 << 20) | ($1171b << 1)
         = $222e36
```

The ES5506 read and CPU backing read agreed for the captured words.  The
first 32 bytes at `$222e36` also matched the corresponding source bytes in
the mounted CDR-04 data.

[OPEN] This demonstrates a functional emulator arrangement, not ASR-10
physical RAM topology, expansion-board wiring, or ownership of CS1 logic.

## Clean 2 MiB control and firmware probe

With the unmodified `./mame` model, V3.50's ready allocator state was base
`$0062a000`, size `$001c7000`, total/largest free `$001c5400` (1,856,512
bytes).  Selecting 9FT-BALDWIN left all four values unchanged and produced
the raw display message `5ELECT IN5T T0 DELETE`: the `$380800` request cannot
fit the sole `$1c5400` free extent.

The executed V3.50 routine at ROM `$f8a166..$f8a244` writes zero, `$1111`,
`$2222`, and `$3333` at `$008000`, `$408000`, `$808000`, and `$c08000`, then
uses the values read at `$008000` and `$808000` to choose configuration.  In
the stock two-MiB modulo model `$008000` reads `$3333`; in the experiment it
read zero and `$808000` read `$2222`.  That is the routine's `$00000000` /
`$00f80000` branch.  Thus the size result follows the firmware probe rather
than a patched stored counter.

The explicit storage control wrote `$1357` at `$230000` and `$2468` at
`$430000`.  In stock 2 MiB backing they collide; in the 16 MiB harness they
read back independently.  This is a positive witness that MB2 and MB4 did
not silently retain the former modulo-2-MiB alias.

## 9FT-BALDWIN allocation

The allocation header at `$02b600` reports `$380800`, leaving a free block
`$3abe00..$f70a00` of `$bc4c00`.  `$f45400 - $bc4c00 = $380800`.

There are 101 WaveSample headers and 26 terminal PCM owners.  Their owner
block addresses/sizes in the loaded object are:

```
owner   address  size    MB       owner   address  size    MB
27      02bdd0   034d70  0        40      1e33b0   01c360  1
28      060b40   022460  0        41      1ff710   01d7d0  1
29      082fa0   01f950  0        42      21cee0   01f6e0  2
30      0a28f0   01f940  0        43      23c5c0   01f120  2
31      0c2230   01cf70  0        44      25b6e0   021970  2
32      0df1a0   021b60  0        45      27d050   01d030  2
33      100d00   022cf0  1        46      29a080   01d0d0  2
34      1239f0   01fb50  1        47      2b7150   01d2a0  2
35      143540   01c8d0  1        48      2d43f0   01c020  2
36      15fe10   01cb00  1        49      2f0410   019030  2
37      17c910   023670  1        50      309440   01d340  3
38      19ff80   027380  1        51      326780   0507b0  3
39      1c7300   01c0b0  1        52      376f30   02ecd0  3
```

MB here means the temporary emulator's CPU byte-address megabyte, not a
claim about physical ASR board banks.  The largest owner remains `$507b0`
(329,648 bytes), so this is a large Instrument of bounded WaveSamples, not
an individual >4 MiB WaveSample fixture.

## Source-to-fetch witness

`chdman extractraw` produced a 2,448-byte-sector stream from `CDR-04.chd`.
Every 2,352-byte raw frame matched the supplied CDR-04 raw track
`bs-laaavo.bin` (89,912 sectors, SHA-1
`fafe55a8b9a63ce5a0503059711c4e9eaae2412e`).  Two 32-byte owner-start
slices and the 32-byte slice at the active fetch address matched that raw
CD user-data stream after the firmware load.  The active fetch sequence
began:

```
logical $01171b -> physical $222e36 -> ES5506/CPU $18e4
bytes: 18 e4 16 3e 13 36 0f fd 0c 62 08 2a 03 73 fe 73 ...
```

The deliberately unshifted alternative address `$21171a` read `$0017`,
ruling it out for this measured fetch.

## Consequence and frontier

[INFERRED] A production model must first preserve distinct storage for every
firmware-addressable byte/MB that it advertises; only then can its allocator,
CS1 table and ES5506 path be evaluated as expanded memory.  The minimal
candidate for later review is therefore a configuration-aware backing-store
model that replaces modulo-`$200000` collapse consistently for CPU writes
and ES5506 translated reads.  This experiment does not establish which
physical ASR memory configurations are linear, where their holes are, or
whether `$f80000` is the final correct model for a particular hardware
expansion.  No production change is made here.

## Discrepancy

The task premise described the top-level 9FT-BALDWIN allocation as
`$380000`.  The CDR file size, loaded allocator header, and free-space delta
all independently establish `$380800`; `$380000` is stale/incorrect for this
fixture.

## Production implementation outcome (2026-09-12)

[VERIFIED — emulator] The experiment's backing-store requirement is now the
production `asr10_boot.cpp` model: one save-stated, distinct 16 MiB backing
for the CPU sample windows and CS1/ES5506 fetch callback. The former
modulo-`$200000` collapse is removed; low-memory ROM-overlay and `$0CE3`
write semantics are retained.

[VERIFIED — acceptance] The ordinary `./mame` V3.50/CDR-04 run again chose
`$00000000/$00f80000`, loaded 9FT-BALDWIN at `$02b600` with size `$380800`,
and reproduced the MB2 witness `$01171b -> $222e36 -> $18e4` with CS1
`[2,3,4,5]`. A save/load buffer round trip restored data above 2 MiB, that
voice's CS1 entry, and a subsequent matching ES5506 fetch; the following
250 ms active-channel PCM windows were bit-identical. This implementation
outcome does not change the physical-topology boundary stated above.
