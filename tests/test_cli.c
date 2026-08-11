#include "test.h"

#include "m65/cli.h"

void test_cli(void)
{
    M65CliOptions options;
    char detail[256];
    char *list_args[] = {"probe", "list", "--json"};
    char *inspect_args[] = {"probe", "inspect", "--device", "disk4"};
    char *missing_ack[] = {"probe", "test-1581", "--device", "disk4"};
    char *test_args[] = {"probe", "test-1581", "--device", "disk4",
                         "--ack-temporary-controller-change", "--output", "new.d81",
                         "--json"};
    char *diagnose_missing_device[] = {"probe", "diagnose", "--json"};
    char *diagnose_args[] = {"probe", "diagnose", "--device", "disk4",
                             "--vid", "0x0644", "--pid", "0x0000", "--json"};
    char *diagnose_reject_output[] = {"probe", "diagnose", "--device", "disk4",
                                      "--output", "new.d81"};
    char *diagnose_reject_ack[] = {"probe", "diagnose", "--device", "disk4",
                                   "--ack-temporary-controller-change"};
    char *inspect_reject_vid[] = {"probe", "inspect", "--device", "disk4",
                                  "--vid", "0x0644"};
    EXPECT_TRUE(m65_cli_parse(3, list_args, &options, detail, sizeof(detail)));
    EXPECT_EQ_INT(options.command, M65_CLI_LIST);
    EXPECT_TRUE(options.json);
    EXPECT_TRUE(m65_cli_parse(4, inspect_args, &options, detail, sizeof(detail)));
    EXPECT_EQ_INT(options.command, M65_CLI_INSPECT);
    EXPECT_STREQ(options.device, "disk4");
    EXPECT_FALSE(m65_cli_parse(4, missing_ack, &options, detail, sizeof(detail)));
    EXPECT_TRUE(strstr(detail, "--ack-temporary-controller-change") != NULL);
    EXPECT_TRUE(m65_cli_parse(8, test_args, &options, detail, sizeof(detail)));
    EXPECT_EQ_INT(options.command, M65_CLI_TEST_1581);
    EXPECT_TRUE(options.acknowledgement);
    EXPECT_TRUE(options.json);
    EXPECT_STREQ(options.output, "new.d81");

    /* diagnose now REQUIRES --device (finding #5: correlate capture device). */
    EXPECT_FALSE(m65_cli_parse(3, diagnose_missing_device, &options, detail,
                               sizeof(detail)));
    EXPECT_TRUE(strstr(detail, "--device") != NULL);

    /* diagnose accepts --device plus optional --vid/--pid/--json. */
    EXPECT_TRUE(m65_cli_parse(9, diagnose_args, &options, detail, sizeof(detail)));
    EXPECT_EQ_INT(options.command, M65_CLI_DIAGNOSE);
    EXPECT_STREQ(options.device, "disk4");
    EXPECT_EQ_INT(options.vid, 0x0644);
    EXPECT_EQ_INT(options.pid, 0x0000);
    EXPECT_TRUE(options.json);

    /* diagnose is strictly read-only: --output and the ack flag are rejected. */
    EXPECT_FALSE(m65_cli_parse(6, diagnose_reject_output, &options, detail,
                               sizeof(detail)));
    EXPECT_FALSE(m65_cli_parse(5, diagnose_reject_ack, &options, detail,
                               sizeof(detail)));

    /* --vid/--pid remain diagnose-only for the other commands. */
    EXPECT_FALSE(m65_cli_parse(6, inspect_reject_vid, &options, detail,
                               sizeof(detail)));
    EXPECT_TRUE(strstr(detail, "--vid") != NULL);
}
