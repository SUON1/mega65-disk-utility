# Phase 2 test checklist — IOUSBHost CBI adapter

**Updated:** 2026-08-13

**Hardware status:** Gates 2 and 3 passed. Gate 4 stopped safely on a malformed
mandatory changeability response. Gate 6 is blocked for this controller.

The non-hardware build gates and the authorized live results below were run on
the target Apple Silicon Mac. Any additional live execution requires separate,
explicit authorization from the owner. The program never invokes `sudo`.

Use a disposable, known-good MEGA65/1581 disk. When macOS says the disk is not
readable, choose **Ignore**, never **Initialize**. Device names are unstable;
never copy an old `diskN` value into a command without rerunning `list`.

Status legend: ☑ passed · ◐ partially demonstrated · ⛔ stopped/blocked ·
☐ pending live authorization.

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

Status: ☑ passed initially on 2026-08-11 and again for the hardware-fix
candidate on 2026-08-13. Debug, Release, and ASan/UBSan builds exited 0 under
strict warnings; each CTest run passed `1/1`. The IOUSBHost translation unit
also passed Clang static analysis.

Executable paths:

- `build/debug/m65floppy-probe`
- `build/release/m65floppy-probe`

## Gate 2 — Discover and record the exact device

```sh
build/release/m65floppy-probe list --json
```

Status: ☑ passed. The selected device was exactly one unmounted, external,
removable, whole `TEAC USB UF000x`, VID `0x0644`, PID `0x0000`, with a
737,280-byte medium in `disk6`. The BSD name remains unstable and must still be
rediscovered before every future command.

Stop unless exactly the intended external, removable, whole, unmounted TEAC is
selected. Record its current BSD name and discovered VID/PID. Do not use
display overrides; `--vid` and `--pid` no longer exist.

## Gate 3 — Unprivileged control

```sh
build/release/m65floppy-probe diagnose --device <name-from-list> --json
```

Status: ☑ passed as UID 501. DeviceCapture failed with the expected
`kIOReturnNotPrivileged` value `0xe00002c1`, the process exited 2,
`transport_created` was true, `capture_acquired` was false, destroy was called,
and `list` immediately rediscovered the same `disk6` candidate.

Expected control result: capture permission failure with
`transport_created: true`, `capture_acquired: false`, and a nonzero exit. Record
the exact IOReturn. If capture unexpectedly succeeds, stop and preserve the
complete output before deciding on any root run.

## Gate 4 — Root capture and read-only diagnose

```sh
sudo build/release/m65floppy-probe diagnose \
  --device <name-from-list> --json
```

Status: ⛔ stopped safely on 2026-08-13. The final candidate run acquired and
released IOUSBHost DeviceCapture, called destroy, and exited 4 after rejecting
the malformed mandatory changeability response. No MODE SELECT, sector READ,
format, or write command was issued.

Successful evidence before the stop:

- INQUIRY: `TEAC`, `USB UF000x`, firmware `0.00`;
- TEST UNIT READY: ready;
- READ CAPACITY: 1,440 blocks × 512 bytes = 737,280 bytes;
- current Flexible Disk page: 500 kbit/s, 2 heads, 18 sectors/track,
  512 bytes/sector, 80 cylinders, 300 RPM;
- capture acquired and released, with destroy called.

The specification-required changeability request used PC=1, page `0x3f`, and
the exact 72-byte UFI all-pages length. The response declared 72 bytes but put
the 24-byte ASCII identity `NEC     USB UF000x      ` where pages `0x01` and
`0x05` should begin. Its exact bytes were:

```text
00460200000000004e454320202020205553422055463030307820202020202000
000000000000052800000000000000012c00001b0a000100000000000000001c
06000500000000
```

Only the later `0x1b` and `0x1c` page headers are recognizable. There is no
valid Flexible Disk page `0x05` changeability mask, so this result cannot
satisfy Gate 4's required evidence and must not be used to justify MODE SELECT.
The live release binary SHA-256 was
`8684f770da486caa9b23050cce3ade9f1e50a3535d6ccbfc6fe121d9358f50d9`;
it was built from an uncommitted candidate based on pushed head `423d166`.

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

Status: ☐ pending and withheld. Gate 4 did prove normal release/rematch on the
executed path, but the separate SIGINT/SIGTERM cases were not authorized or
run. They are not a prerequisite for declaring this controller blocked at
Gate 4.

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

Status: ⛔ blocked for this `0x0644:0x0000` / firmware `0.00` controller. The
mandatory changeability mask is malformed, so the safety precondition “every
changed field is permitted by the mask” cannot be established. Do not run
`test-1581`, with or without `--output`, against this candidate.

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

Status: ☑ passed for the final Gate 4 run. After a five-second rematch window,
`list` rediscovered `disk6` as the same usable external/removable/whole,
unmounted 737,280-byte medium. `system_profiler` independently reported the
removable TEAC at VID `0x0644`, PID `0x0000`, with no serial number exposed.

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
