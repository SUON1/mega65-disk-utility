#ifndef M65_SCSI_MACOS_H
#define M65_SCSI_MACOS_H

#include "m65/transport.h"

M65Transport *m65_scsi_transport_create(const char *bsd_name,
                                        char *detail, size_t detail_size,
                                        M65TransportStatus *status);

#endif
