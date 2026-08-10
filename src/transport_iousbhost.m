/*
 * transport_iousbhost.m — IOUSBHost whole-device capture transport (Phase 2)
 *
 * All Objective-C ARC code for the IOUSBHost CBI transport is isolated in this
 * single translation unit. The rest of the project is pure C and only sees the
 * void*-based API declared in include/m65/transport_iousbhost_bridge.h.
 *
 * Build note: this file MUST be compiled with -fobjc-arc (see CMakeLists.txt).
 */

/* ── §8.1 Imports and Module Header ── */
@import Foundation;
@import IOUSBHost;
#import <IOKit/IOKitLib.h>

#include "m65/transport_iousbhost_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── §6.2 Internal Context (visible only inside this .m file) ── */
@interface M65IOUSBHostCtx : NSObject
@property (nonatomic, strong) IOUSBHostInterface *iface;
@property (nonatomic, strong) IOUSBHostPipe      *pipeOut;   /* bulk-out */
@property (nonatomic, strong) IOUSBHostPipe      *pipeIn;    /* bulk-in  */
@property (nonatomic, strong) IOUSBHostPipe      *pipeIntr;  /* interrupt-in */
@property (nonatomic, assign) uint8_t  epBulkOutAddr;
@property (nonatomic, assign) uint8_t  epBulkInAddr;
@property (nonatomic, assign) uint8_t  epIntrInAddr;
@property (nonatomic, assign) uint8_t  ifaceNumber;
@end

@implementation M65IOUSBHostCtx
@end

/* The opaque C struct wraps the ObjC object via a retained void* */
struct m65_iousbhost_ctx {
    void *objc_ctx;   /* __bridge_retained M65IOUSBHostCtx* */
};

/* ── §8.2 Service Discovery ── */
int m65_iousbhost_open(uint16_t vid, uint16_t pid,
                       m65_iousbhost_ctx_t **ctx_out)
{
    *ctx_out = NULL;

    // Build the matching dictionary: VID + PID + class 08 / subclass 04 / protocol 00
    CFMutableDictionaryRef matchDict =
        [IOUSBHostInterface
            createMatchingDictionaryWithVendorID:@(vid)
            productID:@(pid)
            bcdDevice:nil
            interfaceNumber:nil
            configurationValue:nil
            interfaceClass:@(0x08)    // USB Mass Storage
            interfaceSubclass:@(0x04) // UFI
            interfaceProtocol:@(0x00) // CBI
            speed:nil
            productIDArray:nil];

    // IOServiceGetMatchingService consumes matchDict (releases it)
    io_service_t service =
        IOServiceGetMatchingService(kIOMainPortDefault, matchDict);

    if (service == MACH_PORT_NULL) {
        fprintf(stderr, "iousbhost: no interface 0x%04x:0x%04x "
                        "class=08/04/00 found in IORegistry\n", vid, pid);
        return M65_IOUSBHOST_ERR_NOT_FOUND;
    }

    /* ── §8.3 Capture ── */
    NSError *captureErr = nil;
    IOUSBHostInterface *iface = [[IOUSBHostInterface alloc]
        initWithIOService:service
        options:IOUSBHostObjectInitOptionsDeviceCapture  // terminates mass-storage stack
        queue:nil                                        // framework creates a serial queue
        error:&captureErr
        interestHandler:nil];

    // IOUSBHostInterface retains service; release our reference
    IOObjectRelease(service);

    if (!iface) {
        fprintf(stderr,
            "iousbhost: DeviceCapture failed: %s (IOReturn 0x%08x)\n",
            captureErr.localizedDescription.UTF8String,
            (unsigned int)captureErr.code);
        // If code == 0xe00002c5 → still exclusive; if 0xe00002cd → not authorized
        // Document whatever code appears in phase2-handoff.md
        return M65_IOUSBHOST_ERR_CAPTURE;
    }

    /* ── §8.4 Alternate Setting ── */
    NSError *altErr = nil;
    if (![iface selectAlternateSetting:0 error:&altErr]) {
        fprintf(stderr, "iousbhost: selectAlternateSetting:0 failed: %s\n",
                altErr.localizedDescription.UTF8String);
        [iface destroy];
        return M65_IOUSBHOST_ERR_PIPE;
    }

    /* ── §8.5 Endpoint Discovery ── */
    const IOUSBInterfaceDescriptor *ifDesc = iface.interfaceDescriptor;
    uint8_t ep_bulk_out = 0, ep_bulk_in = 0, ep_intr_in = 0;

    // Descriptor pointer arithmetic: walk past the interface descriptor
    const uint8_t *p   = (const uint8_t *)ifDesc + ifDesc->bLength;
    const uint8_t *end = (const uint8_t *)iface.configurationDescriptor
                         + OSSwapLittleToHostInt16(
                               iface.configurationDescriptor->wTotalLength);

    while (p < end) {
        const IOUSBDescriptorHeader *hdr = (const IOUSBDescriptorHeader *)p;
        if (hdr->bLength == 0) break;  // guard against corrupt descriptor

        if (hdr->bDescriptorType == kUSBEndpointDesc) {
            const IOUSBEndpointDescriptor *ep =
                (const IOUSBEndpointDescriptor *)hdr;
            uint8_t dir  = (ep->bEndpointAddress & 0x80) ? 1 : 0; // 1=in, 0=out
            uint8_t xfer = (ep->bmAttributes & 0x03); // 2=bulk, 3=interrupt

            if (xfer == kUSBBulk      && dir == 0) ep_bulk_out = ep->bEndpointAddress;
            if (xfer == kUSBBulk      && dir == 1) ep_bulk_in  = ep->bEndpointAddress;
            if (xfer == kUSBInterrupt && dir == 1) ep_intr_in  = ep->bEndpointAddress;
        }
        // Stop at next interface descriptor (different interface)
        if (hdr->bDescriptorType == kUSBInterfaceDesc &&
            hdr != (const IOUSBDescriptorHeader *)ifDesc) break;

        p += hdr->bLength;
    }

    if (!ep_bulk_out || !ep_bulk_in || !ep_intr_in) {
        fprintf(stderr,
            "iousbhost: endpoint discovery failed "
            "(bulk-out=0x%02x bulk-in=0x%02x intr-in=0x%02x)\n",
            ep_bulk_out, ep_bulk_in, ep_intr_in);
        [iface destroy];
        return M65_IOUSBHOST_ERR_PIPE;
    }

    /* ── §8.6 Opening Pipes ── */
    NSError *pipeErr = nil;
    IOUSBHostPipe *pipeOut  = [iface copyPipeWithAddress:ep_bulk_out error:&pipeErr];
    IOUSBHostPipe *pipeIn   = [iface copyPipeWithAddress:ep_bulk_in  error:&pipeErr];
    IOUSBHostPipe *pipeIntr = [iface copyPipeWithAddress:ep_intr_in  error:&pipeErr];

    if (!pipeOut || !pipeIn || !pipeIntr) {
        fprintf(stderr, "iousbhost: copyPipeWithAddress failed: %s\n",
                pipeErr.localizedDescription.UTF8String);
        [iface destroy];
        return M65_IOUSBHOST_ERR_PIPE;
    }

    /* ── §8.7 Context Assembly ── */
    M65IOUSBHostCtx *ctx_obj = [[M65IOUSBHostCtx alloc] init];
    ctx_obj.iface        = iface;
    ctx_obj.pipeOut      = pipeOut;
    ctx_obj.pipeIn       = pipeIn;
    ctx_obj.pipeIntr     = pipeIntr;
    ctx_obj.epBulkOutAddr = ep_bulk_out;
    ctx_obj.epBulkInAddr  = ep_bulk_in;
    ctx_obj.epIntrInAddr  = ep_intr_in;
    ctx_obj.ifaceNumber   = ifDesc->bInterfaceNumber;

    m65_iousbhost_ctx_t *ctx = calloc(1, sizeof(*ctx));
    ctx->objc_ctx = (__bridge_retained void*)ctx_obj;  // retain across C boundary
    *ctx_out = ctx;
    return M65_IOUSBHOST_OK;
}

/* ── §8.8 CBI Command Dispatch ── */
int m65_iousbhost_send_ufi(m65_iousbhost_ctx_t *ctx,
                            const uint8_t *cdb, uint8_t cdb_len,
                            uint8_t *data, size_t data_len,
                            int direction,
                            uint8_t cbi_status_out[2])
{
    M65IOUSBHostCtx *c = (__bridge M65IOUSBHostCtx*)ctx->objc_ctx;
    NSError *err = nil;

    /* ── Phase 1: ADSC (class-specific control request on EP0) ── */
    IOUSBDeviceRequest adsc = {0};
    adsc.bmRequestType = IOUSBHostDeviceRequestType(
        kIOUSBDeviceRequestDirectionOut,          // host-to-device
        kIOUSBDeviceRequestTypeClass,             // class request
        kIOUSBDeviceRequestRecipientInterface);   // to the interface
    adsc.bRequest = 0x00;                         // ADSC opcode
    adsc.wValue   = 0x0000;
    adsc.wIndex   = c.ifaceNumber;
    adsc.wLength  = cdb_len;

    NSMutableData *cdbBuf =
        [NSMutableData dataWithBytes:cdb length:cdb_len];
    NSUInteger txd = 0;
    if (![c.iface sendDeviceRequest:adsc
                               data:cdbBuf
                    bytesTransferred:&txd
                   completionTimeout:IOUSBHostDefaultControlCompletionTimeout
                               error:&err]) {
        fprintf(stderr, "iousbhost: ADSC failed: %s\n",
                err.localizedDescription.UTF8String);
        return M65_IOUSBHOST_ERR_IO;
    }

    /* ── Phase 2: Data (optional) ── */
    if (data && data_len > 0) {
        IOUSBHostPipe *dataPipe = (direction == 1) ? c.pipeIn : c.pipeOut;
        NSMutableData *dataBuf;

        if (direction == 1) {
            // Device-to-host: allocate a DMA-mapped buffer
            dataBuf = [c.iface ioDataWithCapacity:data_len error:&err];
            if (!dataBuf) {
                fprintf(stderr, "iousbhost: ioDataWithCapacity failed: %s\n",
                        err.localizedDescription.UTF8String);
                return M65_IOUSBHOST_ERR_IO;
            }
        } else {
            // Host-to-device: wrap the caller's buffer (data-out; only MODE SELECT)
            dataBuf = [NSMutableData dataWithBytes:data length:data_len];
        }

        NSUInteger rx = 0;
        NSTimeInterval timeout = (data_len > 512) ? 30.0 : 5.0;
        BOOL ok = [dataPipe sendIORequestWithData:dataBuf
                                  bytesTransferred:&rx
                                 completionTimeout:timeout
                                             error:&err];
        if (!ok) {
            // Clear STALL before returning; caller should then REQUEST SENSE
            NSError *stallErr = nil;
            [dataPipe clearStallWithError:&stallErr];
            fprintf(stderr, "iousbhost: data transfer failed: %s\n",
                    err.localizedDescription.UTF8String);
            return M65_IOUSBHOST_ERR_IO;
        }

        if (rx != data_len) {
            fprintf(stderr,
                "iousbhost: short transfer: requested %zu got %zu\n",
                data_len, (size_t)rx);
        }

        if (direction == 1) {
            // Copy DMA buffer back to the caller's buffer
            memcpy(data, dataBuf.bytes, MIN(data_len, rx));
        }
    }

    /* ── Phase 3: Interrupt status (two bytes) ── */
    NSMutableData *statusBuf = [c.iface ioDataWithCapacity:2 error:&err];
    if (!statusBuf) {
        fprintf(stderr, "iousbhost: status buffer alloc failed: %s\n",
                err.localizedDescription.UTF8String);
        return M65_IOUSBHOST_ERR_IO;
    }
    NSUInteger srx = 0;
    if (![c.pipeIntr sendIORequestWithData:statusBuf
                           bytesTransferred:&srx
                          completionTimeout:5.0
                                      error:&err]) {
        fprintf(stderr, "iousbhost: interrupt status failed: %s\n",
                err.localizedDescription.UTF8String);
        return M65_IOUSBHOST_ERR_IO;
    }
    if (srx == 2 && cbi_status_out) {
        memcpy(cbi_status_out, statusBuf.bytes, 2);
    }

    return M65_IOUSBHOST_OK;
}

/* ── §8.9 Endpoint Print (for diagnose) ── */
void m65_iousbhost_print_endpoints(m65_iousbhost_ctx_t *ctx)
{
    M65IOUSBHostCtx *c = (__bridge M65IOUSBHostCtx*)ctx->objc_ctx;
    printf("  bulk-out  ep=0x%02x\n", c.epBulkOutAddr);
    printf("  bulk-in   ep=0x%02x\n", c.epBulkInAddr);
    printf("  intr-in   ep=0x%02x\n", c.epIntrInAddr);
}

/* ── §8.10 Close ── */
void m65_iousbhost_close(m65_iousbhost_ctx_t *ctx)
{
    if (!ctx) return;
    M65IOUSBHostCtx *c =
        (__bridge_transfer M65IOUSBHostCtx*)ctx->objc_ctx;  // releases ARC ref
    [c.iface destroy];  // resets device; mass-storage driver re-registers
    c.iface   = nil;
    c.pipeOut = nil;
    c.pipeIn  = nil;
    c.pipeIntr = nil;
    free(ctx);
}
