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
| Subsequent USB state | Device remained electrically visible but became unregistered/unmatched; its SCSI and `IOMedia` nodes disappeared before the corrected metadata build could be rechecked |
| `test-1581` hardware result | **inconclusive — not performed** |
| Reason | Exclusive SCSI inspection requires a manual trusted `sudo` run; no live temporary-controller-change acknowledgement was given |

The live `list` result confirms the supplied 1,440-block baseline. Run
`inspect` manually with the required permission and then explicitly authorize
`test-1581` to replace the inconclusive entry with supported or unsupported
evidence. Do not publish a transient disk number, username, or absolute output
path in a shared hardware report.
