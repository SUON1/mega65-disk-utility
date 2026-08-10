# Phase 2 Handoff — IOUSBHost Capture Transport + `diagnose`

This document records the implementation decisions, SDK observations, review
findings, and test status for Phase 2. It is the reference for anyone building,
verifying, or extending the IOUSBHost transport before it is merged to `main`.

Branch: `phase2/iousbhost-capture` (from base `ae1d960`).

---

## 1. Scope delivered

- `include/m65/transport_iousbhost_bridge.h` — pure-C bridge header. All types
  are opaque (`m65_iousbhost_ctx_t` is forward-declared; the C side sees only a
  `void`-equivalent pointer). No Objective-C leaks across the boundary.
- `src/transport_iousbhost.m` — Objective-C (ARC) implementation of the bridge:
  service discovery by VID/PID, whole-device capture, alternate-setting
  selection, endpoint discovery, pipe opening, CBI ADSC command dispatch, bulk
  I/O with STALL recovery, and clean teardown.
- `src/transport_iousbhost_stubs.c` — no-op implementation for non-Apple builds
  and for the unit-test link (see §4).
- `diagnose` command — registered in `cli.c`/`cli.h`/`main.c`, with a
  data-in-only probe sequence and JSON output support in `output.c`/`output.h`.
- `test-1581` integration — IOUSBHost is opened as the preferred transport on
  macOS in `src/probe.c`, with fallback to the existing IOUSBLib path.
- `CMakeLists.txt` — compiles the `.m` with ARC, links `IOUSBHost` and
  `Foundation`, and wires the stub for non-Apple and unit-test builds.

---

## 2. Implementation decisions

### Objective-C bridge over a C core
The existing codebase is strict C17. IOUSBHost is an Objective-C framework, so
all Objective-C is quarantined inside `transport_iousbhost.m`. The rest of the
program only ever sees the pure-C bridge header. This keeps `m65core` free of
Objective-C and preserves the `-Wstrict-prototypes -Wmissing-prototypes`
warning discipline for the C sources.

### ARC-clean C boundary
The context object created in `m65_iousbhost_open` is handed to the C side with
`__bridge_retained` (transferring one +1 retain to the opaque `void*`). On
`m65_iousbhost_close` it is reclaimed with `__bridge_transfer`, balancing the
retain and letting ARC release it. No other bridge casts are used, so there is
no path for an Objective-C object to escape ARC or to be double-freed.

### No global mutable state
All transport state (device, interface, pipes, endpoint addresses, timeouts)
lives inside `m65_iousbhost_ctx_t`. Nothing is stored in file-scope globals, so
concurrent contexts and repeated open/close cycles are safe.

### VID/PID identification at runtime
The device is matched by VID `0x0644` / PID `0x0000` at runtime. There are no
hardcoded BSD `/dev/disk*` paths, and the drive exposes no serial number, so
VID/PID is the only stable identifier.

### `diagnose` is strictly read-only
`diagnose` issues only data-in UFI commands — INQUIRY, REQUEST SENSE,
READ CAPACITY, and MODE SENSE (Flexible Disk page). It never sends MODE SELECT,
FORMAT UNIT, WRITE, or any data-out to media. MODE SELECT remains confined to
the `test-1581` path, which saves and restores the Flexible Disk page.

### `diagnose` exit codes
- `0` — all probed commands succeeded.
- `1` — device not found (`M65_IOUSBHOST_ERR_NOT_FOUND`).
- `2` — capture failed (could not take the device from the kernel driver).
- `3` — pipe/endpoint setup failed.
- `4` — one or more UFI commands failed. INQUIRY failure is fatal (teardown and
  exit 4); REQUEST SENSE / READ CAPACITY / MODE SENSE failures are logged, the
  sequence continues, and exit 4 is set at the end.

### `test-1581` transport selection
On macOS the IOUSBHost capture context is opened at the start of
`m65_test_1581`, before exclusive access is acquired, so the kernel driver is
detached ahead of the MODE SELECT / LBA-1599 sequence. On
`M65_IOUSBHOST_ERR_NOT_FOUND` or any other failure the code logs to stderr and
falls through to the existing IOUSBLib vtable path. The context is always closed
in the cleanup section, on every exit path. The existing `M65Transport` vtable
logic is left intact — the IOUSBHost block is a selection/ownership layer in
front of it. Binding the capture context directly into the vtable so that UFI
commands flow through IOUSBHost end-to-end is a Phase 3 step (see §7).

### Explicit timeouts
Per the quality standards, bulk transfers never use a zero (infinite) timeout:
5.0 s for UFI commands and 30.0 s for sector reads. Every failure path prints
the `IOReturn` hex code; short bulk transfers are logged as warnings rather than
silently truncated.

---

## 3. SDK behavior differences from expectation

No functional surprises were reconciled against hardware yet (see §5 — no Mac or
device was available in the implementation environment). The implementation was
written directly against the installed SDK header signatures quoted in the task
spec (`IOUSBHostDefinitions.h`, `IOUSBHostObject.h`, `IOUSBHostInterface.h`,
`IOUSBHostPipe.h`). Two build-environment facts worth flagging for the target
machine:

1. **`enable_language(OBJC)` is required.** The project declared only
   `LANGUAGES C`. CMake will not compile a `.m` file until Objective-C is
   enabled, so `enable_language(OBJC)` was added inside the `if(APPLE)` block.
   This is an addition the task's CMake snippet omitted; it is necessary for the
   `.m` to build at all.

2. **The `.m` inherits the target's strict warning set.** `m65_strict_warnings`
   applies `-Werror -Wconversion -Wsign-conversion -Wstrict-prototypes ...` at
   the `m65mac` target level, and `set_source_files_properties(... COMPILE_FLAGS
   "-fobjc-arc")` only *appends* `-fobjc-arc` rather than replacing the target
   flags. The `.m` was written to be warning-clean, but if Clang on the target
   raises a `-Werror` warning specific to the Objective-C source (e.g.
   `-Wstrict-prototypes` on a bridge shim), the intended fix is to relax that
   single warning for the `.m` via its per-file `COMPILE_FLAGS`, not to weaken
   the C target flags. This is called out as a Gate 1 watch item.

---

## 4. Unit-test linkage deviation (necessary)

`src/probe.c` lives in the `m65core` static library and, on macOS, now
references `m65_iousbhost_open` and `m65_iousbhost_close` under
`#if defined(__APPLE__)`.

- The **executable** (`m65floppy-probe`) links `m65mac`, which compiles the real
  `transport_iousbhost.m`, so those symbols resolve there.
- The **unit-test binary** (`m65-unit-tests`) links `m65core` *without* `m65mac`.
  Without intervention it would fail to link with "undefined symbol" for the
  bridge functions.

Resolution: `src/transport_iousbhost_stubs.c` is added **only to the
`m65-unit-tests` target** (guarded by `if(TARGET m65-unit-tests)`), never to
`m65core`. Adding it to `m65core` would collide with the real `.m` definitions
in the executable link (duplicate symbols). This keeps the existing unit tests
(including the `src/cbi.c` tests) linking and passing unchanged, and satisfies
the "no undefined symbol" requirement of Gate 1. `src/cbi.c` itself was not
modified.

---

## 5. Cross-check findings from ChatGPT 5.6 Sol

**Status: PENDING — not yet performed.**

The task's §12 cross-check workflow (pasting the bridge header, the `.m`
implementation, and the `test-1581` integration into ChatGPT 5.6 Sol using the
five-concern review prompt) could not be run in the implementation environment.
This is a **required manual step before merging to `main`.**

The five concerns to review:
1. Correct usage of `IOUSBHostInterface` / `IOUSBHostPipe` / `IOUSBHostObject`
   against the actual method signatures.
2. ARC memory-management errors (missing/over-releases, incorrect `__bridge`
   casts, ObjC objects escaping ARC across the C boundary).
3. Correct CBI ADSC control-request construction (`bmRequestType` = `0x21`,
   `bRequest` = `0x00`, `wIndex`, `wLength`) for a UFI command send.
4. STALL recovery — every bulk-transfer error path must call
   `clearStallWithError:` before the next command.
5. No code path that can send a write opcode (FORMAT UNIT `0x04`, WRITE `0x2A`,
   or any data-out to floppy media sectors).

When the review is run, apply every factually-correct finding and record each
applied change in this section (what changed, and why).

---

## 6. Test results — hardware gates

**Status: ALL PENDING.** No Apple Silicon Mac and no TEAC drive were available
in the implementation environment, so none of the seven gates in
`docs/phase2-test-checklist.md` have been executed on hardware.

What *was* verified in the implementation environment (Linux, no macOS SDK):

- All portable C sources — `cli.c`, `main.c`, `output.c`, `probe.c`,
  `transport_iousbhost_stubs.c`, and the rest of `m65core` — compile cleanly
  under the project's strict warning set (`-Wall -Wextra -Wpedantic -Werror
  -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes
  -Wmissing-prototypes`). The only `-Werror` hits were GCC-only
  `-Wformat-truncation` diagnostics on *pre-existing* lines
  (`probe.c:86`, `probe.c:542`, `main.c:361`); Clang (the macOS compiler) does
  not raise these.
- `src/probe.c` compiles cleanly both with and without `__APPLE__` defined, so
  both the IOUSBHost path and the fallback path are syntactically valid.
- The `CMakeLists.txt` Phase 2 block parses and configures correctly.
- The `.m` file **cannot** be compiled here (no macOS/IOUSBHost SDK). It is
  written against the spec's SDK signatures but is **unverified by a compiler**.

Gates 1–7 (build, diagnose device discovery, INQUIRY, READ CAPACITY, MODE
SENSE, `test-1581`, driver restoration) must be run on the target Mac and their
results recorded in the checklist.

---

## 7. Next steps for Phase 3

1. **Run the cross-check (§5) and the hardware gates (§6) on the target Mac.**
   These are prerequisites for merging the branch.
2. **Bind the capture context into the transport vtable.** Currently
   `test-1581` opens/owns the IOUSBHost context but UFI commands still flow
   through the IOUSBLib vtable. Phase 3 should implement an `M65Transport` whose
   `ops` dispatch through `m65_iousbhost_send_ufi`, so the whole `test-1581`
   sequence (MODE SENSE, MODE SELECT, READ) runs over IOUSBHost.
3. **Surface endpoint addresses in JSON.** The bridge currently exposes
   endpoints only via `m65_iousbhost_print_endpoints` (stdout). Human-readable
   `diagnose` prints them; JSON `diagnose` cannot include them without a getter.
   Add an accessor to the bridge if endpoints are needed in machine-readable
   output.
4. **Sector capture / D81 imaging over IOUSBHost.** Extend beyond the boundary
   reads to full whole-device capture using the 30 s sector-read timeout path.
5. **Consider a per-file warning relaxation for the `.m`** if Gate 1 surfaces a
   Clang `-Werror` warning that is not worth addressing in source (see §3.2).

---

## 8. Safety constraints honored

- No FORMAT UNIT (`0x04`) anywhere.
- No WRITE (`0x2A`) or any floppy-media data-out opcode.
- No `/dev/disk*` / `/dev/rdisk*` opened `O_WRONLY`/`O_RDWR`.
- No `setuid`/`seteuid`/internal `sudo`; root must be supplied by the caller.
- No entitlement or code-signing changes.
- MODE SELECT (`0x55`) is sent only from `test-1581`, only after saving the
  Flexible Disk page for restoration (existing, unchanged logic).
- `diagnose` sends only data-in commands.
- `src/ufi.c`, `src/cbi.c`, and `src/layout.c` were not modified.
- `schema_version` is unchanged (still `1`); `diagnose` JSON is emitted under a
  new top-level `diagnose` key.
