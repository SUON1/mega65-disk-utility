#ifndef M65_DEVICE_H
#define M65_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M65_MAX_DEVICES 16U
#define M65_MAX_BSD_NAME 32U
#define M65_MAX_IDENTITY 128U
#define M65_MAX_REGISTRY_PATH 1024U

typedef struct {
    char bsd_name[M65_MAX_BSD_NAME];
    char raw_path[M65_MAX_IDENTITY];
    char registry_path[M65_MAX_REGISTRY_PATH];
    char media_name[M65_MAX_IDENTITY];
    char usb_manufacturer[M65_MAX_IDENTITY];
    char usb_product[M65_MAX_IDENTITY];
    char product_revision[M65_MAX_IDENTITY];
    uint16_t usb_vid;
    uint16_t usb_pid;
    uint16_t usb_device_revision;
    bool known_controller;
    bool media_present;
    bool external;
    bool removable;
    bool whole;
    bool mounted;
    bool readable;
    uint64_t capacity_bytes;
    uint32_t block_size;
    int permission_errno;
} M65DeviceInfo;

typedef struct {
    M65DeviceInfo items[M65_MAX_DEVICES];
    size_t count;
} M65DeviceList;

typedef enum {
    M65_DEVICE_ACCEPTED = 0,
    M65_DEVICE_INTERNAL,
    M65_DEVICE_MOUNTED,
    M65_DEVICE_NOT_REMOVABLE,
    M65_DEVICE_NOT_WHOLE,
    M65_DEVICE_WRONG_BLOCK_SIZE,
    M65_DEVICE_UNKNOWN_CONTROLLER
} M65DeviceValidation;

M65DeviceValidation m65_validate_device(const M65DeviceInfo *device);
const char *m65_device_validation_text(M65DeviceValidation validation);

#endif
