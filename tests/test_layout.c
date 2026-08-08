#include "test.h"

#include "m65/layout.h"

static void test_known_addresses(void)
{
    M651581PhysicalAddress physical;

    EXPECT_TRUE(m65_1581_logical_to_physical(1U, 0U, &physical));
    EXPECT_EQ_U64(physical.cylinder, 0U);
    EXPECT_EQ_U64(physical.head, 0U);
    EXPECT_EQ_U64(physical.sector_index, 0U);
    EXPECT_EQ_U64(physical.lba, 0U);
    EXPECT_EQ_U64(physical.byte_offset, 0U);

    EXPECT_TRUE(m65_1581_logical_to_physical(1U, 1U, &physical));
    EXPECT_EQ_U64(physical.lba, 0U);
    EXPECT_EQ_U64(physical.byte_offset, 256U);

    EXPECT_TRUE(m65_1581_logical_to_physical(1U, 18U, &physical));
    EXPECT_EQ_U64(physical.head, 0U);
    EXPECT_EQ_U64(physical.sector_index, 9U);
    EXPECT_EQ_U64(physical.lba, 9U);

    EXPECT_TRUE(m65_1581_logical_to_physical(1U, 20U, &physical));
    EXPECT_EQ_U64(physical.head, 1U);
    EXPECT_EQ_U64(physical.sector_index, 0U);
    EXPECT_EQ_U64(physical.lba, 10U);

    EXPECT_TRUE(m65_1581_logical_to_physical(80U, 39U, &physical));
    EXPECT_EQ_U64(physical.cylinder, 79U);
    EXPECT_EQ_U64(physical.head, 1U);
    EXPECT_EQ_U64(physical.sector_index, 9U);
    EXPECT_EQ_U64(physical.lba, 1599U);
    EXPECT_EQ_U64(physical.byte_offset, 256U);
    EXPECT_EQ_U64(physical.image_offset, M65_1581_IMAGE_SIZE - 256U);
}

static void test_exhaustive_round_trip(void)
{
    uint32_t track;

    for (track = 1U; track <= M65_1581_TRACK_COUNT; ++track) {
        uint32_t sector;
        for (sector = 0U; sector < M65_1581_LOGICAL_SECTORS_PER_TRACK; ++sector) {
            M651581PhysicalAddress physical;
            M651581LogicalAddress logical;
            size_t canonical_offset =
                ((size_t)track - 1U) * M65_1581_LOGICAL_SECTORS_PER_TRACK *
                    M65_1581_LOGICAL_SECTOR_SIZE +
                (size_t)sector * M65_1581_LOGICAL_SECTOR_SIZE;

            EXPECT_TRUE(m65_1581_logical_to_physical(
                (uint8_t)track, (uint8_t)sector, &physical));
            EXPECT_TRUE(m65_1581_lba_to_logical(physical.lba,
                                                physical.byte_offset, &logical));
            EXPECT_EQ_U64(logical.track, track);
            EXPECT_EQ_U64(logical.sector, sector);
            EXPECT_EQ_U64(physical.image_offset, canonical_offset);
            EXPECT_EQ_U64(logical.image_offset, canonical_offset);
        }
    }
}

static void test_invalid_and_truncated_inputs(void)
{
    M651581PhysicalAddress physical;
    M651581LogicalAddress logical;
    uint8_t image[1024];
    uint8_t sector[256];
    size_t index;

    EXPECT_FALSE(m65_1581_logical_to_physical(0U, 0U, &physical));
    EXPECT_FALSE(m65_1581_logical_to_physical(81U, 0U, &physical));
    EXPECT_FALSE(m65_1581_logical_to_physical(1U, 40U, &physical));
    EXPECT_FALSE(m65_1581_logical_to_physical(1U, 0U, NULL));
    EXPECT_FALSE(m65_1581_lba_to_logical(1600U, 0U, &logical));
    EXPECT_FALSE(m65_1581_lba_to_logical(0U, 1U, &logical));
    EXPECT_FALSE(m65_1581_lba_to_logical(0U, 512U, &logical));
    EXPECT_FALSE(m65_1581_lba_to_logical(0U, 0U, NULL));

    for (index = 0U; index < sizeof(image); ++index) {
        image[index] = (uint8_t)((index / 256U) * 67U + (index % 256U));
    }
    EXPECT_TRUE(m65_1581_copy_logical_sector(image, sizeof(image), 1U, 3U,
                                             sector));
    EXPECT_MEMEQ(sector, &image[768], sizeof(sector));
    EXPECT_FALSE(m65_1581_copy_logical_sector(image, 1023U, 1U, 3U, sector));
    EXPECT_FALSE(m65_1581_copy_logical_sector(NULL, sizeof(image), 1U, 0U,
                                              sector));
    EXPECT_FALSE(m65_1581_copy_logical_sector(image, sizeof(image), 1U, 0U,
                                              NULL));
}

static void test_interleave_policy(void)
{
    static const uint8_t expected[M65_1581_LOGICAL_SECTORS_PER_TRACK] = {
        0U, 1U, 4U, 5U, 8U, 9U, 12U, 13U, 16U, 17U,
        20U, 21U, 24U, 25U, 28U, 29U, 32U, 33U, 36U, 37U,
        2U, 3U, 6U, 7U, 10U, 11U, 14U, 15U, 18U, 19U,
        22U, 23U, 26U, 27U, 30U, 31U, 34U, 35U, 38U, 39U
    };
    size_t index;
    uint8_t actual = 0U;

    for (index = 0U; index < sizeof(expected); ++index) {
        EXPECT_TRUE(m65_1581_interleave_sector_at(index, &actual));
        EXPECT_EQ_U64(actual, expected[index]);
    }
    EXPECT_FALSE(m65_1581_interleave_sector_at(sizeof(expected), &actual));
    EXPECT_FALSE(m65_1581_interleave_sector_at(0U, NULL));

    EXPECT_TRUE(m65_1581_next_interleave_candidate(0U, &actual));
    EXPECT_EQ_U64(actual, 1U);
    EXPECT_TRUE(m65_1581_next_interleave_candidate(1U, &actual));
    EXPECT_EQ_U64(actual, 4U);
    EXPECT_TRUE(m65_1581_next_interleave_candidate(37U, &actual));
    EXPECT_EQ_U64(actual, 0U);
    EXPECT_TRUE(m65_1581_next_track_candidate(37U, &actual));
    EXPECT_EQ_U64(actual, 4U);
    EXPECT_TRUE(m65_1581_next_track_candidate(38U, &actual));
    EXPECT_EQ_U64(actual, 6U);
    EXPECT_FALSE(m65_1581_next_interleave_candidate(40U, &actual));
    EXPECT_FALSE(m65_1581_next_track_candidate(40U, &actual));
}

void test_layout(void)
{
    test_known_addresses();
    test_exhaustive_round_trip();
    test_invalid_and_truncated_inputs();
    test_interleave_policy();
}
