#include "m65/probe.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M65_SCSI_STATUS_GOOD 0x00U
#define M65_IO_TIMEOUT_MS 15000U
#define M65_READ_CHUNK_BLOCKS 10U

static volatile sig_atomic_t interrupted = 0;

void m65_probe_request_interrupt(void)
{
    interrupted = 1;
}

void m65_probe_clear_interrupt(void)
{
    interrupted = 0;
}

static void reason_set(char *destination, const char *text)
{
    (void)snprintf(destination, M65_MAX_ERROR_TEXT, "%s", text);
}

static void reason_append(char *destination, const char *text)
{
    char original[M65_MAX_ERROR_TEXT];
    (void)snprintf(original, sizeof(original), "%s", destination);
    (void)snprintf(destination, M65_MAX_ERROR_TEXT, "%s%s%s",
                   original, original[0] != '\0' ? "; " : "", text);
}

static M65ProbeCode code_for_transport(M65TransportStatus status)
{
    switch (status) {
    case M65_TRANSPORT_PERMISSION:
        return M65_PROBE_PERMISSION;
    case M65_TRANSPORT_NO_DEVICE:
        return M65_PROBE_NO_DEVICE;
    case M65_TRANSPORT_OK:
        return M65_PROBE_OK;
    case M65_TRANSPORT_TIMEOUT:
    case M65_TRANSPORT_IO:
    case M65_TRANSPORT_PROTOCOL:
        return M65_PROBE_TRANSPORT;
    }
    return M65_PROBE_TRANSPORT;
}

static M65CommandResult execute_checked(M65Transport *transport, M65Command *command)
{
    M65CommandResult result;
    (void)memset(&result, 0, sizeof(result));
    if (!m65_validate_command(command, result.detail, sizeof(result.detail))) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        return result;
    }
    if (interrupted != 0) {
        result.transport_status = M65_TRANSPORT_IO;
        reason_set(result.detail, "operation interrupted");
        return result;
    }
    return transport->ops->execute(transport, command);
}

static bool command_good(const M65CommandResult *result)
{
    return result->transport_status == M65_TRANSPORT_OK &&
           result->scsi_status == M65_SCSI_STATUS_GOOD;
}

static void copy_command_failure(M65ProbeCode *code, char *reason, M65Sense *sense,
                                 const char *operation, const M65CommandResult *result)
{
    if (result->transport_status != M65_TRANSPORT_OK) {
        *code = code_for_transport(result->transport_status);
        if (result->detail[0] != '\0') {
            (void)snprintf(reason, M65_MAX_ERROR_TEXT, "%s: %s", operation, result->detail);
        } else {
            (void)snprintf(reason, M65_MAX_ERROR_TEXT, "%s: transport failure", operation);
        }
    } else {
        *code = M65_PROBE_TRANSPORT;
        (void)snprintf(reason, M65_MAX_ERROR_TEXT,
                       "%s: SCSI status 0x%02x", operation, result->scsi_status);
    }
    if (result->sense.valid) {
        *sense = result->sense;
    }
}

static M65CommandResult execute_simple(M65Transport *transport, const uint8_t *cdb,
                                       size_t cdb_length, M65DataDirection direction,
                                       void *data, size_t data_length)
{
    M65Command command;
    (void)memset(&command, 0, sizeof(command));
    (void)memcpy(command.cdb, cdb, cdb_length);
    command.cdb_length = cdb_length;
    command.direction = direction;
    command.data = data;
    command.data_length = data_length;
    command.timeout_ms = M65_IO_TIMEOUT_MS;
    return execute_checked(transport, &command);
}

static bool inspect_input(M65Transport *transport, M65InspectReport *report,
                          const char *operation, const uint8_t *cdb, size_t cdb_length,
                          uint8_t *buffer, size_t buffer_size, size_t *transferred)
{
    M65CommandResult result = execute_simple(transport, cdb, cdb_length, M65_DATA_IN,
                                             buffer, buffer_size);
    if (!command_good(&result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             operation, &result);
        return false;
    }
    if (result.transferred > buffer_size) {
        report->code = M65_PROBE_TRANSPORT;
        reason_set(report->reason, "transport reported more bytes than the supplied buffer");
        return false;
    }
    *transferred = result.transferred;
    return true;
}

M65ProbeCode m65_inspect(M65Transport *transport, M65InspectReport *report)
{
    uint8_t cdb[16];
    uint8_t inquiry[M65_UFI_INQUIRY_LENGTH];
    uint8_t capacity[8];
    uint8_t formats[252];
    uint8_t mode[M65_UFI_ALL_MODE_LENGTH];
    size_t transferred = 0U;
    char cleanup_detail[M65_MAX_ERROR_TEXT] = "";
    M65TransportStatus acquire_status;
    bool acquired = false;

    if (transport == NULL || transport->ops == NULL || report == NULL) {
        return M65_PROBE_TRANSPORT;
    }
    (void)memset(report, 0, sizeof(*report));
    report->code = M65_PROBE_TRANSPORT;

    acquire_status = transport->ops->acquire_exclusive(transport, report->reason,
                                                        sizeof(report->reason));
    if (acquire_status != M65_TRANSPORT_OK) {
        report->code = code_for_transport(acquire_status);
        if (report->reason[0] == '\0') {
            reason_set(report->reason, "unable to obtain exclusive SCSI access");
        }
        return report->code;
    }
    acquired = true;
    report->exclusive_acquired = true;

    (void)memset(inquiry, 0, sizeof(inquiry));
    if (!inspect_input(transport, report, "INQUIRY", cdb,
                       m65_cdb_inquiry(cdb, (uint8_t)sizeof(inquiry)),
                       inquiry, sizeof(inquiry), &transferred) ||
        !m65_parse_inquiry(inquiry, transferred, &report->inquiry,
                           report->reason, sizeof(report->reason))) {
        if (report->code == M65_PROBE_OK) {
            report->code = M65_PROBE_TRANSPORT;
        }
        goto cleanup;
    }

    {
        M65CommandResult result = execute_simple(transport, cdb,
            m65_cdb_test_unit_ready(cdb), M65_DATA_NONE, NULL, 0U);
        if (!command_good(&result)) {
            copy_command_failure(&report->code, report->reason, &report->sense,
                                 "TEST UNIT READY", &result);
            goto cleanup;
        }
        report->unit_ready = true;
    }

    (void)memset(capacity, 0, sizeof(capacity));
    if (!inspect_input(transport, report, "READ CAPACITY (10)", cdb,
                       m65_cdb_read_capacity_10(cdb), capacity, sizeof(capacity),
                       &transferred) ||
        !m65_parse_read_capacity_10(capacity, transferred, &report->capacity,
                                    report->reason, sizeof(report->reason))) {
        if (report->code == M65_PROBE_OK) {
            report->code = M65_PROBE_TRANSPORT;
        }
        goto cleanup;
    }

    (void)memset(formats, 0, sizeof(formats));
    if (!inspect_input(transport, report, "READ FORMAT CAPACITIES", cdb,
                       m65_cdb_read_format_capacities(cdb, (uint16_t)sizeof(formats)),
                       formats, sizeof(formats), &transferred) ||
        !m65_parse_format_capacities(formats, transferred, &report->format_capacities,
                                     report->reason, sizeof(report->reason))) {
        if (report->code == M65_PROBE_OK) {
            report->code = M65_PROBE_TRANSPORT;
        }
        goto cleanup;
    }

    (void)memset(mode, 0, sizeof(mode));
    if (!inspect_input(transport, report, "MODE SENSE current", cdb,
                       m65_cdb_mode_sense_10(
                           cdb, false, M65_UFI_FLEX_MODE_LENGTH),
                       mode, M65_UFI_FLEX_MODE_LENGTH, &transferred) ||
        !m65_parse_mode_parameters(mode, transferred, &report->current_mode,
                                   report->reason, sizeof(report->reason))) {
        if (report->code == M65_PROBE_OK) {
            report->code = M65_PROBE_TRANSPORT;
        }
        goto cleanup;
    }

    (void)memset(mode, 0, sizeof(mode));
    if (!inspect_input(transport, report, "MODE SENSE changeable", cdb,
                       m65_cdb_mode_sense_10(
                           cdb, true, M65_UFI_ALL_MODE_LENGTH),
                       mode, M65_UFI_ALL_MODE_LENGTH, &transferred) ||
        !m65_parse_mode_parameters(mode, transferred, &report->changeable_mode,
                                   report->reason, sizeof(report->reason)) ||
        !m65_apply_changeability(&report->current_mode.flexible,
                                 &report->changeable_mode.flexible)) {
        if (report->code == M65_PROBE_OK) {
            report->code = M65_PROBE_TRANSPORT;
        }
        goto cleanup;
    }

    report->code = M65_PROBE_OK;
    reason_set(report->reason, "inspection completed");

cleanup:
    if (acquired) {
        M65TransportStatus release_status =
            transport->ops->release_exclusive(transport, cleanup_detail,
                                               sizeof(cleanup_detail));
        if (release_status != M65_TRANSPORT_OK) {
            if (cleanup_detail[0] == '\0') {
                reason_set(cleanup_detail, "failed to release exclusive SCSI access");
            }
            reason_append(report->reason, cleanup_detail);
            if (report->code == M65_PROBE_OK) {
                report->code = code_for_transport(release_status);
            }
        } else {
            report->exclusive_released = true;
        }
    }
    return report->code;
}

static M65CommandResult mode_sense(M65Transport *transport, bool changeable,
                                   M65ModeParameters *parameters)
{
    uint8_t cdb[16];
    uint8_t buffer[M65_UFI_ALL_MODE_LENGTH];
    M65CommandResult result;
    size_t buffer_length = changeable ? M65_UFI_ALL_MODE_LENGTH :
                                        M65_UFI_FLEX_MODE_LENGTH;
    char parse_detail[M65_MAX_ERROR_TEXT] = "MODE SENSE response exceeds its buffer";
    (void)memset(buffer, 0, sizeof(buffer));
    result = execute_simple(transport, cdb,
                            m65_cdb_mode_sense_10(cdb, changeable,
                                                  (uint16_t)buffer_length),
                            M65_DATA_IN, buffer, buffer_length);
    if (command_good(&result)) {
        if (result.transferred > sizeof(buffer) ||
            !m65_parse_mode_parameters(buffer, result.transferred, parameters,
                                       parse_detail, sizeof(parse_detail))) {
            result.transport_status = M65_TRANSPORT_PROTOCOL;
            reason_set(result.detail, parse_detail);
        }
    }
    return result;
}

static M65CommandResult mode_select(M65Transport *transport,
                                    const M65FlexibleDiskPage *page)
{
    uint8_t cdb[16];
    uint8_t payload[40];
    size_t payload_length = 0U;
    M65CommandResult result;
    (void)memset(&result, 0, sizeof(result));
    if (!m65_build_mode_select_payload(page, payload, sizeof(payload), &payload_length,
                                       result.detail, sizeof(result.detail))) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        return result;
    }
    return execute_simple(transport, cdb,
                          m65_cdb_mode_select_10(cdb, (uint16_t)payload_length),
                          M65_DATA_OUT, payload, payload_length);
}

static M65CommandResult read_blocks(M65Transport *transport, uint32_t lba,
                                    uint16_t blocks, uint8_t *buffer)
{
    uint8_t cdb[16];
    size_t length = (size_t)blocks * (size_t)M65_BLOCK_SIZE;
    M65CommandResult result = execute_simple(transport, cdb,
        m65_cdb_read_10(cdb, lba, blocks), M65_DATA_IN, buffer, length);
    if (command_good(&result) && result.transferred != length) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        (void)snprintf(result.detail, sizeof(result.detail),
                       "short READ (10): expected %zu bytes, received %zu",
                       length, result.transferred);
    }
    return result;
}

static bool read_complete_image(M65Transport *transport, uint8_t *image,
                                M651581Report *report)
{
    uint32_t lba = 0U;
    while (lba < M65_1581_BLOCKS) {
        uint32_t remaining = M65_1581_BLOCKS - lba;
        uint16_t blocks = (uint16_t)(remaining > M65_READ_CHUNK_BLOCKS ?
                                     M65_READ_CHUNK_BLOCKS : remaining);
        M65CommandResult result;
        if (interrupted != 0) {
            report->code = M65_PROBE_TRANSPORT;
            reason_set(report->reason, "operation interrupted");
            return false;
        }
        result = read_blocks(transport, lba, blocks,
                             &image[(size_t)lba * (size_t)M65_BLOCK_SIZE]);
        if (!command_good(&result)) {
            copy_command_failure(&report->code, report->reason, &report->sense,
                                 "complete READ (10)", &result);
            return false;
        }
        lba += (uint32_t)blocks;
    }
    return true;
}

const char *m65_1581_status_text(M651581Status status)
{
    switch (status) {
    case M65_1581_SUPPORTED:
        return "supported";
    case M65_1581_UNSUPPORTED:
        return "unsupported";
    case M65_1581_INCONCLUSIVE:
        return "inconclusive";
    }
    return "inconclusive";
}

M65ProbeCode m65_test_1581(M65Transport *transport, bool acknowledgement,
                           M651581Report *report)
{
    M65ModeParameters original_mode;
    M65ModeParameters changeable_mode;
    M65ModeParameters readback_mode;
    M65CommandResult command_result;
    M65TransportStatus transport_status;
    uint8_t boundary[M65_BLOCK_SIZE];
    uint8_t *second_image = NULL;
    char cleanup_detail[M65_MAX_ERROR_TEXT] = "";
    bool restore_needed = false;

    if (report == NULL) {
        return M65_PROBE_TRANSPORT;
    }
    (void)memset(report, 0, sizeof(*report));
    report->status = M65_1581_INCONCLUSIVE;
    report->code = M65_PROBE_TRANSPORT;
    report->acknowledgement_supplied = acknowledgement;

    if (!acknowledgement) {
        report->code = M65_PROBE_USAGE;
        reason_set(report->reason,
                   "test-1581 requires --ack-temporary-controller-change");
        return report->code;
    }
    if (transport == NULL || transport->ops == NULL) {
        reason_set(report->reason, "no transport was provided");
        return report->code;
    }

    transport_status = transport->ops->acquire_exclusive(transport, report->reason,
                                                          sizeof(report->reason));
    if (transport_status != M65_TRANSPORT_OK) {
        report->code = code_for_transport(transport_status);
        if (report->reason[0] == '\0') {
            reason_set(report->reason, "unable to obtain exclusive SCSI access");
        }
        return report->code;
    }
    report->exclusive_acquired = true;

    command_result = mode_sense(transport, false, &original_mode);
    if (!command_good(&command_result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             "save original MODE SENSE parameters", &command_result);
        goto cleanup;
    }
    report->original_saved = true;
    report->original = original_mode.flexible;

    command_result = mode_sense(transport, true, &changeable_mode);
    if (!command_good(&command_result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             "read changeable MODE SENSE mask", &command_result);
        goto cleanup;
    }
    report->changeable = changeable_mode.flexible;
    (void)m65_apply_changeability(&report->original, &report->changeable);
    report->requested = report->original;
    m65_set_1581_geometry(&report->requested);

    if (!m65_geometry_changes_allowed(&report->original, &report->requested,
                                      &report->changeable, report->reason,
                                      sizeof(report->reason))) {
        report->code = M65_PROBE_UNSUPPORTED;
        report->status = M65_1581_UNSUPPORTED;
        goto cleanup;
    }

    restore_needed = true;
    command_result = mode_select(transport, &report->requested);
    if (!command_good(&command_result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             "MODE SELECT requested 1581 geometry", &command_result);
        if (command_result.transport_status == M65_TRANSPORT_OK) {
            report->code = M65_PROBE_UNSUPPORTED;
            report->status = M65_1581_UNSUPPORTED;
        }
        goto cleanup;
    }
    report->controller_changed = true;

    command_result = mode_sense(transport, false, &readback_mode);
    if (!command_good(&command_result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             "verify MODE SENSE parameters", &command_result);
        goto cleanup;
    }
    report->accepted = readback_mode.flexible;
    if (!m65_geometry_is_1581(&report->accepted)) {
        report->code = M65_PROBE_UNSUPPORTED;
        report->status = M65_1581_UNSUPPORTED;
        reason_set(report->reason,
                   "controller did not accept every requested 1581 geometry field");
        goto cleanup;
    }

    command_result = read_blocks(transport, 9U, 1U, boundary);
    if (!command_good(&command_result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             "probe READ (10) LBA 9", &command_result);
        if (command_result.transport_status == M65_TRANSPORT_OK) {
            report->code = M65_PROBE_UNSUPPORTED;
            report->status = M65_1581_UNSUPPORTED;
        }
        goto cleanup;
    }
    report->boundary_lba_9_read = true;

    command_result = read_blocks(transport, 1599U, 1U, boundary);
    if (!command_good(&command_result)) {
        copy_command_failure(&report->code, report->reason, &report->sense,
                             "probe READ (10) LBA 1599", &command_result);
        if (command_result.transport_status == M65_TRANSPORT_OK) {
            report->code = M65_PROBE_UNSUPPORTED;
            report->status = M65_1581_UNSUPPORTED;
        }
        goto cleanup;
    }
    report->boundary_lba_1599_read = true;

    report->image = (uint8_t *)malloc(M65_1581_IMAGE_SIZE);
    second_image = (uint8_t *)malloc(M65_1581_IMAGE_SIZE);
    if (report->image == NULL || second_image == NULL) {
        report->code = M65_PROBE_TRANSPORT;
        reason_set(report->reason, "unable to allocate two 819200-byte read buffers");
        goto cleanup;
    }
    if (!read_complete_image(transport, report->image, report) ||
        !read_complete_image(transport, second_image, report)) {
        goto cleanup;
    }
    if (memcmp(report->image, second_image, M65_1581_IMAGE_SIZE) != 0) {
        report->code = M65_PROBE_MISMATCH;
        report->status = M65_1581_INCONCLUSIVE;
        reason_set(report->reason, "the two complete 819200-byte reads differed");
        goto cleanup;
    }
    report->repeated_reads_match = true;
    report->image_length = M65_1581_IMAGE_SIZE;
    report->code = M65_PROBE_OK;
    report->status = M65_1581_SUPPORTED;
    reason_set(report->reason,
               "controller accepted 1581 geometry and two complete reads matched");

cleanup:
    free(second_image);
    if (restore_needed && report->original_saved) {
        /* An interrupt stops further reads, but must never suppress restoration. */
        interrupted = 0;
        command_result = mode_select(transport, &original_mode.flexible);
        if (command_good(&command_result)) {
            report->controller_restored = true;
        } else {
            char restore_reason[M65_MAX_ERROR_TEXT];
            if (command_result.detail[0] != '\0') {
                (void)snprintf(restore_reason, sizeof(restore_reason),
                               "failed to restore original controller parameters: %s",
                               command_result.detail);
            } else {
                (void)snprintf(restore_reason, sizeof(restore_reason),
                               "failed to restore original controller parameters (SCSI 0x%02x)",
                               command_result.scsi_status);
            }
            reason_append(report->reason, restore_reason);
            report->code = code_for_transport(command_result.transport_status);
            if (report->code == M65_PROBE_OK) {
                report->code = M65_PROBE_TRANSPORT;
            }
            report->status = M65_1581_INCONCLUSIVE;
            if (command_result.sense.valid) {
                report->sense = command_result.sense;
            }
        }
    } else if (report->original_saved) {
        report->controller_restored = true;
    }

    transport_status = transport->ops->release_exclusive(transport, cleanup_detail,
                                                          sizeof(cleanup_detail));
    if (transport_status != M65_TRANSPORT_OK) {
        if (cleanup_detail[0] == '\0') {
            reason_set(cleanup_detail, "failed to release exclusive SCSI access");
        }
        reason_append(report->reason, cleanup_detail);
        report->code = code_for_transport(transport_status);
        if (report->code == M65_PROBE_OK) {
            report->code = M65_PROBE_TRANSPORT;
        }
        report->status = M65_1581_INCONCLUSIVE;
    } else {
        report->exclusive_released = true;
    }

    if (report->status != M65_1581_SUPPORTED) {
        free(report->image);
        report->image = NULL;
        report->image_length = 0U;
    }

    return report->code;
}

void m65_1581_report_destroy(M651581Report *report)
{
    if (report != NULL) {
        free(report->image);
        report->image = NULL;
        report->image_length = 0U;
    }
}

int m65_diagnose_exit_code(bool transport_created,
                           M65TransportStatus create_status,
                           M65ProbeCode inspect_code)
{
    if (!transport_created) {
        /*
         * The IOUSBHost capture transport could not be created.  A missing
         * device is a distinct, benign outcome (exit 1); every other creation
         * failure (permission, timeout, protocol, IO) is a hard error (exit 2).
         */
        if (create_status == M65_TRANSPORT_NO_DEVICE) {
            return 1;
        }
        return 2;
    }

    switch (inspect_code) {
    case M65_PROBE_OK:
        return 0;
    case M65_PROBE_NO_DEVICE:
        return 1;
    case M65_PROBE_PERMISSION:
        return 2;
    default:
        return 4;
    }
}
