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

#endif
