/*
 * usb_cbi_iousbhost_stubs.c — non-Apple fallback for the IOUSBHost transport.
 *
 * IOUSBHost.framework exists only on macOS.  On other platforms the build
 * substitutes this stub so the project still links.  The command engine and
 * the portable C sources are identical across platforms; only this transport
 * factory changes.
 */

#include "m65/usb_cbi_iousbhost_macos.h"

#include <stddef.h>

M65Transport *m65_iousbhost_transport_create(const char *bsd_name,
                                             char *detail, size_t detail_size,
                                             M65TransportStatus *status)
{
    (void)bsd_name;
    if (detail != NULL && detail_size > 0U) {
        detail[0] = '\0';
    }
    if (status != NULL) {
        *status = M65_TRANSPORT_NO_DEVICE;
    }
    return NULL;
}
