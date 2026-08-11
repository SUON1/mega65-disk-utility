# Phase 2 Test Checklist — IOUSBHost CBI I/O Adapter

These gates must be run on the target machine (Apple Silicon MacBook Pro,
macOS 26.5.2, arm64) with the TEAC USB floppy (VID `0x0644`, PID `0x0000`)
attached. Every command that touches the device requires `sudo` (the program
never self-elevates). Record the exact `IOReturn` hex code on any failure.

Every device command requires a BSD device name; always run `list` first to
discover it. The IOUSBHost transport is exposed as a low-level I/O adapter to
the shared `src/cbi.c` engine, so `diagnose`, `inspect`, and `test-1581` all run
their commands end-to-end over IOUSBHost.

Status legend: ☐ pending (not yet run on hardware) · ☑ passed · ✗ failed.

## Gate 0 — Discover the device

    sudo build/m65floppy-probe list
    Expected: the TEAC floppy is listed; note its BSD name (e.g. disk6)

Status: ☐ pending

## Gate 1 — Build succeeds

    cmake --preset debug
    cmake --build --preset debug --parallel
    ctest --preset debug --output-on-failure
    Expected: exit 0; no "undefined symbol"; unit tests (incl. test_diagnose,
              test_cli) pass. The .m compiles here for the first time (it cannot
              be compiled off-Mac).

Status: ☐ pending

## Gate 2 — diagnose captures the selected device

    sudo build/m65floppy-probe diagnose --device <bsd-name>
    Expected: captured=true, destroyed=true, exit 0; runs over IOUSBHost.
    Note: --device is REQUIRED; --output and
          --ack-temporary-controller-change are rejected for diagnose.
    Failure record: exact IOReturn hex printed to stderr; exit 1 = device not
          found, 2 = permission/other create failure, 4 = a UFI command failed.

Status: ☐ pending

## Gate 3 — INQUIRY + TEST UNIT READY

    sudo build/m65floppy-probe diagnose --device <bsd-name> --json
    Expected: ufi.inquiry.vendor contains "TEAC"; ufi.test_unit_ready present
    Failure: STALL or timeout on ADSC or bulk-in; record CBI status bytes

Status: ☐ pending

## Gate 4 — READ CAPACITY + READ FORMAT CAPACITIES

    (from Gate 3 JSON)
    Expected: ufi.capacity.last_lba = 1439 (737,280 B) or 1599 (819,200 B);
              ufi.format_capacities present
    Note: 1439 = firmware-capped; 1599 = 10-sector geometry visible

Status: ☐ pending

## Gate 5 — MODE SENSE current + changeable

    (from Gate 3 JSON)
    Expected: ufi.flexible_disk (current) sectors-per-track = 9 or 10;
              ufi.changeable_flexible_disk present (changeable mask)

Status: ☐ pending

## Gate 6 — test-1581 via IOUSBHost

    sudo build/m65floppy-probe test-1581 --device <bsd-name> \
         --ack-temporary-controller-change [--output disk.d81]
    Expected: MODE SENSE / MODE SELECT / READ all run over IOUSBHost; two
              matching 819,200-byte reads; Flexible Disk page restored; D81
              image created when --output is given.
    Note: --device AND --ack-temporary-controller-change are both required.
    Failure record: which step failed and exact IOReturn or CBI status bytes.

Status: ☐ pending

## Gate 7 — destroy restores driver

    After diagnose/test-1581 exits, run:
    system_profiler SPUSBHostDataType -detailLevel full | grep -A4 TEAC
    Expected: TEAC device block reappears within ~5 seconds (mass-storage
              driver re-attaches). Unplug/replug if it does not.

Status: ☐ pending
