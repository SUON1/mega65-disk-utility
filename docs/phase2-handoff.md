# Phase 2 handoff — IOUSBHost CBI capture experiment

**Updated:** 2026-08-13

**Branch:** `phase2/iousbhost-capture`

**Correction-series starting head:** `f7fec7d9bb0cb89e19bfe48720af1ff2a27d4e59`

**Original Phase 2 base:** `ae1d9605137decbe64e029be44711eba3d46d82c`

## Verdict

The hardware-fix candidate passes the target Apple Silicon Mac's Debug,
Release, ASan/UBSan, unit-test, and static-analysis gates under the existing
strict warning policy. Authorized live testing proved discovery, the
unprivileged permission boundary, IOUSBHost whole-device capture, the CBI
read-only metadata path, normal capture release, and USB/driver/media rematch.

The exact TEAC `0x0644:0x0000` firmware `0.00` does not return a valid UFI
changeability mask for the specification-mandated PC=1/page=`0x3f` query. The
probe rejected the malformed response and released the device normally. This
is a required Gate 4 stop condition, so acknowledged `test-1581` is blocked for
this controller. No MODE SELECT, sector READ, format, or write command was run.

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
- Standard INQUIRY is constrained to UFI's exact 36 required bytes. Current
  Flexible Disk MODE SENSE is constrained to 40 bytes; the mandatory
  all-changeable-pages query uses PC=1/page=`0x3f` and UFI's exact 72 bytes.
- A malformed MODE SENSE response is retained in the report and emitted as raw
  hex without being mislabeled as a valid changeability mask. The exact UF000x
  hardware payload is a unit-test regression case.

## Authorized target-hardware evidence

Gates 2 and 3 passed. The intended unmounted external/removable/whole TEAC was
discovered as `disk6`, VID `0x0644`, PID `0x0000`, with a 737,280-byte medium.
As UID 501, DeviceCapture failed with `0xe00002c1` and exit 2 without acquiring
capture; the media remained listed.

The final root read-only Gate 4 candidate acquired and released capture and
successfully returned:

- INQUIRY `TEAC` / `USB UF000x` / `0.00`;
- ready status;
- 1,440 × 512-byte capacity;
- format-capacity descriptors for 1,440 × 512, 2,400 × 512,
  1,232 × 1,024, and 1,440 × 512;
- current geometry of 500 kbit/s, 2 heads, 18 sectors/track, 512 bytes/sector,
  80 cylinders, and 300 RPM.

The subsequent PC=1/page=`0x3f` response declared the correct 72-byte total but
placed the ASCII identity `NEC     USB UF000x      ` at byte 8, where the UFI
page list must start. Later page `0x1b` and `0x1c` headers were recognizable,
but no valid Flexible Disk page `0x05` mask existed. The parser rejected byte
8 (`0x4e`, reserved bit 6 set), emitted the complete payload for review, and
the process exited 4. This is evidence of a controller/firmware response quirk,
not evidence that any geometry field is changeable.

After five seconds, `list` rediscovered the same usable `disk6` media node and
`system_profiler` independently saw the removable TEAC at `0x0644:0x0000`.
Capture release/rematch therefore passed for this executed path.

The live run used an uncommitted candidate based on pushed head `423d166`, with
release-binary SHA-256
`8684f770da486caa9b23050cce3ade9f1e50a3535d6ccbfc6fe121d9358f50d9`.
After preserving that payload as a regression test, diagnostics were refined
to name header `0x4e` at byte 8 and to omit the ambiguous empty
`changeable_fields` list when changeability is unknown. The rebuilt,
non-hardware-validated release binary is
`5e5ac80a025a52d652d2d61eac73b62cbec4733d9ffb354e9d3401b8ac7de31c`.
No additional device run was needed or performed for those output-only
changes.

## Target-Mac build and unit-test evidence

The unmodified `f7fec7d` source was reproduced first. With the sandbox-only
module-cache environment workaround, AppleClang 21 stopped at:

```text
src/usb_cbi_iousbhost_macos.m:814:34: error:
variable 'adapter' set but not used [-Werror,-Wunused-but-set-variable]
```

After the corrections, these commands completed successfully initially on
2026-08-11 and again for the hardware-fix candidate on 2026-08-13:

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
- Debug CTest: exit 0, `1/1` test target passed.
- Release configure: exit 0.
- Release product and unit-test build: exit 0.
- Release CTest: exit 0, `1/1` test target passed.
- ASan/UBSan product and unit-test build: exit 0; CTest passed `1/1`.
- Clang static analysis of `src/usb_cbi_iousbhost_macos.m`: exit 0 with no
  diagnostics.

The module-cache path is an environment-only sandbox workaround and is not
encoded in CMake.

## Remaining work

Review the uncommitted hardware corrections and evidence before committing or
pushing them. Gate 5's separate SIGINT/SIGTERM cases remain pending, but Gate 6
must not run on this controller: its malformed response cannot establish the
changeability precondition for temporary MODE SELECT. Progressing Gate 6 safely
requires a controller/firmware that supplies a valid UFI page `0x05` mask in
the mandatory all-pages response; do not infer one from the malformed bytes or
add a heuristic that treats the embedded identity as mode-page data.

Do not describe this TEAC as capable of 1,600-block acquisition. LBA 1599 and
two matching complete 819,200-byte reads were not attempted or demonstrated.
