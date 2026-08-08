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

The BSD number is intentionally not recorded because it is unstable.

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
| Reason | macOS does not expose the required documented SCSITaskDeviceInterface for this direct-access device, so exclusive UFI commands cannot be issued under the milestone constraints |

The live `list` result confirms the supplied 1,440-block baseline. A supported
or unsupported 1581 result cannot be obtained through the documented
SCSITaskLib path on this macOS driver stack. Detaching or replacing the kernel
storage driver is outside this milestone and is not attempted. Do not publish
a transient disk number, username, or absolute output path in a shared
hardware report.

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
