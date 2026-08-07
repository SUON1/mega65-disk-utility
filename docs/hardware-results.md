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

## Probe result for this build session

| Field | Result |
|---|---|
| Live controller/media visible during build | No |
| Disk Arbitration available inside the build runner | No; session creation was denied/unavailable |
| `inspect` hardware result | Not performed |
| `test-1581` hardware result | **inconclusive — not performed** |
| Reason | No matching external floppy media was visible, and no live temporary-controller-change acknowledgement was given |

The supplied 1,440-block baseline is not presented as a new live probe result.
Run `list`, `inspect`, and then the explicitly acknowledged `test-1581`
locally to replace the inconclusive entry with supported or unsupported
evidence. Do not record a disk number, serial number, username, or absolute
output path in a public hardware report.
