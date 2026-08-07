#include "test.h"

#include "fake_transport.h"
#include "m65/probe.h"

static void expect_cleanup(const FakeTransport *fake, bool should_restore)
{
    EXPECT_EQ_U64(fake->release_count, 1U);
    EXPECT_FALSE(fake->acquired);
    if (should_restore) {
        EXPECT_TRUE(fake_transport_is_restored(fake));
    }
}

static void test_inspect_success(void)
{
    FakeTransport fake;
    M65InspectReport report;
    fake_transport_init(&fake);
    EXPECT_EQ_INT(m65_inspect(&fake.transport, &report), M65_PROBE_OK);
    EXPECT_EQ_U64(report.capacity.blocks, 1440U);
    EXPECT_EQ_U64(report.capacity.block_size, 512U);
    EXPECT_EQ_U64(report.format_capacities.count, 2U);
    EXPECT_STREQ(report.inquiry.vendor, "TEAC");
    EXPECT_TRUE(report.current_mode.flexible.field_changeable[M65_FLEX_TRANSFER_RATE]);
    expect_cleanup(&fake, true);
}

static void test_acknowledgement_refusal(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, false, &report), M65_PROBE_USAGE);
    EXPECT_EQ_U64(fake.acquire_count, 0U);
    EXPECT_EQ_U64(fake.release_count, 0U);
    m65_1581_report_destroy(&report);
}

static void test_supported(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report), M65_PROBE_OK);
    EXPECT_EQ_INT(report.status, M65_1581_SUPPORTED);
    EXPECT_TRUE(report.controller_changed);
    EXPECT_TRUE(report.controller_restored);
    EXPECT_TRUE(report.boundary_lba_9_read);
    EXPECT_TRUE(report.boundary_lba_1599_read);
    EXPECT_TRUE(report.repeated_reads_match);
    EXPECT_EQ_U64(report.image_length, M65_1581_IMAGE_SIZE);
    EXPECT_EQ_U64(fake.mode_select_count, 2U);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);
}

static void test_unsupported(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    fake_transport_set_all_geometry_changeable(&fake, false);
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_UNSUPPORTED);
    EXPECT_EQ_INT(report.status, M65_1581_UNSUPPORTED);
    EXPECT_EQ_U64(fake.mode_select_count, 0U);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);

    fake_transport_init(&fake);
    fake.accept_wrong_geometry = true;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_UNSUPPORTED);
    EXPECT_EQ_INT(report.status, M65_1581_UNSUPPORTED);
    EXPECT_TRUE(report.controller_restored);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);
}

static void test_permission_denied(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    fake.acquire_status = M65_TRANSPORT_PERMISSION;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_PERMISSION);
    EXPECT_EQ_U64(fake.release_count, 0U);
    EXPECT_FALSE(report.exclusive_acquired);
    m65_1581_report_destroy(&report);
}

static void test_unplugged_and_timeout(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    fake.fail_opcode = M65_OPCODE_READ_10;
    fake.fail_occurrence = 1U;
    fake.fail_status = M65_TRANSPORT_NO_DEVICE;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_NO_DEVICE);
    EXPECT_EQ_INT(report.status, M65_1581_INCONCLUSIVE);
    EXPECT_TRUE(report.controller_restored);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);

    fake_transport_init(&fake);
    fake.fail_opcode = M65_OPCODE_READ_10;
    fake.fail_occurrence = 3U;
    fake.fail_status = M65_TRANSPORT_TIMEOUT;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_TRANSPORT);
    EXPECT_EQ_INT(report.status, M65_1581_INCONCLUSIVE);
    EXPECT_TRUE(report.controller_restored);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);
}

static void test_mismatched_reads(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    fake.mismatch_repeated_read = true;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_MISMATCH);
    EXPECT_EQ_INT(report.status, M65_1581_INCONCLUSIVE);
    EXPECT_FALSE(report.repeated_reads_match);
    EXPECT_TRUE(report.controller_restored);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);
}

static void test_failure_cleanup_matrix(void)
{
    static const struct {
        uint8_t opcode;
        unsigned int occurrence;
    } failures[] = {
        {M65_OPCODE_MODE_SENSE_10, 1U},
        {M65_OPCODE_MODE_SENSE_10, 2U},
        {M65_OPCODE_MODE_SELECT_10, 1U},
        {M65_OPCODE_MODE_SENSE_10, 3U},
        {M65_OPCODE_READ_10, 1U},
        {M65_OPCODE_READ_10, 4U}
    };
    size_t index;
    for (index = 0U; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        FakeTransport fake;
        M651581Report report;
        fake_transport_init(&fake);
        fake.fail_opcode = failures[index].opcode;
        fake.fail_occurrence = failures[index].occurrence;
        fake.fail_status = M65_TRANSPORT_IO;
        EXPECT_TRUE(m65_test_1581(&fake.transport, true, &report) != M65_PROBE_OK);
        expect_cleanup(&fake, true);
        m65_1581_report_destroy(&report);
    }
}

static void test_release_failure(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    fake.release_status = M65_TRANSPORT_IO;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_TRANSPORT);
    EXPECT_EQ_INT(report.status, M65_1581_INCONCLUSIVE);
    EXPECT_EQ_U64(fake.release_count, 1U);
    EXPECT_TRUE(fake_transport_is_restored(&fake));
    m65_1581_report_destroy(&report);
}

static void test_interrupt_restores(void)
{
    FakeTransport fake;
    M651581Report report;
    fake_transport_init(&fake);
    fake.interrupt_after_desired = true;
    EXPECT_EQ_INT(m65_test_1581(&fake.transport, true, &report),
                  M65_PROBE_TRANSPORT);
    EXPECT_EQ_INT(report.status, M65_1581_INCONCLUSIVE);
    EXPECT_TRUE(report.controller_restored);
    expect_cleanup(&fake, true);
    m65_1581_report_destroy(&report);
}

void test_probe(void)
{
    test_inspect_success();
    test_acknowledgement_refusal();
    test_supported();
    test_unsupported();
    test_permission_denied();
    test_unplugged_and_timeout();
    test_mismatched_reads();
    test_failure_cleanup_matrix();
    test_release_failure();
    test_interrupt_restores();
}
