#ifndef M65_USB_CBI_MACOS_H
#define M65_USB_CBI_MACOS_H

#include "m65/transport.h"

/* Acquire, execute, release, and destroy this transport on the same thread. */
M65Transport *m65_usb_cbi_transport_create(const char *bsd_name,
                                            char *detail, size_t detail_size,
                                            M65TransportStatus *status);

#endif
