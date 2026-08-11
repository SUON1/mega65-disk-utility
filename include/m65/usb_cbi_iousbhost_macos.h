#ifndef M65_USB_CBI_IOUSBHOST_MACOS_H
#define M65_USB_CBI_IOUSBHOST_MACOS_H

#include "m65/transport.h"

/*
 * IOUSBHost whole-device capture transport (Phase 2).
 *
 * This is a low-level I/O adapter for the shared CBI/UFI command engine in
 * src/cbi.c.  It captures the exact TEAC USB UFI/CBI interface that hosts the
 * BSD media node (correlated by BSD name, identical to the IOUSBLib path in
 * usb_cbi_macos.c), then satisfies the M65CbiIo operation table using the
 * IOUSBHost.framework object model.  All allowlisting, exact-transfer
 * accounting, REQUEST SENSE, completion-vs-sense validation, and command-block
 * reset recovery are enforced by src/cbi.c, exactly as for the IOUSBLib
 * transport.  This adapter performs no protocol policy of its own.
 *
 * Acquire, execute, release, and destroy this transport on the same thread.
 * On success a ready M65Transport is returned and *status is M65_TRANSPORT_OK.
 * On failure NULL is returned, *status carries the mapped reason, and detail
 * receives a human-readable message (including the raw IOReturn in hex on any
 * IOKit/IOUSBHost failure path).
 */
M65Transport *m65_iousbhost_transport_create(const char *bsd_name,
                                             char *detail, size_t detail_size,
                                             M65TransportStatus *status);

#endif /* M65_USB_CBI_IOUSBHOST_MACOS_H */
