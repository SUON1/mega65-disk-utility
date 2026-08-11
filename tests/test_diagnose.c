#include "test.h"

#include "m65/probe.h"

/*
 * m65_diagnose_exit_code lives in the portable core (src/probe.c) precisely so
 * that the diagnose exit-code contract can be exercised without the macOS
 * IOUSBHost transport.  These cases pin the mapping the CLI relies on.
 */
void test_diagnose(void)
{
    /* Transport could not be created. */
    /* Device genuinely absent -> benign "not found" exit 1. */
    EXPECT_EQ_INT(m65_diagnose_exit_code(false, M65_TRANSPORT_NO_DEVICE,
                                         M65_PROBE_OK), 1);
    /* Any other creation failure is a hard error -> exit 2. */
    EXPECT_EQ_INT(m65_diagnose_exit_code(false, M65_TRANSPORT_PERMISSION,
                                         M65_PROBE_OK), 2);
    EXPECT_EQ_INT(m65_diagnose_exit_code(false, M65_TRANSPORT_TIMEOUT,
                                         M65_PROBE_OK), 2);
    EXPECT_EQ_INT(m65_diagnose_exit_code(false, M65_TRANSPORT_PROTOCOL,
                                         M65_PROBE_OK), 2);
    EXPECT_EQ_INT(m65_diagnose_exit_code(false, M65_TRANSPORT_IO,
                                         M65_PROBE_OK), 2);

    /* Transport created: the inspect outcome drives the exit code. */
    EXPECT_EQ_INT(m65_diagnose_exit_code(true, M65_TRANSPORT_OK,
                                         M65_PROBE_OK), 0);
    EXPECT_EQ_INT(m65_diagnose_exit_code(true, M65_TRANSPORT_OK,
                                         M65_PROBE_NO_DEVICE), 1);
    EXPECT_EQ_INT(m65_diagnose_exit_code(true, M65_TRANSPORT_OK,
                                         M65_PROBE_PERMISSION), 2);
    /* Every remaining inspect failure collapses to the generic error exit 4. */
    EXPECT_EQ_INT(m65_diagnose_exit_code(true, M65_TRANSPORT_OK,
                                         M65_PROBE_TRANSPORT), 4);
    EXPECT_EQ_INT(m65_diagnose_exit_code(true, M65_TRANSPORT_OK,
                                         M65_PROBE_UNSUPPORTED), 4);
    EXPECT_EQ_INT(m65_diagnose_exit_code(true, M65_TRANSPORT_OK,
                                         M65_PROBE_MISMATCH), 4);
}
