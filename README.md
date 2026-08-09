# MEGA65 Disk Utility — hardware feasibility probe

> **Safety milestone:** `m65floppy-probe` does not write, format, partition,
> initialize, mount, eject, or otherwise modify floppy media. There is no GUI
> and no D81 filesystem implementation in this milestone.

This repository contains the first hardware-feasibility milestone for
**MEGA65 Disk Utility**. It is a read-only macOS C17 diagnostic for the TEAC
USB floppy controller used during development.

macOS currently exposes a MEGA65-formatted DD disk through this controller as
1,440 blocks of 512 bytes (737,280 bytes, conventionally called 720 KiB).
A Commodore 1581 / MEGA65 D81 image requires 1,600 blocks:

```
80 cylinders × 2 heads × 10 sectors/track × 512 bytes = 819,200 bytes
```

`/dev/rdiskN` is not a flux-level view of the magnetic medium. It is the
sector window that the USB-floppy firmware and the macOS storage driver chose
to publish; for this disk that window contains only 1,440 sectors. The probe's
direct USB path instead seizes the known UFI/CBI interface and sends documented
sector-level UFI commands without going through that block-device window.
This is the rawest documented access offered by this controller, but it is
still decoded 512-byte sectors, not magnetic transitions or track flux.

The probe determines whether the controller firmware can temporarily accept
the 250 kbit/s, 80-cylinder, 10-sector geometry and read all 1,600 sectors.
If the firmware accepts that geometry, two matching reads can be reconstructed
as a canonical 819,200-byte D81 image. If the bridge firmware exposes only
nine sectors per head, software cannot recover the missing sectors through
the 1,440-sector macOS block view. The probe never sends a floppy-media write
command.

The core library now also contains a read-only 1581 layout translator. It maps
the 80 × 40 Commodore logical sectors of 256 bytes to the controller's
80 × 2 × 10 physical sectors of 512 bytes and back. It includes the supplied
fast-read interleave policy, but deliberately does not implement filesystem
allocation or D81 directory/BAM parsing. See
[disk-layout.md](docs/disk-layout.md).

No open-source license has been selected yet. No license file is included,
and the absence of a license means the usual default copyright restrictions
apply.

## Supported environment

- macOS on Apple Silicon
- Apple Clang with C17 support
- CMake 3.24 or later
- Known controller: VID `0x0644`, PID `0x0000`, TEAC USB UF000x family

Only the C/POSIX runtime supplied by macOS and the CoreFoundation, IOKit
(including the documented IOUSBLib and SCSITaskLib interfaces), and
DiskArbitration frameworks are used. There is no libusb, JSON dependency, or
other third-party runtime dependency.

## Build and test

Terminal, Debug with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Terminal, Release:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

In VS Code:

1. Open this repository folder, not its parent.
2. Install the recommended **C/C++** and **CMake Tools** extensions.
3. Run **CMake: Select Configure Preset** and select `debug` or `release`.
4. Run **CMake: Build**.
5. Run **CMake: Run Tests**.

Hardware tests are excluded by default. `M65_ENABLE_HARDWARE_TESTS=ON` is an
explicit local opt-in and must not be enabled in CI.

## Commands

Build paths below use the Debug preset; substitute `build/release` if needed.
BSD disk numbers are discovered dynamically and must never be assumed to be
`disk4`.

Enumerate matching controllers:

```sh
./build/debug/m65floppy-probe list
./build/debug/m65floppy-probe list --json
```

Inspect one unmounted whole medium through direct USB UFI/CBI. The command is
media-read-only; its automatic REQUEST SENSE operations may consume controller
sense state but never write the floppy:

```sh
./build/debug/m65floppy-probe inspect --device disk4
./build/debug/m65floppy-probe inspect --device disk4 --json
```

Test 1581 read feasibility:

```sh
./build/debug/m65floppy-probe test-1581 \
  --device disk4 \
  --ack-temporary-controller-change
```

Optionally save a new image only after two complete reads match:

```sh
./build/debug/m65floppy-probe test-1581 \
  --device disk4 \
  --ack-temporary-controller-change \
  --output recovered-new-file.d81
```

`test-1581` temporarily changes **volatile controller geometry** with
MODE SELECT (10). It first saves the controller's complete MODE SENSE
response, checks the changeability mask, verifies the accepted values, reads
LBA 9 and LBA 1599, performs two independent full reads, compares them, and
attempts to restore the original Flexible Disk page on every success, failure,
timeout, unplug, or interruption path. If an unplug or transport failure makes
restoration impossible, the result is inconclusive and says so. It refuses to
start without the acknowledgement flag.

If a timeout loses CBI phase synchronization, the backend first performs the
fixed non-data Command Block Reset defined by the CBI specification, clears
both bulk endpoint stalls/toggles, and only then retries restoration. That
internal controller reset has no caller-supplied bytes and is not a floppy
media command.

The command succeeds and permits D81 output only if the USB bridge firmware
accepts and reads ten sectors per head. It cannot infer or synthesize the 160
sectors omitted from a 720 KiB block-device view.

An output file is created with exclusive creation and is never overwritten.
The program rejects `/dev/disk*` and `/dev/rdisk*` output paths.

## Device and permission checks

Disk Arbitration must confirm that selected media is external, removable,
whole, and unmounted. A mounted, internal, non-removable, partition/slice, or
non-512-byte device is rejected before SCSI access.

Raw disk nodes are normally owned by `root:operator`, but raw-node readability
is informational and does not gate the direct USB CBI path. Opening the USB
interface exclusively can still be denied by macOS. A privilege denial is
exit code 3; a refusal that persists as root is a transport limitation and
exit code 4. Both include an explanation. The program never invokes `sudo`.
If local policy permits and the exact device has been checked with `list`,
rerun the chosen command manually:

```sh
sudo ./build/debug/m65floppy-probe inspect --device disk4
```

Treat `sudo test-1581` with the same caution as any low-level hardware
diagnostic, even though its executable SCSI allowlist contains no
floppy-media write opcode.

### macOS direct USB transport

On the tested macOS 26.5.2 stack, the TEAC drive is attached as a direct-access
block device with the in-kernel block-storage driver. macOS does not publish
the SCSITask plug-in/user-client properties on that service, so the documented
`SCSITaskDeviceInterface` cannot be created even with `sudo`.

The new backend follows the drive's I/O Registry ancestry to its documented
USB mass-storage interface and uses IOUSBLib. `USBInterfaceOpenSeize`
temporarily detaches the kernel block-storage driver, so `/dev/diskN` may
disappear while a diagnostic owns the interface. Closing the interface on
every exit path allows the kernel driver to match again; the returning BSD
disk number is not guaranteed to be the same. Disk Arbitration safety checks
are completed before the seize. The documented plug-in can be created, but on
the tested host the Apple mass-storage owner refused
`USBInterfaceOpenSeize` both normally and under a manual `sudo` run. No UFI
command was sent. See [hardware-results.md](docs/hardware-results.md).

## Safety design

- Explicit opcode allowlist: TEST UNIT READY, REQUEST SENSE, INQUIRY,
  READ CAPACITY (10), READ FORMAT CAPACITIES, MODE SENSE (10), READ (10), and
  MODE SELECT (10).
- MODE SELECT (10) is the only data-out command. Its validator accepts only
  an eight-byte parameter header followed by exactly one 32-byte Flexible
  Disk page `0x05`.
- FORMAT UNIT, every WRITE variant, WRITE AND VERIFY, and vendor-specific
  data-out commands are rejected before the backend sees them.
- Raw disk nodes are never opened for writing.
- Exclusive USB-interface access is obtained before UFI diagnostics and
  released on every exit path.
- UFI commands are padded to the required 12-byte CBI command block. The
  backend processes the command-completion interrupt and obtains detailed
  sense key/ASC/ASCQ information with REQUEST SENSE.
- Timeout recovery can send only the CBI specification's fixed, non-data
  Command Block Reset; it then clears both bulk endpoints before any saved
  Flexible Disk page is restored. It cannot carry media data or bypass the
  UFI opcode validator.
- Signal-aware cleanup stops further reads but still permits restoration.
- Command builders and response parsers are independent of hardware and are
  exercised with a stateful fake transport.
- Every controller response is length-checked before decoding.

See [architecture.md](docs/architecture.md) for module boundaries,
[disk-layout.md](docs/disk-layout.md) for the logical/physical translation,
and [hardware-results.md](docs/hardware-results.md) for the current hardware
record. A sanitized valid JSON document is in
[example-report.json](docs/example-report.json).

## JSON and exit codes

`--json` writes exactly one JSON document to stdout; diagnostics use stderr.
Every document has `schema_version: 1` and stable `command`, `device`,
`media`, `ufi`, `result`, and `errors` top-level members. VID and PID are
reported in numeric and zero-padded hexadecimal forms.

| Exit | Meaning |
|---:|---|
| 0 | Command completed successfully |
| 2 | No matching device or ambiguous device |
| 3 | Permission denied |
| 4 | Transport or SCSI protocol failure |
| 5 | 1581 geometry unsupported |
| 6 | Repeated complete reads differed |
| 64 | Invalid command-line usage |

An unsupported controller geometry is a diagnostic outcome, not an internal
implementation error.

## Publishing with VS Code

No external repository is created automatically, and GitHub CLI is not
required.

1. In VS Code, open `/Users/Shared/HomeSlice/mega65-disk-utility`.
2. Open **Source Control** and confirm the committed worktree is clean.
3. Select **Publish Branch** (or **Publish to GitHub**) from the Source
   Control menu.
4. Sign in to GitHub when VS Code asks.
5. Enter the repository name `mega65-disk-utility`.
6. Choose **Public repository**.
7. Review the GitHub page before sharing it. Do not add a generated license
   during publication unless a license has deliberately been selected.
