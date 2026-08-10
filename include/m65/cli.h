#ifndef M65_CLI_H
#define M65_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Default TEAC USB floppy identity used by the diagnose command. */
#define M65_CLI_DEFAULT_VID 0x0644U
#define M65_CLI_DEFAULT_PID 0x0000U

typedef enum {
    M65_CLI_LIST = 0,
    M65_CLI_INSPECT,
    M65_CLI_TEST_1581,
    M65_CLI_DIAGNOSE
} M65CliCommand;

typedef struct {
    M65CliCommand command;
    const char *device;
    const char *output;
    bool json;
    bool acknowledgement;
    uint16_t vid;
    uint16_t pid;
} M65CliOptions;

bool m65_cli_parse(int argc, char **argv, M65CliOptions *options,
                   char *detail, size_t detail_size);
void m65_cli_usage(const char *program);

#endif
