#ifndef M65_TRANSPORT_H
#define M65_TRANSPORT_H

#include "m65/types.h"

typedef struct M65Transport M65Transport;

typedef struct {
    M65TransportStatus (*acquire_exclusive)(M65Transport *transport, char *detail, size_t detail_size);
    M65CommandResult (*execute)(M65Transport *transport, const M65Command *command);
    M65TransportStatus (*release_exclusive)(M65Transport *transport, char *detail, size_t detail_size);
    void (*destroy)(M65Transport *transport);
} M65TransportOps;

struct M65Transport {
    const M65TransportOps *ops;
    void *context;
};

#endif
