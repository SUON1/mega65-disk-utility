# Phase 2.1 plan — Gate 4B page-specific changeability characterization

**Planning date:** 2026-08-14

**Reviewed baseline branch:** `phase2/iousbhost-capture`

**Reviewed baseline commit:** `dcb2d837c7e652cd28734a59714accd207b03525`

**Planning branch:** `phase2/teac-page05-characterization`

**Status:** Planning only. No implementation or hardware execution is included
in this document.

## Recommendation

Add one explicit, opt-in option to the existing read-only `diagnose` command:

```text
m65floppy-probe diagnose --device "$CURRENT_BSD_NAME" \
  --characterize-page05 [--json]
```

The implementation must keep ordinary `diagnose` unchanged. When the option is
present, the workflow should preserve the standard PC=1/page=`0x3f` request and
its complete result, then issue one independent PC=1/page=`0x05` request only
after the standard command completes with its exact 72-byte transfer but the
strict UFI page parser rejects that response. The focused result must be stored
and reported in a separate characterization object. It must never be substituted
for the standard result, consumed by `test-1581`, or treated as authorization
for MODE SELECT.

This design is preferred over a new top-level command because it reuses the
existing device selection, IOUSBHost capture, signal handling, read-only
inspection, reporting, and cleanup boundary without duplicating them. It is
preferred over an automatic second query because the operator must opt in and
the default `diagnose` command must retain its reviewed command surface and
historical result.

## Evidence reviewed

Planning was based on the reviewed baseline and the complete contents of:

- `docs/phase2-handoff.md`;
- `docs/phase2-test-checklist.md`;
- `docs/hardware-results.md`;
- `docs/architecture.md`;
- `README.md`;
- the 2026-08-10 Phase 2 review supplied with the project handoff (also kept
  in the project review archive as `docs/phase2-f7fec7d-review.md`); and
- the relevant CLI, workflow, UFI, CBI, IOUSBHost, output, and unit-test code.

The review established that the shared `src/cbi.c` safety and recovery engine,
the IOUSBHost adapter, accurate capture reporting, strict response parsing, and
the current cleanup path must be preserved. In particular, no raw IOUSBHost
shortcut may bypass `m65_validate_command()` or the CBI engine.

## Why this remains Phase 2

Phase 2 is the hardware-feasibility milestone. It is not complete until the
controller can safely expose and acquire all 1,600 physical 512-byte sectors
twice, producing two matching 819,200-byte byte streams with controller state
restored afterwards.

Gate 4B investigates a read-only prerequisite for that result: whether this
specific inexpensive TEAC firmware returns a usable changeability mask when
asked for only the Flexible Disk page. It does not change geometry, read a
sector, acquire an image, or interpret a D81 filesystem. It therefore remains
Phase 2.1 compatibility characterization.

Phase 3 begins only after Phase 2 has proved complete 819,200-byte acquisition.
Phase 3 will concern verified D81 image and filesystem manipulation; none of
that work belongs in Gate 4B.

The historical gate names remain unchanged:

- Gate 3 is **Unprivileged control**, and it passed.
- Gate 4 is **Root capture and read-only diagnose**, and it stopped safely on
  the malformed standard response.
- The new experiment is **Gate 4B — page-specific changeability
  characterization**.
- Gate 6 remains blocked unless and until a later review changes that status.

## Established hardware evidence

The following is evidence from the reviewed Gate 4 execution, not a prediction
about Gate 4B:

| Fact | Reviewed evidence |
|---|---|
| Device | TEAC USB UF000x |
| USB identity | VID `0x0644`, PID `0x0000`, `bcdDevice` `0x0200` |
| UFI identity | vendor `TEAC`, product `USB UF000x`, firmware `0.00` |
| Non-root boundary | DeviceCapture denied as expected |
| Root capture | Acquired and released successfully |
| Read-only commands | INQUIRY, TEST UNIT READY, READ CAPACITY, READ FORMAT CAPACITIES, and MODE SENSE succeeded through the current page |
| Current geometry | 500 kbit/s, 2 heads, 18 sectors/track, 512 bytes/sector, 80 cylinders, 300 RPM |
| Standard changeability request | PC=1, page `0x3f`, subpage 0, allocation length 72 |
| Standard result | Complete 72-byte transfer, rejected as malformed at byte 8 |
| Cleanup | Capture release and delayed macOS driver/media rematch passed |
| State-changing or media commands | None |

The standard request used this ten-byte CDB before CBI zero padding:

```text
5a 00 7f 00 00 00 00 00 48 00
```

Its complete response was:

```text
00460200000000004e454320202020205553422055463030307820202020202000
000000000000052800000000000000012c00001b0a000100000000000000001c
06000500000000
```

The mode header declares 72 total bytes, but byte 8 is `0x4e` and begins the
embedded identity `NEC     USB UF000x      ` where a UFI mode-page header must
begin. The strict parser correctly rejects reserved bit 6 (`0x40`) and retains
the raw response. Later bytes must not be scanned or reinterpreted
as a page `0x05` mask.

## Exact safety boundary

Gate 4B is input-only controller characterization.

Permitted during the opt-in workflow:

- the existing read-only `diagnose` sequence;
- one additional MODE SENSE (10) with the exact CDB defined below;
- automatic REQUEST SENSE through the existing CBI engine;
- capture acquisition, normal capture release, and transport destruction; and
- JSON or human-readable reporting.

Not permitted:

- MODE SELECT (10), including a no-op or restore-shaped payload;
- READ (10), including LBA 0, LBA 9, or LBA 1599;
- FORMAT UNIT, WRITE, WRITE AND VERIFY, erase, or vendor-specific data-out;
- opening a raw disk node for reading or writing;
- creating an image file;
- automatic privilege escalation or invocation of `sudo` by the program;
- a generic page-code, page-control, allocation-length, CDB, or raw-command CLI;
- deriving a mask by scanning, skipping identity bytes, repairing, padding, or
  otherwise heuristically transforming either response; or
- allowing the focused result to satisfy the standard Gate 4 condition or to
  unlock Gate 6.

The implementation must preserve the existing opcode allowlist and CBI
architecture. Adding the focused request means accepting one additional exact
data-in shape for an already allowlisted opcode; it does not mean permitting
arbitrary MODE SENSE requests.

## Command-surface alternatives

| Alternative | Advantages | Risks | Decision |
|---|---|---|---|
| New explicit top-level command | Very visible and independently auditable | Duplicates selection, capture, workflow, exit mapping, output, and cleanup code; can drift from `diagnose` | Not recommended |
| Opt-in `diagnose` option | Explicit operator intent; reuses the reviewed capture boundary; can preserve standard and focused evidence in one report and one capture | Requires a dedicated opt-in workflow path and careful separation of the two results | **Recommended** |
| Automatic second query after every standard failure | Smallest CLI change | Changes ordinary `diagnose`, hides an extra command behind a historical gate, and can look like permissive fallback | Rejected |

The recommended spelling is `--characterize-page05`. It is intentionally
specific to this one experiment. It must:

- be accepted only with `diagnose`;
- require `--device` exactly as `diagnose` already does;
- allow optional `--json`;
- be rejected when duplicated;
- remain incompatible with `--output` and
  `--ack-temporary-controller-change`; and
- not introduce `--vid`, `--pid`, `--page`, `--pc`, `--subpage`, `--length`,
  or raw-CDB options.

## Recommended workflow

### Ordinary `diagnose`

With no new option, behavior, CDB order, report shape, exit mapping, and cleanup
must remain unchanged. On the reviewed TEAC response it must still reject the
standard page-`0x3f` response, preserve those raw 72 bytes, exit nonzero, and
release capture.

### Opt-in characterization

The opt-in path should use one exclusive IOUSBHost capture and one shared
cleanup path:

1. Use existing discovery and device validation before capture. Require an
   external, removable, whole, unmounted, 512-byte device.
2. Require the exact characterized USB identity: VID `0x0644`, PID `0x0000`,
   and `bcdDevice` `0x0200`.
3. Acquire the IOUSBHost transport through the existing adapter and shared CBI
   engine. Do not add or restore any transport fallback.
4. Run the existing read-only sequence through INQUIRY, TEST UNIT READY, READ
   CAPACITY, READ FORMAT CAPACITIES, and current PC=0/page=`0x05` MODE SENSE.
5. Before considering the focused request, require the same UFI identity:
   `TEAC` / `USB UF000x` / firmware `0.00`. A mismatch is a stop condition.
6. Send the unchanged standard PC=1/page=`0x3f`, 72-byte request.
7. Preserve its complete raw transfer before parsing. Parse it with the
   existing strict parser; do not relax that parser.
8. Do not send the focused request if the standard request stalls, times out,
   is short or overlong, returns a transport/SCSI failure, loses CBI phase, or
   produces an invalid mode header. Do not send it if the standard response is
   valid; the standards-prescribed path then has better evidence and the
   compatibility experiment is unnecessary.
9. When the standard command transferred exactly 72 bytes, its mode header is
   internally complete, and the strict page-list parser rejects its page data,
   record that failure unchanged. Only when the operator supplied
   `--characterize-page05` may the workflow then issue the independent focused
   request below. No byte signature or embedded string is used to recover or
   construct the new request.
10. Copy every focused-response byte delivered by the transport into a
    dedicated report buffer before validation.
11. Validate the focused response with a dedicated strict parser. Do not call
    a scanner that searches for page `0x05` at another offset.
12. Evaluate a valid candidate mask against the actual current Flexible Disk
    page captured in this same run and the fixed 1581 target. Keep that
    evaluation isolated from `M65InspectReport.changeable_mode` and from
    `m65_test_1581()`.
13. Release capture and destroy the transport through the existing cleanup
    path on success, rejection, interruption, timeout, or any other failure.

The standard parse failure remains the overall `diagnose` result. A valid
focused response may produce a nested characterization outcome, but it must not
turn the command into a standard Gate 4 success or change the existing exit
code to zero. No new process exit code is needed for Gate 4B.

## Exact focused CDB

The ten-byte UFI CDB must be hard-coded by a dedicated builder:

| Byte | Value | Meaning |
|---:|---:|---|
| 0 | `0x5a` | MODE SENSE (10) |
| 1 | `0x00` | Existing UFI policy; no optional bits |
| 2 | `0x45` | PC=1 (`0x40`) plus Flexible Disk page `0x05` |
| 3 | `0x00` | Subpage 0 |
| 4–6 | `0x00` | Reserved |
| 7–8 | `0x00 0x28` | Allocation length 40 |
| 9 | `0x00` | Control |

Exact ten-byte CDB:

```text
5a 00 45 00 00 00 00 00 28 00
```

Exact twelve-byte CBI command block after zero padding:

```text
5a 00 45 00 00 00 00 00 28 00 00 00
```

The command contract is:

- CDB length: 10;
- direction: `M65_DATA_IN`;
- caller buffer: non-null and exactly 40 bytes;
- CDB allocation length: exactly 40;
- subpage: exactly 0;
- timeout: the existing bounded read-only command timeout; and
- no caller-selectable variation.

Prefer a dedicated builder such as
`m65_cdb_mode_sense_page05_changeable()` over generalizing the current boolean
builder into a public arbitrary-page API. Extend `m65_validate_command()` with
one exact accepted shape. Preserve the existing accepted current-page
`0x05`/40-byte and all-changeable-pages `0x7f`/72-byte shapes and all existing
rejections.

## Strict focused-response validation

The focused response must have its own strict, offset-fixed parser. Success
requires all of the following:

1. The transport reports exactly 40 bytes, and exactly 40 bytes are available
   in the report buffer.
2. Bytes 0–1 contain Mode Data Length `0x0026` (38), so the declared total is
   exactly 40 bytes.
3. Reserved mode-header bytes 4–5 are zero.
4. Bytes 6–7 declare zero block-descriptor bytes.
5. The one and only page starts at byte 8; the parser never scans for it.
6. The page header's reserved bit 6 (`0x40`) is clear.
7. The lower six page-code bits equal `0x05`. The defined PS bit (`0x80`) may
   be recorded but must not change the page-code comparison.
8. Byte 9, the page length, is exactly 30, making the page exactly 32 bytes.
9. The page consumes bytes 8–39 exactly, with no prefix, suffix, trailing page,
   padding, or inferred bytes.

Only after those checks may the typed Flexible Disk fields and candidate mask
be decoded. Reserved or currently uninterpreted page payload bytes must remain
in `raw_hex`; they must not be repurposed as new fields without a separately
reviewed specification basis.

The implementation should not weaken `m65_parse_mode_parameters()`. A focused
strict parser can share byte-reading helpers, but it must not add identity
skipping, page searching, truncated-page acceptance, declared-length clamping,
or padding.

## Failure and contradiction handling

Raw evidence and validity are separate facts. The report must retain all bytes
actually delivered into the 40-byte buffer even if validation fails.

| Condition | Required handling |
|---|---|
| 0–39 bytes transferred | Record exact length and bytes; mark short and invalid; do not pad, retry, or infer |
| More than 40 bytes reported | Retain the 40 buffer bytes, record both reported and captured lengths, mark the response incomplete/overlong and invalid; do not parse past the buffer |
| Exactly 40 bytes, inconsistent declared length | Preserve raw bytes; mark malformed and stop |
| Nonzero block-descriptor length | Preserve raw bytes; reject; do not skip descriptors |
| Wrong page, reserved bit 6, or page length | Preserve raw bytes; reject; do not scan for another header |
| Bulk or completion STALL | Report the exact transport/sense result and any bytes actually delivered; rely on existing CBI recovery rules; do not retry the focused query |
| Timeout or lost phase | Report and stop; do not issue another ordinary command; proceed only through cleanup/release |
| Unsupported/check condition | Report completion and REQUEST SENSE evidence; do not retry with another page, length, or CDB shape |
| Device unplug or identity change | Stop, release/destroy as possible, and require rediscovery |
| Valid focused response but no valid current page from the same capture | Report candidate raw bytes but mark required-change evaluation unavailable |
| Valid focused response after malformed standard response | Report both independently; do not claim the focused response repairs or contradicts the standard response |
| A later run returns different bytes | Preserve both runs and stop for review; do not choose the more permissive result automatically |

No automatic retries are planned. One root execution should produce at most one
focused page-`0x05` request.

## Candidate-mask evaluation

For a strictly valid focused response, evaluate only whether every bit that
would change from the current page captured in the same run to the fixed target
is permitted:

```text
250 kbit/s
2 heads
10 sectors per track
512 bytes per sector
80 cylinders
300 RPM
```

The evaluation must remain bitwise, using the existing
`m65_geometry_changes_allowed()` policy or an equivalently strict isolated
helper. A field name alone is not enough: every changed bit must be covered by
the candidate mask. Report the actual current values, target values, changed
fields, and the first forbidden field/bit class when applicable.

This evaluation is advisory characterization only. Do not copy the focused
mask into the standard `changeable_mode`, do not set authoritative
`changeable_fields` on the standard current page, and do not modify
`m65_test_1581()` to consume it.

## JSON and human-readable reporting

Keep all current standard fields. In particular:

- retain `mode_sense_changeable_response` and its exact 72-byte raw value;
- retain the standard parse failure as the overall reason;
- emit `flexible_disk_changeable_mask` only when the standard parser actually
  produced a valid standard mask; and
- do not rename any historical capture or Gate 4 evidence.

Add a separate object under the diagnose UFI body, with stable fields equivalent
to:

```json
{
  "page05_changeability_characterization": {
    "requested": true,
    "attempted": true,
    "request": {
      "cdb_hex": "5a004500000000002800",
      "page_control": 1,
      "page_code": 5,
      "subpage": 0,
      "allocation_length": 40,
      "direction": "in"
    },
    "response": {
      "reported_length": 40,
      "captured_length": 40,
      "complete": true,
      "raw_hex": "..."
    },
    "validation": {
      "status": "valid",
      "reason": "strict 40-byte page 0x05 response"
    },
    "required_delta": {
      "status": "permitted",
      "forbidden_fields": []
    },
    "mode_select_authorized": false
  }
}
```

Exact internal type and key naming may be refined during code review, but the
following distinctions are mandatory:

- requested versus attempted;
- CDB/request metadata;
- transport-reported versus captured length;
- exact raw bytes, including invalid and short responses;
- transport/SCSI/sense outcome;
- strict validation outcome and reason;
- candidate required-delta evaluation; and
- an explicit statement that MODE SELECT is not authorized.

When the flag is absent, omit the object or emit no new fields; do not change
ordinary JSON snapshots. When the flag is present but a stop condition prevents
the query, emit `requested: true`, `attempted: false`, and the exact reason.

Human output must print:

- the standard page-`0x3f` result and raw response exactly as before;
- a clearly labeled “Gate 4B page-specific characterization” section;
- the exact focused CDB and transfer lengths;
- the complete captured raw hex;
- validation and required-delta outcomes; and
- `MODE SELECT authorized: no` plus `Gate 6 remains blocked pending review`.

JSON must remain exactly one document on stdout; diagnostics remain on stderr.

## Controller limitation

Gate 4B is initially limited to the exact evidence-bearing controller:

- VID `0x0644`;
- PID `0x0000`;
- `bcdDevice` `0x0200`;
- INQUIRY vendor `TEAC`;
- INQUIRY product `USB UF000x`; and
- INQUIRY firmware `0.00`.

Discovery strings should still be reported, but a substring or display string
must not substitute for the numeric identity and INQUIRY checks. A mismatch
prevents the focused request and is reported as `attempted: false`.

A valid result must not be generalized to every UF000x, every TEAC-branded
drive, or every inexpensive USB floppy controller. Generalization requires
separate controller evidence and review.

## Test plan

### UFI builder and allowlist tests

- Assert the exact 10-byte focused CDB and zero-filled remainder of the
  16-byte builder buffer.
- Assert the CBI engine receives the exact 12-byte zero-padded block.
- Accept only CDB byte 2 `0x45`, subpage 0, allocation 40, 10-byte CDB,
  data-in direction, and a 40-byte non-null buffer.
- Reject wrong PC, page, subpage, allocation length, CDB length, direction,
  buffer length, reserved byte, and control byte.
- Prove existing current-page `0x05`/40 and standard changeable
  `0x7f`/72 requests remain accepted.
- Prove arbitrary MODE SENSE shapes and every existing data-out rejection
  remain rejected.

### Strict parser tests

- Valid exact 40-byte page-`0x05` response with PS clear.
- Valid exact response with the defined PS bit set, while the page code remains
  `0x05`.
- Every short length from 0 through 39, with raw preservation checked at
  representative boundaries 0, 1, 7, 8, 9, and 39.
- A 41-byte parser input or an overlong reported transfer.
- Declared totals below and above 40.
- Nonzero block-descriptor length.
- Nonzero reserved mode-header bytes.
- Page code other than `0x05`.
- Reserved page-header bit 6 set.
- Page length below and above 30.
- Prefix bytes, suffix bytes, and a valid-looking page at an offset other than
  8; all must fail without scanning.
- The exact reviewed 72-byte UF000x malformed payload; it must remain invalid
  and must never be converted into a focused mask.

### Workflow/probe tests

- Ordinary diagnose/inspect emits no focused CDB and retains its existing
  command order and result.
- Opt-in path performs one acquire and one release.
- Opt-in path preserves the standard raw response and its validation failure,
  then issues exactly one focused CDB.
- No focused query on device/firmware mismatch, an earlier read-only failure,
  a valid standard response, invalid standard header/length, short/overlong
  standard transfer, stall, timeout, unplug, or interruption.
- Valid focused candidate that permits every current-to-target changed bit.
- Valid focused candidate that forbids at least one required changed bit.
- Short, overlong, malformed, stalled, timed-out, unsupported, and unplugged
  focused responses, each with normal cleanup expectations.
- Release failure remains an error and cannot be reported as rematch success.
- Opcode counters prove zero MODE SELECT, zero READ (10), and zero data-out
  operations for all characterization paths.
- `m65_test_1581()` remains blocked by the malformed standard response and
  never consumes the candidate focused mask.

### CBI/transport tests

- The exact focused data-in CDB passes through `src/cbi.c`, is padded to 12
  bytes, performs bulk-IN and two-byte completion, and obtains REQUEST SENSE
  under the existing rules.
- Short/overlong transfer accounting is reported without buffer overrun.
- Existing completion/sense agreement, STALL, timeout, recovery, and cleanup
  tests remain unchanged and passing.
- A pending recovery never bypasses the UFI allowlist.
- The IOUSBHost adapter requires no policy shortcut or raw UFI bridge.

### CLI tests

- Accept `diagnose --device diskN --characterize-page05` with optional JSON.
- Reject a missing device, duplicate characterization flag, output path,
  acknowledgement, VID/PID override, or unknown option.
- Reject the characterization flag for `list`, `inspect`, and `test-1581`.
- Keep every existing CLI case passing.

### Output tests

- JSON and human output for requested/not-attempted, valid/permitted,
  valid/forbidden, short, overlong, malformed, stall, timeout, and sense cases.
- Exact raw hex and both transfer lengths for every response-bearing failure.
- Standard raw page-`0x3f` evidence remains present and unchanged.
- A focused candidate is never serialized under the standard
  `flexible_disk_changeable_mask` key.
- `mode_select_authorized` is always false in Gate 4B.
- Capture-acquired, capture-released, destroy-called, result, error, sense, and
  exit-code facts remain internally consistent.
- With the flag absent, existing ordinary diagnose output remains stable.

### Regression and safety audit

- Keep the exact malformed UF000x response as a regression fixture.
- Keep all FORMAT/WRITE/vendor-data-out negative tests.
- Audit source and tests for MODE SELECT, READ (10), output-file, raw-device,
  and `sudo` call sites; explain legitimate historical matches.
- Confirm no new generic command or transport bypass exists.

## Non-hardware verification

The future implementation task must run, without a connected-drive command or
`sudo`:

```sh
cmake --preset debug
CLANG_MODULE_CACHE_PATH=/private/tmp/m65-phase21-clang-module-cache \
  cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure

cmake --preset release
CLANG_MODULE_CACHE_PATH=/private/tmp/m65-phase21-clang-module-cache \
  cmake --build --preset release --parallel
ctest --preset release --output-on-failure

git diff --check
```

The Debug preset is the required ASan/UBSan run. Record configure, build, test,
and sanitizer results separately. Do not weaken strict warnings or encode the
temporary module-cache path in CMake.

Also run Clang static analysis for the IOUSBHost Objective-C translation unit
using its real Debug compile arguments (from `build/debug/compile_commands.json`)
with `--analyze`, `-fobjc-arc`, `-fmodules`, and the same temporary module-cache
environment. Record the exact command and diagnostics. If the analyzer is not
available, report that as an unmet verification item rather than claiming it
passed.

Finally inspect the command surface:

```sh
rg -n "MODE SELECT|READ \(10\)|FORMAT UNIT|WRITE|0x04|0x2a|O_WRONLY|O_RDWR|sudo" \
  include src tests docs README.md
```

Interpret matches. Do not delete valid safety tests, documentation, or the
separately acknowledged historical `test-1581` implementation.

## Separately authorized hardware sequence

This section is a future manual checklist. It is not authorization to run any
command now, and the implementation agent must not execute it.

Use the reviewed Release binary and keep the drive unmounted. If macOS offers
to initialize unreadable media, choose **Ignore**. The operator must type all
commands manually.

### 1. Rediscover the device

```sh
./build/release/m65floppy-probe list --json
```

Stop unless exactly one intended external, removable, whole, unmounted TEAC is
listed with VID `0x0644`, PID `0x0000`, revision `0x0200`, and a 512-byte block
size. Record the current BSD name. Never reuse a prior `diskN` without this
step.

Read the value just observed into a shell variable. This avoids placing a stale
example disk number in a command:

```sh
printf 'Enter the current BSD name from list (for example, disk6): '
IFS= read -r CURRENT_BSD_NAME
```

### 2. Recheck the unprivileged boundary

```sh
id -u
./build/release/m65floppy-probe diagnose \
  --device "$CURRENT_BSD_NAME" \
  --characterize-page05 \
  --json
probe_exit=$?
printf 'diagnose_exit=%d\n' "$probe_exit"
./build/release/m65floppy-probe list --json
```

Expected: UID is nonzero, DeviceCapture is denied, capture is not acquired, no
UFI query is issued, and the device remains discoverable. If capture succeeds,
stop and preserve all output; do not proceed to root.

### 3. One root read-only characterization execution

Only after the unprivileged evidence is reviewed and the owner gives separate
authorization:

```sh
sudo ./build/release/m65floppy-probe diagnose \
  --device "$CURRENT_BSD_NAME" \
  --characterize-page05 \
  --json
probe_exit=$?
printf 'diagnose_exit=%d\n' "$probe_exit"
sleep 5
./build/release/m65floppy-probe list --json
system_profiler SPUSBHostDataType -detailLevel full
```

This is one root command invocation and at most one focused page-`0x05` CDB.
Record stdout, stderr, exit code, exact raw responses, sense data, capture and
release facts, executable commit, binary SHA-256, macOS build, and rematch
evidence.

Passing cleanup requires both USB visibility and a usable external/removable/
whole media node after the delay. `destroy_called: true` alone is not proof of
rematch.

## Explicit stop conditions

Stop without a focused query, or stop immediately after the current command,
on any of the following:

- dirty or unexpected implementation worktree, branch, baseline, or upstream;
- build, unit-test, sanitizer, warning, or static-analysis failure;
- ambiguous, mounted, internal, non-removable, non-whole, or non-512-byte media;
- BSD-name or controller-identity mismatch;
- VID/PID/revision or INQUIRY firmware mismatch;
- unprivileged capture unexpectedly succeeding;
- any command sequence containing MODE SELECT, READ (10), FORMAT, WRITE,
  vendor data-out, or raw-device access;
- the default diagnose path issuing the focused request;
- standard page-`0x3f` success (the focused compatibility request is then
  unnecessary and should not be sent);
- standard command transport/SCSI failure, short/overlong transfer, invalid
  mode header, timeout, stall, unplug, or lost phase;
- more than one focused request in an execution;
- missing raw-byte evidence for a response that reached validation;
- focused short/overlong, malformed, stalled, timed-out, unsupported, or
  contradictory result;
- unsuccessful capture release or missing delayed rematch; or
- any proposal to treat the focused result as automatic MODE SELECT approval.

## Decision outcomes

### A. Valid focused mask permits every required change

Record the result as **candidate compatibility evidence**, scoped to the exact
controller and firmware. The standard Gate 4 page-`0x3f` result remains failed,
Gate 6 remains blocked, and no MODE SELECT or sector read follows in this task.

Before using the candidate mask, require a fresh code review, raw-evidence
review, and an explicit policy decision on whether a page-specific response is
an acceptable controller-specific exception to the standards-prescribed
all-pages workflow.

### B. Valid focused mask forbids at least one required changed bit

Record the exact forbidden field and candidate mask. This controller cannot
proceed safely to the requested 1581 geometry under the current policy. Do not
issue MODE SELECT or sector reads.

### C. Focused response is malformed, stalled, unsupported, or unavailable

Preserve the exact raw/transport/sense evidence, release capture, verify
rematch, and leave Gate 6 blocked for this controller. Do not try alternate
allocation lengths, pages, subpages, CDBs, or heuristic parsing.

## Review required before MODE SELECT or acquisition

Even outcome A does not authorize the next command. Before any MODE SELECT:

1. Review the complete implementation diff and all non-hardware verification.
2. Review the standard and focused live raw responses, identities, binary hash,
   exit status, release, and rematch evidence.
3. Decide explicitly whether the exact `0x0644:0x0000` / `0x0200` / `0.00`
   controller may use a page-specific mask as a documented compatibility
   exception.
4. Prepare a separate plan and coding task that cannot silently fall back to
   the focused result and retains exact mask, payload, readback, restoration,
   and interruption safeguards.
5. Obtain explicit owner authorization for the exact MODE SELECT hardware
   execution. A no-op MODE SELECT is still data-out and requires authorization.

Before any sector acquisition, separately review successful controller
readback/restoration evidence and explicitly authorize the boundary reads and
complete dual read. Phase 2 completes only after LBA 1599 is accessible, two
full 819,200-byte reads match, original controller parameters restore, capture
releases, and rematch succeeds. Only then may Phase 3 be planned.
