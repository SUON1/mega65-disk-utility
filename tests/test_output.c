#include "test.h"

#include "m65/output.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef bool (*OutputWriter)(const void *context);

typedef struct {
    const M65DeviceInfo *device;
    const M65InspectReport *report;
} InspectContext;

typedef struct {
    const M65DeviceInfo *device;
    const M651581Report *report;
} Test1581Context;

static bool write_inspect(const void *context)
{
    const InspectContext *inspect = (const InspectContext *)context;
    return m65_output_inspect_json(inspect->device, inspect->report);
}

static bool write_diagnose(const void *context)
{
    return m65_output_diagnose_json((const M65DiagnoseReport *)context);
}

static bool write_1581(const void *context)
{
    const Test1581Context *test = (const Test1581Context *)context;
    return m65_output_1581_json(test->device, test->report, NULL);
}

static char *capture_output(OutputWriter writer, const void *context)
{
    FILE *temporary = tmpfile();
    int saved_stdout;
    long length;
    size_t bytes;
    char *output;

    if (temporary == NULL) {
        return NULL;
    }
    (void)fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(fileno(temporary), STDOUT_FILENO) < 0) {
        if (saved_stdout >= 0) {
            (void)close(saved_stdout);
        }
        (void)fclose(temporary);
        return NULL;
    }

    EXPECT_TRUE(writer(context));
    (void)fflush(stdout);
    length = ftell(temporary);
    if (length < 0L || fseek(temporary, 0L, SEEK_SET) != 0) {
        (void)dup2(saved_stdout, STDOUT_FILENO);
        (void)close(saved_stdout);
        (void)fclose(temporary);
        return NULL;
    }
    bytes = (size_t)length;
    output = (char *)malloc(bytes + 1U);
    if (output != NULL) {
        size_t read_bytes = fread(output, 1U, bytes, temporary);
        output[read_bytes] = '\0';
    }
    (void)dup2(saved_stdout, STDOUT_FILENO);
    (void)close(saved_stdout);
    (void)fclose(temporary);
    return output;
}

static M65DeviceInfo sample_device(void)
{
    M65DeviceInfo device;
    (void)memset(&device, 0, sizeof(device));
    (void)snprintf(device.bsd_name, sizeof(device.bsd_name), "disk6");
    (void)snprintf(device.media_name, sizeof(device.media_name),
                   "TEAC USB UF000x");
    (void)snprintf(device.usb_manufacturer, sizeof(device.usb_manufacturer),
                   "TEACV0.0");
    (void)snprintf(device.usb_product, sizeof(device.usb_product), "TEACV0.0");
    device.usb_vid = 0x0644U;
    device.usb_pid = 0x0000U;
    device.usb_device_revision = 0x0200U;
    device.known_controller = true;
    device.media_present = true;
    device.external = true;
    device.removable = true;
    device.whole = true;
    device.block_size = 512U;
    device.capacity_bytes = 737280U;
    return device;
}

static M65InspectReport sample_inspect_report(void)
{
    M65InspectReport report;
    (void)memset(&report, 0, sizeof(report));
    report.code = M65_PROBE_OK;
    (void)snprintf(report.reason, sizeof(report.reason), "inspection completed");
    report.exclusive_acquired = true;
    report.exclusive_released = true;
    report.unit_ready = true;
    (void)snprintf(report.inquiry.vendor, sizeof(report.inquiry.vendor), "TEAC");
    report.capacity.blocks = 1440U;
    report.capacity.block_size = 512U;
    report.current_mode.flexible.transfer_rate_kbit = 250U;
    report.current_mode.flexible.heads = 2U;
    report.current_mode.flexible.sectors_per_track = 9U;
    report.current_mode.flexible.bytes_per_sector = 512U;
    report.current_mode.flexible.cylinders = 80U;
    report.current_mode.flexible.medium_rotation_rate_rpm = 300U;
    report.current_mode.flexible.field_changeable[M65_FLEX_TRANSFER_RATE] = true;
    report.current_mode.flexible.field_changeable[M65_FLEX_SECTORS_PER_TRACK] = true;
    report.changeable_mode.flexible.page[0] = M65_FLEXIBLE_DISK_PAGE;
    report.changeable_mode.flexible.page[1] = 30U;
    report.changeable_mode.flexible.page[2] = 0xaaU;
    return report;
}

static void test_inspect_serializer(void)
{
    M65DeviceInfo device = sample_device();
    M65InspectReport report = sample_inspect_report();
    InspectContext context = {&device, &report};
    char *output = capture_output(write_inspect, &context);

    EXPECT_TRUE(output != NULL);
    if (output == NULL) {
        return;
    }
    EXPECT_TRUE(strstr(output, "\"bsd_name\":\"disk6\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"numeric\":1604") != NULL);
    EXPECT_TRUE(strstr(output, "\"backend\":\"iousbhost\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"capture_acquired\":true") != NULL);
    EXPECT_TRUE(strstr(output, "\"capture_released\":true") != NULL);
    EXPECT_TRUE(strstr(output, "\"changeable_fields\":[\"transfer_rate\",\"sectors_per_track\"]") != NULL);
    EXPECT_TRUE(strstr(output, "\"flexible_disk_changeable_mask\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"raw_hex\":\"051eaa") != NULL);
    EXPECT_TRUE(strstr(output, "changeable_flexible_disk") == NULL);
    free(output);
}

static void test_inspect_failed_changeable_response(void)
{
    M65DeviceInfo device = sample_device();
    M65InspectReport report = sample_inspect_report();
    InspectContext context = {&device, &report};
    char *output;
    report.code = M65_PROBE_TRANSPORT;
    (void)snprintf(report.reason, sizeof(report.reason),
                   "MODE SENSE UFI page header 0x4e at byte 8 has reserved bit 6 set");
    (void)memset(&report.changeable_mode, 0,
                 sizeof(report.changeable_mode));
    report.changeable_mode.raw_length = M65_UFI_ALL_MODE_LENGTH;
    report.changeable_mode.raw[0] = 0U;
    report.changeable_mode.raw[1] = 70U;
    report.changeable_mode.raw[8] = 0x41U;
    report.changeable_mode.raw[9] = 10U;

    output = capture_output(write_inspect, &context);
    EXPECT_TRUE(output != NULL);
    if (output == NULL) {
        return;
    }
    EXPECT_TRUE(strstr(output,
                       "\"mode_sense_changeable_response\":{\"length\":72") != NULL);
    EXPECT_TRUE(strstr(output, "\"raw_hex\":\"0046") != NULL);
    EXPECT_TRUE(strstr(output,
                       "\"flexible_disk_changeable_mask\"") == NULL);
    EXPECT_TRUE(strstr(output, "\"changeable_fields\"") == NULL);
    free(output);
}

static void test_diagnose_capture_failure(void)
{
    M65DiagnoseReport report;
    char *output;
    (void)memset(&report, 0, sizeof(report));
    report.device = sample_device();
    report.transport_created = true;
    report.destroy_called = true;
    report.inspect.code = M65_PROBE_PERMISSION;
    report.inspect.sense.valid = true;
    report.inspect.sense.key = 0x05U;
    report.inspect.sense.asc = 0x20U;
    report.exit_code = 2;
    (void)snprintf(report.reason, sizeof(report.reason),
                   "DeviceCapture permission denied");

    output = capture_output(write_diagnose, &report);
    EXPECT_TRUE(output != NULL);
    if (output == NULL) {
        return;
    }
    EXPECT_TRUE(strstr(output, "\"transport_created\":true") != NULL);
    EXPECT_TRUE(strstr(output, "\"capture_acquired\":false") != NULL);
    EXPECT_TRUE(strstr(output, "\"capture_released\":false") != NULL);
    EXPECT_TRUE(strstr(output, "\"destroy_called\":true") != NULL);
    EXPECT_TRUE(strstr(output, "\"driver_media_rematch\":\"unverified\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"status\":\"error\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"exit_code\":2") != NULL);
    EXPECT_TRUE(strstr(output, "\"sense_key\":5") != NULL);
    EXPECT_TRUE(strstr(output, "\"asc\":32") != NULL);
    free(output);
}

static void test_1581_transport_state(void)
{
    M65DeviceInfo device = sample_device();
    M651581Report report;
    Test1581Context context;
    char *output;
    (void)memset(&report, 0, sizeof(report));
    report.code = M65_PROBE_PERMISSION;
    report.status = M65_1581_INCONCLUSIVE;
    report.acknowledgement_supplied = true;
    (void)snprintf(report.reason, sizeof(report.reason), "capture denied");
    context.device = &device;
    context.report = &report;
    output = capture_output(write_1581, &context);
    EXPECT_TRUE(output != NULL);
    if (output == NULL) {
        return;
    }
    EXPECT_TRUE(strstr(output, "\"backend\":\"iousbhost\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"capture_acquired\":false") != NULL);
    EXPECT_TRUE(strstr(output, "\"capture_released\":false") != NULL);
    EXPECT_TRUE(strstr(output, "\"status\":\"inconclusive\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"reason\":\"capture denied\"") != NULL);
    free(output);
}

void test_output(void)
{
    test_inspect_serializer();
    test_inspect_failed_changeable_response();
    test_diagnose_capture_failure();
    test_1581_transport_state();
}
