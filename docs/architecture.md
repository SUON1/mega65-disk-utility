# Architecture and safety boundary

`m65floppy-probe` keeps platform access behind typed interfaces so byte-level
logic can be tested without hardware.

| Area | Files | Responsibility |
|---|---|---|
| CLI | `src/cli.c`, `src/main.c` | Strict option parsing, device selection, exit-code mapping, exclusive output-file creation |
| UFI | `src/ufi.c` | CDB construction, allowlist enforcement, bounds-checked response parsing, Flexible Disk page handling |
| Workflow | `src/probe.c` | Exclusive-access lifetime, inspect sequence, two-read 1581 sequence, restoration and interruption cleanup |
| Transport | `include/m65/transport.h` | Backend-neutral typed command/result interface |
| macOS transport | `src/scsi_macos.c` | Documented SCSITaskDeviceInterface and SCSITaskInterface calls |
| Discovery | `src/discovery_macos.c` | I/O Registry USB identity plus Disk Arbitration media checks |
| Output | `src/json.c`, `src/output.c` | Dependency-free JSON writer and stable reports |
| Tests | `tests/` | Fake transport, parsers, CDBs, policy checks, cleanup and result matrix |

The CLI never handles raw SCSI response bytes. The transport returns a typed
command result, UFI parsers produce typed structures, and report structures
cross the workflow/output boundary.

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

## Cleanup ownership

The macOS transport owns and releases its I/O Registry objects, plug-in
interfaces, SCSI task objects, device interface, and heap context. Probe
workflows own the exclusive-access lifetime. Every post-acquisition branch
converges on restoration where necessary and then release. Transport
destruction also makes a best-effort release as a final guard.

The Debug preset enables ASan and UBSan. Project warnings are errors; Apple
SDK headers retain their system-header treatment and are not made project
warning sources.
