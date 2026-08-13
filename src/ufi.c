#include "m65/ufi.h"

#include "m65/bytes.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    size_t offset;
    size_t length;
    const char *name;
} FlexibleFieldInfo;

static const FlexibleFieldInfo flexible_fields[M65_FLEX_FIELD_COUNT] = {
    {2U, 2U, "transfer_rate"},
    {4U, 1U, "heads"},
    {5U, 1U, "sectors_per_track"},
    {6U, 2U, "bytes_per_sector"},
    {8U, 2U, "cylinders"},
    {10U, 2U, "write_precompensation_start"},
    {12U, 2U, "reduced_write_current_start"},
    {14U, 2U, "step_rate"},
    {16U, 1U, "step_pulse_width"},
    {17U, 2U, "head_settle_delay"},
    {19U, 1U, "motor_on_delay"},
    {20U, 1U, "motor_off_delay"},
    {21U, 1U, "trdy_ssn_mo"},
    {22U, 1U, "spc"},
    {23U, 1U, "write_compensation"},
    {24U, 1U, "head_load_delay"},
    {25U, 1U, "head_unload_delay"},
    {26U, 1U, "pin_34_pin_2"},
    {27U, 1U, "pin_4_pin_1"},
    {28U, 2U, "medium_rotation_rate"}
};

static void set_detail(char *detail, size_t detail_size, const char *text)
{
    if (detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size, "%s", text);
    }
}

static void cdb_clear(uint8_t cdb[16])
{
    (void)memset(cdb, 0, 16U);
}

const char *m65_flexible_field_name(M65FlexibleField field)
{
    if ((size_t)field >= (size_t)M65_FLEX_FIELD_COUNT) {
        return "unknown";
    }
    return flexible_fields[field].name;
}

size_t m65_cdb_test_unit_ready(uint8_t cdb[16])
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_TEST_UNIT_READY;
    return 6U;
}

size_t m65_cdb_request_sense(uint8_t cdb[16], uint8_t allocation_length)
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_REQUEST_SENSE;
    cdb[4] = allocation_length;
    return 6U;
}

size_t m65_cdb_inquiry(uint8_t cdb[16], uint8_t allocation_length)
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_INQUIRY;
    cdb[4] = allocation_length;
    return 6U;
}

size_t m65_cdb_read_capacity_10(uint8_t cdb[16])
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_READ_CAPACITY_10;
    return 10U;
}

size_t m65_cdb_read_format_capacities(uint8_t cdb[16], uint16_t allocation_length)
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_READ_FORMAT_CAPACITIES;
    m65_write_be16(&cdb[7], allocation_length);
    return 10U;
}

size_t m65_cdb_mode_sense_10(uint8_t cdb[16], bool changeable, uint16_t allocation_length)
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_MODE_SENSE_10;
    cdb[2] = changeable ? 0x7fU : M65_FLEXIBLE_DISK_PAGE;
    m65_write_be16(&cdb[7], allocation_length);
    return 10U;
}

size_t m65_cdb_mode_select_10(uint8_t cdb[16], uint16_t parameter_length)
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_MODE_SELECT_10;
    cdb[1] = 0x10U;
    m65_write_be16(&cdb[7], parameter_length);
    return 10U;
}

size_t m65_cdb_read_10(uint8_t cdb[16], uint32_t lba, uint16_t blocks)
{
    cdb_clear(cdb);
    cdb[0] = M65_OPCODE_READ_10;
    m65_write_be32(&cdb[2], lba);
    m65_write_be16(&cdb[7], blocks);
    return 10U;
}

static void trim_copy(char *destination, size_t destination_size,
                      const uint8_t *source, size_t source_size)
{
    size_t used = source_size;
    while (used > 0U && (source[used - 1U] == (uint8_t)' ' || source[used - 1U] == 0U)) {
        --used;
    }
    if (used >= destination_size) {
        used = destination_size - 1U;
    }
    (void)memcpy(destination, source, used);
    destination[used] = '\0';
}

bool m65_parse_inquiry(const uint8_t *data, size_t length, M65Inquiry *out,
                       char *detail, size_t detail_size)
{
    if (data == NULL || out == NULL || length < 36U) {
        set_detail(detail, detail_size, "INQUIRY response is shorter than 36 bytes");
        return false;
    }
    (void)memset(out, 0, sizeof(*out));
    trim_copy(out->vendor, sizeof(out->vendor), &data[8], 8U);
    trim_copy(out->product, sizeof(out->product), &data[16], 16U);
    trim_copy(out->firmware, sizeof(out->firmware), &data[32], 4U);
    return true;
}

bool m65_parse_sense(const uint8_t *data, size_t length, M65Sense *out)
{
    uint8_t code;
    if (data == NULL || out == NULL || length == 0U) {
        return false;
    }
    (void)memset(out, 0, sizeof(*out));
    code = (uint8_t)(data[0] & 0x7fU);
    out->response_code = code;
    if ((code == 0x70U || code == 0x71U) && length >= 14U) {
        out->key = (uint8_t)(data[2] & 0x0fU);
        out->asc = data[12];
        out->ascq = data[13];
        out->valid = true;
        return true;
    }
    if ((code == 0x72U || code == 0x73U) && length >= 4U) {
        out->key = (uint8_t)(data[1] & 0x0fU);
        out->asc = data[2];
        out->ascq = data[3];
        out->valid = true;
        return true;
    }
    return false;
}

bool m65_parse_read_capacity_10(const uint8_t *data, size_t length, M65Capacity *out,
                                char *detail, size_t detail_size)
{
    uint32_t last_lba;
    uint32_t block_size;
    if (out == NULL || !m65_read_be32(data, length, 0U, &last_lba) ||
        !m65_read_be32(data, length, 4U, &block_size)) {
        set_detail(detail, detail_size, "READ CAPACITY (10) response is shorter than 8 bytes");
        return false;
    }
    if (last_lba == UINT32_MAX) {
        set_detail(detail, detail_size, "READ CAPACITY (10) uses the unsupported 0xffffffff sentinel");
        return false;
    }
    out->blocks = last_lba + 1U;
    out->block_size = block_size;
    return true;
}

bool m65_parse_format_capacities(const uint8_t *data, size_t length,
                                 M65FormatCapacities *out, char *detail, size_t detail_size)
{
    size_t descriptor_bytes;
    size_t offset;
    if (data == NULL || out == NULL || length < 4U) {
        set_detail(detail, detail_size, "READ FORMAT CAPACITIES response is shorter than 4 bytes");
        return false;
    }
    descriptor_bytes = (size_t)data[3];
    if ((descriptor_bytes % 8U) != 0U) {
        set_detail(detail, detail_size, "format-capacity list length is not a multiple of 8");
        return false;
    }
    if (descriptor_bytes > length - 4U) {
        set_detail(detail, detail_size, "format-capacity descriptor list is truncated");
        return false;
    }
    if (descriptor_bytes / 8U > M65_MAX_FORMAT_DESCRIPTORS) {
        set_detail(detail, detail_size, "too many format-capacity descriptors");
        return false;
    }
    (void)memset(out, 0, sizeof(*out));
    for (offset = 4U; offset < 4U + descriptor_bytes; offset += 8U) {
        M65FormatCapacityDescriptor *descriptor = &out->descriptors[out->count];
        if (!m65_read_be32(data, length, offset, &descriptor->blocks) ||
            !m65_read_be24(data, length, offset + 5U, &descriptor->block_size)) {
            set_detail(detail, detail_size, "format-capacity descriptor is truncated");
            return false;
        }
        descriptor->descriptor_code = (uint8_t)(data[offset + 4U] & 0x03U);
        ++out->count;
    }
    return true;
}

static bool decode_flexible(const uint8_t *page, size_t length,
                            M65FlexibleDiskPage *out, char *detail, size_t detail_size)
{
    if (length < M65_FLEX_PAGE_LENGTH) {
        set_detail(detail, detail_size, "Flexible Disk page is shorter than 32 bytes");
        return false;
    }
    if ((page[0] & 0x3fU) != M65_FLEXIBLE_DISK_PAGE || (page[0] & 0x40U) != 0U) {
        set_detail(detail, detail_size,
                   "response does not contain a valid UFI Flexible Disk page");
        return false;
    }
    if (page[1] < 30U || (size_t)page[1] + 2U > length) {
        set_detail(detail, detail_size, "Flexible Disk page length is malformed");
        return false;
    }
    (void)memset(out, 0, sizeof(*out));
    (void)memcpy(out->page, page, M65_FLEX_PAGE_LENGTH);
    (void)m65_read_be16(page, length, 2U, &out->transfer_rate_kbit);
    out->heads = page[4];
    out->sectors_per_track = page[5];
    (void)m65_read_be16(page, length, 6U, &out->bytes_per_sector);
    (void)m65_read_be16(page, length, 8U, &out->cylinders);
    (void)m65_read_be16(page, length, 28U, &out->medium_rotation_rate_rpm);
    return true;
}

bool m65_parse_mode_parameters(const uint8_t *data, size_t length,
                               M65ModeParameters *out, char *detail, size_t detail_size)
{
    uint16_t mode_data_length;
    uint16_t block_descriptor_length;
    size_t declared_length;
    size_t offset;
    if (data == NULL || out == NULL || length < 8U ||
        !m65_read_be16(data, length, 0U, &mode_data_length) ||
        !m65_read_be16(data, length, 6U, &block_descriptor_length)) {
        set_detail(detail, detail_size, "MODE SENSE (10) response is shorter than its header");
        return false;
    }
    declared_length = (size_t)mode_data_length + 2U;
    if (declared_length < 8U || declared_length > length || declared_length > M65_MODE_BUFFER_SIZE) {
        set_detail(detail, detail_size, "MODE SENSE (10) declared length is truncated or too large");
        return false;
    }
    offset = 8U + (size_t)block_descriptor_length;
    if (offset > declared_length) {
        set_detail(detail, detail_size, "MODE SENSE block descriptor length exceeds the response");
        return false;
    }
    (void)memset(out, 0, sizeof(*out));
    (void)memcpy(out->raw, data, declared_length);
    out->raw_length = declared_length;
    while (offset < declared_length) {
        size_t page_length;
        if (declared_length - offset < 2U) {
            set_detail(detail, detail_size, "MODE SENSE page header is truncated");
            return false;
        }
        if ((data[offset] & 0x40U) != 0U) {
            if (detail != NULL && detail_size > 0U) {
                (void)snprintf(detail, detail_size,
                               "MODE SENSE UFI page header 0x%02x at byte %zu "
                               "has reserved bit 6 set",
                               (unsigned int)data[offset], offset);
            }
            return false;
        }
        page_length = (size_t)data[offset + 1U] + 2U;
        if (page_length > declared_length - offset) {
            set_detail(detail, detail_size, "MODE SENSE page data is truncated");
            return false;
        }
        if ((data[offset] & 0x3fU) == M65_FLEXIBLE_DISK_PAGE) {
            out->page_offset = offset;
            return decode_flexible(&data[offset], page_length, &out->flexible,
                                   detail, detail_size);
        }
        offset += page_length;
    }
    set_detail(detail, detail_size, "MODE SENSE response has no Flexible Disk page");
    return false;
}

bool m65_apply_changeability(M65FlexibleDiskPage *current,
                             const M65FlexibleDiskPage *mask)
{
    size_t field;
    if (current == NULL || mask == NULL) {
        return false;
    }
    for (field = 0U; field < (size_t)M65_FLEX_FIELD_COUNT; ++field) {
        size_t byte_index;
        const FlexibleFieldInfo *info = &flexible_fields[field];
        current->field_changeable[field] = false;
        for (byte_index = 0U; byte_index < info->length; ++byte_index) {
            if (mask->page[info->offset + byte_index] != 0U) {
                current->field_changeable[field] = true;
            }
        }
    }
    return true;
}

void m65_set_1581_geometry(M65FlexibleDiskPage *page)
{
    m65_write_be16(&page->page[2], 250U);
    page->page[4] = 2U;
    page->page[5] = 10U;
    m65_write_be16(&page->page[6], 512U);
    m65_write_be16(&page->page[8], 80U);
    m65_write_be16(&page->page[28], 300U);
    page->transfer_rate_kbit = 250U;
    page->heads = 2U;
    page->sectors_per_track = 10U;
    page->bytes_per_sector = 512U;
    page->cylinders = 80U;
    page->medium_rotation_rate_rpm = 300U;
}

bool m65_geometry_is_1581(const M65FlexibleDiskPage *page)
{
    return page != NULL && page->transfer_rate_kbit == 250U && page->heads == 2U &&
           page->sectors_per_track == 10U && page->bytes_per_sector == 512U &&
           page->cylinders == 80U && page->medium_rotation_rate_rpm == 300U;
}

bool m65_geometry_changes_allowed(const M65FlexibleDiskPage *current,
                                  const M65FlexibleDiskPage *desired,
                                  const M65FlexibleDiskPage *mask,
                                  char *detail, size_t detail_size)
{
    static const M65FlexibleField required[] = {
        M65_FLEX_TRANSFER_RATE, M65_FLEX_HEADS, M65_FLEX_SECTORS_PER_TRACK,
        M65_FLEX_BYTES_PER_SECTOR, M65_FLEX_CYLINDERS, M65_FLEX_ROTATION_RATE
    };
    size_t index;
    if (current == NULL || desired == NULL || mask == NULL) {
        set_detail(detail, detail_size, "missing Flexible Disk page");
        return false;
    }
    for (index = 0U; index < sizeof(required) / sizeof(required[0]); ++index) {
        const FlexibleFieldInfo *info = &flexible_fields[required[index]];
        size_t byte_index;
        for (byte_index = 0U; byte_index < info->length; ++byte_index) {
            size_t offset = info->offset + byte_index;
            uint8_t changed = (uint8_t)(current->page[offset] ^ desired->page[offset]);
            if ((uint8_t)(changed & (uint8_t)~mask->page[offset]) != 0U) {
                if (detail != NULL && detail_size > 0U) {
                    (void)snprintf(detail, detail_size,
                                   "controller mask does not permit requested %s change",
                                   info->name);
                }
                return false;
            }
        }
    }
    return true;
}

bool m65_build_mode_select_payload(const M65FlexibleDiskPage *page,
                                   uint8_t *data, size_t capacity, size_t *length,
                                   char *detail, size_t detail_size)
{
    const size_t required = 8U + M65_FLEX_PAGE_LENGTH;
    if (page == NULL || data == NULL || length == NULL || capacity < required) {
        set_detail(detail, detail_size, "MODE SELECT buffer is too small");
        return false;
    }
    if ((page->page[0] & 0x3fU) != M65_FLEXIBLE_DISK_PAGE ||
        (page->page[0] & 0x40U) != 0U || page->page[1] != 30U) {
        set_detail(detail, detail_size, "MODE SELECT payload is not exactly Flexible Disk page 0x05");
        return false;
    }
    (void)memset(data, 0, required);
    (void)memcpy(&data[8], page->page, M65_FLEX_PAGE_LENGTH);
    data[8] = (uint8_t)(data[8] & 0x3fU);
    *length = required;
    return true;
}

static bool validate_data(const M65Command *command, M65DataDirection direction,
                          char *detail, size_t detail_size)
{
    if (command->direction != direction) {
        set_detail(detail, detail_size, "opcode has an invalid data direction");
        return false;
    }
    if (direction == M65_DATA_NONE) {
        if (command->data != NULL || command->data_length != 0U) {
            set_detail(detail, detail_size, "no-data opcode was given a data buffer");
            return false;
        }
    } else if (command->data == NULL || command->data_length == 0U) {
        set_detail(detail, detail_size, "data-transfer opcode has no data buffer");
        return false;
    }
    return true;
}

bool m65_validate_command(const M65Command *command, char *detail, size_t detail_size)
{
    uint16_t encoded_length;
    if (detail != NULL && detail_size > 0U) {
        detail[0] = '\0';
    }
    if (command == NULL || command->cdb_length == 0U) {
        set_detail(detail, detail_size, "missing command descriptor block");
        return false;
    }
    switch (command->cdb[0]) {
    case M65_OPCODE_TEST_UNIT_READY:
        return command->cdb_length == 6U &&
               validate_data(command, M65_DATA_NONE, detail, detail_size);
    case M65_OPCODE_REQUEST_SENSE:
        if (command->cdb_length != 6U ||
            !validate_data(command, M65_DATA_IN, detail, detail_size) ||
            command->cdb[1] != 0U || command->cdb[2] != 0U ||
            command->cdb[3] != 0U || command->cdb[5] != 0U ||
            command->cdb[4] == 0U ||
            command->data_length != (size_t)command->cdb[4]) {
            set_detail(detail, detail_size,
                       "REQUEST SENSE length or reserved fields are invalid");
            return false;
        }
        return true;
    case M65_OPCODE_INQUIRY:
        if (command->cdb_length != 6U ||
            !validate_data(command, M65_DATA_IN, detail, detail_size) ||
            command->cdb[1] != 0U || command->cdb[2] != 0U ||
            command->cdb[3] != 0U || command->cdb[5] != 0U ||
            command->cdb[4] != M65_UFI_INQUIRY_LENGTH ||
            command->data_length != M65_UFI_INQUIRY_LENGTH) {
            set_detail(detail, detail_size,
                       "INQUIRY must request the 36-byte standard UFI response");
            return false;
        }
        return true;
    case M65_OPCODE_READ_FORMAT_CAPACITIES:
    case M65_OPCODE_READ_CAPACITY_10:
        return command->cdb_length == 10U &&
               validate_data(command, M65_DATA_IN, detail, detail_size);
    case M65_OPCODE_MODE_SENSE_10:
        if (command->cdb_length != 10U ||
            !validate_data(command, M65_DATA_IN, detail, detail_size) ||
            command->cdb[1] != 0U || command->cdb[3] != 0U ||
            command->cdb[4] != 0U || command->cdb[5] != 0U ||
            command->cdb[6] != 0U || command->cdb[9] != 0U ||
            !m65_read_be16(command->cdb, command->cdb_length, 7U,
                           &encoded_length)) {
            set_detail(detail, detail_size,
                       "MODE SENSE (10) reserved fields are invalid");
            return false;
        }
        if (command->cdb[2] == M65_FLEXIBLE_DISK_PAGE &&
            encoded_length == M65_UFI_FLEX_MODE_LENGTH &&
            command->data_length == M65_UFI_FLEX_MODE_LENGTH) {
            return true;
        }
        if (command->cdb[2] == 0x7fU &&
            encoded_length == M65_UFI_ALL_MODE_LENGTH &&
            command->data_length == M65_UFI_ALL_MODE_LENGTH) {
            return true;
        }
        set_detail(detail, detail_size,
                   "MODE SENSE (10) page and allocation length are invalid");
        return false;
    case M65_OPCODE_READ_10:
        if (command->cdb_length != 10U ||
            !validate_data(command, M65_DATA_IN, detail, detail_size) ||
            !m65_read_be16(command->cdb, command->cdb_length, 7U, &encoded_length) ||
            encoded_length == 0U ||
            command->data_length != (size_t)encoded_length * (size_t)M65_BLOCK_SIZE) {
            set_detail(detail, detail_size, "READ (10) length does not match its 512-byte buffer");
            return false;
        }
        return true;
    case M65_OPCODE_MODE_SELECT_10:
        if (command->cdb_length != 10U ||
            !validate_data(command, M65_DATA_OUT, detail, detail_size) ||
            command->cdb[1] != 0x10U || command->cdb[2] != 0U ||
            command->cdb[3] != 0U || command->cdb[4] != 0U ||
            command->cdb[5] != 0U || command->cdb[6] != 0U ||
            command->cdb[9] != 0U ||
            !m65_read_be16(command->cdb, command->cdb_length, 7U, &encoded_length) ||
            (size_t)encoded_length != command->data_length || command->data_length != 40U) {
            set_detail(detail, detail_size, "MODE SELECT (10) length is invalid");
            return false;
        }
        {
            const uint8_t *payload = (const uint8_t *)command->data;
            size_t header_index;
            for (header_index = 0U; header_index < 8U; ++header_index) {
                if (payload[header_index] != 0U) {
                    set_detail(detail, detail_size,
                               "MODE SELECT parameter header must be empty");
                    return false;
                }
            }
            if (payload[8] != M65_FLEXIBLE_DISK_PAGE || payload[9] != 30U) {
                set_detail(detail, detail_size,
                           "MODE SELECT data-out is not the single permitted Flexible Disk page");
                return false;
            }
        }
        return true;
    default:
        set_detail(detail, detail_size, "opcode is not on the explicit diagnostic allowlist");
        return false;
    }
}
