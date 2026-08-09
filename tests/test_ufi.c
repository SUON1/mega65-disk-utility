#include "test.h"

#include "m65/bytes.h"
#include "m65/ufi.h"

#include <string.h>

static void test_cdbs(void)
{
    uint8_t cdb[16];
    size_t length;
    length = m65_cdb_test_unit_ready(cdb);
    EXPECT_EQ_U64(length, 6U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x00U}), 16U);
    length = m65_cdb_request_sense(cdb, 18U);
    EXPECT_EQ_U64(length, 6U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x03U, 0U, 0U, 0U, 18U}), 16U);
    length = m65_cdb_inquiry(cdb, 96U);
    EXPECT_EQ_U64(length, 6U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x12U, 0U, 0U, 0U, 96U}), 16U);
    length = m65_cdb_read_capacity_10(cdb);
    EXPECT_EQ_U64(length, 10U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x25U}), 16U);
    length = m65_cdb_read_format_capacities(cdb, 0x1234U);
    EXPECT_EQ_U64(length, 10U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x23U, 0U, 0U, 0U, 0U, 0U, 0U,
                                                   0x12U, 0x34U}), 16U);
    length = m65_cdb_mode_sense_10(cdb, false, 0x0100U);
    EXPECT_EQ_U64(length, 10U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x5aU, 0U, 0x05U, 0U, 0U, 0U, 0U,
                                                   0x01U, 0x00U}), 16U);
    length = m65_cdb_mode_sense_10(cdb, true, 0x0100U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x5aU, 0U, 0x45U, 0U, 0U, 0U, 0U,
                                                   0x01U, 0x00U}), 16U);
    length = m65_cdb_mode_select_10(cdb, 40U);
    EXPECT_EQ_U64(length, 10U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x55U, 0x10U, 0U, 0U, 0U, 0U, 0U,
                                                   0U, 40U}), 16U);
    length = m65_cdb_read_10(cdb, 0x12345678U, 0x009aU);
    EXPECT_EQ_U64(length, 10U);
    EXPECT_MEMEQ(cdb, ((const uint8_t[16]){0x28U, 0U, 0x12U, 0x34U, 0x56U, 0x78U,
                                                   0U, 0U, 0x9aU}), 16U);
}

static void descriptor(uint8_t *data, uint32_t blocks, uint8_t code, uint32_t size)
{
    m65_write_be32(data, blocks);
    data[4] = code;
    data[5] = (uint8_t)(size >> 16U);
    data[6] = (uint8_t)(size >> 8U);
    data[7] = (uint8_t)size;
}

static void test_formats(void)
{
    M65FormatCapacities capacities;
    char detail[256];
    uint8_t zero[4] = {0U, 0U, 0U, 0U};
    uint8_t one[12] = {0U};
    uint8_t multiple[20] = {0U};
    uint8_t truncated[11] = {0U};
    uint8_t malformed[13] = {0U};
    EXPECT_TRUE(m65_parse_format_capacities(zero, sizeof(zero), &capacities,
                                            detail, sizeof(detail)));
    EXPECT_EQ_U64(capacities.count, 0U);
    one[3] = 8U;
    descriptor(&one[4], 1600U, 2U, 512U);
    EXPECT_TRUE(m65_parse_format_capacities(one, sizeof(one), &capacities,
                                            detail, sizeof(detail)));
    EXPECT_EQ_U64(capacities.count, 1U);
    EXPECT_EQ_U64(capacities.descriptors[0].blocks, 1600U);
    multiple[3] = 16U;
    descriptor(&multiple[4], 1440U, 2U, 512U);
    descriptor(&multiple[12], 1600U, 0U, 512U);
    EXPECT_TRUE(m65_parse_format_capacities(multiple, sizeof(multiple), &capacities,
                                            detail, sizeof(detail)));
    EXPECT_EQ_U64(capacities.count, 2U);
    truncated[3] = 8U;
    EXPECT_FALSE(m65_parse_format_capacities(truncated, sizeof(truncated), &capacities,
                                             detail, sizeof(detail)));
    malformed[3] = 9U;
    EXPECT_FALSE(m65_parse_format_capacities(malformed, sizeof(malformed), &capacities,
                                             detail, sizeof(detail)));
}

static void test_mode_and_sense(void)
{
    uint8_t mode[40] = {0U};
    M65ModeParameters parsed;
    M65ModeParameters mask;
    M65Sense sense;
    char detail[256];
    m65_write_be16(mode, 38U);
    mode[8] = 0x85U;
    mode[9] = 30U;
    m65_write_be16(&mode[10], 250U);
    mode[12] = 2U;
    mode[13] = 10U;
    m65_write_be16(&mode[14], 512U);
    m65_write_be16(&mode[16], 80U);
    m65_write_be16(&mode[36], 300U);
    EXPECT_TRUE(m65_parse_mode_parameters(mode, sizeof(mode), &parsed,
                                          detail, sizeof(detail)));
    EXPECT_EQ_U64(parsed.flexible.transfer_rate_kbit, 250U);
    EXPECT_EQ_U64(parsed.flexible.heads, 2U);
    EXPECT_EQ_U64(parsed.flexible.medium_rotation_rate_rpm, 300U);
    (void)memset(mode, 0, sizeof(mode));
    m65_write_be16(mode, 38U);
    mode[8] = 0x05U;
    mode[9] = 30U;
    mode[10] = 0xffU;
    mode[11] = 0xffU;
    mode[12] = 0xffU;
    EXPECT_TRUE(m65_parse_mode_parameters(mode, sizeof(mode), &mask,
                                          detail, sizeof(detail)));
    EXPECT_TRUE(m65_apply_changeability(&parsed.flexible, &mask.flexible));
    EXPECT_TRUE(parsed.flexible.field_changeable[M65_FLEX_TRANSFER_RATE]);
    EXPECT_TRUE(parsed.flexible.field_changeable[M65_FLEX_HEADS]);
    EXPECT_FALSE(parsed.flexible.field_changeable[M65_FLEX_SECTORS_PER_TRACK]);
    EXPECT_FALSE(m65_parse_mode_parameters(mode, 20U, &mask, detail, sizeof(detail)));
    mode[0] = 0U;
    mode[1] = 50U;
    EXPECT_FALSE(m65_parse_mode_parameters(mode, sizeof(mode), &mask,
                                           detail, sizeof(detail)));

    EXPECT_TRUE(m65_parse_sense(
        (const uint8_t[]){0x70U, 0U, 0x05U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                          0x24U, 0x01U}, 14U, &sense));
    EXPECT_EQ_U64(sense.key, 5U);
    EXPECT_EQ_U64(sense.asc, 0x24U);
    EXPECT_EQ_U64(sense.ascq, 1U);
    EXPECT_TRUE(m65_parse_sense(
        (const uint8_t[]){0x72U, 0x02U, 0x3aU, 0x00U}, 4U, &sense));
    EXPECT_EQ_U64(sense.key, 2U);
    EXPECT_EQ_U64(sense.asc, 0x3aU);
}

static void test_allowlist(void)
{
    static const uint8_t writing_opcodes[] = {0x04U, 0x0aU, 0x2aU, 0x2eU, 0xaaU, 0xc0U};
    M65Command command;
    uint8_t data[512] = {0U};
    char detail[256];
    size_t index;
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = 10U;
    command.direction = M65_DATA_OUT;
    command.data = data;
    command.data_length = sizeof(data);
    for (index = 0U; index < sizeof(writing_opcodes); ++index) {
        command.cdb[0] = writing_opcodes[index];
        EXPECT_FALSE(m65_validate_command(&command, detail, sizeof(detail)));
    }
    for (index = 0U; index < 256U; ++index) {
        command.cdb[0] = (uint8_t)index;
        EXPECT_FALSE(m65_validate_command(&command, detail, sizeof(detail)));
    }
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_mode_select_10(command.cdb, 40U);
    command.direction = M65_DATA_OUT;
    command.data = data;
    command.data_length = 40U;
    data[8] = M65_FLEXIBLE_DISK_PAGE;
    data[9] = 30U;
    EXPECT_TRUE(m65_validate_command(&command, detail, sizeof(detail)));
    data[8] = 0x08U;
    EXPECT_FALSE(m65_validate_command(&command, detail, sizeof(detail)));

    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_request_sense(command.cdb, 18U);
    command.direction = M65_DATA_IN;
    command.data = data;
    command.data_length = 18U;
    EXPECT_TRUE(m65_validate_command(&command, detail, sizeof(detail)));
    command.data_length = 17U;
    EXPECT_FALSE(m65_validate_command(&command, detail, sizeof(detail)));
    command.data_length = 18U;
    command.direction = M65_DATA_OUT;
    EXPECT_FALSE(m65_validate_command(&command, detail, sizeof(detail)));
}

void test_ufi(void)
{
    M65Capacity capacity;
    M65Inquiry inquiry;
    char detail[256];
    test_cdbs();
    test_formats();
    test_mode_and_sense();
    test_allowlist();
    EXPECT_FALSE(m65_parse_read_capacity_10(
        (const uint8_t[]){0U, 0U, 0U}, 3U, &capacity, detail, sizeof(detail)));
    EXPECT_TRUE(m65_parse_read_capacity_10(
        (const uint8_t[]){0U, 0U, 0x05U, 0x9fU, 0U, 0U, 0x02U, 0U},
        8U, &capacity, detail, sizeof(detail)));
    EXPECT_EQ_U64(capacity.blocks, 1440U);
    EXPECT_EQ_U64(capacity.block_size, 512U);
    EXPECT_FALSE(m65_parse_inquiry((const uint8_t[]){0U}, 1U, &inquiry,
                                   detail, sizeof(detail)));
}
