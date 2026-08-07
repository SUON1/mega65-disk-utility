#include "m65/bytes.h"

static bool available(size_t length, size_t offset, size_t needed)
{
    return offset <= length && needed <= length - offset;
}

bool m65_read_be16(const uint8_t *data, size_t length, size_t offset, uint16_t *value)
{
    if (data == NULL || value == NULL || !available(length, offset, 2U)) {
        return false;
    }
    *value = (uint16_t)(((uint16_t)data[offset] << 8U) |
                        (uint16_t)data[offset + 1U]);
    return true;
}

bool m65_read_be24(const uint8_t *data, size_t length, size_t offset, uint32_t *value)
{
    if (data == NULL || value == NULL || !available(length, offset, 3U)) {
        return false;
    }
    *value = ((uint32_t)data[offset] << 16U) |
             ((uint32_t)data[offset + 1U] << 8U) |
             (uint32_t)data[offset + 2U];
    return true;
}

bool m65_read_be32(const uint8_t *data, size_t length, size_t offset, uint32_t *value)
{
    if (data == NULL || value == NULL || !available(length, offset, 4U)) {
        return false;
    }
    *value = ((uint32_t)data[offset] << 24U) |
             ((uint32_t)data[offset + 1U] << 16U) |
             ((uint32_t)data[offset + 2U] << 8U) |
             (uint32_t)data[offset + 3U];
    return true;
}

void m65_write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)(value & 0xffU);
}

void m65_write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)((value >> 16U) & 0xffU);
    data[2] = (uint8_t)((value >> 8U) & 0xffU);
    data[3] = (uint8_t)(value & 0xffU);
}
