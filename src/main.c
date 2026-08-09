#include "m65/cli.h"
#include "m65/device.h"
#include "m65/discovery.h"
#include "m65/output.h"
#include "m65/probe.h"
#include "m65/usb_cbi_macos.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int exit_code(M65ProbeCode code)
{
    switch (code) {
    case M65_PROBE_OK:
        return 0;
    case M65_PROBE_NO_DEVICE:
        return 2;
    case M65_PROBE_PERMISSION:
        return 3;
    case M65_PROBE_TRANSPORT:
        return 4;
    case M65_PROBE_UNSUPPORTED:
        return 5;
    case M65_PROBE_MISMATCH:
        return 6;
    case M65_PROBE_USAGE:
        return 64;
    }
    return 4;
}

static bool argv_has_json(int argc, char **argv)
{
    int index;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--json") == 0) {
            return true;
        }
    }
    return false;
}

static void signal_handler(int signal_number)
{
    (void)signal_number;
    m65_probe_request_interrupt();
}

static bool forbidden_output_path(const char *path)
{
    return path == NULL || strncmp(path, "/dev/disk", 9U) == 0 ||
           strncmp(path, "/dev/rdisk", 10U) == 0;
}

static bool write_new_image(const char *path, const uint8_t *image, size_t length,
                            char *detail, size_t detail_size)
{
    int descriptor;
    size_t written = 0U;
    if (forbidden_output_path(path)) {
        (void)snprintf(detail, detail_size,
                       "output must be a new regular file, never a /dev disk node");
        return false;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        (void)snprintf(detail, detail_size, "cannot create output file exclusively: %s",
                       strerror(errno));
        return false;
    }
    while (written < length) {
        ssize_t result = write(descriptor, &image[written], length - written);
        if (result <= 0) {
            int saved_errno = errno;
            (void)close(descriptor);
            (void)unlink(path);
            (void)snprintf(detail, detail_size, "output write failed: %s",
                           result == 0 ? "zero-byte write" : strerror(saved_errno));
            return false;
        }
        written += (size_t)result;
    }
    if (close(descriptor) != 0) {
        int saved_errno = errno;
        (void)unlink(path);
        (void)snprintf(detail, detail_size, "closing output failed: %s",
                       strerror(saved_errno));
        return false;
    }
    return true;
}

static void emit_usage_json(const char *reason)
{
    (void)m65_output_error_json("invalid", NULL, reason);
}

static const char *command_name(M65CliCommand command)
{
    switch (command) {
    case M65_CLI_LIST:
        return "list";
    case M65_CLI_INSPECT:
        return "inspect";
    case M65_CLI_TEST_1581:
        return "test-1581";
    }
    return "invalid";
}

static int run_selected(const M65CliOptions *options, const M65DeviceInfo *device)
{
    M65TransportStatus create_status;
    M65Transport *transport;
    char detail[M65_MAX_ERROR_TEXT] = "";
    int result_code;
    transport = m65_usb_cbi_transport_create(device->bsd_name, detail, sizeof(detail),
                                             &create_status);
    if (options->command == M65_CLI_INSPECT) {
        M65InspectReport report;
        (void)memset(&report, 0, sizeof(report));
        if (transport == NULL) {
            report.code = create_status == M65_TRANSPORT_PERMISSION ?
                          M65_PROBE_PERMISSION :
                          (create_status == M65_TRANSPORT_NO_DEVICE ?
                           M65_PROBE_NO_DEVICE : M65_PROBE_TRANSPORT);
            (void)snprintf(report.reason, sizeof(report.reason), "%s", detail);
        } else {
            (void)m65_inspect(transport, &report);
            transport->ops->destroy(transport);
        }
        if (options->json) {
            (void)m65_output_inspect_json(device, &report);
        } else {
            m65_output_inspect_human(device, &report);
        }
        return exit_code(report.code);
    }
    {
        M651581Report report;
        (void)memset(&report, 0, sizeof(report));
        if (transport == NULL) {
            report.status = M65_1581_INCONCLUSIVE;
            report.code = create_status == M65_TRANSPORT_PERMISSION ?
                          M65_PROBE_PERMISSION :
                          (create_status == M65_TRANSPORT_NO_DEVICE ?
                           M65_PROBE_NO_DEVICE : M65_PROBE_TRANSPORT);
            (void)snprintf(report.reason, sizeof(report.reason), "%s", detail);
        } else {
            (void)signal(SIGINT, signal_handler);
            (void)signal(SIGTERM, signal_handler);
            (void)m65_test_1581(transport, options->acknowledgement, &report);
            transport->ops->destroy(transport);
            (void)signal(SIGINT, SIG_DFL);
            (void)signal(SIGTERM, SIG_DFL);
        }
        if (report.code == M65_PROBE_OK && options->output != NULL &&
            !write_new_image(options->output, report.image, report.image_length,
                             detail, sizeof(detail))) {
            report.code = M65_PROBE_TRANSPORT;
            report.status = M65_1581_INCONCLUSIVE;
            (void)snprintf(report.reason, sizeof(report.reason),
                           "hardware read succeeded but %s", detail);
        }
        if (options->json) {
            (void)m65_output_1581_json(device, &report, options->output);
        } else {
            m65_output_1581_human(device, &report, options->output);
        }
        result_code = exit_code(report.code);
        m65_1581_report_destroy(&report);
    }
    return result_code;
}

int main(int argc, char **argv)
{
    M65CliOptions options;
    M65DeviceList devices;
    const M65DeviceInfo *selected;
    M65DeviceValidation validation;
    char detail[M65_MAX_ERROR_TEXT] = "";
    bool ambiguous = false;

    if (!m65_cli_parse(argc, argv, &options, detail, sizeof(detail))) {
        if (argv_has_json(argc, argv)) {
            emit_usage_json(detail);
        } else {
            (void)fprintf(stderr, "error: %s\n", detail);
            m65_cli_usage(argv[0]);
        }
        return 64;
    }
    if (!m65_discover_devices(&devices, detail, sizeof(detail))) {
        if (options.json) {
            (void)m65_output_error_json(
                command_name(options.command), NULL, detail);
        } else {
            (void)fprintf(stderr, "discovery failed: %s\n", detail);
        }
        return 4;
    }
    if (options.command == M65_CLI_LIST) {
        if (options.json) {
            (void)m65_output_list_json(&devices,
                                       devices.count == 0U ? "no matching device" : NULL);
        } else {
            m65_output_list_human(&devices,
                                  devices.count == 0U ? "no matching device" : NULL);
        }
        return devices.count == 0U ? 2 : 0;
    }

    selected = m65_select_device(&devices, options.device, &ambiguous);
    if (selected == NULL) {
        (void)snprintf(detail, sizeof(detail), "%s",
                       ambiguous ? "multiple matching devices; specify --device" :
                       "no matching device with that BSD name");
        if (options.json) {
            (void)m65_output_error_json(
                command_name(options.command), selected, detail);
        } else {
            (void)fprintf(stderr, "%s\n", detail);
        }
        return 2;
    }
    validation = m65_validate_device(selected);
    if (validation != M65_DEVICE_ACCEPTED) {
        (void)snprintf(detail, sizeof(detail), "%s",
                       m65_device_validation_text(validation));
        if (options.json) {
            (void)m65_output_error_json(
                command_name(options.command), selected, detail);
        } else {
            (void)fprintf(stderr, "refusing device %s: %s\n",
                          selected->bsd_name, detail);
        }
        return 2;
    }
    return run_selected(&options, selected);
}
