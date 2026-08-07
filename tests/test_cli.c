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
}
