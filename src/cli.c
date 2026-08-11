#include "m65/cli.h"

#include <stdio.h>
#include <string.h>

static bool set_once(const char **destination, const char *value,
                     const char *option, char *detail, size_t detail_size)
{
    if (*destination != NULL) {
        (void)snprintf(detail, detail_size, "%s was specified more than once", option);
        return false;
    }
    *destination = value;
    return true;
}

bool m65_cli_parse(int argc, char **argv, M65CliOptions *options,
                   char *detail, size_t detail_size)
{
    int index;
    if (options == NULL || detail == NULL || detail_size == 0U) {
        return false;
    }
    (void)memset(options, 0, sizeof(*options));
    detail[0] = '\0';
    if (argc < 2) {
        (void)snprintf(detail, detail_size, "missing command");
        return false;
    }
    if (strcmp(argv[1], "list") == 0) {
        options->command = M65_CLI_LIST;
    } else if (strcmp(argv[1], "inspect") == 0) {
        options->command = M65_CLI_INSPECT;
    } else if (strcmp(argv[1], "test-1581") == 0) {
        options->command = M65_CLI_TEST_1581;
    } else if (strcmp(argv[1], "diagnose") == 0) {
        options->command = M65_CLI_DIAGNOSE;
    } else {
        (void)snprintf(detail, detail_size, "unknown command: %s", argv[1]);
        return false;
    }

    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--json") == 0) {
            if (options->json) {
                (void)snprintf(detail, detail_size, "--json was specified more than once");
                return false;
            }
            options->json = true;
        } else if (strcmp(argv[index], "--ack-temporary-controller-change") == 0) {
            if (options->acknowledgement) {
                (void)snprintf(detail, detail_size,
                               "acknowledgement flag was specified more than once");
                return false;
            }
            options->acknowledgement = true;
        } else if (strcmp(argv[index], "--device") == 0) {
            if (index + 1 >= argc) {
                (void)snprintf(detail, detail_size, "--device requires a BSD name");
                return false;
            }
            ++index;
            if (!set_once(&options->device, argv[index], "--device",
                          detail, detail_size)) {
                return false;
            }
        } else if (strcmp(argv[index], "--output") == 0) {
            if (index + 1 >= argc) {
                (void)snprintf(detail, detail_size, "--output requires a file path");
                return false;
            }
            ++index;
            if (!set_once(&options->output, argv[index], "--output",
                          detail, detail_size)) {
                return false;
            }
        } else {
            (void)snprintf(detail, detail_size, "unknown option: %s", argv[index]);
            return false;
        }
    }

    if (options->command == M65_CLI_DIAGNOSE) {
        if (options->output != NULL || options->acknowledgement) {
            (void)snprintf(detail, detail_size,
                           "diagnose accepts only --device and --json");
            return false;
        }
        if (options->device == NULL) {
            (void)snprintf(detail, detail_size,
                           "diagnose requires --device <bsd-name>");
            return false;
        }
        return true;
    }

    if (options->command == M65_CLI_LIST) {
        if (options->device != NULL || options->output != NULL || options->acknowledgement) {
            (void)snprintf(detail, detail_size,
                           "list accepts only the optional --json flag");
            return false;
        }
        return true;
    }
    if (options->device == NULL) {
        (void)snprintf(detail, detail_size, "%s requires --device <bsd-name>",
                       options->command == M65_CLI_INSPECT ? "inspect" : "test-1581");
        return false;
    }
    if (options->command == M65_CLI_INSPECT) {
        if (options->output != NULL || options->acknowledgement) {
            (void)snprintf(detail, detail_size,
                           "inspect does not accept --output or the acknowledgement flag");
            return false;
        }
        return true;
    }
    if (!options->acknowledgement) {
        (void)snprintf(detail, detail_size,
                       "test-1581 requires --ack-temporary-controller-change");
        return false;
    }
    return true;
}

void m65_cli_usage(const char *program)
{
    (void)fprintf(stderr,
        "Usage:\n"
        "  %s list [--json]\n"
        "  %s inspect --device <bsd-name> [--json]\n"
        "  %s test-1581 --device <bsd-name> "
        "--ack-temporary-controller-change [--json] [--output <new-file.d81>]\n"
        "  %s diagnose --device <bsd-name> [--json]\n",
        program, program, program, program);
}
