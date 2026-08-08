#include "m65/layout.h"

#include <string.h>

_Static_assert(M65_1581_PHYSICAL_SECTORS_PER_TRACK ==
                   M65_1581_HEAD_COUNT * M65_1581_PHYSICAL_SECTORS_PER_HEAD,
               "1581 physical geometry must describe both heads");
_Static_assert(M65_1581_LOGICAL_SECTORS_PER_TRACK *
                   M65_1581_LOGICAL_SECTOR_SIZE ==
                   M65_1581_PHYSICAL_SECTORS_PER_TRACK * M65_BLOCK_SIZE,
               "1581 logical and physical track sizes must match");
_Static_assert(M65_1581_TRACK_COUNT * M65_1581_PHYSICAL_SECTORS_PER_TRACK ==
                   M65_1581_BLOCKS,
               "1581 geometry must describe the complete image");

bool m65_1581_logical_to_physical(uint8_t track, uint8_t sector,
                                  M651581PhysicalAddress *physical)
{
    uint32_t cylinder;
    uint32_t physical_sector;
    uint32_t lba;
    uint32_t half;

    if (physical == NULL || track == 0U || track > M65_1581_TRACK_COUNT ||
        sector >= M65_1581_LOGICAL_SECTORS_PER_TRACK) {
        return false;
    }

    cylinder = (uint32_t)track - 1U;
    physical_sector = (uint32_t)sector / 2U;
    lba = cylinder * M65_1581_PHYSICAL_SECTORS_PER_TRACK + physical_sector;
    half = (uint32_t)sector % 2U;

    physical->cylinder = (uint8_t)cylinder;
    physical->head = (uint8_t)(physical_sector /
                               M65_1581_PHYSICAL_SECTORS_PER_HEAD);
    physical->sector_index = (uint8_t)(physical_sector %
                                       M65_1581_PHYSICAL_SECTORS_PER_HEAD);
    physical->lba = lba;
    physical->byte_offset = (uint16_t)(half * M65_1581_LOGICAL_SECTOR_SIZE);
    physical->image_offset = (size_t)lba * (size_t)M65_BLOCK_SIZE +
                             (size_t)physical->byte_offset;
    return true;
}

bool m65_1581_lba_to_logical(uint32_t lba, uint16_t byte_offset,
                             M651581LogicalAddress *logical)
{
    uint32_t cylinder;
    uint32_t physical_sector;
    uint32_t half;
    uint32_t logical_sector;

    if (logical == NULL || lba >= M65_1581_BLOCKS ||
        (byte_offset != 0U && byte_offset != M65_1581_LOGICAL_SECTOR_SIZE)) {
        return false;
    }

    cylinder = lba / M65_1581_PHYSICAL_SECTORS_PER_TRACK;
    physical_sector = lba % M65_1581_PHYSICAL_SECTORS_PER_TRACK;
    half = (uint32_t)byte_offset / M65_1581_LOGICAL_SECTOR_SIZE;
    logical_sector = physical_sector * 2U + half;

    logical->track = (uint8_t)(cylinder + 1U);
    logical->sector = (uint8_t)logical_sector;
    logical->image_offset = (size_t)lba * (size_t)M65_BLOCK_SIZE +
                            (size_t)byte_offset;
    return true;
}

bool m65_1581_copy_logical_sector(const uint8_t *physical_image,
                                  size_t physical_image_length,
                                  uint8_t track, uint8_t sector,
                                  uint8_t destination[M65_1581_LOGICAL_SECTOR_SIZE])
{
    M651581PhysicalAddress physical;

    if (physical_image == NULL || destination == NULL ||
        !m65_1581_logical_to_physical(track, sector, &physical) ||
        physical.image_offset > physical_image_length ||
        physical_image_length - physical.image_offset <
            M65_1581_LOGICAL_SECTOR_SIZE) {
        return false;
    }

    (void)memcpy(destination, &physical_image[physical.image_offset],
                 M65_1581_LOGICAL_SECTOR_SIZE);
    return true;
}

bool m65_1581_interleave_sector_at(size_t ordinal, uint8_t *sector)
{
    size_t pair_ordinal;
    size_t half;
    size_t physical_sector;

    if (sector == NULL || ordinal >= M65_1581_LOGICAL_SECTORS_PER_TRACK) {
        return false;
    }

    pair_ordinal = ordinal / 2U;
    half = ordinal % 2U;
    if (pair_ordinal < M65_1581_PHYSICAL_SECTORS_PER_HEAD) {
        physical_sector = pair_ordinal * 2U;
    } else {
        physical_sector =
            (pair_ordinal - M65_1581_PHYSICAL_SECTORS_PER_HEAD) * 2U + 1U;
    }
    *sector = (uint8_t)(physical_sector * 2U + half);
    return true;
}

bool m65_1581_next_interleave_candidate(uint8_t last_sector, uint8_t *sector)
{
    uint32_t candidate;

    if (sector == NULL || last_sector >= M65_1581_LOGICAL_SECTORS_PER_TRACK) {
        return false;
    }
    candidate = (uint32_t)last_sector + 1U +
                2U * ((uint32_t)last_sector % 2U);
    *sector = (uint8_t)(candidate % M65_1581_LOGICAL_SECTORS_PER_TRACK);
    return true;
}

bool m65_1581_next_track_candidate(uint8_t last_sector, uint8_t *sector)
{
    uint32_t candidate;

    if (sector == NULL || last_sector >= M65_1581_LOGICAL_SECTORS_PER_TRACK) {
        return false;
    }
    candidate = (uint32_t)last_sector + 8U -
                ((uint32_t)last_sector % 2U);
    *sector = (uint8_t)(candidate % M65_1581_LOGICAL_SECTORS_PER_TRACK);
    return true;
}
