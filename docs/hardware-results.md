# Hardware results

This document separates the known baseline from results produced by the
current probe.

## Controller and host

| Field | Value |
|---|---|
| USB service/manufacturer | TEACV0.0 |
| Device/media product | TEAC USB UF000x |
| USB VID | `0x0644` (1604) |
| USB PID | `0x0000` (0) |
| Product revision / supplied firmware identifier | `0x0200` |
| Serial number | none |
| Host architecture | Apple Silicon |
| macOS during this milestone build | 26.5.2 (build 25F84) |

## Known baseline

With a MEGA65-formatted DD disk inserted, macOS has previously exposed an
unmounted whole raw device with:

| Field | Value |
|---|---:|
| Reported blocks | 1,440 |
| Block size | 512 bytes |
| Reported bytes | 737,280 |
| Required 1581 blocks | 1,600 |
| Required 1581 bytes | 819,200 |

The BSD number is not a stable hardware identifier and must never be
hardcoded. The live observation below records it only to make that one test
reproducible.

## Live probe result — 2026-08-08

| Field | Result |
|---|---|
| Live controller/media visible outside the build sandbox | Yes, dynamically discovered as `disk4` |
| USB identity | `0x0644:0x0000`, TEACV0.0, `bcdDevice` `0x0200` |
| Disk Arbitration checks | external, removable, whole, unmounted |
| Media capacity | 737,280 bytes, 512-byte blocks (1,440 blocks) |
| Raw-node permission | denied for `/dev/rdisk4` with `errno` 13 |
| `inspect` hardware result | Permission denied (exit 3); no UFI commands issued |
| `inspect` with manual `sudo` | Transport/API failure (exit 4) before INQUIRY: `IOCreatePlugInInterfaceForService` returned `kIOReturnUnsupported` (`0xe00002c7`) |
| Apple API finding | The direct-access peripheral (`Peripheral Device Type` 0) has an in-kernel block-storage driver and no SCSITask plug-in/user-client properties; the documented interface cannot be created |
| Subsequent USB state | The device briefly became unregistered/unmatched, then returned with its SCSI and `IOMedia` nodes |
| `test-1581` hardware result | **inconclusive — not performed** |
| Reason at time of test | macOS did not expose the required documented SCSITaskDeviceInterface for this direct-access device, so that backend could not issue exclusive UFI commands |

The live `list` result confirms the supplied 1,440-block baseline. A supported
or unsupported 1581 result cannot be obtained through the documented
SCSITaskLib path on this macOS driver stack. Do not publish a transient disk
number, username, or absolute output path in a shared hardware report.

## Direct USB CBI follow-up

The next probe backend uses the documented IOUSBLib interface for the drive's
USB mass-storage class `08`, UFI subclass `04`, CBI protocol `00` interface.
It retains the interface associated with the selected BSD medium, then uses
`USBInterfaceOpenSeize` to temporarily detach the kernel block driver. UFI
command blocks, bulk sector data, two-byte CBI completion status, and REQUEST
SENSE therefore travel directly to the controller instead of through the
1,440-sector `/dev/rdiskN` view. On close, the kernel storage stack is allowed
to match again and may assign a different BSD number.

| Direct CBI field | Result |
|---|---|
| Implementation status | Platform-neutral CBI engine complete and covered by mock/unit tests; macOS adapter compiled and live-tested through the seize boundary |
| Interface discovery | Exact selected ancestry found: class `08`, subclass `04`, protocol `00`, three endpoints advertised |
| Documented IOUSBLib plug-in | Created successfully; `IOUSBInterfaceInterface190` queried successfully |
| Non-privileged interface seize | Refused with `kIOReturnExclusiveAccess` (`0xe00002c5`); no UFI command sent |
| Manual `sudo` interface seize | Refused with the same `kIOReturnExclusiveAccess`; no UFI command sent |
| Direct `inspect` | **inconclusive — Apple mass-storage driver retained exclusive ownership** |
| Flexible Disk current/changeable pages | **pending** |
| Controller acceptance of 10 sectors/head | **pending** |
| LBA 9 and LBA 1599 reads | **pending** |
| Two matching 1,600-sector reads | **pending** |
| D81 output | **pending; no image has been claimed or created** |
| Final status | **inconclusive — direct CBI is implemented, but this macOS driver stack will not yield the interface** |

The direct path is still sector-level rather than flux-level; the USB bridge
continues to decode the magnetic recording. Raw-node readability is not a
gate for this path. A manually chosen `sudo` run was attempted and did not
change the exclusive-access result. The program never elevates itself.

The next documented macOS experiment is whole-device capture through
`IOUSBHost`. That route terminates the kernel clients for the complete USB
device and resets/re-registers the device during cleanup, rather than asking
the existing mass-storage client to yield only one interface. It requires a
small Objective-C bridge plus the Foundation and IOUSBHost frameworks, which
are outside this milestone's original C-only/framework constraint and are not
implemented without explicit project approval.

The only permitted data-out operation remains MODE SELECT (10), constrained
to the saved/restored or requested Flexible Disk page. It changes volatile
controller geometry only. No floppy-media write opcode is present in the
executable allowlist. A D81 can be created only after the firmware accepts ten
sectors per head and two complete 819,200-byte reads match; until that happens,
the hardware result must remain pending rather than supported.

## MEGA65 internal-drive reference

Photos supplied after the USB-controller test add context about the drive used
inside a MEGA65. The photographed label identifies an **Alps Electric
DF354N164G**, made in Malaysia; its serial number is intentionally omitted
from this public hardware record. The accompanying spare-part listing
describes a used 3.5-inch drive with 720 KB and 1.44 MB support, a 34-pin
ribbon connection, and a four-pin power connection.

The supplied MEGA65 manual page says the internal controller expects a Double
Density disk. It also says an HD disk can be made to present as DD by covering
both sides of its HD-detection hole, while noting that the drive hardware can
read HD media. These internal-drive facts do not change the tested USB bridge
identity or macOS's 737,280-byte report.

The supplied layout notes describe 80 logical tracks with 40 × 256-byte
sectors per track, backed by 20 × 512-byte physical sectors split ten per
head. That mapping and the supplied interleave policy are now encoded in the
platform-independent layout backend and documented in
[disk-layout.md](disk-layout.md). Hardware confirmation remains pending the
macOS transport limitation above.
