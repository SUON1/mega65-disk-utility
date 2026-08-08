#include "m65/discovery.h"

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/usb/USBSpec.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void copy_cf_string(CFTypeRef value, char *destination, size_t destination_size)
{
    if (destination_size == 0U) {
        return;
    }
    destination[0] = '\0';
    if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
        if (!CFStringGetCString((CFStringRef)value, destination,
                                (CFIndex)destination_size, kCFStringEncodingUTF8)) {
            destination[0] = '\0';
        }
    }
}

static CFTypeRef copy_recursive_property(io_registry_entry_t entry, CFStringRef key)
{
    return IORegistryEntrySearchCFProperty(entry, kIOServicePlane, key,
                                            kCFAllocatorDefault,
                                            kIORegistryIterateRecursively |
                                            kIORegistryIterateParents);
}

static uint16_t recursive_u16(io_registry_entry_t entry, const char *key)
{
    CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                                   kCFStringEncodingUTF8);
    CFTypeRef value = NULL;
    int64_t number = 0;
    uint16_t result = 0U;
    if (cf_key != NULL) {
        value = copy_recursive_property(entry, cf_key);
        CFRelease(cf_key);
    }
    if (value != NULL) {
        if (CFGetTypeID(value) == CFNumberGetTypeID() &&
            CFNumberGetValue((CFNumberRef)value, kCFNumberSInt64Type, &number) &&
            number >= 0 && number <= (int64_t)UINT16_MAX) {
            result = (uint16_t)number;
        }
        CFRelease(value);
    }
    return result;
}

static void recursive_string(io_registry_entry_t entry, const char *key,
                             char *destination, size_t destination_size)
{
    CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                                   kCFStringEncodingUTF8);
    CFTypeRef value = NULL;
    if (cf_key != NULL) {
        value = copy_recursive_property(entry, cf_key);
        CFRelease(cf_key);
    }
    copy_cf_string(value, destination, destination_size);
    if (value != NULL) {
        CFRelease(value);
    }
}

static bool dictionary_bool(CFDictionaryRef dictionary, CFStringRef key, bool fallback)
{
    CFTypeRef value;
    if (dictionary == NULL) {
        return fallback;
    }
    value = CFDictionaryGetValue(dictionary, key);
    if (value != NULL && CFGetTypeID(value) == CFBooleanGetTypeID()) {
        return CFBooleanGetValue((CFBooleanRef)value);
    }
    return fallback;
}

static uint64_t dictionary_u64(CFDictionaryRef dictionary, CFStringRef key)
{
    CFTypeRef value;
    int64_t number = 0;
    if (dictionary == NULL) {
        return 0U;
    }
    value = CFDictionaryGetValue(dictionary, key);
    if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID() &&
        CFNumberGetValue((CFNumberRef)value, kCFNumberSInt64Type, &number) &&
        number >= 0) {
        return (uint64_t)number;
    }
    return 0U;
}

static bool identity_matches(const M65DeviceInfo *device)
{
    bool product_match = strstr(device->usb_product, "UF000") != NULL ||
                         strstr(device->usb_product, "TEAC") != NULL ||
                         strstr(device->media_name, "UF000") != NULL ||
                         strstr(device->usb_manufacturer, "TEACV0.0") != NULL;
    return device->usb_vid == 0x0644U && device->usb_pid == 0x0000U && product_match;
}

static void populate_identity(io_registry_entry_t entry, M65DeviceInfo *device)
{
    device->usb_vid = recursive_u16(entry, kUSBVendorID);
    device->usb_pid = recursive_u16(entry, kUSBProductID);
    device->usb_device_revision = recursive_u16(entry, "bcdDevice");
    recursive_string(entry, kUSBVendorString, device->usb_manufacturer,
                     sizeof(device->usb_manufacturer));
    recursive_string(entry, kUSBProductString, device->usb_product,
                     sizeof(device->usb_product));
    if (device->usb_product[0] == '\0') {
        recursive_string(entry, kIOPropertyProductNameKey, device->usb_product,
                         sizeof(device->usb_product));
    }
    (void)snprintf(device->product_revision, sizeof(device->product_revision),
                   "0x%04x", (unsigned int)device->usb_device_revision);
    device->known_controller = identity_matches(device);
}

static bool fill_da_description(DASessionRef session, M65DeviceInfo *device)
{
    char block_path[M65_MAX_IDENTITY];
    DADiskRef disk;
    CFDictionaryRef description;
    CFTypeRef volume_path;
    (void)snprintf(block_path, sizeof(block_path), "/dev/%s", device->bsd_name);
    disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, block_path);
    if (disk == NULL) {
        return false;
    }
    description = DADiskCopyDescription(disk);
    if (description == NULL) {
        CFRelease(disk);
        return false;
    }
    device->external = !dictionary_bool(description,
                                        kDADiskDescriptionDeviceInternalKey, true);
    device->removable = dictionary_bool(description,
                                        kDADiskDescriptionMediaRemovableKey, false);
    device->whole = dictionary_bool(description, kDADiskDescriptionMediaWholeKey, false);
    device->capacity_bytes = dictionary_u64(description, kDADiskDescriptionMediaSizeKey);
    device->block_size = (uint32_t)dictionary_u64(description,
                                                  kDADiskDescriptionMediaBlockSizeKey);
    volume_path = CFDictionaryGetValue(description, kDADiskDescriptionVolumePathKey);
    device->mounted = volume_path != NULL;
    device->media_present = device->capacity_bytes > 0U;
    CFRelease(description);
    CFRelease(disk);
    return true;
}

bool m65_discover_devices(M65DeviceList *list, char *detail, size_t detail_size)
{
    CFMutableDictionaryRef matching;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_object_t media;
    DASessionRef session;
    kern_return_t kernel_result;

    if (list == NULL) {
        return false;
    }
    (void)memset(list, 0, sizeof(*list));
    if (detail != NULL && detail_size > 0U) {
        detail[0] = '\0';
    }
    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        if (detail != NULL && detail_size > 0U) {
            (void)snprintf(detail, detail_size, "unable to create a Disk Arbitration session");
        }
        return false;
    }
    matching = IOServiceMatching(kIOMediaClass);
    if (matching == NULL) {
        CFRelease(session);
        return false;
    }
    kernel_result = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kernel_result != KERN_SUCCESS) {
        if (detail != NULL && detail_size > 0U) {
            (void)snprintf(detail, detail_size,
                           "I/O Registry media enumeration failed: 0x%08x",
                           (unsigned int)kernel_result);
        }
        CFRelease(session);
        return false;
    }

    while ((media = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        CFTypeRef bsd_value;
        M65DeviceInfo device;
        io_name_t media_name;
        io_string_t registry_path;
        (void)memset(&device, 0, sizeof(device));
        bsd_value = IORegistryEntryCreateCFProperty(media, CFSTR("BSD Name"),
                                                    kCFAllocatorDefault, 0U);
        copy_cf_string(bsd_value, device.bsd_name, sizeof(device.bsd_name));
        if (bsd_value != NULL) {
            CFRelease(bsd_value);
        }
        if (device.bsd_name[0] == '\0') {
            IOObjectRelease(media);
            continue;
        }
        if (IORegistryEntryGetName(media, media_name) == KERN_SUCCESS) {
            size_t name_length;
            (void)snprintf(device.media_name, sizeof(device.media_name),
                           "%s", media_name);
            name_length = strlen(device.media_name);
            if (name_length > 6U &&
                strcmp(&device.media_name[name_length - 6U], " Media") == 0) {
                device.media_name[name_length - 6U] = '\0';
            }
        }
        if (IORegistryEntryGetPath(media, kIOServicePlane, registry_path) == KERN_SUCCESS) {
            (void)snprintf(device.registry_path, sizeof(device.registry_path),
                           "%s", registry_path);
        }
        populate_identity(media, &device);
        (void)snprintf(device.raw_path, sizeof(device.raw_path),
                       "/dev/r%s", device.bsd_name);
        (void)fill_da_description(session, &device);
        errno = 0;
        device.readable = access(device.raw_path, R_OK) == 0;
        device.permission_errno = device.readable ? 0 : errno;

        if (device.known_controller && list->count < M65_MAX_DEVICES) {
            list->items[list->count] = device;
            ++list->count;
        }
        IOObjectRelease(media);
    }
    IOObjectRelease(iterator);
    CFRelease(session);
    return true;
}

static const char *normalize_bsd_name(const char *name)
{
    const char *normalized = name;
    if (normalized == NULL) {
        return NULL;
    }
    if (strncmp(normalized, "/dev/", 5U) == 0) {
        normalized += 5;
    }
    if (strncmp(normalized, "rdisk", 5U) == 0) {
        ++normalized;
    }
    return normalized;
}

const M65DeviceInfo *m65_select_device(const M65DeviceList *list,
                                       const char *bsd_name,
                                       bool *ambiguous)
{
    const M65DeviceInfo *selected = NULL;
    const char *normalized = normalize_bsd_name(bsd_name);
    size_t index;
    size_t matches = 0U;
    if (ambiguous != NULL) {
        *ambiguous = false;
    }
    if (list == NULL) {
        return NULL;
    }
    for (index = 0U; index < list->count; ++index) {
        const M65DeviceInfo *candidate = &list->items[index];
        if (!candidate->known_controller) {
            continue;
        }
        if (normalized != NULL && strcmp(candidate->bsd_name, normalized) != 0) {
            continue;
        }
        selected = candidate;
        ++matches;
    }
    if (matches != 1U) {
        if (ambiguous != NULL) {
            *ambiguous = matches > 1U;
        }
        return NULL;
    }
    return selected;
}
