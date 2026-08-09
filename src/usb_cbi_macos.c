#include "m65/usb_cbi_macos.h"

#include "m65/cbi.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define M65_USB_MASS_STORAGE_CLASS 0x08U
#define M65_USB_UFI_SUBCLASS 0x04U
#define M65_USB_CBI_PROTOCOL 0x00U
#define M65_USB_TEAC_VID 0x0644U
#define M65_USB_TEAC_PID 0x0000U
#define M65_INTERRUPT_ABORT_DRAIN_SECONDS 2.0

typedef struct {
    /* One reference belongs to the context and one to a submitted callback. */
    atomic_uint references;
    atomic_bool complete;
    IOReturn result;
    size_t transferred;
    uint8_t buffer[M65_CBI_STATUS_LENGTH];
} MacInterruptRequest;

typedef struct {
    IOUSBInterfaceInterface190 **interface;
    CFRunLoopSourceRef event_source;
    CFRunLoopRef run_loop;
    UInt8 interface_number;
    UInt8 bulk_in_pipe;
    UInt8 bulk_out_pipe;
    UInt8 interrupt_in_pipe;
    bool open;
    bool desynchronized;
    bool reset_armed;
    MacInterruptRequest *interrupt_request;
} MacCbiContext;

static void set_detail(char *detail, size_t detail_size, const char *text)
{
    if (detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size, "%s", text);
    }
}

static void describe_io_return(char *detail, size_t detail_size,
                               const char *operation, IOReturn result)
{
    if (detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size, "%s failed with IOReturn 0x%08x",
                       operation, (unsigned int)result);
    }
}

static void append_io_return(char *detail, size_t detail_size,
                             const char *operation, IOReturn result)
{
    size_t used;
    if (detail == NULL || detail_size == 0U) {
        return;
    }
    used = strlen(detail);
    if (used < detail_size) {
        (void)snprintf(&detail[used], detail_size - used,
                       "%s%s failed with IOReturn 0x%08x",
                       used == 0U ? "" : "; ", operation,
                       (unsigned int)result);
    }
}

static M65CbiIoStatus map_io_return(IOReturn result)
{
    if (result == kIOReturnSuccess) {
        return M65_CBI_IO_OK;
    }
    if (result == kIOUSBPipeStalled) {
        return M65_CBI_IO_STALL;
    }
    if (result == kIOReturnNotPrivileged || result == kIOReturnNotPermitted ||
        result == kIOReturnExclusiveAccess) {
        return M65_CBI_IO_PERMISSION;
    }
    if (result == kIOReturnNoDevice || result == kIOReturnOffline) {
        return M65_CBI_IO_NO_DEVICE;
    }
    if (result == kIOReturnTimeout || result == kIOUSBTransactionTimeout) {
        return M65_CBI_IO_TIMEOUT;
    }
    if (result == kIOReturnBadArgument || result == kIOReturnUnsupported) {
        return M65_CBI_IO_PROTOCOL;
    }
    return M65_CBI_IO_ERROR;
}

static bool cf_number_u16(io_registry_entry_t entry, CFStringRef key, uint16_t *out)
{
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, key,
                                                       kCFAllocatorDefault, 0U);
    int32_t number = 0;
    bool valid = false;
    if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID() &&
        CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &number) &&
        number >= 0 && number <= (int32_t)UINT16_MAX) {
        *out = (uint16_t)number;
        valid = true;
    }
    if (value != NULL) {
        CFRelease(value);
    }
    return valid;
}

typedef struct {
    CFStringRef expected;
    bool found;
} PluginTypeSearch;

static void find_plugin_type(const void *key, const void *value, void *context)
{
    PluginTypeSearch *search = (PluginTypeSearch *)context;
    (void)value;
    if (!search->found && CFGetTypeID((CFTypeRef)key) == CFStringGetTypeID() &&
        CFStringCompare((CFStringRef)key, search->expected,
                        kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
        search->found = true;
    }
}

static bool exposes_usb_interface_plugin(io_registry_entry_t service)
{
    CFTypeRef value = IORegistryEntryCreateCFProperty(
        service, CFSTR(kIOCFPlugInTypesKey), kCFAllocatorDefault, 0U);
    CFStringRef type_string;
    PluginTypeSearch search;
    (void)memset(&search, 0, sizeof(search));
    if (value == NULL || CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        if (value != NULL) {
            CFRelease(value);
        }
        return false;
    }
    type_string = CFUUIDCreateString(kCFAllocatorDefault,
                                     kIOUSBInterfaceUserClientTypeID);
    if (type_string != NULL) {
        search.expected = type_string;
        CFDictionaryApplyFunction((CFDictionaryRef)value, find_plugin_type,
                                  &search);
        CFRelease(type_string);
    }
    CFRelease(value);
    return search.found;
}

static bool is_known_ufi_cbi_interface(io_registry_entry_t service)
{
    uint16_t interface_class = 0U;
    uint16_t interface_subclass = 0U;
    uint16_t interface_protocol = 0U;
    uint16_t vendor = 0U;
    uint16_t product = 0U;
    return cf_number_u16(service, CFSTR(kUSBInterfaceClass), &interface_class) &&
           cf_number_u16(service, CFSTR(kUSBInterfaceSubClass), &interface_subclass) &&
           cf_number_u16(service, CFSTR(kUSBInterfaceProtocol), &interface_protocol) &&
           cf_number_u16(service, CFSTR(kUSBVendorID), &vendor) &&
           cf_number_u16(service, CFSTR(kUSBProductID), &product) &&
           interface_class == M65_USB_MASS_STORAGE_CLASS &&
           interface_subclass == M65_USB_UFI_SUBCLASS &&
           interface_protocol == M65_USB_CBI_PROTOCOL &&
           vendor == M65_USB_TEAC_VID && product == M65_USB_TEAC_PID &&
           exposes_usb_interface_plugin(service);
}

static const char *normalize_name(const char *bsd_name)
{
    const char *name = bsd_name;
    if (name != NULL && strncmp(name, "/dev/", 5U) == 0) {
        name += 5;
    }
    if (name != NULL && strncmp(name, "rdisk", 5U) == 0) {
        ++name;
    }
    return name;
}

static io_service_t copy_usb_interface_for_bsd_name(const char *bsd_name)
{
    CFMutableDictionaryRef matching = IOBSDNameMatching(kIOMainPortDefault, 0U,
                                                         bsd_name);
    io_registry_entry_t current;
    if (matching == NULL) {
        return IO_OBJECT_NULL;
    }
    current = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    while (current != IO_OBJECT_NULL) {
        io_registry_entry_t parent = IO_OBJECT_NULL;
        if (is_known_ufi_cbi_interface(current)) {
            return current;
        }
        if (IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) !=
            KERN_SUCCESS) {
            parent = IO_OBJECT_NULL;
        }
        IOObjectRelease(current);
        current = parent;
    }
    return IO_OBJECT_NULL;
}

static M65TransportStatus transport_status_for_cbi(M65CbiIoStatus status)
{
    switch (status) {
    case M65_CBI_IO_OK:
        return M65_TRANSPORT_OK;
    case M65_CBI_IO_PERMISSION:
        return M65_TRANSPORT_PERMISSION;
    case M65_CBI_IO_NO_DEVICE:
        return M65_TRANSPORT_NO_DEVICE;
    case M65_CBI_IO_TIMEOUT:
        return M65_TRANSPORT_TIMEOUT;
    case M65_CBI_IO_STALL:
    case M65_CBI_IO_ERROR:
        return M65_TRANSPORT_IO;
    case M65_CBI_IO_PROTOCOL:
        return M65_TRANSPORT_PROTOCOL;
    }
    return M65_TRANSPORT_IO;
}

static bool inspect_endpoints(MacCbiContext *context,
                              char *detail, size_t detail_size)
{
    UInt8 interface_class = 0U;
    UInt8 interface_subclass = 0U;
    UInt8 interface_protocol = 0U;
    UInt8 endpoint_count = 0U;
    UInt8 pipe;
    IOReturn result;

    result = (*context->interface)->GetInterfaceClass(context->interface,
                                                       &interface_class);
    if (result == kIOReturnSuccess) {
        result = (*context->interface)->GetInterfaceSubClass(context->interface,
                                                              &interface_subclass);
    }
    if (result == kIOReturnSuccess) {
        result = (*context->interface)->GetInterfaceProtocol(context->interface,
                                                              &interface_protocol);
    }
    if (result == kIOReturnSuccess) {
        result = (*context->interface)->GetInterfaceNumber(context->interface,
                                                            &context->interface_number);
    }
    if (result == kIOReturnSuccess) {
        result = (*context->interface)->GetNumEndpoints(context->interface,
                                                         &endpoint_count);
    }
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size, "read USB interface descriptors", result);
        return false;
    }
    if (interface_class != M65_USB_MASS_STORAGE_CLASS ||
        interface_subclass != M65_USB_UFI_SUBCLASS ||
        interface_protocol != M65_USB_CBI_PROTOCOL || endpoint_count != 3U) {
        set_detail(detail, detail_size,
                   "selected USB interface is not an exact 08/04/00 UFI CBI interface with three endpoints");
        return false;
    }

    context->bulk_in_pipe = 0U;
    context->bulk_out_pipe = 0U;
    context->interrupt_in_pipe = 0U;
    for (pipe = 1U; pipe <= endpoint_count; ++pipe) {
        UInt8 direction = 0U;
        UInt8 endpoint_number = 0U;
        UInt8 transfer_type = 0U;
        UInt16 max_packet_size = 0U;
        UInt8 interval = 0U;
        result = (*context->interface)->GetPipeProperties(
            context->interface, pipe, &direction, &endpoint_number,
            &transfer_type, &max_packet_size, &interval);
        if (result != kIOReturnSuccess) {
            describe_io_return(detail, detail_size, "enumerate USB CBI endpoints", result);
            return false;
        }
        if (transfer_type == kUSBBulk && direction == kUSBIn &&
            context->bulk_in_pipe == 0U) {
            context->bulk_in_pipe = pipe;
        } else if (transfer_type == kUSBBulk && direction == kUSBOut &&
                   context->bulk_out_pipe == 0U) {
            context->bulk_out_pipe = pipe;
        } else if (transfer_type == kUSBInterrupt && direction == kUSBIn &&
                   max_packet_size == M65_CBI_STATUS_LENGTH &&
                   context->interrupt_in_pipe == 0U) {
            context->interrupt_in_pipe = pipe;
        } else {
            (void)endpoint_number;
            (void)interval;
            set_detail(detail, detail_size,
                       "USB CBI endpoint set is duplicated or malformed");
            return false;
        }
    }
    if (context->bulk_in_pipe == 0U || context->bulk_out_pipe == 0U ||
        context->interrupt_in_pipe == 0U) {
        set_detail(detail, detail_size,
                   "USB CBI endpoint set lacks bulk-IN, bulk-OUT, or two-byte interrupt-IN");
        return false;
    }
    return true;
}

static void interrupt_request_release(MacInterruptRequest *request)
{
    if (atomic_fetch_sub_explicit(&request->references, 1U,
                                  memory_order_acq_rel) == 1U) {
        free(request);
    }
}

static void interrupt_callback(void *reference, IOReturn result, void *argument)
{
    MacInterruptRequest *request = (MacInterruptRequest *)reference;
    request->result = result;
    request->transferred = (size_t)(uintptr_t)argument;
    atomic_store_explicit(&request->complete, true, memory_order_release);
    interrupt_request_release(request);
}

static bool interrupt_request_complete(const MacInterruptRequest *request)
{
    return request != NULL &&
           atomic_load_explicit(&request->complete, memory_order_acquire);
}

static void release_interrupt_request(MacCbiContext *context)
{
    MacInterruptRequest *request = context->interrupt_request;
    if (request == NULL) {
        return;
    }
    context->interrupt_request = NULL;
    interrupt_request_release(request);
}

static bool run_until_interrupt(MacCbiContext *context, CFTimeInterval seconds)
{
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + seconds;
    while (!interrupt_request_complete(context->interrupt_request)) {
        CFTimeInterval remaining = deadline - CFAbsoluteTimeGetCurrent();
        if (remaining <= 0.0) {
            return false;
        }
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, remaining, true);
    }
    return true;
}

static bool on_transport_run_loop(const MacCbiContext *context,
                                  char *detail, size_t detail_size)
{
    if (context->run_loop != NULL && CFRunLoopGetCurrent() != context->run_loop) {
        set_detail(detail, detail_size,
                   "USB CBI transport must be used and released on its acquiring thread");
        return false;
    }
    return true;
}

static bool is_command_block_reset(
    const uint8_t cdb[M65_CBI_CDB_LENGTH])
{
    static const uint8_t expected[M65_CBI_CDB_LENGTH] = {
        0x1dU, 0x04U, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
    };
    return memcmp(cdb, expected, sizeof(expected)) == 0;
}

static bool ready_for_adsc(const MacCbiContext *context,
                              const uint8_t cdb[M65_CBI_CDB_LENGTH],
                              char *detail, size_t detail_size)
{
    bool reset = is_command_block_reset(cdb);
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return false;
    }
    if (context->interrupt_request != NULL) {
        set_detail(detail, detail_size,
                   "USB CBI transport has a pending completion interrupt");
        return false;
    }
    if (context->reset_armed != reset) {
        set_detail(detail, detail_size,
                   reset ?
                   "unarmed CBI command-block reset was rejected" :
                   "only the armed CBI command-block reset is permitted during recovery");
        return false;
    }
    if (context->desynchronized && !reset) {
        set_detail(detail, detail_size,
                   "USB CBI transport requires command-block reset recovery");
        return false;
    }
    return true;
}

static IOReturn abort_and_drain_interrupt(MacCbiContext *context)
{
    IOReturn result = kIOReturnSuccess;
    if (context->interrupt_request != NULL &&
        !interrupt_request_complete(context->interrupt_request)) {
        result = (*context->interface)->AbortPipe(context->interface,
                                                   context->interrupt_in_pipe);
        if (result == kIOReturnSuccess &&
            !run_until_interrupt(context, M65_INTERRUPT_ABORT_DRAIN_SECONDS)) {
            result = kIOReturnTimeout;
        }
    }
    return result;
}

static void remove_event_source(MacCbiContext *context)
{
    if (context->event_source != NULL) {
        if (context->run_loop != NULL) {
            CFRunLoopRemoveSource(context->run_loop, context->event_source,
                                  kCFRunLoopDefaultMode);
        }
        CFRelease(context->event_source);
        context->event_source = NULL;
    }
    if (context->run_loop != NULL) {
        CFRelease(context->run_loop);
        context->run_loop = NULL;
    }
}

static M65CbiIoStatus mac_open_seize(M65CbiIo *io,
                                     char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    IOReturn result;
    if (context->open) {
        return M65_CBI_IO_OK;
    }
    result = (*context->interface)->USBInterfaceOpenSeize(context->interface);
    if (result != kIOReturnSuccess) {
        if (result == kIOReturnExclusiveAccess) {
            if (detail != NULL && detail_size > 0U) {
                (void)snprintf(
                    detail, detail_size,
                    "macOS mass-storage driver refused USB interface seizure "
                    "(IOReturn 0x%08x)%s",
                    (unsigned int)result,
                    geteuid() == 0 ?
                    "; the interface remains unavailable even with root privileges" :
                    "; retry manually with sudo to distinguish permission from a driver limitation");
            }
            return geteuid() == 0 ? M65_CBI_IO_ERROR : M65_CBI_IO_PERMISSION;
        }
        if (result == kIOReturnNotPrivileged || result == kIOReturnNotPermitted) {
            if (detail != NULL && detail_size > 0U) {
                (void)snprintf(
                    detail, detail_size,
                    "permission denied while seizing the selected USB interface "
                    "(IOReturn 0x%08x); the program never invokes sudo, so retry "
                    "manually with sudo if local policy permits",
                    (unsigned int)result);
            }
            return M65_CBI_IO_PERMISSION;
        }
        describe_io_return(detail, detail_size, "USBInterfaceOpenSeize", result);
        return map_io_return(result);
    }
    context->open = true;
    context->desynchronized = false;
    context->reset_armed = false;
    if (!inspect_endpoints(context, detail, detail_size)) {
        (void)(*context->interface)->USBInterfaceClose(context->interface);
        context->open = false;
        return M65_CBI_IO_PROTOCOL;
    }
    result = (*context->interface)->CreateInterfaceAsyncEventSource(
        context->interface, &context->event_source);
    if (result != kIOReturnSuccess || context->event_source == NULL) {
        if (result == kIOReturnSuccess) {
            set_detail(detail, detail_size,
                       "CreateInterfaceAsyncEventSource returned NULL");
        } else {
            describe_io_return(detail, detail_size,
                               "CreateInterfaceAsyncEventSource", result);
        }
        remove_event_source(context);
        (void)(*context->interface)->USBInterfaceClose(context->interface);
        context->open = false;
        return result == kIOReturnSuccess ? M65_CBI_IO_ERROR :
               map_io_return(result);
    }
    context->run_loop = CFRunLoopGetCurrent();
    CFRetain(context->run_loop);
    CFRunLoopAddSource(context->run_loop, context->event_source,
                       kCFRunLoopDefaultMode);
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus mac_close(M65CbiIo *io, char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    IOReturn abort_result;
    IOReturn result;
    if (!context->open) {
        return M65_CBI_IO_OK;
    }
    if (!on_transport_run_loop(context, detail, detail_size)) {
        abort_result = kIOReturnBadArgument;
    } else {
        abort_result = abort_and_drain_interrupt(context);
    }
    result = (*context->interface)->USBInterfaceClose(context->interface);
    context->open = false;
    context->reset_armed = false;
    release_interrupt_request(context);
    remove_event_source(context);
    if (abort_result != kIOReturnSuccess) {
        if (detail != NULL && detail_size > 0U && detail[0] == '\0') {
            describe_io_return(detail, detail_size,
                               "abort pending CBI interrupt before close",
                               abort_result);
        }
        if (result != kIOReturnSuccess) {
            append_io_return(detail, detail_size, "USBInterfaceClose", result);
        }
        return map_io_return(abort_result);
    }
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size, "USBInterfaceClose", result);
    }
    return map_io_return(result);
}

static M65CbiIoStatus mac_adsc(M65CbiIo *io,
                               const uint8_t cdb[M65_CBI_CDB_LENGTH],
                               uint32_t timeout_ms,
                               char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    IOUSBDevRequestTO request;
    M65CbiAdsc adsc;
    IOReturn result;
    if (!ready_for_adsc(context, cdb, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    (void)memset(&request, 0, sizeof(request));
    if (!m65_cbi_build_adsc(context->interface_number, cdb, &adsc)) {
        set_detail(detail, detail_size, "unable to construct CBI ADSC request");
        return M65_CBI_IO_PROTOCOL;
    }
    request.bmRequestType = adsc.request_type;
    request.bRequest = adsc.request;
    request.wValue = adsc.value;
    request.wIndex = adsc.index;
    request.wLength = adsc.length;
    request.pData = adsc.cdb;
    request.noDataTimeout = timeout_ms;
    request.completionTimeout = timeout_ms;
    result = (*context->interface)->ControlRequestTO(context->interface, 0U,
                                                      &request);
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size, "CBI ADSC command", result);
        if (result == kIOReturnTimeout || result == kIOUSBTransactionTimeout) {
            context->desynchronized = true;
        }
        return map_io_return(result);
    }
    if (request.wLenDone != M65_CBI_CDB_LENGTH) {
        context->desynchronized = true;
        set_detail(detail, detail_size, "CBI ADSC command transferred fewer than 12 bytes");
        return M65_CBI_IO_PROTOCOL;
    }
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus mac_bulk_in(M65CbiIo *io, void *data, size_t length,
                                  size_t *transferred, uint32_t timeout_ms,
                                  char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    UInt32 usb_length;
    IOReturn result;
    IOReturn clear_result;
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (length > (size_t)UINT32_MAX) {
        set_detail(detail, detail_size, "CBI bulk-IN length exceeds UInt32");
        return M65_CBI_IO_PROTOCOL;
    }
    usb_length = (UInt32)length;
    result = (*context->interface)->ReadPipeTO(
        context->interface, context->bulk_in_pipe, data, &usb_length,
        timeout_ms, timeout_ms);
    *transferred = result == kIOReturnSuccess ? (size_t)usb_length : 0U;
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size, "CBI bulk-IN", result);
        if (result == kIOReturnTimeout || result == kIOUSBTransactionTimeout) {
            context->desynchronized = true;
            clear_result = (*context->interface)->ClearPipeStallBothEnds(
                context->interface, context->bulk_in_pipe);
            if (clear_result != kIOReturnSuccess) {
                append_io_return(detail, detail_size,
                                 "resynchronize bulk-IN after timeout", clear_result);
            }
        }
    }
    return map_io_return(result);
}

static M65CbiIoStatus mac_bulk_out(M65CbiIo *io, const void *data, size_t length,
                                   size_t *transferred, uint32_t timeout_ms,
                                   char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    IOReturn result;
    IOReturn clear_result;
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (length > (size_t)UINT32_MAX) {
        set_detail(detail, detail_size, "CBI bulk-OUT length exceeds UInt32");
        return M65_CBI_IO_PROTOCOL;
    }
    result = (*context->interface)->WritePipeTO(
        context->interface, context->bulk_out_pipe, (void *)(uintptr_t)data,
        (UInt32)length, timeout_ms, timeout_ms);
    *transferred = result == kIOReturnSuccess ? length : 0U;
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size, "CBI bulk-OUT", result);
        if (result == kIOReturnTimeout || result == kIOUSBTransactionTimeout) {
            context->desynchronized = true;
            clear_result = (*context->interface)->ClearPipeStallBothEnds(
                context->interface, context->bulk_out_pipe);
            if (clear_result != kIOReturnSuccess) {
                append_io_return(detail, detail_size,
                                 "resynchronize bulk-OUT after timeout", clear_result);
            }
        }
    }
    return map_io_return(result);
}

static M65CbiIoStatus mac_interrupt_in_with_deadline(
    M65CbiIo *io, uint8_t status[M65_CBI_STATUS_LENGTH], size_t *transferred,
    uint32_t timeout_ms, char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    MacInterruptRequest *request;
    IOReturn result;
    IOReturn abort_result;
    IOReturn clear_result;
    CFTimeInterval timeout_seconds = (CFTimeInterval)timeout_ms / 1000.0;
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (context->interrupt_request != NULL) {
        set_detail(detail, detail_size,
                   "previous CBI interrupt request has not drained");
        return M65_CBI_IO_PROTOCOL;
    }
    request = (MacInterruptRequest *)calloc(1U, sizeof(*request));
    if (request == NULL) {
        set_detail(detail, detail_size,
                   "out of memory submitting CBI interrupt-IN");
        return M65_CBI_IO_ERROR;
    }
    atomic_init(&request->references, 2U);
    atomic_init(&request->complete, false);
    request->result = kIOReturnNotReady;
    context->interrupt_request = request;
    result = (*context->interface)->ReadPipeAsync(
        context->interface, context->interrupt_in_pipe,
        request->buffer, M65_CBI_STATUS_LENGTH,
        interrupt_callback, request);
    if (result != kIOReturnSuccess) {
        /* A failed submission cannot produce a completion callback. */
        interrupt_request_release(request);
        release_interrupt_request(context);
        describe_io_return(detail, detail_size, "submit CBI interrupt-IN", result);
        return map_io_return(result);
    }
    if (!run_until_interrupt(context, timeout_seconds)) {
        context->desynchronized = true;
        abort_result = abort_and_drain_interrupt(context);
        set_detail(detail, detail_size, "CBI interrupt-IN timed out and was aborted");
        if (abort_result != kIOReturnSuccess) {
            append_io_return(detail, detail_size,
                             "abort timed-out interrupt-IN", abort_result);
            if (interrupt_request_complete(context->interrupt_request)) {
                release_interrupt_request(context);
            }
            return map_io_return(abort_result);
        }
        clear_result = (*context->interface)->ClearPipeStallBothEnds(
            context->interface, context->interrupt_in_pipe);
        if (clear_result != kIOReturnSuccess) {
            append_io_return(detail, detail_size,
                             "resynchronize interrupt-IN after timeout", clear_result);
        }
        release_interrupt_request(context);
        return M65_CBI_IO_TIMEOUT;
    }
    *transferred = request->transferred;
    result = request->result;
    if (*transferred <= M65_CBI_STATUS_LENGTH) {
        (void)memcpy(status, request->buffer, *transferred);
    }
    release_interrupt_request(context);
    if (result != kIOReturnSuccess) {
        context->desynchronized = true;
        describe_io_return(detail, detail_size, "CBI interrupt-IN",
                           result);
        return map_io_return(result);
    }
    if (*transferred != M65_CBI_STATUS_LENGTH) {
        context->desynchronized = true;
        set_detail(detail, detail_size,
                   "CBI interrupt-IN did not return exactly two bytes");
        return M65_CBI_IO_PROTOCOL;
    }
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus mac_clear_stall(M65CbiIo *io,
                                      M65DataDirection bulk_direction,
                                      char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    UInt8 pipe;
    IOReturn result;
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (bulk_direction == M65_DATA_IN) {
        pipe = context->bulk_in_pipe;
    } else if (bulk_direction == M65_DATA_OUT) {
        pipe = context->bulk_out_pipe;
    } else {
        set_detail(detail, detail_size, "CBI stall clear requires a bulk direction");
        return M65_CBI_IO_PROTOCOL;
    }
    result = (*context->interface)->ClearPipeStallBothEnds(context->interface, pipe);
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size, "clear CBI bulk-pipe stall", result);
    }
    return map_io_return(result);
}

static M65CbiIoStatus mac_prepare_command_block_reset(
    M65CbiIo *io, char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    IOReturn result;
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (!context->open || context->reset_armed) {
        set_detail(detail, detail_size,
                   "CBI command-block reset preparation has invalid state");
        return M65_CBI_IO_PROTOCOL;
    }
    result = abort_and_drain_interrupt(context);
    if (result != kIOReturnSuccess) {
        if (interrupt_request_complete(context->interrupt_request)) {
            release_interrupt_request(context);
        }
        describe_io_return(detail, detail_size,
                           "drain previous CBI interrupt before reset", result);
        return map_io_return(result);
    }
    release_interrupt_request(context);
    result = (*context->interface)->ClearPipeStallBothEnds(
        context->interface, context->interrupt_in_pipe);
    if (result != kIOReturnSuccess) {
        describe_io_return(detail, detail_size,
                           "resynchronize interrupt-IN before CBI reset", result);
        return map_io_return(result);
    }
    context->reset_armed = true;
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus mac_finish_command_block_reset(
    M65CbiIo *io, bool recovered, char *detail, size_t detail_size)
{
    MacCbiContext *context = (MacCbiContext *)io->context;
    if (!on_transport_run_loop(context, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (!context->reset_armed) {
        set_detail(detail, detail_size,
                   "CBI command-block reset completion was not armed");
        context->desynchronized = true;
        return M65_CBI_IO_PROTOCOL;
    }
    context->reset_armed = false;
    context->desynchronized = !recovered;
    return M65_CBI_IO_OK;
}

static void mac_destroy_io(M65CbiIo *io)
{
    MacCbiContext *context;
    if (io == NULL) {
        return;
    }
    context = (MacCbiContext *)io->context;
    if (context != NULL) {
        if (context->open) {
            char ignored[M65_MAX_ERROR_TEXT];
            (void)mac_close(io, ignored, sizeof(ignored));
        }
        remove_event_source(context);
        release_interrupt_request(context);
        if (context->interface != NULL) {
            (void)(*context->interface)->Release(context->interface);
        }
        free(context);
    }
    free(io);
}

static const M65CbiIoOps mac_cbi_ops = {
    mac_open_seize,
    mac_close,
    mac_adsc,
    mac_bulk_in,
    mac_bulk_out,
    mac_interrupt_in_with_deadline,
    mac_clear_stall,
    mac_prepare_command_block_reset,
    mac_finish_command_block_reset,
    mac_destroy_io
};

M65Transport *m65_usb_cbi_transport_create(const char *bsd_name,
                                            char *detail, size_t detail_size,
                                            M65TransportStatus *status)
{
    const char *name = normalize_name(bsd_name);
    io_service_t service;
    IOCFPlugInInterface **plugin = NULL;
    IOUSBInterfaceInterface190 **interface = NULL;
    SInt32 score = 0;
    IOReturn create_result;
    HRESULT query_result;
    M65CbiIo *io;
    MacCbiContext *context;
    M65Transport *transport;

    if (status != NULL) {
        *status = M65_TRANSPORT_IO;
    }
    if (name == NULL || name[0] == '\0') {
        set_detail(detail, detail_size, "missing BSD device name");
        return NULL;
    }
    service = copy_usb_interface_for_bsd_name(name);
    if (service == IO_OBJECT_NULL) {
        if (status != NULL) {
            *status = M65_TRANSPORT_NO_DEVICE;
        }
        set_detail(detail, detail_size,
                   "selected media has no matching TEAC USB UFI/CBI interface in its ancestry");
        return NULL;
    }
    create_result = IOCreatePlugInInterfaceForService(
        service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID,
        &plugin, &score);
    IOObjectRelease(service);
    if (create_result != kIOReturnSuccess || plugin == NULL) {
        if (plugin != NULL) {
            (void)IODestroyPlugInInterface(plugin);
        }
        if (status != NULL) {
            *status = create_result == kIOReturnSuccess ? M65_TRANSPORT_IO :
                      transport_status_for_cbi(map_io_return(create_result));
        }
        if (create_result == kIOReturnSuccess) {
            set_detail(detail, detail_size,
                       "documented IOUSBLib plug-in creation returned NULL");
        } else {
            describe_io_return(detail, detail_size,
                               "create documented IOUSBLib interface plug-in",
                               create_result);
        }
        return NULL;
    }
    query_result = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID190),
        (LPVOID *)(void *)&interface);
    (void)IODestroyPlugInInterface(plugin);
    if (query_result != S_OK || interface == NULL) {
        if (interface != NULL) {
            (void)(*interface)->Release(interface);
        }
        if (status != NULL) {
            *status = M65_TRANSPORT_IO;
        }
        if (detail != NULL && detail_size > 0U) {
            (void)snprintf(detail, detail_size,
                           "query documented IOUSBInterfaceInterface190 failed with HRESULT 0x%08x",
                           (unsigned int)query_result);
        }
        return NULL;
    }

    io = (M65CbiIo *)calloc(1U, sizeof(*io));
    context = (MacCbiContext *)calloc(1U, sizeof(*context));
    if (io == NULL || context == NULL) {
        free(io);
        free(context);
        (void)(*interface)->Release(interface);
        set_detail(detail, detail_size, "out of memory creating USB CBI transport");
        return NULL;
    }
    context->interface = interface;
    io->ops = &mac_cbi_ops;
    io->context = context;
    transport = m65_cbi_transport_create(io);
    if (transport == NULL) {
        mac_destroy_io(io);
        set_detail(detail, detail_size, "unable to create USB CBI command engine");
        return NULL;
    }
    if (status != NULL) {
        *status = M65_TRANSPORT_OK;
    }
    return transport;
}
