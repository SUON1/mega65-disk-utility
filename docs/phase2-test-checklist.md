# Phase 2 Test Checklist — IOUSBHost Capture Transport

These gates must be run on the target machine (Apple Silicon MacBook Pro,
macOS 26.5.2, arm64) with the TEAC USB floppy (VID `0x0644`, PID `0x0000`)
attached. Every command that touches the device requires `sudo` (the program
never self-elevates). Record the exact `IOReturn` hex code on any failure.

Status legend: ☐ pending (not yet run on hardware) · ☑ passed · ✗ failed.

## Gate 1 — Build succeeds

    cmake -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    Expected: exit 0; no "undefined symbol" warnings

Status: ☐ pending

## Gate 2 — diagnose finds device

    sudo build/m65floppy-probe diagnose --vid 0x0644 --pid 0x0000
    Expected: "IOUSBHost capture: OK"
    Failure record: exact IOReturn hex code printed to stderr

Status: ☐ pending

## Gate 3 — INQUIRY delivers vendor string

    (from Gate 2 output)
    Expected: vendor contains "TEAC"
    Failure: STALL or timeout on ADSC or bulk-in; document CBI behavior

Status: ☐ pending

## Gate 4 — READ CAPACITY reports block count

    (from Gate 2 output)
    Expected: lastLBA=1439 (737,280 bytes) or lastLBA=1599 (819,200 bytes)
    Note: 1439 = firmware-capped; 1599 = 10-sector geometry visible

Status: ☐ pending

## Gate 5 — MODE SENSE Flexible Disk page returns geometry

    (from Gate 2 output)
    Expected: sectors-per-track field present; value 9 or 10

Status: ☐ pending

## Gate 6 — test-1581 via IOUSBHost

    sudo build/m65floppy-probe test-1581
    Expected: two matching 819,200-byte reads; D81 image created
    Failure record: which step failed and exact IOReturn or CBI status bytes

Status: ☐ pending

## Gate 7 — destroy restores driver

    After diagnose exits, run:
    system_profiler SPUSBHostDataType -detailLevel full | grep -A4 TEAC
    Expected: TEAC device block reappears within ~5 seconds

Status: ☐ pending
