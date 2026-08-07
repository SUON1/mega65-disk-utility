#include "fake_transport.h"

#include "m65/bytes.h"
#include "m65/probe.h"

#include <stdio.h>
#include <string.h>

static M65TransportStatus fake_acquire(M65Transport *transport,
                                       char *detail, size_t detail_size)
{
    FakeTransport *fake = (FakeTransport *)transport->context;
    ++fake->acquire_count;
    if (fake->acquire_status != M65_TRANSPORT_OK) {
        (void)snprintf(detail, detail_size, "simulated acquire failure");
        return fake->acquire_status;
    }
    fake->acquired = true;
    return M65_TRANSPORT_OK;
}

static M65TransportStatus fake_release(M65Transport *transport,
                                       char *detail, size_t detail_size)
{
    FakeTransport *fake = (FakeTransport *)transport->context;
    ++fake->release_count;
    if (fake->release_status != M65_TRANSPORT_OK) {
        (void)snprintf(detail, detail_size, "simulated release failure");
        return fake->release_status;
    }
    fake->acquired = false;
    return M65_TRANSPORT_OK;
}

static void fill_mode_response(uint8_t *data, size_t capacity,
                               const uint8_t page[M65_FLEX_PAGE_LENGTH],
                               M65CommandResult *result)
{
    if (capacity < 40U) {
        result->transport_status = M65_TRANSPORT_PROTOCOL;
        return;
    }
    (void)memset(data, 0, capacity);
    m65_write_be16(data, 38U);
    (void)memcpy(&data[8], page, M65_FLEX_PAGE_LENGTH);
    result->transferred = 40U;
}

static void fill_inquiry(uint8_t *data, size_t capacity, M65CommandResult *result)
{
    if (capacity < 36U) {
        result->transport_status = M65_TRANSPORT_PROTOCOL;
        return;
    }
    (void)memset(data, 0, capacity);
    data[4] = 31U;
    (void)memcpy(&data[8], "TEAC    ", 8U);
    (void)memcpy(&data[16], "USB UF000x      ", 16U);
    (void)memcpy(&data[32], "0200", 4U);
    result->transferred = 36U;
}

static void fill_capacity(uint8_t *data, size_t capacity, M65CommandResult *result)
{
    if (capacity < 8U) {
        result->transport_status = M65_TRANSPORT_PROTOCOL;
        return;
    }
    m65_write_be32(data, 1439U);
    m65_write_be32(&data[4], 512U);
    result->transferred = 8U;
}

static void format_descriptor(uint8_t *data, uint32_t blocks,
                              uint8_t code, uint32_t block_size)
{
    m65_write_be32(data, blocks);
    data[4] = code;
    data[5] = (uint8_t)(block_size >> 16U);
    data[6] = (uint8_t)(block_size >> 8U);
    data[7] = (uint8_t)block_size;
}

static void fill_format_capacities(uint8_t *data, size_t capacity,
                                   M65CommandResult *result)
{
    if (capacity < 20U) {
        result->transport_status = M65_TRANSPORT_PROTOCOL;
        return;
    }
    (void)memset(data, 0, capacity);
    data[3] = 16U;
    format_descriptor(&data[4], 1440U, 2U, 512U);
    format_descriptor(&data[12], 1600U, 0U, 512U);
    result->transferred = 20U;
}

static void fill_read(FakeTransport *fake, const M65Command *command,
                      M65CommandResult *result)
{
    uint32_t lba = 0U;
    size_t index;
    uint8_t *data = (uint8_t *)command->data;
    (void)m65_read_be32(command->cdb, command->cdb_length, 2U, &lba);
    ++fake->read_count;
    for (index = 0U; index < command->data_length; ++index) {
        data[index] = (uint8_t)(((uint64_t)lba * M65_BLOCK_SIZE + index) & 0xffU);
    }
    if (fake->mismatch_repeated_read && fake->read_count >= 53U &&
        command->data_length > 0U) {
        data[0] ^= 0xffU;
    }
    result->transferred = command->data_length;
}

static M65CommandResult fake_execute(M65Transport *transport,
                                     const M65Command *command)
{
    FakeTransport *fake = (FakeTransport *)transport->context;
    M65CommandResult result;
    uint8_t opcode = command->cdb[0];
    (void)memset(&result, 0, sizeof(result));
    result.transport_status = M65_TRANSPORT_OK;
    if (!fake->acquired) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        (void)snprintf(result.detail, sizeof(result.detail), "not acquired");
        return result;
    }
    ++fake->opcode_counts[opcode];
    if (fake->fail_occurrence != 0U && opcode == fake->fail_opcode &&
        fake->opcode_counts[opcode] == fake->fail_occurrence) {
        result.transport_status = fake->fail_status;
        (void)snprintf(result.detail, sizeof(result.detail), "simulated command failure");
        return result;
    }
    switch (opcode) {
    case M65_OPCODE_TEST_UNIT_READY:
        break;
    case M65_OPCODE_INQUIRY:
        fill_inquiry((uint8_t *)command->data, command->data_length, &result);
        break;
    case M65_OPCODE_READ_CAPACITY_10:
        fill_capacity((uint8_t *)command->data, command->data_length, &result);
        break;
    case M65_OPCODE_READ_FORMAT_CAPACITIES:
        fill_format_capacities((uint8_t *)command->data, command->data_length, &result);
        break;
    case M65_OPCODE_MODE_SENSE_10:
        fill_mode_response((uint8_t *)command->data, command->data_length,
                           (command->cdb[2] & 0xc0U) == 0x40U ?
                           fake->mask_page : fake->current_page, &result);
        break;
    case M65_OPCODE_MODE_SELECT_10:
        ++fake->mode_select_count;
        if (fake->desired_mode_rejected &&
            memcmp(&((const uint8_t *)command->data)[8],
                   fake->original_page, M65_FLEX_PAGE_LENGTH) != 0) {
            result.scsi_status = 0x02U;
            result.sense.valid = true;
            result.sense.response_code = 0x70U;
            result.sense.key = 0x05U;
            result.sense.asc = 0x26U;
            result.sense.ascq = 0x00U;
        } else {
            (void)memcpy(fake->current_page,
                         &((const uint8_t *)command->data)[8],
                         M65_FLEX_PAGE_LENGTH);
            if (fake->accept_wrong_geometry &&
                memcmp(fake->current_page, fake->original_page,
                       M65_FLEX_PAGE_LENGTH) != 0) {
                fake->current_page[4] = 1U;
            }
            if (fake->interrupt_after_desired &&
                memcmp(fake->current_page, fake->original_page,
                       M65_FLEX_PAGE_LENGTH) != 0) {
                m65_probe_request_interrupt();
            }
            result.transferred = command->data_length;
        }
        break;
    case M65_OPCODE_READ_10:
        fill_read(fake, command, &result);
        break;
    default:
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        break;
    }
    return result;
}

static void fake_destroy(M65Transport *transport)
{
    FakeTransport *fake = (FakeTransport *)transport->context;
    fake->destroyed = true;
}

static const M65TransportOps fake_ops = {
    fake_acquire,
    fake_execute,
    fake_release,
    fake_destroy
};

void fake_transport_set_all_geometry_changeable(FakeTransport *fake, bool changeable)
{
    static const size_t offsets[] = {2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 28U, 29U};
    size_t index;
    (void)memset(fake->mask_page, 0, sizeof(fake->mask_page));
    fake->mask_page[0] = M65_FLEXIBLE_DISK_PAGE;
    fake->mask_page[1] = 30U;
    if (changeable) {
        for (index = 0U; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
            fake->mask_page[offsets[index]] = 0xffU;
        }
    }
}

void fake_transport_init(FakeTransport *fake)
{
    M65FlexibleDiskPage page;
    (void)memset(fake, 0, sizeof(*fake));
    fake->transport.ops = &fake_ops;
    fake->transport.context = fake;
    fake->acquire_status = M65_TRANSPORT_OK;
    fake->release_status = M65_TRANSPORT_OK;
    fake->fail_status = M65_TRANSPORT_IO;
    (void)memset(&page, 0, sizeof(page));
    page.page[0] = M65_FLEXIBLE_DISK_PAGE;
    page.page[1] = 30U;
    m65_write_be16(&page.page[2], 500U);
    page.page[4] = 2U;
    page.page[5] = 18U;
    m65_write_be16(&page.page[6], 512U);
    m65_write_be16(&page.page[8], 80U);
    m65_write_be16(&page.page[28], 300U);
    (void)memcpy(fake->original_page, page.page, sizeof(fake->original_page));
    (void)memcpy(fake->current_page, page.page, sizeof(fake->current_page));
    fake_transport_set_all_geometry_changeable(fake, true);
}

bool fake_transport_is_restored(const FakeTransport *fake)
{
    return memcmp(fake->current_page, fake->original_page,
                  M65_FLEX_PAGE_LENGTH) == 0;
}
