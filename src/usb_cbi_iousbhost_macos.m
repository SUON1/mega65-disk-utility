/*
 * usb_cbi_iousbhost_macos.m — IOUSBHost whole-device capture I/O adapter
 *
 * Phase 2 rework.  This file is the ONLY Objective-C translation unit in the
 * project.  It implements the low-level M65CbiIo operation table (see cbi.h)
 * on top of IOUSBHost.framework and hands it to the shared, unit-tested CBI/UFI
 * command engine in src/cbi.c via m65_cbi_transport_create().
 *
 * Design (per Phase 2 code review):
 *   - The engine in cbi.c owns ALL protocol policy: command allowlist,
 *     exact-transfer accounting, REQUEST SENSE issuance, completion-vs-sense
 *     validation, and command-block reset recovery.  This adapter performs no
 *     policy of its own; it only moves bytes and reports precise IOReturns.
 *   - The exact TEAC UFI/CBI interface that hosts the caller's BSD media node
 *     is located up front by walking the IORegistry ancestry (identical
 *     identity test to the IOUSBLib transport in usb_cbi_macos.c: interface
 *     class 0x08 / subclass 0x04 / protocol 0x00 AND VID 0x0644 / PID 0x0000).
 *     Capture (initWithIOService:options:DeviceCapture:...) binds to THAT exact
 *     io_service_t, so the captured device is provably the same one selected by
 *     --device.
 *   - Descriptor parsing is self-contained (local packed structs) so the build
 *     never depends on SDK descriptor-type macros such as kUSBEndpointDesc.
 *   - The class-specific ADSC control request fills IOUSBDeviceRequest fields
 *     directly from the engine-built M65CbiAdsc, so we never call
 *     IOUSBHostDeviceRequestType (which triggered -Werror enum conversions).
 *
 * Build note: compiled with -fobjc-arc -fmodules (see CMakeLists.txt).
 */

@import Foundation;
@import IOUSBHost;

#import <IOKit/IOKitLib.h>
#import <IOKit/IOKitKeys.h>
#import <IOKit/IOBSD.h>
#import <IOKit/usb/USB.h>
#import <libkern/OSByteOrder.h>

#include "m65/usb_cbi_iousbhost_macos.h"
#include "m65/cbi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* USB-IF mass-storage identity of the supported TEAC UFI/CBI drive. */
#define M65_USB_MASS_STORAGE_CLASS 0x08U
#define M65_USB_UFI_SUBCLASS 0x04U
#define M65_USB_CBI_PROTOCOL 0x00U
#define M65_USB_TEAC_VID 0x0644U
#define M65_USB_TEAC_PID 0x0000U

/* Self-contained USB descriptor constants (no SDK macro dependency). */
#define M65_USB_DESC_INTERFACE 0x04U
#define M65_USB_DESC_ENDPOINT 0x05U
#define M65_USB_XFER_MASK 0x03U
#define M65_USB_XFER_BULK 0x02U
#define M65_USB_XFER_INTERRUPT 0x03U
#define M65_USB_DIR_IN 0x80U
#define M65_INTERRUPT_ABORT_DRAIN_MS 2000U

#pragma pack(push, 1)
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
} M65UsbDescHeader;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} M65UsbConfigDesc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} M65UsbInterfaceDesc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} M65UsbEndpointDesc;
#pragma pack(pop)

@interface M65IOUSBHostInterruptRequest : NSObject
@property (nonatomic, strong) NSMutableData *buffer;
@property (nonatomic, strong) dispatch_semaphore_t completion;
@property (nonatomic) IOReturn result;
@property (nonatomic) NSUInteger transferred;
@end

@implementation M65IOUSBHostInterruptRequest
@end

/* Strong ObjC references are held by this class; ARC forbids strong pointers in
 * a plain C struct, so the context keeps it behind a bridged void*. */
@interface M65IOUSBHostAdapter : NSObject
@property (nonatomic, strong) IOUSBHostInterface *iface;
@property (nonatomic, strong) IOUSBHostPipe *pipeIn;   /* bulk-IN */
@property (nonatomic, strong) IOUSBHostPipe *pipeOut;  /* bulk-OUT */
@property (nonatomic, strong) IOUSBHostPipe *pipeIntr; /* interrupt-IN */
@property (nonatomic, strong) M65IOUSBHostInterruptRequest *pendingInterrupt;
@end

@implementation M65IOUSBHostAdapter
@end

typedef struct {
    io_service_t service;   /* retained until destroy */
    void *adapter;          /* __bridge_retained M65IOUSBHostAdapter* while open */
    uint8_t interface_number;
    bool open;
    bool desynchronized;
    bool reset_armed;
} IousbhostContext;

/* ── detail / IOReturn helpers (mirrors usb_cbi_macos.c) ── */

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
    if (result == kIOReturnNoDevice || result == kIOReturnOffline ||
        result == kIOReturnNotAttached) {
        return M65_CBI_IO_NO_DEVICE;
    }
    if (result == kIOReturnTimeout || result == kIOUSBTransactionTimeout) {
        return M65_CBI_IO_TIMEOUT;
    }
    if (result == kIOReturnBadArgument || result == kIOReturnUnsupported ||
        result == kIOUSBUnknownPipeErr) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (result == kIOReturnAborted || result == kIOUSBTransactionReturned) {
        return M65_CBI_IO_ERROR;
    }
    return M65_CBI_IO_ERROR;
}

static IOReturn io_return_from_error(NSError *error)
{
    if (error == nil) {
        return kIOReturnError;
    }
    return (IOReturn)error.code;
}

/* ── BSD-name correlation (identical identity test to the IOUSBLib path) ── */

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

static bool is_known_ufi_cbi_interface(io_registry_entry_t service)
{
    uint16_t interface_class = 0U;
    uint16_t interface_subclass = 0U;
    uint16_t interface_protocol = 0U;
    uint16_t vendor = 0U;
    uint16_t product = 0U;
    return cf_number_u16(service, CFSTR("bInterfaceClass"), &interface_class) &&
           cf_number_u16(service, CFSTR("bInterfaceSubClass"), &interface_subclass) &&
           cf_number_u16(service, CFSTR("bInterfaceProtocol"), &interface_protocol) &&
           cf_number_u16(service, CFSTR("idVendor"), &vendor) &&
           cf_number_u16(service, CFSTR("idProduct"), &product) &&
           interface_class == M65_USB_MASS_STORAGE_CLASS &&
           interface_subclass == M65_USB_UFI_SUBCLASS &&
           interface_protocol == M65_USB_CBI_PROTOCOL &&
           vendor == M65_USB_TEAC_VID && product == M65_USB_TEAC_PID;
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

/* ── engine-facing gating (mirrors ready_for_adsc in usb_cbi_macos.c) ── */

static bool is_command_block_reset(const uint8_t cdb[M65_CBI_CDB_LENGTH])
{
    static const uint8_t expected[M65_CBI_CDB_LENGTH] = {
        0x1dU, 0x04U, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
    };
    return memcmp(cdb, expected, sizeof(expected)) == 0;
}

static bool ready_for_adsc(const IousbhostContext *context,
                           const uint8_t cdb[M65_CBI_CDB_LENGTH],
                           char *detail, size_t detail_size)
{
    bool reset = is_command_block_reset(cdb);
    M65IOUSBHostAdapter *adapter =
        (__bridge M65IOUSBHostAdapter *)context->adapter;
    if (!context->open) {
        set_detail(detail, detail_size,
                   "IOUSBHost CBI transport is not open");
        return false;
    }
    if (adapter.pendingInterrupt != nil) {
        set_detail(detail, detail_size,
                   "IOUSBHost CBI transport has a pending completion interrupt");
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
                   "IOUSBHost CBI transport requires command-block reset recovery");
        return false;
    }
    return true;
}

static NSTimeInterval seconds_from_ms(uint32_t timeout_ms)
{
    /* The engine never passes 0; guard anyway so a transfer can never wait
     * forever. */
    if (timeout_ms == 0U) {
        return 5.0;
    }
    return (NSTimeInterval)timeout_ms / 1000.0;
}

static dispatch_time_t dispatch_deadline_from_ms(uint32_t timeout_ms)
{
    uint32_t effective_ms = timeout_ms == 0U ? 1U : timeout_ms;
    int64_t nanoseconds = (int64_t)effective_ms * 1000000LL;
    return dispatch_time(DISPATCH_TIME_NOW, nanoseconds);
}

static bool wait_for_interrupt_request(
    M65IOUSBHostInterruptRequest *request, uint32_t timeout_ms)
{
    return dispatch_semaphore_wait(request.completion,
                                   dispatch_deadline_from_ms(timeout_ms)) == 0L;
}

/*
 * Abort is synchronous per IOUSBHostPipe.h: it does not return until the
 * aborted I/O has completed. The separate bounded semaphore drain observes
 * the completion handler before the request buffer is released. If the SDK
 * contract is ever violated, the request remains strongly retained by both
 * the adapter and completion block, so a late callback cannot access freed
 * storage and the caller receives a hard recovery error.
 */
static M65CbiIoStatus abort_and_drain_interrupt(
    M65IOUSBHostAdapter *adapter, IOReturn *completion_result,
    char *detail, size_t detail_size)
{
    M65IOUSBHostInterruptRequest *request = adapter.pendingInterrupt;
    NSError *error = nil;
    IOReturn abort_code = kIOReturnSuccess;
    BOOL aborted;

    if (completion_result != NULL) {
        *completion_result = kIOReturnSuccess;
    }
    if (request == nil) {
        return M65_CBI_IO_OK;
    }

    if (dispatch_semaphore_wait(request.completion, DISPATCH_TIME_NOW) == 0L) {
        if (completion_result != NULL) {
            *completion_result = request.result;
        }
        adapter.pendingInterrupt = nil;
        return M65_CBI_IO_OK;
    }

    aborted = [adapter.pipeIntr
        abortWithOption:IOUSBHostAbortOptionSynchronous
                  error:&error];
    if (!aborted) {
        abort_code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "synchronously abort CBI interrupt-IN", abort_code);
    }
    if (!wait_for_interrupt_request(request, M65_INTERRUPT_ABORT_DRAIN_MS)) {
        if (aborted) {
            set_detail(detail, detail_size,
                       "synchronous CBI interrupt-IN abort returned before its completion drained");
            return M65_CBI_IO_ERROR;
        }
        append_io_return(detail, detail_size,
                         "drain CBI interrupt-IN after abort failure",
                         kIOReturnTimeout);
        return map_io_return(abort_code);
    }

    if (completion_result != NULL) {
        *completion_result = request.result;
    }
    adapter.pendingInterrupt = nil;
    return aborted ? M65_CBI_IO_OK : map_io_return(abort_code);
}

/* ── endpoint discovery over IOUSBHost descriptors ── */

static bool discover_endpoints(M65IOUSBHostAdapter *adapter,
                               uint8_t *bulk_in, uint8_t *bulk_out,
                               uint8_t *intr_in,
                               char *detail, size_t detail_size)
{
    const void *cfg_raw = (const void *)adapter.iface.configurationDescriptor;
    const void *if_raw = (const void *)adapter.iface.interfaceDescriptor;
    const M65UsbConfigDesc *cfg;
    const M65UsbInterfaceDesc *ifd;
    const uint8_t *base;
    const uint8_t *end;
    const uint8_t *p;
    uint16_t total_length;

    if (cfg_raw == NULL || if_raw == NULL) {
        set_detail(detail, detail_size,
                   "IOUSBHost returned NULL configuration/interface descriptor");
        return false;
    }
    cfg = (const M65UsbConfigDesc *)cfg_raw;
    ifd = (const M65UsbInterfaceDesc *)if_raw;

    if (ifd->bInterfaceClass != M65_USB_MASS_STORAGE_CLASS ||
        ifd->bInterfaceSubClass != M65_USB_UFI_SUBCLASS ||
        ifd->bInterfaceProtocol != M65_USB_CBI_PROTOCOL) {
        set_detail(detail, detail_size,
                   "captured interface is not an exact 08/04/00 UFI CBI interface");
        return false;
    }
    if (ifd->bNumEndpoints != 3U) {
        set_detail(detail, detail_size,
                   "captured UFI CBI interface does not expose exactly three endpoints");
        return false;
    }

    total_length = OSSwapLittleToHostInt16(cfg->wTotalLength);
    base = (const uint8_t *)cfg_raw;
    end = base + total_length;
    p = (const uint8_t *)if_raw + ifd->bLength;
    if ((const uint8_t *)if_raw < base ||
        (const uint8_t *)if_raw + ifd->bLength > end) {
        set_detail(detail, detail_size,
                   "IOUSBHost interface descriptor lies outside the configuration buffer");
        return false;
    }

    *bulk_in = 0U;
    *bulk_out = 0U;
    *intr_in = 0U;

    while (p + sizeof(M65UsbDescHeader) <= end) {
        const M65UsbDescHeader *hdr = (const M65UsbDescHeader *)p;
        if (hdr->bLength == 0U) {
            break;
        }
        if (p + hdr->bLength > end) {
            break;
        }
        /* Stop at the next interface descriptor (a different interface). */
        if (hdr->bDescriptorType == M65_USB_DESC_INTERFACE &&
            p != (const uint8_t *)if_raw) {
            break;
        }
        if (hdr->bDescriptorType == M65_USB_DESC_ENDPOINT) {
            const M65UsbEndpointDesc *ep;
            uint8_t dir;
            uint8_t xfer;
            if (hdr->bLength < sizeof(M65UsbEndpointDesc)) {
                set_detail(detail, detail_size,
                           "IOUSBHost endpoint descriptor is truncated");
                return false;
            }
            ep = (const M65UsbEndpointDesc *)p;
            dir = (uint8_t)(ep->bEndpointAddress & M65_USB_DIR_IN);
            xfer = (uint8_t)(ep->bmAttributes & M65_USB_XFER_MASK);
            if (xfer == M65_USB_XFER_BULK && dir == M65_USB_DIR_IN &&
                *bulk_in == 0U) {
                *bulk_in = ep->bEndpointAddress;
            } else if (xfer == M65_USB_XFER_BULK && dir == 0U &&
                       *bulk_out == 0U) {
                *bulk_out = ep->bEndpointAddress;
            } else if (xfer == M65_USB_XFER_INTERRUPT && dir == M65_USB_DIR_IN &&
                       OSSwapLittleToHostInt16(ep->wMaxPacketSize) ==
                           M65_CBI_STATUS_LENGTH &&
                       *intr_in == 0U) {
                *intr_in = ep->bEndpointAddress;
            } else {
                set_detail(detail, detail_size,
                           "IOUSBHost CBI endpoint set is duplicated or malformed");
                return false;
            }
        }
        p += hdr->bLength;
    }

    if (*bulk_in == 0U || *bulk_out == 0U || *intr_in == 0U) {
        set_detail(detail, detail_size,
                   "IOUSBHost CBI endpoint set lacks bulk-IN, bulk-OUT, or two-byte interrupt-IN");
        return false;
    }
    return true;
}

/* ── M65CbiIo operations ── */

static M65CbiIoStatus iousbhost_open_seize(M65CbiIo *io,
                                           char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter;
    NSError *error = nil;
    IOUSBHostInterface *iface;
    uint8_t bulk_in = 0U;
    uint8_t bulk_out = 0U;
    uint8_t intr_in = 0U;
    const void *if_raw;
    IOUSBHostPipe *pipe_in;
    IOUSBHostPipe *pipe_out;
    IOUSBHostPipe *pipe_intr;

    if (context->open) {
        return M65_CBI_IO_OK;
    }

    iface = [[IOUSBHostInterface alloc]
        initWithIOService:context->service
                  options:IOUSBHostObjectInitOptionsDeviceCapture
                    queue:nil
                    error:&error
          interestHandler:nil];
    if (iface == nil) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "IOUSBHost DeviceCapture (initWithIOService)", code);
        return map_io_return(code);
    }

    error = nil;
    if (![iface selectAlternateSetting:0 error:&error]) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "IOUSBHost selectAlternateSetting:0", code);
        [iface destroy];
        return map_io_return(code);
    }

    adapter = [[M65IOUSBHostAdapter alloc] init];
    adapter.iface = iface;
    if (!discover_endpoints(adapter, &bulk_in, &bulk_out, &intr_in,
                            detail, detail_size)) {
        [iface destroy];
        return M65_CBI_IO_PROTOCOL;
    }

    error = nil;
    pipe_out = [iface copyPipeWithAddress:bulk_out error:&error];
    if (pipe_out == nil) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "IOUSBHost copyPipeWithAddress (bulk-OUT)", code);
        [iface destroy];
        return map_io_return(code);
    }
    error = nil;
    pipe_in = [iface copyPipeWithAddress:bulk_in error:&error];
    if (pipe_in == nil) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "IOUSBHost copyPipeWithAddress (bulk-IN)", code);
        [iface destroy];
        return map_io_return(code);
    }
    error = nil;
    pipe_intr = [iface copyPipeWithAddress:intr_in error:&error];
    if (pipe_intr == nil) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "IOUSBHost copyPipeWithAddress (interrupt-IN)", code);
        [iface destroy];
        return map_io_return(code);
    }

    adapter.pipeOut = pipe_out;
    adapter.pipeIn = pipe_in;
    adapter.pipeIntr = pipe_intr;

    if_raw = (const void *)iface.interfaceDescriptor;
    context->interface_number =
        ((const M65UsbInterfaceDesc *)if_raw)->bInterfaceNumber;
    context->adapter = (__bridge_retained void *)adapter;
    context->open = true;
    context->desynchronized = false;
    context->reset_armed = false;
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_close(M65CbiIo *io,
                                      char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter;
    M65CbiIoStatus abort_status = M65_CBI_IO_OK;
    IOReturn completion_result = kIOReturnSuccess;
    if (!context->open) {
        return M65_CBI_IO_OK;
    }
    adapter = (__bridge_transfer M65IOUSBHostAdapter *)context->adapter;
    context->adapter = NULL;
    context->open = false;
    context->reset_armed = false;
    if (adapter != nil) {
        abort_status = abort_and_drain_interrupt(adapter, &completion_result,
                                                 detail, detail_size);
        [adapter.iface destroy]; /* resets device; kernel re-registers driver */
        adapter.pendingInterrupt = nil;
        adapter.pipeIn = nil;
        adapter.pipeOut = nil;
        adapter.pipeIntr = nil;
        adapter.iface = nil;
    }
    (void)completion_result;
    return abort_status;
}

static M65CbiIoStatus iousbhost_adsc(M65CbiIo *io,
                                     const uint8_t cdb[M65_CBI_CDB_LENGTH],
                                     uint32_t timeout_ms,
                                     char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter;
    M65CbiAdsc adsc;
    IOUSBDeviceRequest request;
    NSMutableData *cdb_buffer;
    NSError *error = nil;
    NSUInteger transferred = 0U;
    BOOL ok;

    if (!ready_for_adsc(context, cdb, detail, detail_size)) {
        return M65_CBI_IO_PROTOCOL;
    }
    if (!m65_cbi_build_adsc(context->interface_number, cdb, &adsc)) {
        set_detail(detail, detail_size, "unable to construct CBI ADSC request");
        return M65_CBI_IO_PROTOCOL;
    }
    adapter = (__bridge M65IOUSBHostAdapter *)context->adapter;

    (void)memset(&request, 0, sizeof(request));
    /* Fill the request directly from the engine-built ADSC — never via
     * IOUSBHostDeviceRequestType(), which produces -Werror enum conversions. */
    request.bmRequestType = adsc.request_type; /* 0x21: OUT | class | interface */
    request.bRequest = adsc.request;           /* 0x00: ADSC */
    request.wValue = adsc.value;               /* 0x0000 */
    request.wIndex = adsc.index;               /* interface number */
    request.wLength = adsc.length;             /* 12 */

    cdb_buffer = [NSMutableData dataWithBytes:adsc.cdb
                                       length:(NSUInteger)M65_CBI_CDB_LENGTH];
    ok = [adapter.iface sendDeviceRequest:request
                                     data:cdb_buffer
                         bytesTransferred:&transferred
                        completionTimeout:seconds_from_ms(timeout_ms)
                                    error:&error];
    if (!ok) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size, "CBI ADSC command", code);
        if (code == kIOReturnTimeout || code == kIOUSBTransactionTimeout) {
            context->desynchronized = true;
        }
        return map_io_return(code);
    }
    if (transferred != (NSUInteger)M65_CBI_CDB_LENGTH) {
        context->desynchronized = true;
        set_detail(detail, detail_size,
                   "CBI ADSC command transferred fewer than 12 bytes");
        return M65_CBI_IO_PROTOCOL;
    }
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_bulk_in(M65CbiIo *io, void *data, size_t length,
                                        size_t *transferred, uint32_t timeout_ms,
                                        char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter = (__bridge M65IOUSBHostAdapter *)context->adapter;
    NSError *error = nil;
    NSMutableData *buffer;
    NSUInteger received = 0U;
    size_t copy_length;
    BOOL ok;

    *transferred = 0U;
    if (length == 0U) {
        return M65_CBI_IO_OK;
    }
    buffer = [adapter.iface ioDataWithCapacity:(NSUInteger)length error:&error];
    if (buffer == nil) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "allocate CBI bulk-IN DMA buffer", code);
        return map_io_return(code);
    }
    ok = [adapter.pipeIn sendIORequestWithData:buffer
                              bytesTransferred:&received
                             completionTimeout:seconds_from_ms(timeout_ms)
                                         error:&error];
    if (!ok) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size, "CBI bulk-IN", code);
        if (code != kIOUSBPipeStalled) {
            context->desynchronized = true;
        }
        return map_io_return(code);
    }
    *transferred = (size_t)received;
    copy_length = length < (size_t)received ? length : (size_t)received;
    if (copy_length > 0U) {
        (void)memcpy(data, buffer.bytes, copy_length);
    }
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_bulk_out(M65CbiIo *io, const void *data,
                                         size_t length, size_t *transferred,
                                         uint32_t timeout_ms,
                                         char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter = (__bridge M65IOUSBHostAdapter *)context->adapter;
    NSError *error = nil;
    NSMutableData *buffer;
    NSUInteger sent = 0U;
    BOOL ok;

    *transferred = 0U;
    if (length == 0U) {
        return M65_CBI_IO_OK;
    }
    buffer = [NSMutableData dataWithBytes:data length:(NSUInteger)length];
    ok = [adapter.pipeOut sendIORequestWithData:buffer
                               bytesTransferred:&sent
                              completionTimeout:seconds_from_ms(timeout_ms)
                                          error:&error];
    if (!ok) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size, "CBI bulk-OUT", code);
        if (code != kIOUSBPipeStalled) {
            context->desynchronized = true;
        }
        return map_io_return(code);
    }
    *transferred = (size_t)sent;
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_interrupt_in_with_deadline(
    M65CbiIo *io, uint8_t status[M65_CBI_STATUS_LENGTH], size_t *transferred,
    uint32_t timeout_ms, char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter = (__bridge M65IOUSBHostAdapter *)context->adapter;
    M65IOUSBHostInterruptRequest *request;
    NSError *error = nil;
    IOReturn completion_result = kIOReturnSuccess;
    M65CbiIoStatus drain_status;
    char abort_detail[M65_MAX_ERROR_TEXT] = "";
    size_t copy_length;
    BOOL submitted;

    *transferred = 0U;
    if (adapter.pendingInterrupt != nil) {
        set_detail(detail, detail_size,
                   "previous IOUSBHost CBI interrupt-IN has not drained");
        return M65_CBI_IO_PROTOCOL;
    }

    request = [[M65IOUSBHostInterruptRequest alloc] init];
    request.buffer =
        [adapter.iface ioDataWithCapacity:(NSUInteger)M65_CBI_STATUS_LENGTH
                                   error:&error];
    if (request.buffer == nil) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "allocate CBI interrupt-IN DMA buffer", code);
        return map_io_return(code);
    }
    request.completion = dispatch_semaphore_create(0L);
    request.result = kIOReturnNotReady;
    adapter.pendingInterrupt = request;

    /* IOUSBHostPipe.h requires a zero framework timeout for interrupt pipes.
     * The semaphore below owns the application deadline instead. */
    submitted = [adapter.pipeIntr
        enqueueIORequestWithData:request.buffer
               completionTimeout:0.0
                           error:&error
               completionHandler:^(IOReturn result, NSUInteger received) {
                   request.result = result;
                   request.transferred = received;
                   dispatch_semaphore_signal(request.completion);
               }];
    if (!submitted) {
        IOReturn code = io_return_from_error(error);
        adapter.pendingInterrupt = nil;
        describe_io_return(detail, detail_size,
                           "submit asynchronous CBI interrupt-IN", code);
        return map_io_return(code);
    }

    if (!wait_for_interrupt_request(request, timeout_ms)) {
        context->desynchronized = true;
        drain_status = abort_and_drain_interrupt(
            adapter, &completion_result, abort_detail, sizeof(abort_detail));
        if (drain_status != M65_CBI_IO_OK) {
            if (abort_detail[0] != '\0') {
                (void)snprintf(detail, detail_size,
                               "CBI interrupt-IN exceeded its %u ms application deadline; %s",
                               (unsigned int)timeout_ms, abort_detail);
            } else {
                (void)snprintf(detail, detail_size,
                               "CBI interrupt-IN exceeded its %u ms application deadline and did not drain",
                               (unsigned int)timeout_ms);
            }
            return drain_status;
        }
        if (map_io_return(completion_result) == M65_CBI_IO_NO_DEVICE) {
            describe_io_return(detail, detail_size,
                               "CBI interrupt-IN device removal during deadline abort",
                               completion_result);
            return M65_CBI_IO_NO_DEVICE;
        }
        (void)snprintf(detail, detail_size,
                       "CBI interrupt-IN exceeded its %u ms application deadline; synchronous abort completion was IOReturn 0x%08x",
                       (unsigned int)timeout_ms,
                       (unsigned int)completion_result);
        return M65_CBI_IO_TIMEOUT;
    }

    completion_result = request.result;
    *transferred = (size_t)request.transferred;
    copy_length = *transferred < (size_t)M65_CBI_STATUS_LENGTH ?
                  *transferred : (size_t)M65_CBI_STATUS_LENGTH;
    if (copy_length > 0U) {
        (void)memcpy(status, request.buffer.bytes, copy_length);
    }
    adapter.pendingInterrupt = nil;
    if (completion_result != kIOReturnSuccess) {
        context->desynchronized = true;
        describe_io_return(detail, detail_size, "CBI interrupt-IN",
                           completion_result);
        return map_io_return(completion_result);
    }
    if (*transferred != (size_t)M65_CBI_STATUS_LENGTH) {
        context->desynchronized = true;
        set_detail(detail, detail_size,
                   "CBI interrupt-IN did not return exactly two bytes");
        return M65_CBI_IO_PROTOCOL;
    }
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_clear_stall(M65CbiIo *io,
                                            M65DataDirection bulk_direction,
                                            char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter = (__bridge M65IOUSBHostAdapter *)context->adapter;
    IOUSBHostPipe *pipe;
    NSError *error = nil;
    if (bulk_direction == M65_DATA_IN) {
        pipe = adapter.pipeIn;
    } else if (bulk_direction == M65_DATA_OUT) {
        pipe = adapter.pipeOut;
    } else {
        set_detail(detail, detail_size,
                   "CBI stall clear requires a bulk direction");
        return M65_CBI_IO_PROTOCOL;
    }
    if (![pipe clearStallWithError:&error]) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size, "clear CBI bulk-pipe stall", code);
        return map_io_return(code);
    }
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_prepare_command_block_reset(
    M65CbiIo *io, char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
    M65IOUSBHostAdapter *adapter = (__bridge M65IOUSBHostAdapter *)context->adapter;
    NSError *error = nil;
    IOReturn completion_result = kIOReturnSuccess;
    M65CbiIoStatus drain_status;
    if (!context->open || context->reset_armed) {
        set_detail(detail, detail_size,
                   "CBI command-block reset preparation has invalid state");
        return M65_CBI_IO_PROTOCOL;
    }
    /* A timed-out asynchronous completion remains owned by the adapter until
     * this reset preparation cancels and observes it. Only after that drain
     * may exactly one reset ADSC be armed. */
    drain_status = abort_and_drain_interrupt(adapter, &completion_result,
                                             detail, detail_size);
    if (drain_status != M65_CBI_IO_OK) {
        return drain_status;
    }
    (void)completion_result;
    /* Any non-timeout interrupt error halts the pipe; clear the halt and reset
     * its data toggle before accepting the reset completion request. */
    if (![adapter.pipeIntr clearStallWithError:&error]) {
        IOReturn code = io_return_from_error(error);
        describe_io_return(detail, detail_size,
                           "resynchronize interrupt-IN before CBI reset", code);
        return map_io_return(code);
    }
    context->reset_armed = true;
    return M65_CBI_IO_OK;
}

static M65CbiIoStatus iousbhost_finish_command_block_reset(
    M65CbiIo *io, bool recovered, char *detail, size_t detail_size)
{
    IousbhostContext *context = (IousbhostContext *)io->context;
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

static void iousbhost_destroy_io(M65CbiIo *io)
{
    IousbhostContext *context;
    if (io == NULL) {
        return;
    }
    context = (IousbhostContext *)io->context;
    if (context != NULL) {
        if (context->open) {
            char ignored[M65_MAX_ERROR_TEXT];
            (void)iousbhost_close(io, ignored, sizeof(ignored));
        }
        if (context->adapter != NULL) {
            M65IOUSBHostAdapter *adapter =
                (__bridge_transfer M65IOUSBHostAdapter *)context->adapter;
            context->adapter = NULL;
            /* The bridge transfer balances __bridge_retained from open; ARC
             * releases the strong local at the end of this scope. */
            (void)adapter;
        }
        if (context->service != IO_OBJECT_NULL) {
            IOObjectRelease(context->service);
            context->service = IO_OBJECT_NULL;
        }
        free(context);
    }
    free(io);
}

static const M65CbiIoOps iousbhost_cbi_ops = {
    iousbhost_open_seize,
    iousbhost_close,
    iousbhost_adsc,
    iousbhost_bulk_in,
    iousbhost_bulk_out,
    iousbhost_interrupt_in_with_deadline,
    iousbhost_clear_stall,
    iousbhost_prepare_command_block_reset,
    iousbhost_finish_command_block_reset,
    iousbhost_destroy_io
};

M65Transport *m65_iousbhost_transport_create(const char *bsd_name,
                                             char *detail, size_t detail_size,
                                             M65TransportStatus *status)
{
    const char *name = normalize_name(bsd_name);
    io_service_t service;
    M65CbiIo *io;
    IousbhostContext *context;
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
                   "selected media has no matching TEAC USB UFI/CBI interface for IOUSBHost capture");
        return NULL;
    }

    io = (M65CbiIo *)calloc(1U, sizeof(*io));
    context = (IousbhostContext *)calloc(1U, sizeof(*context));
    if (io == NULL || context == NULL) {
        free(io);
        free(context);
        IOObjectRelease(service);
        set_detail(detail, detail_size,
                   "out of memory creating IOUSBHost CBI transport");
        return NULL;
    }
    context->service = service; /* retained; released in iousbhost_destroy_io */
    io->ops = &iousbhost_cbi_ops;
    io->context = context;
    transport = m65_cbi_transport_create(io);
    if (transport == NULL) {
        iousbhost_destroy_io(io);
        set_detail(detail, detail_size,
                   "unable to create IOUSBHost CBI command engine");
        return NULL;
    }
    if (status != NULL) {
        *status = M65_TRANSPORT_OK;
    }
    return transport;
}
