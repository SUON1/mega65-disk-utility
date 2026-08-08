# 1581 logical-to-physical layout

This document records the read-only translation layer implemented in
`include/m65/layout.h`. It is geometry and byte-address translation only; it
does not parse a D81 filesystem, allocate files, format media, or write a
floppy.

## Address spaces

The same 819,200 bytes have two useful views:

| View | Geometry |
|---|---|
| Commodore logical | 80 tracks, 40 sectors per track, 256 bytes per sector |
| Controller physical | 80 cylinders, 2 heads, 10 sectors per head, 512 bytes per sector |

Commodore tracks are numbered 1–80. Logical sectors are numbered 0–39. The
controller address uses zero-based cylinders, heads, physical-sector indexes,
and LBAs.

Each pair of adjacent logical sectors is one physical sector. For logical
track `T` and sector `S`:

```text
cylinder       = T - 1
combinedSector = S / 2                 (0..19)
head           = combinedSector / 10  (0..1)
sectorIndex    = combinedSector % 10  (0..9)
LBA            = cylinder * 20 + combinedSector
byteOffset     = (S % 2) * 256         (0 or 256)
```

Examples:

| Logical address | Physical address |
|---|---|
| Track 1, sector 0 | cylinder 0, head 0, sector index 0, LBA 0, byte offset 0 |
| Track 1, sector 1 | cylinder 0, head 0, sector index 0, LBA 0, byte offset 256 |
| Track 1, sector 18 | cylinder 0, head 0, sector index 9, LBA 9, byte offset 0 |
| Track 1, sector 20 | cylinder 0, head 1, sector index 0, LBA 10, byte offset 0 |
| Track 80, sector 39 | cylinder 79, head 1, sector index 9, LBA 1599, byte offset 256 |

This mapping has an important consequence: a complete physical LBA read in
ascending order is already in canonical D81 byte order. Splitting each
512-byte block into its lower and upper 256-byte halves does not require a
byte permutation. The translation API still makes this relationship explicit
and bounds-checks individual logical-sector extraction.

## Supplied interleave policy

The supplied performance notes describe a file-allocation/read order that
visits every other physical sector, keeping the two 256-byte logical halves
together:

```text
0, 1, 4, 5, 8, 9, ... 36, 37,
2, 3, 6, 7, 10, 11, ... 38, 39
```

The layout module exposes that exact 40-sector ordering. It also exposes the
two candidate calculations from the supplied Ruby example:

```text
same track: (last + 1 + 2 * (last % 2)) % 40
next track: (last + 8 - (last % 2)) % 40
```

These functions return candidate sector numbers only. A future filesystem
allocator must still check sector availability, reserved sectors, directory
and BAM rules, track bounds, and disk-full conditions. None of that
filesystem behavior is implemented in this milestone.

The interleave is an allocation/read policy, not an alternative address
mapping. A D81 remains a linear track/sector image regardless of the order in
which a file's sector chain was allocated.

## Verification status

Unit tests exhaustively round-trip all 3,200 logical sectors, prove that the
physical and canonical D81 byte offsets are identical, verify boundary and
truncation rejection, and compare all 40 interleave positions byte-for-byte
with the supplied sequence.

The mapping has not yet been confirmed by reading the physical drive because
the tested macOS stack does not publish `SCSITaskDeviceInterface` for this
direct-access device while its kernel block-storage driver is attached.
