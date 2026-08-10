#include "m65/cli.h"
#include "m65/device.h"
#include "m65/discovery.h"
#include "m65/output.h"
#include "m65/probe.h"
#include "m65/transport_iousbhost_bridge.h"
#include "m65/ufi.h"
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
    case M65_CLI_DIAGNOSE:
        return "diagnose";
    }
    return "invalid";
}

#define M65_UFI_BLOCK_LEN 12U

/*
 * diagnose exercises the IOUSBHost whole-device capture path with data-in UFI
 * commands only. It never sends FORMAT UNIT, WRITE, MODE SELECT, or any other
 * data-out opcode. INQUIRY, REQUEST SENSE, READ CAPACITY, and MODE SENSE are
 * all device-to-host transfers.
 */
static int diagnose_send_in(m65_iousbhost_ctx_t *ctx, const uint8_t *cdb,
                            uint8_t *data, size_t data_len)
{
    uint8_t status[2] = {0U, 0U};
    (void)memset(data, 0, data_len);
    return m65_iousbhost_send_ufi(ctx, cdb, (uint8_t)M65_UFI_BLOCK_LEN,
                                  data, data_len, 1, status);
}

static int run_diagnose(const M65CliOptions *options)
{
    M65DiagnoseReport report;
    m65_iousbhost_ctx_t *ctx = NULL;
    uint8_t cdb[16];
    uint8_t inquiry[36];
    uint8_t sense_buf[18];
    uint8_t capacity[8];
    uint8_t mode[96];
    char parse_detail[M65_MAX_ERROR_TEXT] = "";
    bool human = !options->json;
    bool ufi_failed = false;
    int rc;

    (void)memset(&report, 0, sizeof(report));
    report.vid = options->vid;
    report.pid = options->pid;

    if (human) {
        (void)printf("diagnose: searching for %04x:%04x class=08/04/00...\n",
                     (unsigned int)report.vid, (unsigned int)report.pid);
    }

    rc = m65_iousbhost_open(report.vid, report.pid, &ctx);
    if (rc != M65_IOUSBHOST_OK) {
        switch (rc) {
        case M65_IOUSBHOST_ERR_NOT_FOUND:
            report.exit_code = 1;
            (void)snprintf(report.reason, sizeof(report.reason),
                           "interface service not found in IORegistry");
            break;
        case M65_IOUSBHOST_ERR_PIPE:
            report.exit_code = 3;
            (void)snprintf(report.reason, sizeof(report.reason),
                           "endpoint discovery or pipe open failed");
            break;
        case M65_IOUSBHOST_ERR_CAPTURE:
        default:
            report.exit_code = 2;
            (void)snprintf(report.reason, sizeof(report.reason),
                           "IOUSBHost DeviceCapture failed (see stderr for IOReturn)");
            break;
        }
        if (human) {
            (void)printf("diagnose: %s\n", report.reason);
        } else {
            (void)m65_output_diagnose_json(&report);
        }
        return report.exit_code;
    }

    report.captured = true;
    report.alt_setting_ok = true;
    if (human) {
        (void)printf("diagnose: IOUSBHost capture: OK\n");
        (void)printf("diagnose: endpoints:\n");
        m65_iousbhost_print_endpoints(ctx);
        (void)printf("diagnose: selectAlternateSetting 0: OK\n");
    }

    /* Step 5 — INQUIRY (data-in). Failure here is fatal (exit 4). */
    (void)m65_cdb_inquiry(cdb, (uint8_t)sizeof(inquiry));
    if (diagnose_send_in(ctx, cdb, inquiry, sizeof(inquiry)) == M65_IOUSBHOST_OK &&
        m65_parse_inquiry(inquiry, sizeof(inquiry), &report.inquiry,
                          parse_detail, sizeof(parse_detail))) {
        report.inquiry_ok = true;
        if (human) {
            (void)printf("diagnose: INQUIRY: vendor=\"%s\" product=\"%s\" rev=\"%s\"\n",
                         report.inquiry.vendor, report.inquiry.product,
                         report.inquiry.firmware);
        }
    } else {
        report.exit_code = 4;
        (void)snprintf(report.reason, sizeof(report.reason),
                       "INQUIRY failed over IOUSBHost transport");
        if (human) {
            (void)fprintf(stderr, "diagnose: INQUIRY failed\n");
        }
        goto teardown;
    }

    /* Step 6 — REQUEST SENSE (data-in). Log and continue. */
    (void)m65_cdb_request_sense(cdb, (uint8_t)sizeof(sense_buf));
    if (diagnose_send_in(ctx, cdb, sense_buf, sizeof(sense_buf)) == M65_IOUSBHOST_OK &&
        m65_parse_sense(sense_buf, sizeof(sense_buf), &report.sense)) {
        report.request_sense_ok = true;
        if (human) {
            (void)printf("diagnose: REQUEST SENSE: SK=0x%02x ASC=0x%02x ASCQ=0x%02x%s\n",
                         (unsigned int)report.sense.key,
                         (unsigned int)report.sense.asc,
                         (unsigned int)report.sense.ascq,
                         (report.sense.key == 0U && report.sense.asc == 0U &&
                          report.sense.ascq == 0U) ? " (no error)" : "");
        }
    } else {
        ufi_failed = true;
        if (human) {
            (void)fprintf(stderr, "diagnose: REQUEST SENSE failed\n");
        }
    }

    /* Step 7 — READ CAPACITY (10) (data-in). Log and continue. */
    (void)m65_cdb_read_capacity_10(cdb);
    if (diagnose_send_in(ctx, cdb, capacity, sizeof(capacity)) == M65_IOUSBHOST_OK &&
        m65_parse_read_capacity_10(capacity, sizeof(capacity), &report.capacity,
                                   parse_detail, sizeof(parse_detail))) {
        report.read_capacity_ok = true;
        if (human) {
            unsigned long long bytes = (unsigned long long)report.capacity.blocks *
                                       (unsigned long long)report.capacity.block_size;
            (void)printf("diagnose: READ CAPACITY: lastLBA=%u blockSize=%u "
                         "(%u sectors / %llu bytes)\n",
                         (unsigned int)(report.capacity.blocks - 1U),
                         (unsigned int)report.capacity.block_size,
                         (unsigned int)report.capacity.blocks, bytes);
        }
    } else {
        ufi_failed = true;
        if (human) {
            (void)fprintf(stderr, "diagnose: READ CAPACITY failed\n");
        }
    }

    /* Step 8 — MODE SENSE (10) Flexible Disk page 0x05 (data-in). Log and continue. */
    {
        M65ModeParameters mode_params;
        (void)m65_cdb_mode_sense_10(cdb, false, (uint16_t)sizeof(mode));
        if (diagnose_send_in(ctx, cdb, mode, sizeof(mode)) == M65_IOUSBHOST_OK &&
            m65_parse_mode_parameters(mode, sizeof(mode), &mode_params,
                                      parse_detail, sizeof(parse_detail))) {
            report.mode_sense_ok = true;
            report.flexible = mode_params.flexible;
            if (human) {
                (void)printf("diagnose: MODE SENSE Flexible Disk page:\n");
                (void)printf("  sectors/track=%u heads=%u tracks=%u data-rate=%u rpm=%u\n",
                             (unsigned int)report.flexible.sectors_per_track,
                             (unsigned int)report.flexible.heads,
                             (unsigned int)report.flexible.cylinders,
                             (unsigned int)report.flexible.transfer_rate_kbit,
                             (unsigned int)report.flexible.medium_rotation_rate_rpm);
            }
        } else {
            ufi_failed = true;
            if (human) {
                (void)fprintf(stderr, "diagnose: MODE SENSE failed\n");
            }
        }
    }

    if (ufi_failed) {
        report.exit_code = 4;
        (void)snprintf(report.reason, sizeof(report.reason),
                       "at least one data-in UFI command failed over IOUSBHost");
    }

teardown:
    /* Step 9 — destroy: resets the device so the mass-storage driver re-registers. */
    m65_iousbhost_close(ctx);
    report.destroyed = true;
    if (human) {
        (void)printf("diagnose: destroy: OK\n");
    }

    if (report.exit_code == 0 && report.reason[0] == '\0') {
        (void)snprintf(report.reason, sizeof(report.reason),
                       "all diagnose steps completed over IOUSBHost");
    }
    if (!human) {
        (void)m65_output_diagnose_json(&report);
    }
    return report.exit_code;
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
    if (options.command == M65_CLI_DIAGNOSE) {
        return run_diagnose(&options);
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
