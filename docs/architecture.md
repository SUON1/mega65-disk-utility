# Architecture and safety boundary

`m65floppy-probe` keeps platform access behind typed interfaces so byte-level
logic can be tested without hardware.

| Area | Files | Responsibility |
|---|---|---|
| CLI | `src/cli.c`, `src/main.c` | Strict option parsing, device selection, exit-code mapping, exclusive output-file creation |
| UFI | `src/ufi.c` | CDB construction, allowlist enforcement, bounds-checked response parsing, Flexible Disk page handling |
| 1581 layout | `src/layout.c` | Pure logical-sector/LBA translation, bounded sector extraction, and interleave policy |
| Workflow | `src/probe.c` | Exclusive-access lifetime, inspect sequence, two-read 1581 sequence, restoration and interruption cleanup |
| Transport | `include/m65/transport.h` | Backend-neutral typed command/result interface |
| USB CBI engine | `src/cbi.c` | Backend-neutral CBI phases, 12-byte UFI command blocks, status/sense handling, and transport-policy enforcement |
| macOS USB I/O | `src/usb_cbi_macos.c` | Documented IOUSBLib interface discovery, `USBInterfaceOpenSeize`, control/bulk/interrupt transfers, and cleanup |
| Legacy macOS SCSI path | `src/scsi_macos.c` | SCSITaskDeviceInterface implementation retained for platforms that publish that documented user client |
| Discovery | `src/discovery_macos.c` | I/O Registry USB identity plus Disk Arbitration media checks |
| Output | `src/json.c`, `src/output.c` | Dependency-free JSON writer and stable reports |
| Tests | `tests/` | Fake transport, parsers, CDBs, policy checks, cleanup and result matrix |

The CLI never handles raw SCSI response bytes. The transport returns a typed
command result, UFI parsers produce typed structures, and report structures
cross the workflow/output boundary.

The layout module is platform-independent and has no transport or filesystem
dependency. Its 1-based logical tracks and 0-based physical addresses make
the convention change explicit. A sequential 1,600-block physical read and a
canonical 3,200-sector D81 image have identical byte order; the module proves
and tests the mapping rather than relying on that fact implicitly.

## Two different meanings of "raw"

The macOS `/dev/rdiskN` node is a raw *block-device* path: it bypasses the
filesystem cache, but it still exposes only the geometry selected by the USB
bridge firmware and the kernel storage driver. For the known disk that is
1,440 × 512 bytes. It does not provide the 160 additional sectors required by
a D81 image.

The direct backend operates one layer lower, at the documented USB
Control/Bulk/Interrupt (CBI) transport used by the drive's UFI interface. A
12-byte command block is sent with the ADSC class request, sector data crosses
the bulk endpoint, and command completion arrives on the two-byte interrupt
endpoint. REQUEST SENSE provides the detailed sense key, ASC, and ASCQ used by
typed reports. This is sector-level access: the USB controller still performs
the magnetic encoding/decoding, so neither flux nor arbitrary raw tracks are
available.

The workflow is therefore:

1. discover the BSD medium and verify it with Disk Arbitration;
2. retain its matching USB UFI/CBI interface;
3. seize that interface from the kernel block driver;
4. issue only allowlisted UFI operations;
5. restore saved volatile mode parameters when they were changed; and
6. close the interface so the normal storage driver can match again.

The BSD node may disappear during step 3 and may return under a different disk
number after step 6. Raw-node readability is reported by `list`, but it is not
an authorization prerequisite for direct USB access. macOS may separately
deny the interface seize. A non-privileged denial is reported as permission
denied; if the same exclusive-access denial occurs as root, it is reported as
a transport limitation. The program never invokes `sudo` itself.

The legacy IOUSBLib adapter is serialized and thread-affine because its
interrupt completion source belongs to the run loop that acquired the USB
interface. Acquire, command execution, restoration, release, and destruction
must occur on that same thread. A future GUI must place one transport on one
dedicated backend worker rather than call it concurrently from UI threads.

## MODE SELECT containment

The only permitted data-out path is MODE SELECT (10). Before the backend can
execute it, validation requires:

1. opcode `0x55`;
2. a 10-byte CDB with PF set;
3. an exact 40-byte transfer matching the CDB parameter length;
4. an eight-byte header with zero block-descriptor length; and
5. exactly one 32-byte, non-subpage Flexible Disk page `0x05`.

The desired payload starts as a complete copy of the saved Flexible Disk
page; only transfer rate, heads, sectors per track, bytes per sector,
cylinders, and rotation rate are changed. Restoration uses the complete saved
page. No arbitrary data-out API is exposed to the CLI.

Every other allowlisted opcode is data-in or has no data-out phase, and none
writes floppy media. REQUEST SENSE can consume controller sense state, but it
does not alter media. There is no executable FORMAT UNIT or media WRITE path.
A successful `test-1581` therefore means only that the bridge accepted
volatile 10-sector geometry and that two full reads agreed; it does not modify
the floppy.

## CBI timeout recovery

A timeout, malformed transport phase, or low-level I/O error marks the CBI
engine as requiring recovery. The next otherwise-valid UFI command cannot run
until the engine completes the CBI 1.1 Command Block Reset sequence. That
sequence is hardcoded as the twelve-byte non-data block
`1D 04 FF FF FF FF FF FF FF FF FF FF`, consumes its completion interrupt,
and clears the halt and data toggle on both bulk endpoints. It is not exposed
as a CLI/UFI command and cannot accept caller data.

The macOS adapter arms exactly one such reset block while desynchronized and
rejects every other ADSC. Only a successful reset plus both endpoint clears
restores normal command eligibility. This permits probe cleanup to issue the
saved Flexible Disk page after a read or MODE SELECT timeout. If recovery or
that restoration fails, the result remains inconclusive and reports the
controller state as unknown.

## Cleanup ownership

The macOS transports own and release their I/O Registry objects, plug-in
interfaces, USB/SCSI interfaces, asynchronous event source, run-loop
reference, task objects where applicable, and heap context. Probe workflows
own the exclusive-access lifetime. Every post-acquisition branch converges on
restoration where necessary and then release. Transport destruction also
makes a best-effort release as a final guard.

The Debug preset enables ASan and UBSan. Project warnings are errors; Apple
SDK headers retain their system-header treatment and are not made project
warning sources.
