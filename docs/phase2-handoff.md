# Phase 2 Handoff — IOUSBHost CBI I/O Adapter + `diagnose`

This document records the implementation decisions, SDK observations, the
resolution of every merge-blocking review finding, and the current test status
for Phase 2. It is the reference for anyone building, verifying, or extending
the IOUSBHost transport before it is merged to `main`.

Branch: `phase2/iousbhost-capture` (from base `ae1d960`).

This revision reworks the branch in response to the `phase2-codex-review.md`
rejection. The earlier prototype exposed a raw `m65_iousbhost_send_ufi` bridge
and layered IOUSBHost capture *in front of* the legacy IOUSBLib vtable. That
approach is gone. IOUSBHost is now a low-level I/O adapter consumed by the
tested `src/cbi.c` engine, exactly as the review's recommended direction
requires.

---

## 1. Architecture

The Objective-C layer is a thin **`M65CbiIo` adapter**. It does not implement
any CBI/UFI protocol policy of its own. It only performs raw USB operations
(control ADSC, bulk-in, bulk-out, interrupt-in, clear-stall, reset prep/finish,
open/close/destroy) against an IOUSBHost-captured interface, and returns actual
transferred byte counts and mapped status codes to `src/cbi.c`.

    m65_iousbhost_transport_create(bsd_name, ...)          [ Objective-C, .m ]
        └─ copy_usb_interface_for_bsd_name(bsd_name)   (identity correlation)
        └─ build M65CbiIo { ops = iousbhost_cbi_ops, context }
        └─ m65_cbi_transport_create(io)                    [ C, src/cbi.c ]
               └─ returns M65Transport whose ops run the tested
                  allowlist / exact-transfer / REQUEST SENSE /
                  completion-vs-sense / command-block-reset engine,
                  driving the raw adapter ops below it.

Because the returned object is a normal `M65Transport` built by
`m65_cbi_transport_create`, **`diagnose`, `inspect`, and `test-1581` all flow
end-to-end over IOUSBHost** through the identical, already-tested command
engine. There is no second code path and no protocol logic duplicated in the
`.m`.

### Files

- `include/m65/usb_cbi_iousbhost_macos.h` — pure-C header. Declares exactly one
  symbol: `m65_iousbhost_transport_create(const char *bsd_name, char *detail,
  size_t detail_size, M65TransportStatus *status)`. No Objective-C types cross
  the boundary; it includes only `m65/transport.h`.
- `src/usb_cbi_iousbhost_macos.m` — Objective-C (ARC) `M65CbiIo` adapter and the
  factory. Contains all IOUSBHost/IOKit code.
- `src/usb_cbi_iousbhost_stubs.c` — non-Apple build stub. Returns `NULL`, sets
  `*status = M65_TRANSPORT_NO_DEVICE`, and clears `detail`, so the portable
  build and the unit-test link resolve the factory symbol without any macOS SDK.

The previous filenames (`transport_iousbhost_bridge.h`, `transport_iousbhost.m`,
`transport_iousbhost_stubs.c`) were renamed to the `usb_cbi_iousbhost_macos*`
names to match the `usb_cbi_macos.c` IOUSBLib sibling and to signal that this is
a CBI I/O adapter, not a standalone transport.

---

## 2. Resolution of the review's merge-blocking findings

### Finding 1 — The macOS target did not build
Fixed. The `.m` no longer depends on `kUSBEndpointDesc` and does not pass legacy
`kIOUSBDeviceRequest*` enumerators into `IOUSBHostDeviceRequestType`:

- USB descriptor parsing uses self-contained packed structs
  (`M65UsbDescHeader`, `M65UsbConfigDesc`, `M65UsbInterfaceDesc`,
  `M65UsbEndpointDesc`) and local `#define`s for the descriptor-type,
  transfer-type, and direction constants. No `USBSpec.h` symbol is required.
- The CBI ADSC control request fills an `IOUSBDeviceRequest` (`bmRequestType`
  `0x21`, `bRequest` `0x00`, `wIndex` = interface number, `wLength` = 12)
  field-by-field from the `M65CbiAdsc` produced by `m65_cbi_build_adsc`; it never
  assigns a legacy enum to an `IOUSBHostDeviceRequestType`, so the
  `-Werror` enum-conversion diagnostics cannot occur.
- `CMakeLists.txt` compiles the `.m` with `-fobjc-arc -fmodules` (so
  `@import Foundation; @import IOUSBHost;` is valid), enables `OBJC`, and links
  `-framework IOUSBHost -framework Foundation`.

NOTE: the `.m` still **cannot be compiled in this Linux environment** (no clang,
no macOS SDK). It is written against the SDK signatures but is **unverified by a
compiler** — see §5.

### Finding 2 — `test-1581` was not transported over IOUSBHost
Fixed. All three IOUSBHost/`__APPLE__` blocks were removed from `src/probe.c`;
`m65_test_1581` is now fully transport-agnostic and drives whatever
`M65Transport` it is given. `src/main.c` `create_selected_transport()` builds the
IOUSBHost-backed transport (via `m65_iousbhost_transport_create` →
`m65_cbi_transport_create`) and passes it to `m65_test_1581`, so MODE SENSE,
MODE SELECT, READ, recovery, and restoration **all run over IOUSBHost**. The old
"capture-then-legacy-vtable" split and its early-return cleanup leak are gone;
`transport->ops->destroy(transport)` runs on every path.

### Finding 3 — `diagnose` did not run the agreed safe sequence
Fixed. `diagnose` calls `m65_inspect`, which issues the full staged, read-only
UFI workflow: **INQUIRY → TEST UNIT READY → READ CAPACITY → READ FORMAT
CAPACITIES → MODE SENSE (current) → MODE SENSE (changeable)**, with automatic
REQUEST SENSE on check-condition. TEST UNIT READY is a no-data command; every
other step is data-in. No data-out is ever issued.

### Finding 4 — The raw bridge bypassed the CBI/UFI safety engine
Fixed by the architecture in §1. There is no `m65_iousbhost_send_ufi` any more.
Every command goes through `src/cbi.c`, so `m65_validate_command` (allowlist),
command-block reset/recovery, exact-transfer accounting, automatic REQUEST
SENSE, and completion-vs-sense validation are all enforced. The adapter returns
**actual** transferred lengths (not requested lengths) and requires **exactly
two** interrupt-status bytes — a short or mismatched completion is surfaced as a
failure to the engine, never reported as success or parsed as zero-filled data.

### Finding 5 — Device identity was not correlated
Fixed. `diagnose` now **requires `--device <bsd-name>`** and goes through the
same discovery/validation as `inspect`/`test-1581`
(external, removable, whole, unmounted, 512-byte media). The factory's
`copy_usb_interface_for_bsd_name()` starts from that exact BSD node
(`IOBSDNameMatching`), walks up the IORegistry parent chain to the owning USB
interface, and confirms identity (interface class `0x08` / subclass `0x04` /
protocol `0x00`, VID `0x0644`, PID `0x0000`) before capture. The capture is
therefore proven to be the same physical device selected by `--device`; there is
no free-floating `IOServiceGetMatchingService` first-match and no independent
VID/PID hardcode driving a second device.

### Additional corrections
- **Cleanup on every path** — `open_seize` failure, ADSC/bulk/interrupt failure,
  timeout, unplug, and interruption all funnel through the engine and
  `destroy`. §8 documents the unplug/replug recovery action.
- **Bounded descriptor walking** — the configuration descriptor is validated
  against `wTotalLength` and every header length is bounds-checked before it is
  dereferenced; endpoint discovery requires exactly bulk-IN + bulk-OUT +
  interrupt-IN (with `wMaxPacketSize == M65_CBI_STATUS_LENGTH`, i.e. 2).
- **Exact hex evidence** — every IOKit/IOUSBHost failure path prints the raw
  `IOReturn 0x%08x`, not just a localized description.
- **Tests** — new coverage for `diagnose` CLI parsing/validation, the exit-code
  mapping, and JSON body (see §4).

---

## 3. `diagnose` command

Usage:

    m65floppy-probe diagnose --device <bsd-name> [--vid <id>] [--pid <id>] [--json]

- `--device` is **required** (Finding 5). `--output` and
  `--ack-temporary-controller-change` are rejected for `diagnose` (it never
  writes media and never changes the controller). `--vid`/`--pid`/`--json` are
  accepted; `--vid`/`--pid` remain rejected for `list`/`inspect`/`test-1581`.
- `diagnose` is strictly read-only: it opens the IOUSBHost capture transport,
  runs `m65_inspect` (§2 Finding 3), destroys the transport, and reports.

### Exit codes (`m65_diagnose_exit_code`, unit-tested in `tests/test_diagnose.c`)

| Condition | Exit |
|---|---|
| Transport not created, `create_status == NO_DEVICE` | 1 |
| Transport not created, any other status (permission/timeout/protocol/IO) | 2 |
| Transport created, inspect `M65_PROBE_OK` | 0 |
| Transport created, inspect `M65_PROBE_NO_DEVICE` | 1 |
| Transport created, inspect `M65_PROBE_PERMISSION` | 2 |
| Transport created, inspect any other probe code | 4 |

### JSON output
`m65_output_diagnose_json` emits a top-level `diagnose` object with
`schema_version: 1` (unchanged), `transport: "iousbhost"`, the VID/PID
identifier, `captured`/`destroyed` booleans, a `ufi` body (present when
captured), the numeric `exit_code`, and any error text. The `ufi` body is
produced by a shared helper (`json_inspect_ufi_body`) also used by
`m65_output_inspect_json`, so `diagnose` and `inspect` report identical UFI
evidence.

**Additive schema note:** the shared UFI body now includes a
`changeable_flexible_disk` object (the MODE SENSE *changeable* Flexible Disk
page) alongside the existing current-page `flexible_disk`. This is purely
additive — no existing key changed name, type, or meaning — so `schema_version`
remains `1`.

---

## 4. Tests

All portable unit tests build and pass in this environment (Linux, GCC). The
test binary links `m65core` only — no macOS symbols — and the non-Apple stub
resolves the factory.

- `tests/test_diagnose.c` (new) — 12 assertions covering every branch of
  `m65_diagnose_exit_code`.
- `tests/test_cli.c` — `diagnose` requires `--device`; parses
  `--device`/`--vid`/`--pid`/`--json`; rejects `--output`; rejects
  `--ack-temporary-controller-change`; `inspect` still rejects `--vid`.
- Registered in `tests/test.h` and `tests/test_main.c`.

The `src/cbi.c` engine tests (allowlist, exact accounting, REQUEST SENSE,
reset/recovery) are unchanged and still pass, which is what gives confidence
that the new adapter — feeding that same engine — inherits those guarantees.

---

## 5. Cross-check with ChatGPT 5.6 Sol

**Status: PENDING — not yet performed.**

The task's §12 cross-check (pasting the header, the `.m` adapter, and the CLI
wiring into ChatGPT 5.6 Sol with the five-concern prompt) has not been run in
this environment. It remains a **required manual step before merging.** The five
concerns: (1) correct `IOUSBHostInterface`/`IOUSBHostPipe`/`IOUSBHostObject`
usage; (2) ARC memory correctness (`__bridge_retained`/`__bridge_transfer`
balance, no ObjC escaping ARC); (3) correct CBI ADSC control request
(`0x21`/`0x00`/wIndex/wLength=12); (4) STALL recovery via `clearStall...` on
every bulk error before the next command; (5) no path that can emit a write
opcode (FORMAT UNIT `0x04`, WRITE `0x2A`, or any media data-out). Record every
applied finding here when the review is run.

---

## 6. Test results — hardware gates

**Status: ALL PENDING.** No Apple Silicon Mac and no TEAC drive were available,
so none of the gates in `docs/phase2-test-checklist.md` ran on hardware.

Verified in this environment (Linux, no macOS SDK):

- All portable C sources — `cli.c`, `main.c`, `output.c`, `probe.c`,
  `usb_cbi_iousbhost_stubs.c`, and the rest of `m65core` — object-compile
  cleanly under the project's strict warning set (`-Wall -Wextra -Wpedantic
  -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes
  -Wmissing-prototypes`). The only `-Werror` hits were GCC-only
  `-Wformat-truncation` diagnostics on **pre-existing** lines
  (`probe.c` `copy_command_failure`, `main.c` write-image message), confirmed
  identical on base `ae1d960`; Clang (the macOS compiler) does not implement
  that warning.
- The full `m65core` + unit-test binary builds and runs: `all unit tests
  passed`, exit 0.
- The `.m` file **cannot** be compiled here (no macOS/IOUSBHost SDK). It is
  **unverified by a compiler.** Gate 1 on the target Mac is the first real
  compile.

Gates 1–7 must be run on the target Mac and recorded in the checklist.

---

## 7. Next steps before merge

1. Build on the target Mac (Gate 1) and fix any Clang-specific diagnostics. If a
   single `-Werror` warning is specific to the `.m`, relax that one warning via
   the file's per-source `COMPILE_FLAGS` — do **not** weaken the C target flags.
2. Run the ChatGPT 5.6 Sol cross-check (§5) and apply every correct finding.
3. Run hardware gates 1–7 (§6, checklist) on the TEAC drive under `sudo` and
   record the exact `IOReturn` hex on any failure.
4. Only after a clean target-Mac build and the cross-check should a human run
   the root-only capture gate.

---

## 8. Safety constraints honored

- No FORMAT UNIT (`0x04`) anywhere.
- No WRITE (`0x2A`) or any floppy-media data-out opcode. `diagnose` is data-in
  only (plus the no-data TEST UNIT READY).
- MODE SELECT (`0x55`) is emitted only from `test-1581`, only after saving the
  Flexible Disk page for restoration (existing, unchanged `src/ufi.c` logic).
- No `/dev/disk*` / `/dev/rdisk*` opened `O_WRONLY`/`O_RDWR`.
- No `setuid`/`seteuid`/internal `sudo`; root must be supplied by the caller.
- No entitlement or code-signing changes.
- `src/ufi.c`, `src/cbi.c`, and `src/layout.c` were not modified.
- `schema_version` is unchanged (still `1`); `diagnose` JSON is emitted under a
  new top-level `diagnose` key, and the added `changeable_flexible_disk` field is
  purely additive.

### Unplug/replug recovery
If the drive is physically removed mid-capture, IOUSBHost operations fail with
an IOReturn (e.g. `kIOReturnNoDevice`/`kIOReturnNotAttached`); the engine maps
this to a transport error, prints the hex code, and tears the transport down.
Recovery action: re-plug the drive, re-run `list` to confirm the BSD node
reappears, then re-issue the command with the (possibly new) `--device` name.
