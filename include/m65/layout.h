#ifndef M65_LAYOUT_H
#define M65_LAYOUT_H

#include "m65/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M65_1581_TRACK_COUNT 80U
#define M65_1581_HEAD_COUNT 2U
#define M65_1581_PHYSICAL_SECTORS_PER_HEAD 10U
#define M65_1581_PHYSICAL_SECTORS_PER_TRACK 20U
#define M65_1581_LOGICAL_SECTORS_PER_TRACK 40U
#define M65_1581_LOGICAL_SECTOR_SIZE 256U

typedef struct {
    uint8_t track;
    uint8_t sector;
    size_t image_offset;
} M651581LogicalAddress;

typedef struct {
    uint8_t cylinder;
    uint8_t head;
    uint8_t sector_index;
    uint32_t lba;
    uint16_t byte_offset;
    size_t image_offset;
} M651581PhysicalAddress;

bool m65_1581_logical_to_physical(uint8_t track, uint8_t sector,
                                  M651581PhysicalAddress *physical);
bool m65_1581_lba_to_logical(uint32_t lba, uint16_t byte_offset,
                             M651581LogicalAddress *logical);
bool m65_1581_copy_logical_sector(const uint8_t *physical_image,
                                  size_t physical_image_length,
                                  uint8_t track, uint8_t sector,
                                  uint8_t destination[M65_1581_LOGICAL_SECTOR_SIZE]);

bool m65_1581_interleave_sector_at(size_t ordinal, uint8_t *sector);
bool m65_1581_next_interleave_candidate(uint8_t last_sector, uint8_t *sector);
bool m65_1581_next_track_candidate(uint8_t last_sector, uint8_t *sector);

#endif
