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
 * diagnose command report (IOUSBHost whole-device capture path).
 *
 * The diagnose workflow binds the device discovered on the command line to the
 * IOUSBHost capture transport, then runs the same read-only UFI inspection
 * (m65_inspect) that the inspect command uses.  All UFI traffic flows through
 * the CBI/UFI safety engine, so the staged results are captured verbatim in the
 * embedded M65InspectReport.  captured records that the capture transport was
 * created; destroyed records that it was torn down cleanly afterwards.
 */
typedef struct {
    uint16_t vid;
    uint16_t pid;
    bool captured;
    bool destroyed;
    M65InspectReport inspect;
    int exit_code;
    char reason[M65_MAX_ERROR_TEXT];
} M65DiagnoseReport;

bool m65_output_diagnose_json(const M65DiagnoseReport *report);
void m65_output_diagnose_human(const M65DiagnoseReport *report);

#endif
