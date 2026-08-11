# Phase 2 test checklist — IOUSBHost CBI adapter

**Updated:** 2026-08-11

**Hardware status:** all live gates pending

The non-hardware build gates below were run on the target Apple Silicon Mac.
Device commands were not run. Any future live execution requires separate,
explicit authorization from the owner. The program never invokes `sudo`.

Use a disposable, known-good MEGA65/1581 disk. When macOS says the disk is not
readable, choose **Ignore**, never **Initialize**. Device names are unstable;
never copy an old `diskN` value into a command without rerunning `list`.

Status legend: ☑ passed without hardware · ☐ pending live authorization.

## Gate 1 — Debug and Release builds/tests

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

Status: ☑ passed on 2026-08-11. Debug and Release product builds exited 0 under
strict warnings; Debug CTest passed `1/1` in 0.41 seconds and Release CTest
passed `1/1` in 0.23 seconds.

Executable paths:

- `build/debug/m65floppy-probe`
- `build/release/m65floppy-probe`

## Gate 2 — Discover and record the exact device

```sh
build/release/m65floppy-probe list --json
```

Status: ☐ pending.

Stop unless exactly the intended external, removable, whole, unmounted TEAC is
selected. Record its current BSD name and discovered VID/PID. Do not use
display overrides; `--vid` and `--pid` no longer exist.

## Gate 3 — Unprivileged control

```sh
build/release/m65floppy-probe diagnose --device <name-from-list> --json
```

Status: ☐ pending.

Expected control result: capture permission failure with
`transport_created: true`, `capture_acquired: false`, and a nonzero exit. Record
the exact IOReturn. If capture unexpectedly succeeds, stop and preserve the
complete output before deciding on any root run.

## Gate 4 — Root capture and read-only diagnose

```sh
sudo build/release/m65floppy-probe diagnose \
  --device <name-from-list> --json
```

Status: ☐ pending.

The owner must type this command manually after authorization. Expected command
surface: INQUIRY, TEST UNIT READY, READ CAPACITY, READ FORMAT CAPACITIES, MODE
SENSE current, and MODE SENSE changeable only. REQUEST SENSE may be automatic.
There is no MODE SELECT or sector READ in `diagnose`.

Required evidence:

- backend is `iousbhost`;
- selected BSD name and discovered VID/PID match Gate 2;
- `capture_acquired: true` only after DeviceCapture and pipe setup succeed;
- `capture_released: true` after normal release;
- exact IOReturn/CBI/sense data on failure;
- current-page `changeable_fields` and raw
  `flexible_disk_changeable_mask` are present and not conflated.

Stop on ambiguous identity, permission denial, timeout that does not recover,
unplug, short completion, malformed response, or unsuccessful release.

## Gate 5 — Interruption cleanup

During a separately authorized read-only `diagnose`, send one SIGINT and, in a
separate run, one SIGTERM. Do not use SIGKILL for the positive cleanup test.

Status: ☐ pending.

Expected: the command returns through capture release and transport destroy.
Record output and exit status. SIGKILL, crash, or power loss can still require
unplug/replug and is not a passing cleanup path.

## Gate 6 — Acknowledged `test-1581`

Only proceed if the read-only gates are reviewed and the owner explicitly
authorizes the temporary controller change.

```sh
sudo build/release/m65floppy-probe test-1581 \
  --device <name-from-list> \
  --ack-temporary-controller-change \
  --json
```

Optional image evidence uses a new regular file only:

```sh
sudo build/release/m65floppy-probe test-1581 \
  --device <name-from-list> \
  --ack-temporary-controller-change \
  --output phase2-teac-read.d81 \
  --json
```

Status: ☐ pending.

Required positive evidence:

- backend `iousbhost`, never an IOUSBLib fallback;
- acknowledgement recorded;
- complete current parameters saved;
- every changed geometry field allowed by the changeable mask;
- exact validated 40-byte MODE SELECT payload only;
- controller accepts 80 cylinders, 2 heads, 10 sectors, 512 bytes, 250 kbit/s,
  and 300 RPM;
- LBA 9 and LBA 1599 reads succeed;
- two complete 819,200-byte reads match byte-for-byte;
- original controller parameters restore;
- capture releases;
- an output path, if used, is a new regular file and never a device node.

Stop immediately on any failed changeability check, restore failure, mismatch,
short transfer, unexpected data-out command, or stale capture.

## Gate 7 — Prove driver/media rematch

After every capture-backed command exits:

```sh
system_profiler SPUSBHostDataType -detailLevel full
build/release/m65floppy-probe list --json
```

Status: ☐ pending.

Passing Gate 7 requires both:

1. System Information sees the TEAC USB device again; and
2. `list` sees a usable external/removable/whole media node, allowing for a new
   BSD name, with no stale capture.

`destroy_called: true` alone is not rematch evidence. If the driver or media
node does not return, unplug/replug, rerun both checks, and record the recovery.

## Evidence record

For each live run, retain the exact command, macOS version, executable commit,
selected identity, stdout/stderr, exit code, IOReturn and sense values,
acquire/release facts, rematch result, whether controller state changed, and
SHA-256 hashes for any complete images. Do not publish a private serial number.
