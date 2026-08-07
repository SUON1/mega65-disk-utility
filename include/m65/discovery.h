#ifndef M65_DISCOVERY_H
#define M65_DISCOVERY_H

#include "m65/device.h"

bool m65_discover_devices(M65DeviceList *list, char *detail, size_t detail_size);
const M65DeviceInfo *m65_select_device(const M65DeviceList *list,
                                       const char *bsd_name,
                                       bool *ambiguous);

#endif
