## ASR-10 OS-image backing hypothesis: `ff0a00 + OS offset`

### Critical preservation note

Do not confuse CPU address `ff0ae2` with OS-file offset `0x0ae2`.

The current evidence points to a different relation:

```text
CPU address = ff0a00 + OS offset
```

### Evidence

Runtime state:

```text
ff8258 = 8140
ff825a = ffff
```

Both V161 and V350 OS images contain:

```text
OS offset 0x7858 = 8140 ffff
```

Therefore:

```text
OS offset 0x7858 -> CPU ff8258
```

This implies:

```text
OS offset 0x0000 -> CPU ff0a00
OS offset 0x7740 -> CPU ff8140
OS offset 0x7858 -> CPU ff8258
```

### Posted node calculation

Observed posted node:

```text
node = 14f4/89a2
```

Runtime signed-index calculation:

```text
ff8140 + signed(89a2) = ff0ae2
```

Using the OS-image relation:

```text
OS offset 0x7740 + signed(89a2) = OS offset 0x00e2
```

### OS data at the corresponding offset

In both V161 and V350 OS images:

```text
OS offset 0x00e2:
ff f8 89 68 ff f8 85 54 27 00 ...
```

This is nonzero pointer/vector-like data, including ROM-looking addresses:

```text
fff88968
fff88554
```

Current MAME, however, observes:

```text
CPU ff0ae2 = 0000
```

### Negative checks

The exact byte pattern at OS offset `0x00e2` appears only once in the OS image.

The naive OS offset `0x0ae2` is not the relevant location:

```text
OS offset 0x0ae2 = 0000 0001 0000 0000 ...
```

Therefore, the useful mapping is not:

```text
CPU ff0ae2 -> OS offset 0x0ae2
```

but rather:

```text
CPU ff0ae2 -> OS offset 0x00e2
```

through the inferred base:

```text
CPU ff0a00 = OS offset 0x0000
```

### Current hypothesis

Firmware appears to expect an OS-image-backed logical window beginning at:

```text
CPU ff0a00
```

Current MAME has loaded/backed the high part:

```text
OS offset 0x7600 -> CPU ff8000
OS offset 0x7858 -> CPU ff8258
```

but the lower logical window:

```text
OS offset 0x0000..0x75ff -> CPU ff0a00..ff7fff
```

is not populated/backed in current execution.

This explains why `$8258=8140` is correct, the signed lookup to `ff0ae2` is coherent, but `ff0ae2` is zero in current MAME.
