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
