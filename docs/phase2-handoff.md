# Phase 2 handoff — IOUSBHost CBI capture experiment

**Updated:** 2026-08-11

**Branch:** `phase2/iousbhost-capture`

**Correction-series starting head:** `f7fec7d9bb0cb89e19bfe48720af1ff2a27d4e59`

**Original Phase 2 base:** `ae1d9605137decbe64e029be44711eba3d46d82c`

## Verdict

The branch passes the target Apple Silicon Mac's non-hardware Debug and Release
acceptance gates under the existing strict warning policy. It is ready for the
project owner to consider a separately authorized manual hardware run.

No live device command, whole-device capture, `sudo`, MODE SELECT, sector read,
or media operation was run while preparing this handoff. All hardware gates in
`docs/phase2-test-checklist.md` remain pending. Phase 2 is not physically proven
until those gates run and the resulting evidence is reviewed.

## Preserved architecture and safety boundary

```text
diagnose / inspect / test-1581
             |
             v
       M65Transport
             |
             v
 shared src/cbi.c engine
             |
             v
    M65CbiIo operation table
             |
             v
 IOUSBHost adapter (Objective-C/ARC)
```

The Objective-C layer performs only device/interface correlation and raw CBI
USB phases. Every UFI command remains behind `m65_validate_command`, exact
transfer accounting, REQUEST SENSE, completion-versus-sense validation, and
command-block recovery in `src/cbi.c`. The removed raw
`m65_iousbhost_send_ufi` bridge was not restored, and `m65_test_1581` remains
transport-neutral.

The executable still has no FORMAT UNIT, WRITE, WRITE AND VERIFY, erase, or
vendor-specific data-out path. `diagnose` is the full agreed read-only sequence:
INQUIRY, TEST UNIT READY, READ CAPACITY, READ FORMAT CAPACITIES, MODE SENSE
current, and MODE SENSE changeable. The only permitted data-out is the existing
validated 40-byte temporary MODE SELECT payload in acknowledged `test-1581`.

## Corrections after the `f7fec7d` review

### IOUSBHost/SDK contract

- The ARC cleanup balances `__bridge_retained` with `__bridge_transfer` and
  consumes the local ownership explicitly, without suppressing warnings.
- Interrupt-IN uses `enqueueIORequestWithData` with framework
  `completionTimeout:0.0`, as required by the installed `IOUSBHostPipe.h`.
- A dispatch semaphore enforces the caller's application deadline. On expiry,
  the pipe receives `IOUSBHostAbortOptionSynchronous`; completion is then
  observed through a bounded drain before return.
- The request owns its DMA buffer, completion semaphore, IOReturn, and byte
  count. The adapter and completion block retain the request until it drains,
  preventing a late callback from touching freed state.
- Exactly two interrupt completion bytes are required.
- Close and command-block-reset preparation cancel/drain any pending request.
  Reset preparation then clears the interrupt halt/toggle and arms exactly one
  reset ADSC; finish always consumes that permission.
- The adapter imports the official `IOKit/usb/USB.h` declarations. Real
  `kIOUSBPipeStalled` (`0xe000404f`) maps to STALL;
  `kIOUSBUnknownPipeErr` (`0xe0004061`) maps to protocol failure rather than
  STALL. Timeout, aborted/returned transaction, no-device/not-attached,
  permission, and generic failures remain distinct at the CBI boundary.
- Endpoint timeouts are not treated as implicit stalls. Real bulk STALL follows
  the engine's clear-stall branch; other phase-losing failures require bounded
  command-block recovery before another ordinary command.

### Capture state, targeting, and cleanup

- Transport allocation/correlation, capture acquisition, capture release, and
  the void destroy call are separate report facts.
- A DeviceCapture permission failure reports `transport_created: true` but
  `capture_acquired: false`; destroy is never described as proof of rematch.
- Reports contain the selected BSD name and discovered `M65DeviceInfo` USB
  identity. Display-only `--vid`/`--pid` overrides were removed.
- Phase 2 command selection is IOUSBHost-only. The legacy IOUSBLib source stays
  as historical evidence but is not a silent CLI fallback.
- SIGINT/SIGTERM handlers are installed with `sigaction` around `diagnose`,
  `inspect`, and `test-1581`, previous handlers are restored afterwards, and
  normal interruption returns through workflow release and transport destroy.
  Signals are blocked while clearing the workflow flag and installing handlers
  to avoid the setup race.
- SIGKILL, process crash, host power loss, and framework/OS failure cannot be
  made cleanup-safe in process; unplug/replug may still be required.

### Output and tests

- Inspect, diagnose, and test-1581 output names the IOUSBHost backend and
  reports capture acquisition/release accurately.
- Changeable field names remain on the current Flexible Disk page, where
  `m65_apply_changeability` records them.
- The changeable page is emitted separately as
  `flexible_disk_changeable_mask` with its page code and exact raw bytes, not as
  normal geometry.
- `tests/test_output.c` exercises the real serializers. It checks discovered
  BSD/VID identity, backend/capture state, permission failure reporting,
  accurate current-page changeable fields, raw-mask labeling/bytes, and
  result/error/exit/sense consistency.
- Probe tests verify acquisition and release facts; CBI tests require reset
  recovery after an interrupt endpoint STALL.

## Target-Mac build and unit-test evidence

The unmodified `f7fec7d` source was reproduced first. With the sandbox-only
module-cache environment workaround, AppleClang 21 stopped at:

```text
src/usb_cbi_iousbhost_macos.m:814:34: error:
variable 'adapter' set but not used [-Werror,-Wunused-but-set-variable]
```

After the corrections, these commands completed successfully on 2026-08-11:

```sh
cmake --preset debug
CLANG_MODULE_CACHE_PATH=/private/tmp/m65-phase2-clang-module-cache \
  cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure

cmake --preset release
CLANG_MODULE_CACHE_PATH=/private/tmp/m65-phase2-clang-module-cache \
  cmake --build --preset release --parallel
ctest --preset release --output-on-failure
```

Results:

- Debug configure: exit 0.
- Debug product and unit-test build: exit 0; no warning suppression added.
- Debug CTest: exit 0, `1/1` test target passed (0.41 seconds).
- Release configure: exit 0.
- Release product and unit-test build: exit 0.
- Release CTest: exit 0, `1/1` test target passed (0.23 seconds).

The module-cache path is an environment-only sandbox workaround and is not
encoded in CMake.

## Remaining work

All live gates remain pending. Use only the commands and stop conditions in
`docs/phase2-test-checklist.md`, after explicit owner authorization. A successful
destroy call is not sufficient evidence of driver/media rematch: Gate 7 must
also rerun `list`, confirm that the selected device (possibly under a new BSD
name) reappears, and confirm it is usable with no stale capture.

Do not merge solely on portable tests, and do not describe the TEAC as capable
of 1,600-block acquisition until LBA 1599 and two matching complete reads are
physically demonstrated.
