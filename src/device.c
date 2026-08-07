#include "m65/device.h"

M65DeviceValidation m65_validate_device(const M65DeviceInfo *device)
{
    if (device == NULL || !device->external) {
        return M65_DEVICE_INTERNAL;
    }
    if (device->mounted) {
        return M65_DEVICE_MOUNTED;
    }
    if (!device->removable) {
        return M65_DEVICE_NOT_REMOVABLE;
    }
    if (!device->whole) {
        return M65_DEVICE_NOT_WHOLE;
    }
    if (device->block_size != 512U) {
        return M65_DEVICE_WRONG_BLOCK_SIZE;
    }
    if (!device->known_controller) {
        return M65_DEVICE_UNKNOWN_CONTROLLER;
    }
    return M65_DEVICE_ACCEPTED;
}

const char *m65_device_validation_text(M65DeviceValidation validation)
{
    switch (validation) {
    case M65_DEVICE_ACCEPTED:
        return "accepted";
    case M65_DEVICE_INTERNAL:
        return "device is internal";
    case M65_DEVICE_MOUNTED:
        return "media is mounted";
    case M65_DEVICE_NOT_REMOVABLE:
        return "device is not removable";
    case M65_DEVICE_NOT_WHOLE:
        return "device is not whole media";
    case M65_DEVICE_WRONG_BLOCK_SIZE:
        return "device block size is not 512 bytes";
    case M65_DEVICE_UNKNOWN_CONTROLLER:
        return "USB identity does not match the known TEAC controller";
    }
    return "unknown device validation result";
}
