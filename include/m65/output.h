#ifndef M65_OUTPUT_H
#define M65_OUTPUT_H

#include "m65/device.h"
#include "m65/probe.h"

#include <stdbool.h>

bool m65_output_list_json(const M65DeviceList *list, const char *error);
void m65_output_list_human(const M65DeviceList *list, const char *error);
bool m65_output_error_json(const char *command, const M65DeviceInfo *device,
                           const char *error);
bool m65_output_inspect_json(const M65DeviceInfo *device,
                             const M65InspectReport *report);
void m65_output_inspect_human(const M65DeviceInfo *device,
                              const M65InspectReport *report);
bool m65_output_1581_json(const M65DeviceInfo *device,
                          const M651581Report *report,
                          const char *output_path);
void m65_output_1581_human(const M65DeviceInfo *device,
                           const M651581Report *report,
                           const char *output_path);

/*
 * diagnose command report (IOUSBHost capture path).
 *
 * Endpoint addresses are printed live for the human path via the bridge's
 * m65_iousbhost_print_endpoints(); they are not exposed by the opaque bridge
 * handle, so the JSON path records only that capture and pipes succeeded.
 */
typedef struct {
    uint16_t vid;
    uint16_t pid;
    bool captured;
    bool alt_setting_ok;
    bool inquiry_ok;
    M65Inquiry inquiry;
    bool request_sense_ok;
    M65Sense sense;
    bool read_capacity_ok;
    M65Capacity capacity;
    bool mode_sense_ok;
    M65FlexibleDiskPage flexible;
    bool destroyed;
    int exit_code;
    char reason[M65_MAX_ERROR_TEXT];
} M65DiagnoseReport;

bool m65_output_diagnose_json(const M65DiagnoseReport *report);

#endif
