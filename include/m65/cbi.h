#ifndef M65_CBI_H
#define M65_CBI_H

#include "m65/transport.h"

#include <stddef.h>
#include <stdint.h>

#define M65_CBI_CDB_LENGTH 12U
#define M65_CBI_STATUS_LENGTH 2U
#define M65_UFI_SENSE_LENGTH 18U
#define M65_CBI_ADSC_REQUEST_TYPE 0x21U
#define M65_CBI_ADSC_REQUEST 0x00U

typedef struct {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
    uint8_t cdb[M65_CBI_CDB_LENGTH];
} M65CbiAdsc;

typedef enum {
    M65_CBI_IO_OK = 0,
    M65_CBI_IO_STALL,
    M65_CBI_IO_PERMISSION,
    M65_CBI_IO_NO_DEVICE,
    M65_CBI_IO_TIMEOUT,
    M65_CBI_IO_ERROR,
    M65_CBI_IO_PROTOCOL
} M65CbiIoStatus;

typedef struct M65CbiIo M65CbiIo;

typedef struct {
    M65CbiIoStatus (*open_seize)(M65CbiIo *io, char *detail, size_t detail_size);
    M65CbiIoStatus (*close)(M65CbiIo *io, char *detail, size_t detail_size);
    M65CbiIoStatus (*adsc)(M65CbiIo *io,
                           const uint8_t cdb[M65_CBI_CDB_LENGTH],
                           uint32_t timeout_ms,
                           char *detail, size_t detail_size);
    M65CbiIoStatus (*bulk_in)(M65CbiIo *io, void *data, size_t length,
                              size_t *transferred, uint32_t timeout_ms,
                              char *detail, size_t detail_size);
    M65CbiIoStatus (*bulk_out)(M65CbiIo *io, const void *data, size_t length,
                               size_t *transferred, uint32_t timeout_ms,
                               char *detail, size_t detail_size);
    M65CbiIoStatus (*interrupt_in_with_deadline)(
        M65CbiIo *io, uint8_t status[M65_CBI_STATUS_LENGTH],
        size_t *transferred, uint32_t timeout_ms,
        char *detail, size_t detail_size);
    M65CbiIoStatus (*clear_stall)(M65CbiIo *io,
                                  M65DataDirection bulk_direction,
                                  char *detail, size_t detail_size);
    /*
     * Prepare the platform adapter for one Command Block Reset.  An adapter
     * with a pending completion request must cancel and drain it here.  It
     * must then arm exactly one ADSC containing the fixed CBI reset block,
     * without allowing any other command through while desynchronized.
     */
    M65CbiIoStatus (*prepare_command_block_reset)(
        M65CbiIo *io, char *detail, size_t detail_size);
    /*
     * Called exactly once after every successful prepare call.  recovered is
     * true only after the reset ADSC, its two-byte completion interrupt, and
     * both bulk-pipe stall/toggle clears succeeded.  The adapter must always
     * consume its one-shot reset permission.  It may clear its local
     * desynchronized state only when recovered is true and it returns OK;
     * otherwise it must remain eligible for another reset attempt.
     */
    M65CbiIoStatus (*finish_command_block_reset)(
        M65CbiIo *io, bool recovered,
        char *detail, size_t detail_size);
    void (*destroy)(M65CbiIo *io);
} M65CbiIoOps;

struct M65CbiIo {
    const M65CbiIoOps *ops;
    void *context;
};

/* Ownership of io transfers to the returned transport only on success. */
M65Transport *m65_cbi_transport_create(M65CbiIo *io);
bool m65_cbi_build_adsc(uint8_t interface_number,
                        const uint8_t cdb[M65_CBI_CDB_LENGTH],
                        M65CbiAdsc *out);

#endif
