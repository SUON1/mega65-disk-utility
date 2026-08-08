#include "m65/output.h"

#include "m65/json.h"

#include <stdio.h>
#include <string.h>

static void json_identifier(M65Json *json, uint16_t value)
{
    char hexadecimal[7];
    (void)snprintf(hexadecimal, sizeof(hexadecimal), "0x%04x", (unsigned int)value);
    (void)m65_json_begin_object(json);
    (void)m65_json_key(json, "numeric");
    (void)m65_json_uint(json, value);
    (void)m65_json_key(json, "hex");
    (void)m65_json_string(json, hexadecimal);
    (void)m65_json_end_object(json);
}

static void json_device_fields(M65Json *json, const M65DeviceInfo *device)
{
    (void)m65_json_key(json, "bsd_name");
    if (device != NULL && device->bsd_name[0] != '\0') {
        (void)m65_json_string(json, device->bsd_name);
    } else {
        (void)m65_json_null(json);
    }
    (void)m65_json_key(json, "name");
    (void)m65_json_string(json, device != NULL ? device->media_name : "");
    (void)m65_json_key(json, "usb");
    (void)m65_json_begin_object(json);
    (void)m65_json_key(json, "vid");
    json_identifier(json, device != NULL ? device->usb_vid : 0U);
    (void)m65_json_key(json, "pid");
    json_identifier(json, device != NULL ? device->usb_pid : 0U);
    (void)m65_json_key(json, "device_revision");
    json_identifier(json, device != NULL ? device->usb_device_revision : 0U);
    (void)m65_json_key(json, "manufacturer");
    (void)m65_json_string(json, device != NULL ? device->usb_manufacturer : "");
    (void)m65_json_key(json, "product");
    (void)m65_json_string(json, device != NULL ? device->usb_product : "");
    (void)m65_json_key(json, "product_revision");
    (void)m65_json_string(json, device != NULL ? device->product_revision : "");
    (void)m65_json_key(json, "known_controller");
    (void)m65_json_bool(json, device != NULL && device->known_controller);
    (void)m65_json_end_object(json);
}

static void json_media_fields(M65Json *json, const M65DeviceInfo *device)
{
    (void)m65_json_key(json, "present");
    (void)m65_json_bool(json, device != NULL && device->media_present);
    (void)m65_json_key(json, "external");
    (void)m65_json_bool(json, device != NULL && device->external);
    (void)m65_json_key(json, "removable");
    (void)m65_json_bool(json, device != NULL && device->removable);
    (void)m65_json_key(json, "whole");
    (void)m65_json_bool(json, device != NULL && device->whole);
    (void)m65_json_key(json, "mounted");
    (void)m65_json_bool(json, device != NULL && device->mounted);
    (void)m65_json_key(json, "capacity_bytes");
    (void)m65_json_uint(json, device != NULL ? device->capacity_bytes : 0U);
    (void)m65_json_key(json, "block_size");
    (void)m65_json_uint(json, device != NULL ? device->block_size : 0U);
    (void)m65_json_key(json, "permissions");
    (void)m65_json_begin_object(json);
    (void)m65_json_key(json, "raw_path");
    (void)m65_json_string(json, device != NULL ? device->raw_path : "");
    (void)m65_json_key(json, "readable");
    (void)m65_json_bool(json, device != NULL && device->readable);
    (void)m65_json_key(json, "errno");
    (void)m65_json_int(json, device != NULL ? device->permission_errno : 0);
    (void)m65_json_end_object(json);
}

static void json_sense(M65Json *json, const M65Sense *sense)
{
    (void)m65_json_begin_object(json);
    (void)m65_json_key(json, "valid");
    (void)m65_json_bool(json, sense != NULL && sense->valid);
    (void)m65_json_key(json, "sense_key");
    (void)m65_json_uint(json, sense != NULL ? sense->key : 0U);
    (void)m65_json_key(json, "asc");
    (void)m65_json_uint(json, sense != NULL ? sense->asc : 0U);
    (void)m65_json_key(json, "ascq");
    (void)m65_json_uint(json, sense != NULL ? sense->ascq : 0U);
    (void)m65_json_end_object(json);
}

static void json_flexible(M65Json *json, const M65FlexibleDiskPage *page,
                          bool include_changeable)
{
    size_t field;
    (void)m65_json_begin_object(json);
    (void)m65_json_key(json, "transfer_rate_kbit_s");
    (void)m65_json_uint(json, page->transfer_rate_kbit);
    (void)m65_json_key(json, "heads");
    (void)m65_json_uint(json, page->heads);
    (void)m65_json_key(json, "sectors_per_track");
    (void)m65_json_uint(json, page->sectors_per_track);
    (void)m65_json_key(json, "bytes_per_sector");
    (void)m65_json_uint(json, page->bytes_per_sector);
    (void)m65_json_key(json, "cylinders");
    (void)m65_json_uint(json, page->cylinders);
    (void)m65_json_key(json, "medium_rotation_rate_rpm");
    (void)m65_json_uint(json, page->medium_rotation_rate_rpm);
    if (include_changeable) {
        (void)m65_json_key(json, "changeable_fields");
        (void)m65_json_begin_array(json);
        for (field = 0U; field < (size_t)M65_FLEX_FIELD_COUNT; ++field) {
            if (page->field_changeable[field]) {
                (void)m65_json_string(json,
                    m65_flexible_field_name((M65FlexibleField)field));
            }
        }
        (void)m65_json_end_array(json);
    }
    (void)m65_json_end_object(json);
}

static void json_errors(M65Json *json, const char *error, const M65Sense *sense)
{
    (void)m65_json_key(json, "errors");
    (void)m65_json_begin_array(json);
    if (error != NULL && error[0] != '\0') {
        (void)m65_json_begin_object(json);
        (void)m65_json_key(json, "message");
        (void)m65_json_string(json, error);
        if (sense != NULL && sense->valid) {
            (void)m65_json_key(json, "sense");
            json_sense(json, sense);
        }
        (void)m65_json_end_object(json);
    }
    (void)m65_json_end_array(json);
}

static bool json_emit(M65Json *json)
{
    bool valid = m65_json_finish(json);
    if (valid) {
        (void)fputs(m65_json_data(json), stdout);
        (void)fputc('\n', stdout);
    } else {
        (void)fprintf(stderr, "internal error: failed to construct JSON output\n");
    }
    m65_json_destroy(json);
    return valid;
}

static bool json_top(M65Json *json, const char *command)
{
    if (!m65_json_init(json)) {
        return false;
    }
    if (!m65_json_begin_object(json) ||
        !m65_json_key(json, "schema_version") ||
        !m65_json_uint(json, M65_SCHEMA_VERSION) ||
        !m65_json_key(json, "command") ||
        !m65_json_begin_object(json) ||
        !m65_json_key(json, "name") ||
        !m65_json_string(json, command) ||
        !m65_json_end_object(json)) {
        m65_json_destroy(json);
        return false;
    }
    return true;
}

bool m65_output_list_json(const M65DeviceList *list, const char *error)
{
    M65Json json;
    size_t index;
    if (!json_top(&json, "list")) {
        return false;
    }
    (void)m65_json_key(&json, "device");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "candidates");
    (void)m65_json_begin_array(&json);
    for (index = 0U; list != NULL && index < list->count; ++index) {
        (void)m65_json_begin_object(&json);
        json_device_fields(&json, &list->items[index]);
        (void)m65_json_key(&json, "media");
        (void)m65_json_begin_object(&json);
        json_media_fields(&json, &list->items[index]);
        (void)m65_json_end_object(&json);
        (void)m65_json_end_object(&json);
    }
    (void)m65_json_end_array(&json);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "media");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "candidate_count");
    (void)m65_json_uint(&json, list != NULL ? list->count : 0U);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "ufi");
    (void)m65_json_begin_object(&json);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "result");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "status");
    (void)m65_json_string(&json, error == NULL ? "ok" : "error");
    (void)m65_json_key(&json, "reason");
    (void)m65_json_string(&json, error == NULL ? "enumeration completed" : error);
    (void)m65_json_end_object(&json);
    json_errors(&json, error, NULL);
    (void)m65_json_end_object(&json);
    return json_emit(&json);
}

bool m65_output_error_json(const char *command, const M65DeviceInfo *device,
                           const char *error)
{
    M65Json json;
    if (!json_top(&json, command != NULL ? command : "invalid")) {
        return false;
    }
    (void)m65_json_key(&json, "device");
    (void)m65_json_begin_object(&json);
    json_device_fields(&json, device);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "media");
    (void)m65_json_begin_object(&json);
    json_media_fields(&json, device);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "ufi");
    (void)m65_json_begin_object(&json);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "result");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "status");
    (void)m65_json_string(&json, "error");
    (void)m65_json_key(&json, "reason");
    (void)m65_json_string(&json, error != NULL ? error : "unknown error");
    (void)m65_json_end_object(&json);
    json_errors(&json, error, NULL);
    (void)m65_json_end_object(&json);
    return json_emit(&json);
}

void m65_output_list_human(const M65DeviceList *list, const char *error)
{
    size_t index;
    if (error != NULL) {
        (void)printf("Result: %s\n", error);
    }
    if (list == NULL || list->count == 0U) {
        (void)printf("No matching TEAC USB floppy device found.\n");
        return;
    }
    for (index = 0U; index < list->count; ++index) {
        const M65DeviceInfo *device = &list->items[index];
        (void)printf(
            "%s\n"
            "  Name: %s\n"
            "  USB: %s / %s, VID 0x%04x (%u), PID 0x%04x (%u), revision %s\n"
            "  Media: %s, %llu bytes, %u-byte blocks, %s, %s, %s, %s\n"
            "  Permissions: %s (%s)\n",
            device->bsd_name,
            device->media_name,
            device->usb_manufacturer, device->usb_product,
            (unsigned int)device->usb_vid, (unsigned int)device->usb_vid,
            (unsigned int)device->usb_pid, (unsigned int)device->usb_pid,
            device->product_revision,
            device->media_present ? "present" : "not present",
            (unsigned long long)device->capacity_bytes,
            (unsigned int)device->block_size,
            device->external ? "external" : "internal",
            device->removable ? "removable" : "not removable",
            device->whole ? "whole" : "not whole",
            device->mounted ? "mounted" : "unmounted",
            device->readable ? "readable" : "permission denied",
            device->raw_path);
    }
}

bool m65_output_inspect_json(const M65DeviceInfo *device,
                             const M65InspectReport *report)
{
    M65Json json;
    size_t index;
    const char *error = report->code == M65_PROBE_OK ? NULL : report->reason;
    if (!json_top(&json, "inspect")) {
        return false;
    }
    (void)m65_json_key(&json, "device");
    (void)m65_json_begin_object(&json);
    json_device_fields(&json, device);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "media");
    (void)m65_json_begin_object(&json);
    json_media_fields(&json, device);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "ufi");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "inquiry");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "vendor");
    (void)m65_json_string(&json, report->inquiry.vendor);
    (void)m65_json_key(&json, "product");
    (void)m65_json_string(&json, report->inquiry.product);
    (void)m65_json_key(&json, "firmware");
    (void)m65_json_string(&json, report->inquiry.firmware);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "test_unit_ready");
    (void)m65_json_bool(&json, report->unit_ready);
    (void)m65_json_key(&json, "capacity");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "blocks");
    (void)m65_json_uint(&json, report->capacity.blocks);
    (void)m65_json_key(&json, "block_size");
    (void)m65_json_uint(&json, report->capacity.block_size);
    (void)m65_json_key(&json, "bytes");
    (void)m65_json_uint(&json, (uint64_t)report->capacity.blocks *
                               (uint64_t)report->capacity.block_size);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "format_capacities");
    (void)m65_json_begin_array(&json);
    for (index = 0U; index < report->format_capacities.count; ++index) {
        const M65FormatCapacityDescriptor *descriptor =
            &report->format_capacities.descriptors[index];
        (void)m65_json_begin_object(&json);
        (void)m65_json_key(&json, "blocks");
        (void)m65_json_uint(&json, descriptor->blocks);
        (void)m65_json_key(&json, "block_size");
        (void)m65_json_uint(&json, descriptor->block_size);
        (void)m65_json_key(&json, "descriptor_code");
        (void)m65_json_uint(&json, descriptor->descriptor_code);
        (void)m65_json_end_object(&json);
    }
    (void)m65_json_end_array(&json);
    (void)m65_json_key(&json, "flexible_disk");
    json_flexible(&json, &report->current_mode.flexible, true);
    (void)m65_json_key(&json, "sense");
    json_sense(&json, &report->sense);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "result");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "status");
    (void)m65_json_string(&json, report->code == M65_PROBE_OK ? "ok" : "error");
    (void)m65_json_key(&json, "reason");
    (void)m65_json_string(&json, report->reason);
    (void)m65_json_end_object(&json);
    json_errors(&json, error, &report->sense);
    (void)m65_json_end_object(&json);
    return json_emit(&json);
}

void m65_output_inspect_human(const M65DeviceInfo *device,
                              const M65InspectReport *report)
{
    size_t index;
    size_t field;
    (void)printf("Device: %s\n", device->bsd_name);
    (void)printf("INQUIRY: vendor \"%s\", product \"%s\", firmware \"%s\"\n",
                 report->inquiry.vendor, report->inquiry.product,
                 report->inquiry.firmware);
    (void)printf("TEST UNIT READY: %s\n", report->unit_ready ? "ready" : "not ready");
    (void)printf("Capacity: %u blocks x %u bytes = %llu bytes\n",
                 (unsigned int)report->capacity.blocks,
                 (unsigned int)report->capacity.block_size,
                 (unsigned long long)report->capacity.blocks *
                 (unsigned long long)report->capacity.block_size);
    (void)printf("Advertised format capacities:\n");
    for (index = 0U; index < report->format_capacities.count; ++index) {
        const M65FormatCapacityDescriptor *descriptor =
            &report->format_capacities.descriptors[index];
        (void)printf("  %u blocks x %u bytes (descriptor code %u)\n",
                     (unsigned int)descriptor->blocks,
                     (unsigned int)descriptor->block_size,
                     (unsigned int)descriptor->descriptor_code);
    }
    (void)printf(
        "Flexible Disk: %u kbit/s, %u heads, %u sectors/track, "
        "%u bytes/sector, %u cylinders, %u RPM\n",
        (unsigned int)report->current_mode.flexible.transfer_rate_kbit,
        (unsigned int)report->current_mode.flexible.heads,
        (unsigned int)report->current_mode.flexible.sectors_per_track,
        (unsigned int)report->current_mode.flexible.bytes_per_sector,
        (unsigned int)report->current_mode.flexible.cylinders,
        (unsigned int)report->current_mode.flexible.medium_rotation_rate_rpm);
    (void)printf("Changeable Flexible Disk fields:");
    for (field = 0U; field < (size_t)M65_FLEX_FIELD_COUNT; ++field) {
        if (report->current_mode.flexible.field_changeable[field]) {
            (void)printf(" %s", m65_flexible_field_name((M65FlexibleField)field));
        }
    }
    (void)printf("\nResult: %s\n", report->reason);
    if (report->sense.valid) {
        (void)printf("Sense: key 0x%02x, ASC 0x%02x, ASCQ 0x%02x\n",
                     report->sense.key, report->sense.asc, report->sense.ascq);
    }
}

bool m65_output_1581_json(const M65DeviceInfo *device,
                          const M651581Report *report,
                          const char *output_path)
{
    M65Json json;
    const char *error = report->code == M65_PROBE_OK ? NULL : report->reason;
    if (!json_top(&json, "test-1581")) {
        return false;
    }
    (void)m65_json_key(&json, "device");
    (void)m65_json_begin_object(&json);
    json_device_fields(&json, device);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "media");
    (void)m65_json_begin_object(&json);
    json_media_fields(&json, device);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "ufi");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "original_flexible_disk");
    json_flexible(&json, &report->original, true);
    (void)m65_json_key(&json, "requested_flexible_disk");
    json_flexible(&json, &report->requested, false);
    (void)m65_json_key(&json, "accepted_flexible_disk");
    json_flexible(&json, &report->accepted, false);
    (void)m65_json_key(&json, "sense");
    json_sense(&json, &report->sense);
    (void)m65_json_end_object(&json);
    (void)m65_json_key(&json, "result");
    (void)m65_json_begin_object(&json);
    (void)m65_json_key(&json, "status");
    (void)m65_json_string(&json, m65_1581_status_text(report->status));
    (void)m65_json_key(&json, "reason");
    (void)m65_json_string(&json, report->reason);
    (void)m65_json_key(&json, "acknowledgement_supplied");
    (void)m65_json_bool(&json, report->acknowledgement_supplied);
    (void)m65_json_key(&json, "original_parameters_saved");
    (void)m65_json_bool(&json, report->original_saved);
    (void)m65_json_key(&json, "controller_restored");
    (void)m65_json_bool(&json, report->controller_restored);
    (void)m65_json_key(&json, "boundary_lba_9_read");
    (void)m65_json_bool(&json, report->boundary_lba_9_read);
    (void)m65_json_key(&json, "boundary_lba_1599_read");
    (void)m65_json_bool(&json, report->boundary_lba_1599_read);
    (void)m65_json_key(&json, "repeated_reads_match");
    (void)m65_json_bool(&json, report->repeated_reads_match);
    (void)m65_json_key(&json, "output");
    if (output_path != NULL && report->code == M65_PROBE_OK) {
        (void)m65_json_string(&json, output_path);
    } else {
        (void)m65_json_null(&json);
    }
    (void)m65_json_end_object(&json);
    json_errors(&json, error, &report->sense);
    (void)m65_json_end_object(&json);
    return json_emit(&json);
}

void m65_output_1581_human(const M65DeviceInfo *device,
                           const M651581Report *report,
                           const char *output_path)
{
    (void)printf("Device: %s\n", device->bsd_name);
    (void)printf("1581 geometry result: %s\n", m65_1581_status_text(report->status));
    (void)printf("Reason: %s\n", report->reason);
    (void)printf("Original parameters saved: %s\n",
                 report->original_saved ? "yes" : "no");
    (void)printf("Original parameters restored: %s\n",
                 report->controller_restored ? "yes" : "no");
    (void)printf("Boundary reads: LBA 9 %s, LBA 1599 %s\n",
                 report->boundary_lba_9_read ? "read" : "not read",
                 report->boundary_lba_1599_read ? "read" : "not read");
    (void)printf("Repeated complete reads: %s\n",
                 report->repeated_reads_match ? "matched" : "did not match/not completed");
    if (output_path != NULL && report->code == M65_PROBE_OK) {
        (void)printf("New image written: %s (%zu bytes)\n",
                     output_path, report->image_length);
    }
    if (report->sense.valid) {
        (void)printf("Sense: key 0x%02x, ASC 0x%02x, ASCQ 0x%02x\n",
                     report->sense.key, report->sense.asc, report->sense.ascq);
    }
}
