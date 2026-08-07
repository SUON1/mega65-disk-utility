#include "test.h"

#include "m65/bytes.h"

void test_bytes(void)
{
    const uint8_t data[] = {0x12U, 0x34U, 0x56U, 0x78U};
    uint16_t value16 = 0U;
    uint32_t value32 = 0U;
    uint8_t encoded[4];
    EXPECT_TRUE(m65_read_be16(data, sizeof(data), 0U, &value16));
    EXPECT_EQ_U64(value16, 0x1234U);
    EXPECT_TRUE(m65_read_be24(data, sizeof(data), 1U, &value32));
    EXPECT_EQ_U64(value32, 0x345678U);
    EXPECT_TRUE(m65_read_be32(data, sizeof(data), 0U, &value32));
    EXPECT_EQ_U64(value32, 0x12345678U);
    EXPECT_FALSE(m65_read_be16(data, sizeof(data), 3U, &value16));
    EXPECT_FALSE(m65_read_be32(data, sizeof(data), 1U, &value32));
    EXPECT_FALSE(m65_read_be16(NULL, sizeof(data), 0U, &value16));
    EXPECT_FALSE(m65_read_be16(data, sizeof(data), 0U, NULL));
    m65_write_be16(encoded, 0xabcdU);
    EXPECT_EQ_U64(encoded[0], 0xabU);
    EXPECT_EQ_U64(encoded[1], 0xcdU);
    m65_write_be32(encoded, 0x89abcdefU);
    EXPECT_MEMEQ(encoded, ((const uint8_t[]){0x89U, 0xabU, 0xcdU, 0xefU}), 4U);
}
