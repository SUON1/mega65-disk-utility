#ifndef M65_BYTES_H
#define M65_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool m65_read_be16(const uint8_t *data, size_t length, size_t offset, uint16_t *value);
bool m65_read_be24(const uint8_t *data, size_t length, size_t offset, uint32_t *value);
bool m65_read_be32(const uint8_t *data, size_t length, size_t offset, uint32_t *value);
void m65_write_be16(uint8_t *data, uint16_t value);
void m65_write_be32(uint8_t *data, uint32_t value);

#endif
