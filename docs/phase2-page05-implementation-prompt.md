# Prompt for a separate Codex agent — implement Gate 4B safely

You are the implementation and non-hardware verification agent for the MEGA65
Disk Utility Phase 2.1 compatibility characterization.

Your task is limited to implementing the reviewed, read-only
**Gate 4B — page-specific changeability characterization** plan on its existing
branch. You must not run live hardware commands, invoke `sudo`, issue MODE
SELECT, issue sector reads, commit, push, merge, or open a pull request.

## Repository and branch

- Repository: `SUON1/mega65-disk-utility`
- Local path:
  `/Users/Shared/HomeSlice/mega65-disk-utility/build/phase2-finish-source`
- Required branch: `phase2/teac-page05-characterization`
- Required reviewed ancestor:
  `dcb2d837c7e652cd28734a59714accd207b03525`
- Reviewed baseline branch: `phase2/iousbhost-capture`
- Baseline upstream: `origin/phase2/iousbhost-capture`

The required branch should initially contain only the two planning documents
on top of the reviewed baseline. Treat all existing work as user-owned. Never
reset, clean, overwrite, discard, rebase, recreate, or force-push anything.

## Mandatory preflight

Before editing:

1. Run read-only Git checks for status, branch, HEAD, upstream, and recent log.
2. Confirm the worktree is clean.
3. Confirm the current branch is exactly
   `phase2/teac-page05-characterization`.
4. Confirm commit `dcb2d837c7e652cd28734a59714accd207b03525` is an ancestor
   of HEAD.
5. Confirm the only commits after that ancestor are the reviewed planning
   commit(s), and inspect their complete diff.
6. Fetch normally if needed to check for an upstream move. Do not overwrite or
   force anything.

If the worktree is dirty, the branch is different, the reviewed ancestor is
missing, source files already differ unexpectedly, the branch contains
unreviewed implementation, or the remote state conflicts, stop and report the
exact condition. Do not repair it by resetting or discarding work.

## Read the review and handoff first

Do not inspect or edit implementation code until you have completely read, in
this order:

1. The Phase 2 review titled **“Phase 2 review of ChatLLM completion
   attempt”**, kept in the shared project review archive at
   `/Users/Shared/HomeSlice/mega65-disk-utility/docs/phase2-f7fec7d-review.md`.
   If that archive file is unavailable, read the complete review supplied in
   the task context. If neither copy is available, stop and request it rather
   than proceeding from this prompt alone.
2. `docs/phase2-handoff.md`
3. `docs/phase2-test-checklist.md`
4. `docs/hardware-results.md`
5. `docs/architecture.md`
6. `README.md`
7. `docs/phase2-page05-characterization-plan.md`

Then read the complete relevant implementation and tests, including:

- `src/main.c`
- `src/cli.c`
- `src/probe.c`
- `src/cbi.c`
- `src/ufi.c`
- `src/output.c`
- `src/usb_cbi_iousbhost_macos.m`
- `include/m65/cli.h`
- `include/m65/ufi.h`
- `include/m65/probe.h`
- `include/m65/output.h`
- `include/m65/cbi.h`
- `include/m65/transport.h`
- `tests/fake_transport.c` and `.h`
- `tests/test_ufi.c`
- `tests/test_probe.c`
- `tests/test_cbi.c`
- `tests/test_cli.c`
- `tests/test_diagnose.c`
- `tests/test_output.c`
- `tests/test_main.c`
- `CMakeLists.txt` and `CMakePresets.json`

The review's binding architectural conclusions are:

- all UFI traffic remains behind `m65_validate_command()` and the shared
  `src/cbi.c` engine;
- the IOUSBHost Objective-C file remains a low-level `M65CbiIo` adapter and
  must not regain a raw UFI bridge;
- capture creation, acquisition, release, destroy, and rematch remain distinct
  report facts;
- the strict parser must reject malformed page lists rather than repair them;
- ordinary `diagnose` remains the reviewed standard read-only path;
- Gate 3 remains the passed historical “Unprivileged control” gate;
- Gate 6 remains blocked on the reviewed baseline; and
- no build/test result permits a live hardware run without separate owner
  authorization.

## Established evidence you must preserve

The exact controller is TEAC USB UF000x, USB VID:PID `0x0644:0x0000`,
`bcdDevice` `0x0200`, INQUIRY firmware `0.00`.

Reviewed root Gate 4 proved capture, INQUIRY, TEST UNIT READY, READ CAPACITY,
READ FORMAT CAPACITIES, current Flexible Disk geometry, release, and delayed
rematch. The standard PC=1/page=`0x3f`, allocation-72 request returned this
malformed response:

```text
00460200000000004e454320202020205553422055463030307820202020202000
000000000000052800000000000000012c00001b0a000100000000000000001c
06000500000000
```

The strict parser correctly rejected page header byte `0x4e` at offset 8. This
is not a valid mask. No MODE SELECT, READ (10), FORMAT, WRITE, or media-changing
operation was executed.

Do not rewrite this history, rename Gate 3, claim Gate 4 passed, describe the
controller as capable of 1,600-block acquisition, or call this Phase 3.

## Authorized implementation scope

Implement only the opt-in read-only characterization described in
`docs/phase2-page05-characterization-plan.md`:

```text
m65floppy-probe diagnose --device "$CURRENT_BSD_NAME" \
  --characterize-page05 [--json]
```

Required behavior:

1. Without `--characterize-page05`, ordinary `diagnose` behavior, CDB order,
   output, error, exit mapping, and cleanup remain unchanged.
2. The new flag is accepted only by `diagnose`, requires `--device`, may be
   combined with `--json`, and is rejected when duplicated or combined with
   output, acknowledgement, identity overrides, or another command.
3. The opt-in path uses the existing discovery checks, IOUSBHost-only
   transport, signal guard, shared CBI engine, and one capture/release lifetime.
4. Limit the experiment to VID `0x0644`, PID `0x0000`, `bcdDevice` `0x0200`,
   INQUIRY vendor `TEAC`, product `USB UF000x`, and firmware `0.00`.
5. Run the existing read-only diagnose sequence through current page `0x05`.
6. Send and retain the unchanged standard PC=1/page=`0x3f`, 72-byte response
   first. Parse it strictly and report it exactly as before.
7. Do not issue the focused query if the standard response is valid. Do not
   issue it after a standard transport/SCSI failure, timeout, stall, unplug,
   short/overlong transfer, lost phase, or invalid mode header.
8. If the standard command completes with exactly 72 bytes, its mode header is
   internally complete, but the strict page-list parse fails, keep that failure
   unchanged. Only on the explicitly opted-in path may you issue one separate
   focused request.
9. Do not inspect an embedded identity string, scan bytes, skip a prefix, or
   infer any mask from the 72-byte response. The focused CDB is independently
   and statically constructed.
10. Preserve every focused-response byte supplied to the workflow before any
    parsing. Report reported and captured lengths separately.
11. Parse the focused response with strict length, mode-header, page-offset,
    page-code, reserved-bit, and page-length checks. Do not add permissive
    parsing or heuristic recovery.
12. Evaluate a valid candidate mask only against the actual current page from
    the same capture and the fixed 1581 target. Keep it in a dedicated
    characterization report.
13. Never copy the focused candidate into the standard
    `M65InspectReport.changeable_mode`, never make it an authoritative
    `flexible_disk_changeable_mask`, and never let `m65_test_1581()` consume it.
14. Preserve capture release and transport destruction on every path.
15. The standard parse failure remains the overall diagnose result and nonzero
    exit. A nested valid characterization must not turn Gate 4 into success.

## Exact focused command contract

Build exactly this ten-byte CDB:

```text
5a 00 45 00 00 00 00 00 28 00
```

It means:

- MODE SENSE (10), opcode `0x5a`;
- PC=1 (changeable values);
- page `0x05` (Flexible Disk);
- subpage 0;
- allocation length 40;
- input direction only; and
- control/reserved bytes zero.

The shared CBI engine must pad it to exactly:

```text
5a 00 45 00 00 00 00 00 28 00 00 00
```

Prefer a dedicated builder such as
`m65_cdb_mode_sense_page05_changeable()` rather than exposing a caller-selected
PC/page/subpage/length API. Extend `m65_validate_command()` with only this exact
data-in shape. Keep the current `0x05`/40 and standard `0x7f`/72 shapes and all
other allowlist restrictions intact.

No generic MODE SENSE option, raw CDB entry point, transport shortcut, or
caller-supplied length is authorized.

## Strict response contract

A focused response is valid only when:

- the reported and captured transfer are exactly 40 bytes;
- Mode Data Length is exactly 38 (`0x0026`), declaring 40 total bytes;
- reserved mode-header bytes 4–5 are zero;
- block-descriptor length is zero;
- the only page starts exactly at byte 8;
- page-header reserved bit 6 (`0x40`) is clear;
- the lower six page-code bits are exactly `0x05`;
- page length is exactly 30, making a 32-byte page; and
- the page consumes bytes 8–39 with no prefix, suffix, scan, padding, or
  trailing page.

The defined PS bit may be recorded but must not alter the page-code comparison.
Copy raw bytes into the dedicated report before validating any of these facts.

For short, overlong, malformed, stalled, timed-out, unsupported, check-condition,
or unplugged outcomes, preserve all bytes actually delivered and the exact
transport/SCSI/sense facts, mark the candidate invalid or unavailable, stop,
and release capture. Do not retry with another CDB, page, subpage, or allocation
length.

## Required reporting

Keep every existing standard diagnose field, especially the standard
`mode_sense_changeable_response`, its raw bytes, and overall failure reason.

Add a separate `page05_changeability_characterization` object under the
diagnose UFI body. It must distinguish:

- requested and attempted;
- exact CDB, PC, page, subpage, allocation length, and input direction;
- transport-reported and captured response lengths;
- whether the full allocated response was captured;
- exact raw hex whenever bytes were delivered;
- transport, SCSI, and sense outcome;
- strict validation status and reason;
- required current-to-target delta status and forbidden fields; and
- `mode_select_authorized: false`.

When requested but not attempted, report the exact stop reason. With the flag
absent, preserve ordinary output compatibility and do not emit characterization
data.

Human output must label the standard evidence and Gate 4B evidence separately,
print exact raw bytes and lengths, and state both:

```text
MODE SELECT authorized: no
Gate 6 remains blocked pending review
```

JSON must remain one valid document on stdout, with diagnostics on stderr.

## Absolute safety prohibitions

Do not:

- issue or newly route MODE SELECT (10);
- issue READ (10), even for one boundary sector;
- run `test-1581` against hardware;
- issue FORMAT UNIT, WRITE, WRITE AND VERIFY, erase, or vendor data-out;
- open `/dev/disk*` or `/dev/rdisk*` for reading or writing;
- create a D81 output file;
- add a generic raw command/page interface;
- add permissive parsing, identity skipping, byte scanning, response padding,
  or mask inference;
- change `test-1581` to accept the candidate mask;
- invoke `sudo`, run `diagnose`, `inspect`, `test-1581`, or any other live
  hardware command;
- enable hardware tests;
- change media, privilege, or controller allowlists except for the one exact
  data-in CDB validation shape;
- implement D81 filesystem work or call this Phase 3;
- weaken warnings, sanitizers, timeout recovery, signal cleanup, output-file
  safeguards, or transport identity correlation; or
- commit, push, merge, rebase, force, or open a pull request.

If implementation appears to require any prohibited action or a broader
architecture change, stop and report the conflict instead of improvising.

## Required tests

Write comprehensive tests before claiming readiness.

### UFI and parser

- Exact ten-byte builder output and zero remainder.
- Exact `0x45`/40 data-in allowlist acceptance.
- Rejection of every wrong CDB length, PC, page, subpage, allocation, reserved
  byte, control byte, direction, null buffer, and buffer length.
- Regression acceptance for current `0x05`/40 and standard `0x7f`/72.
- Valid 40-byte focused response with PS clear and with PS set.
- Short lengths and raw preservation at boundary cases.
- Overlong reported/input length.
- Wrong declared length, reserved header, block descriptor, page offset, page
  code, reserved bit 6, page length, prefix, suffix, and trailing page.
- Exact reviewed 72-byte malformed payload remains invalid and is never
  converted into a focused mask.

### Workflow and safety

- Ordinary path never sends `0x45`.
- Opt-in path sends standard `0x7f` first and at most one `0x45` afterwards.
- One acquire and one release on the opted-in workflow.
- No focused request for identity mismatch, valid standard response, earlier
  failure, invalid standard header, short/overlong standard result, stall,
  timeout, unplug, or interruption.
- Valid candidate permitting the actual required delta.
- Valid candidate forbidding at least one changed bit.
- Short, overlong, malformed, stalled, timed-out, unsupported, and unplugged
  candidate cases with cleanup.
- Release failure is reported accurately.
- Fake-transport opcode and direction counters prove zero MODE SELECT, zero
  READ (10), and zero data-out for every characterization case.
- Existing `m65_test_1581()` behavior remains based on the standard mask only.

### CBI, CLI, and output

- Focused CDB is padded to the exact 12-byte CBI block and follows existing
  bulk-IN/completion/REQUEST SENSE rules.
- Existing completion/sense, STALL, timeout, reset, and allowlist tests pass.
- CLI accepts only the documented flag placement and rejects duplicates and
  forbidden combinations.
- JSON and human output cover requested/not-attempted, valid/permitted,
  valid/forbidden, short, overlong, malformed, stall, timeout, and sense cases.
- Raw bytes and both lengths remain visible on validation failure.
- Standard and focused evidence cannot be mislabeled or conflated.
- Capture, release, destroy, result, error, sense, and exit code remain
  consistent.
- Existing output without the flag remains stable.

Add focused test files to the existing unit-test executable if that improves
separation, and register them normally. Do not add a hardware test.

## Non-hardware verification you must run

You are authorized and required to build and run portable/non-hardware tests.
You are not authorized to run the probe against a device.

Run:

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

Debug is the ASan/UBSan verification. Record each command and exit result.
Do not add the module-cache path to repository configuration.

Run Clang static analysis on `src/usb_cbi_iousbhost_macos.m` with the actual
Debug compile arguments from `build/debug/compile_commands.json`, substituting
`--analyze` for compilation while retaining `-fobjc-arc`, `-fmodules`, and the
temporary module-cache environment. Record the exact command and diagnostics.
If the analyzer is unavailable or fails, report that verification item as
unmet.

Audit the final command surface:

```sh
rg -n "MODE SELECT|READ \(10\)|FORMAT UNIT|WRITE|0x04|0x2a|O_WRONLY|O_RDWR|sudo" \
  include src tests docs README.md
```

Interpret all matches. Do not mechanically delete existing safety tests,
documentation, or the separately acknowledged historical `test-1581` code.

Also inspect:

```sh
git status --short
git diff --stat
git diff
git diff --check
```

Confirm the diff contains only the approved characterization implementation,
tests, and accurate documentation. Preserve unrelated work.

## Documentation requirements

Update existing documentation only as needed to describe implemented but
not-yet-hardware-tested behavior. Keep claims exact:

- Gate 3 passed and retains its historical name.
- Gate 4 stopped safely on the malformed standard response.
- Gate 4B is implemented and pending separate hardware authorization.
- Gate 6 remains blocked.
- No MODE SELECT or sector read has been authorized or run.
- No complete 819,200-byte acquisition has been demonstrated.
- Phase 3 has not begun.

Do not edit the hardware-results evidence to imply a new live result. Do not
record build or test passes until you actually obtain them.

## Stop conditions

Stop and report, without committing or pushing, if:

- preflight Git state is unexpected;
- the review or handoff is unavailable;
- the plan conflicts with current source;
- implementation would require a generic command or parser relaxation;
- ordinary `diagnose` cannot remain unchanged;
- the focused result cannot remain isolated from `test-1581`;
- any characterization test observes MODE SELECT, READ (10), or data-out;
- Debug, Release, ASan/UBSan, strict warnings, unit tests, or static analysis
  fail and cannot be fixed within the approved scope;
- cleanup/capture reporting regresses; or
- live hardware, root access, a commit, or a push would be needed to continue.

## Final response

Return a review handoff containing:

1. A clear verdict: ready for code review and separately authorized Gate 4B
   hardware testing, or still blocked.
2. Starting and ending uncommitted HEAD (they should be the same because you
   must not commit).
3. Files changed and the architecture used.
4. Exact CLI and focused CDB implemented.
5. How standard page-`0x3f` evidence remains preserved and separate.
6. Strict validation and raw-byte reporting behavior.
7. Test coverage added.
8. Exact Debug, Release, sanitizer, static-analysis, and `git diff --check`
   commands with exit results.
9. Safety audit results, explicitly confirming zero live hardware commands,
   zero `sudo`, zero MODE SELECT, zero sector READ, and zero media write.
10. Current `git status` and a statement that nothing was committed or pushed.
11. Remaining review and authorization required before any hardware run, MODE
    SELECT, sector acquisition, or Phase 3 work.

Do not execute the future hardware checklist. Do not commit or push your work
unless the owner gives a separate instruction after reviewing the complete
diff and verification evidence.
