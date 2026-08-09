#include "m65/cbi.h"

#include "m65/ufi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M65_SCSI_STATUS_GOOD 0x00U
#define M65_SCSI_STATUS_CHECK_CONDITION 0x02U

typedef struct {
    M65CbiIo *io;
    bool opened;
    bool recovery_required;
    char recovery_detail[M65_MAX_ERROR_TEXT];
} CbiContext;

typedef struct {
    bool adsc_stalled;
    bool data_stalled;
    size_t transferred;
    uint8_t completion[M65_CBI_STATUS_LENGTH];
} CbiExchange;

bool m65_cbi_build_adsc(uint8_t interface_number,
                        const uint8_t cdb[M65_CBI_CDB_LENGTH],
                        M65CbiAdsc *out)
{
    if (cdb == NULL || out == NULL) {
        return false;
    }
    (void)memset(out, 0, sizeof(*out));
    out->request_type = M65_CBI_ADSC_REQUEST_TYPE;
    out->request = M65_CBI_ADSC_REQUEST;
    out->index = interface_number;
    out->length = M65_CBI_CDB_LENGTH;
    (void)memcpy(out->cdb, cdb, M65_CBI_CDB_LENGTH);
    return true;
}

static void set_detail(char *detail, size_t detail_size, const char *text)
{
    if (detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size, "%s", text);
    }
}

static void set_default_io_detail(char *detail, size_t detail_size,
                                  const char *operation, M65CbiIoStatus status)
{
    if (detail != NULL && detail_size > 0U && detail[0] == '\0') {
        (void)snprintf(detail, detail_size, "%s failed with CBI I/O status %d",
                       operation, (int)status);
    }
}

static void append_detail(char *detail, size_t detail_size, const char *text)
{
    size_t used;
    size_t available;
    size_t amount;

    if (detail == NULL || detail_size == 0U || text == NULL) {
        return;
    }
    used = 0U;
    while (used < detail_size && detail[used] != '\0') {
        ++used;
    }
    if (used >= detail_size - 1U) {
        return;
    }
    available = detail_size - used - 1U;
    amount = strlen(text);
    if (amount > available) {
        amount = available;
    }
    (void)memcpy(detail + used, text, amount);
    detail[used + amount] = '\0';
}

static bool io_status_loses_phase(M65CbiIoStatus status)
{
    return status == M65_CBI_IO_TIMEOUT ||
           status == M65_CBI_IO_PROTOCOL ||
           status == M65_CBI_IO_ERROR;
}

static void require_recovery(CbiContext *context, const char *detail,
                             const char *fallback)
{
    if (context->recovery_required) {
        return;
    }
    context->recovery_required = true;
    if (detail != NULL && detail[0] != '\0') {
        set_detail(context->recovery_detail,
                   sizeof(context->recovery_detail), detail);
    } else {
        set_detail(context->recovery_detail,
                   sizeof(context->recovery_detail), fallback);
    }
}

static M65CbiIoStatus phase_io_failure(CbiContext *context,
                                       M65CbiIoStatus status,
                                       const char *operation,
                                       char *detail, size_t detail_size)
{
    set_default_io_detail(detail, detail_size, operation, status);
    if (io_status_loses_phase(status)) {
        require_recovery(context, detail, operation);
    }
    return status;
}

static M65TransportStatus map_io_status(M65CbiIoStatus status)
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
    case M65_CBI_IO_ERROR:
        return M65_TRANSPORT_IO;
    case M65_CBI_IO_STALL:
    case M65_CBI_IO_PROTOCOL:
        return M65_TRANSPORT_PROTOCOL;
    }
    return M65_TRANSPORT_IO;
}

static M65CbiIoStatus run_exchange(CbiContext *context,
                                   const uint8_t cdb[M65_CBI_CDB_LENGTH],
                                   M65DataDirection direction,
                                   void *data, size_t data_length,
                                   uint32_t timeout_ms, CbiExchange *exchange,
                                   char *detail, size_t detail_size)
{
    M65CbiIoStatus status;
    size_t transferred = 0U;

    (void)memset(exchange, 0, sizeof(*exchange));
    status = context->io->ops->adsc(context->io, cdb, timeout_ms,
                                    detail, detail_size);
    if (status == M65_CBI_IO_STALL) {
        exchange->adsc_stalled = true;
        return M65_CBI_IO_OK;
    }
    if (status != M65_CBI_IO_OK) {
        return phase_io_failure(context, status, "ADSC",
                                detail, detail_size);
    }

    if (direction == M65_DATA_IN) {
        status = context->io->ops->bulk_in(
            context->io, data, data_length, &transferred, timeout_ms,
            detail, detail_size);
    } else if (direction == M65_DATA_OUT) {
        status = context->io->ops->bulk_out(
            context->io, data, data_length, &transferred, timeout_ms,
            detail, detail_size);
    } else {
        status = M65_CBI_IO_OK;
    }
    exchange->transferred = transferred;
    if (status == M65_CBI_IO_STALL) {
        exchange->data_stalled = true;
        status = context->io->ops->clear_stall(context->io, direction,
                                                detail, detail_size);
        if (status != M65_CBI_IO_OK) {
            return phase_io_failure(context, status,
                                    "clear bulk pipe stall",
                                    detail, detail_size);
        }
    } else if (status != M65_CBI_IO_OK) {
        return phase_io_failure(context, status, "bulk data phase",
                                detail, detail_size);
    } else if (transferred > data_length) {
        set_detail(detail, detail_size,
                   "CBI bulk phase reported more bytes than its buffer");
        require_recovery(context, detail,
                         "CBI bulk phase returned an invalid byte count");
        return M65_CBI_IO_PROTOCOL;
    }

    transferred = 0U;
    status = context->io->ops->interrupt_in_with_deadline(
        context->io, exchange->completion, &transferred, timeout_ms,
        detail, detail_size);
    if (status != M65_CBI_IO_OK) {
        return phase_io_failure(context, status,
                                "CBI command-completion interrupt",
                                detail, detail_size);
    }
    if (transferred != M65_CBI_STATUS_LENGTH) {
        set_detail(detail, detail_size,
                   "CBI command-completion interrupt was not exactly two bytes");
        require_recovery(context, detail,
                         "CBI completion interrupt had an invalid length");
        return M65_CBI_IO_PROTOCOL;
    }
    return M65_CBI_IO_OK;
}

static void append_clause(char *detail, size_t detail_size, const char *clause)
{
    if (detail == NULL || detail_size == 0U || clause == NULL ||
        clause[0] == '\0') {
        return;
    }
    if (detail[0] != '\0') {
        append_detail(detail, detail_size, "; ");
    }
    append_detail(detail, detail_size, clause);
}

static void describe_recovery_failure(const CbiContext *context,
                                      const char *failure,
                                      char *detail, size_t detail_size)
{
    set_detail(detail, detail_size,
               "CBI command-block recovery required after: ");
    if (context->recovery_detail[0] != '\0') {
        append_detail(detail, detail_size, context->recovery_detail);
    } else {
        append_detail(detail, detail_size,
                      "a previous command lost phase synchronization");
    }
    append_detail(detail, detail_size, "; reset failed: ");
    if (failure != NULL && failure[0] != '\0') {
        append_detail(detail, detail_size, failure);
    } else {
        append_detail(detail, detail_size, "unspecified transport error");
    }
}

static M65CbiIoStatus recover_if_required(CbiContext *context,
                                          uint32_t timeout_ms,
                                          char *detail, size_t detail_size)
{
    static const uint8_t reset_cdb[M65_CBI_CDB_LENGTH] = {
        0x1dU, 0x04U, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
    };
    CbiExchange exchange;
    M65CbiIoStatus status;
    M65CbiIoStatus first_failure = M65_CBI_IO_OK;
    M65CbiIoStatus in_status;
    M65CbiIoStatus out_status;
    M65CbiIoStatus finish_status;
    char failure[M65_MAX_ERROR_TEXT] = "";
    char phase_detail[M65_MAX_ERROR_TEXT] = "";
    char in_detail[M65_MAX_ERROR_TEXT] = "";
    char out_detail[M65_MAX_ERROR_TEXT] = "";
    char finish_detail[M65_MAX_ERROR_TEXT] = "";
    bool reset_completed = false;
    bool recovered;

    if (!context->recovery_required) {
        return M65_CBI_IO_OK;
    }

    status = context->io->ops->prepare_command_block_reset(
        context->io, phase_detail, sizeof(phase_detail));
    if (status != M65_CBI_IO_OK) {
        set_default_io_detail(phase_detail, sizeof(phase_detail),
                              "prepare CBI command-block reset", status);
        describe_recovery_failure(context, phase_detail,
                                  detail, detail_size);
        return status;
    }

    status = run_exchange(context, reset_cdb, M65_DATA_NONE, NULL, 0U,
                          timeout_ms, &exchange,
                          phase_detail, sizeof(phase_detail));
    if (status != M65_CBI_IO_OK) {
        first_failure = status;
        append_clause(failure, sizeof(failure), phase_detail);
    } else if (exchange.adsc_stalled) {
        first_failure = M65_CBI_IO_PROTOCOL;
        append_clause(failure, sizeof(failure),
                      "CBI command-block reset ADSC stalled");
    } else if (exchange.data_stalled) {
        first_failure = M65_CBI_IO_PROTOCOL;
        append_clause(failure, sizeof(failure),
                      "CBI command-block reset unexpectedly had a data stall");
    } else if (exchange.completion[0] != 0U || exchange.completion[1] != 0U) {
        first_failure = M65_CBI_IO_PROTOCOL;
        append_clause(failure, sizeof(failure),
                      "CBI command-block reset returned nonzero UFI status");
    } else {
        reset_completed = true;
    }

    if (reset_completed) {
        in_status = context->io->ops->clear_stall(
            context->io, M65_DATA_IN, in_detail, sizeof(in_detail));
        out_status = context->io->ops->clear_stall(
            context->io, M65_DATA_OUT, out_detail, sizeof(out_detail));
        if (in_status != M65_CBI_IO_OK) {
            set_default_io_detail(in_detail, sizeof(in_detail),
                                  "clear bulk-IN after CBI reset", in_status);
            first_failure = in_status;
            append_clause(failure, sizeof(failure), in_detail);
        }
        if (out_status != M65_CBI_IO_OK) {
            set_default_io_detail(out_detail, sizeof(out_detail),
                                  "clear bulk-OUT after CBI reset", out_status);
            if (first_failure == M65_CBI_IO_OK) {
                first_failure = out_status;
            }
            append_clause(failure, sizeof(failure), out_detail);
        }
    }

    recovered = reset_completed && first_failure == M65_CBI_IO_OK;
    finish_status = context->io->ops->finish_command_block_reset(
        context->io, recovered, finish_detail, sizeof(finish_detail));
    if (finish_status != M65_CBI_IO_OK) {
        set_default_io_detail(finish_detail, sizeof(finish_detail),
                              "finish CBI command-block reset", finish_status);
        if (first_failure == M65_CBI_IO_OK) {
            first_failure = finish_status;
        }
        append_clause(failure, sizeof(failure), finish_detail);
        recovered = false;
    }

    if (recovered) {
        context->recovery_required = false;
        context->recovery_detail[0] = '\0';
        return M65_CBI_IO_OK;
    }

    if (first_failure == M65_CBI_IO_OK) {
        first_failure = M65_CBI_IO_PROTOCOL;
    }
    describe_recovery_failure(context, failure, detail, detail_size);
    return first_failure;
}

static M65TransportStatus fetch_sense(CbiContext *context, uint32_t timeout_ms,
                                      M65Sense *sense,
                                      char *detail, size_t detail_size)
{
    uint8_t cdb[16];
    uint8_t padded[M65_CBI_CDB_LENGTH];
    uint8_t data[M65_UFI_SENSE_LENGTH];
    CbiExchange exchange;
    M65CbiIoStatus status;
    size_t cdb_length;

    (void)memset(data, 0, sizeof(data));
    cdb_length = m65_cdb_request_sense(cdb, M65_UFI_SENSE_LENGTH);
    (void)memset(padded, 0, sizeof(padded));
    (void)memcpy(padded, cdb, cdb_length);
    status = run_exchange(context, padded, M65_DATA_IN, data, sizeof(data),
                          timeout_ms, &exchange, detail, detail_size);
    if (status != M65_CBI_IO_OK) {
        return map_io_status(status);
    }
    if (exchange.adsc_stalled) {
        set_detail(detail, detail_size, "REQUEST SENSE ADSC stalled");
        require_recovery(context, detail,
                         "REQUEST SENSE ADSC stalled");
        return M65_TRANSPORT_PROTOCOL;
    }
    if (exchange.data_stalled) {
        set_detail(detail, detail_size, "REQUEST SENSE bulk-in phase stalled");
        require_recovery(context, detail,
                         "REQUEST SENSE bulk-in phase stalled");
        return M65_TRANSPORT_PROTOCOL;
    }
    if (exchange.transferred != sizeof(data)) {
        set_detail(detail, detail_size,
                   "REQUEST SENSE did not return its required 18 bytes");
        require_recovery(context, detail,
                         "REQUEST SENSE returned an invalid byte count");
        return M65_TRANSPORT_PROTOCOL;
    }
    if (!m65_parse_sense(data, sizeof(data), sense)) {
        set_detail(detail, detail_size, "REQUEST SENSE returned malformed sense data");
        require_recovery(context, detail,
                         "REQUEST SENSE returned malformed sense data");
        return M65_TRANSPORT_PROTOCOL;
    }
    return M65_TRANSPORT_OK;
}

static M65TransportStatus cbi_acquire(M65Transport *transport,
                                      char *detail, size_t detail_size)
{
    CbiContext *context = (CbiContext *)transport->context;
    M65CbiIoStatus status;
    if (context->opened) {
        return M65_TRANSPORT_OK;
    }
    if (detail != NULL && detail_size > 0U) {
        detail[0] = '\0';
    }
    status = context->io->ops->open_seize(context->io, detail, detail_size);
    if (status == M65_CBI_IO_OK) {
        context->opened = true;
        context->recovery_required = false;
        context->recovery_detail[0] = '\0';
    } else {
        set_default_io_detail(detail, detail_size, "open/seize USB interface", status);
    }
    return map_io_status(status);
}

static M65TransportStatus cbi_release(M65Transport *transport,
                                      char *detail, size_t detail_size)
{
    CbiContext *context = (CbiContext *)transport->context;
    M65CbiIoStatus status;
    if (!context->opened) {
        return M65_TRANSPORT_OK;
    }
    if (detail != NULL && detail_size > 0U) {
        detail[0] = '\0';
    }
    status = context->io->ops->close(context->io, detail, detail_size);
    if (status == M65_CBI_IO_OK || status == M65_CBI_IO_NO_DEVICE) {
        context->opened = false;
    }
    if (status != M65_CBI_IO_OK) {
        set_default_io_detail(detail, detail_size, "close USB interface", status);
    }
    return map_io_status(status);
}

static M65CommandResult execute_request_sense(CbiContext *context,
                                              const M65Command *command,
                                              const uint8_t padded[M65_CBI_CDB_LENGTH])
{
    M65CommandResult result;
    CbiExchange exchange;
    M65CbiIoStatus status;

    (void)memset(&result, 0, sizeof(result));
    status = run_exchange(context, padded, M65_DATA_IN,
                          command->data, command->data_length,
                          command->timeout_ms, &exchange,
                          result.detail, sizeof(result.detail));
    result.transport_status = map_io_status(status);
    result.transferred = exchange.transferred;
    if (status != M65_CBI_IO_OK) {
        return result;
    }
    if (exchange.adsc_stalled || exchange.data_stalled) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "REQUEST SENSE failed during CBI transport");
        require_recovery(context, result.detail,
                         "REQUEST SENSE failed during CBI transport");
        return result;
    }
    if (!m65_parse_sense((const uint8_t *)command->data,
                         result.transferred, &result.sense)) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "REQUEST SENSE returned malformed sense data");
        require_recovery(context, result.detail,
                         "REQUEST SENSE returned malformed sense data");
        return result;
    }
    result.scsi_status = M65_SCSI_STATUS_GOOD;
    return result;
}

static M65CommandResult cbi_execute(M65Transport *transport,
                                    const M65Command *command)
{
    CbiContext *context = (CbiContext *)transport->context;
    M65CommandResult result;
    CbiExchange exchange;
    M65CbiIoStatus io_status;
    M65TransportStatus sense_status;
    M65CbiIoStatus recovery_status;
    uint8_t padded[M65_CBI_CDB_LENGTH];
    char validation_detail[M65_MAX_ERROR_TEXT];
    bool forced_failure;
    bool exact_data_required;

    (void)memset(&result, 0, sizeof(result));
    if (!context->opened) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "CBI command attempted without seized USB interface");
        return result;
    }
    if (!m65_validate_command(command, validation_detail,
                              sizeof(validation_detail))) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   validation_detail[0] != '\0' ? validation_detail :
                   "command failed diagnostic allowlist validation");
        return result;
    }
    if (command->cdb_length > M65_CBI_CDB_LENGTH) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "UFI command is longer than the 12-byte CBI command block");
        return result;
    }
    if (command->direction == M65_DATA_OUT &&
        command->cdb[0] != M65_OPCODE_MODE_SELECT_10) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "CBI data-out is restricted to validated MODE SELECT (10)");
        return result;
    }

    recovery_status = recover_if_required(context, command->timeout_ms,
                                           result.detail,
                                           sizeof(result.detail));
    if (recovery_status != M65_CBI_IO_OK) {
        result.transport_status = map_io_status(recovery_status);
        return result;
    }

    (void)memset(padded, 0, sizeof(padded));
    (void)memcpy(padded, command->cdb, command->cdb_length);
    if (command->cdb[0] == M65_OPCODE_REQUEST_SENSE) {
        return execute_request_sense(context, command, padded);
    }

    io_status = run_exchange(context, padded, command->direction,
                             command->data, command->data_length,
                             command->timeout_ms, &exchange,
                             result.detail, sizeof(result.detail));
    result.transport_status = map_io_status(io_status);
    result.transferred = exchange.transferred;
    if (io_status != M65_CBI_IO_OK) {
        return result;
    }

    forced_failure = exchange.adsc_stalled || exchange.data_stalled;
    exact_data_required = command->direction == M65_DATA_OUT ||
                          command->cdb[0] == M65_OPCODE_READ_10;
    sense_status = fetch_sense(context, command->timeout_ms, &result.sense,
                               result.detail, sizeof(result.detail));
    if (sense_status != M65_TRANSPORT_OK) {
        result.transport_status = sense_status;
        return result;
    }
    if (!exchange.adsc_stalled &&
        (exchange.completion[0] != result.sense.asc ||
         exchange.completion[1] != result.sense.ascq)) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "UFI interrupt ASC/ASCQ does not match REQUEST SENSE");
        require_recovery(context, result.detail,
                         "UFI completion and sense data disagreed");
        return result;
    }
    if (exact_data_required && !forced_failure &&
        result.transferred != command->data_length) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        set_detail(result.detail, sizeof(result.detail),
                   "CBI data phase returned an unexpected byte count");
        require_recovery(context, result.detail,
                         "CBI data phase returned an unexpected byte count");
        return result;
    }

    result.transport_status = M65_TRANSPORT_OK;
    if (forced_failure || result.sense.key > 1U) {
        result.scsi_status = M65_SCSI_STATUS_CHECK_CONDITION;
        if (result.detail[0] == '\0') {
            (void)snprintf(result.detail, sizeof(result.detail),
                           "UFI command failed: sense key 0x%02x, ASC 0x%02x, ASCQ 0x%02x",
                           result.sense.key, result.sense.asc, result.sense.ascq);
        }
    } else {
        result.scsi_status = M65_SCSI_STATUS_GOOD;
    }
    return result;
}

static void cbi_destroy(M65Transport *transport)
{
    CbiContext *context;
    char detail[M65_MAX_ERROR_TEXT];
    if (transport == NULL) {
        return;
    }
    context = (CbiContext *)transport->context;
    if (context != NULL) {
        if (context->opened) {
            (void)context->io->ops->close(context->io, detail, sizeof(detail));
            context->opened = false;
        }
        context->io->ops->destroy(context->io);
        free(context);
    }
    free(transport);
}

static const M65TransportOps cbi_ops = {
    cbi_acquire,
    cbi_execute,
    cbi_release,
    cbi_destroy
};

M65Transport *m65_cbi_transport_create(M65CbiIo *io)
{
    M65Transport *transport;
    CbiContext *context;
    if (io == NULL || io->ops == NULL || io->ops->open_seize == NULL ||
        io->ops->close == NULL || io->ops->adsc == NULL ||
        io->ops->bulk_in == NULL || io->ops->bulk_out == NULL ||
        io->ops->interrupt_in_with_deadline == NULL ||
        io->ops->clear_stall == NULL ||
        io->ops->prepare_command_block_reset == NULL ||
        io->ops->finish_command_block_reset == NULL ||
        io->ops->destroy == NULL) {
        return NULL;
    }
    transport = (M65Transport *)calloc(1U, sizeof(*transport));
    context = (CbiContext *)calloc(1U, sizeof(*context));
    if (transport == NULL || context == NULL) {
        free(context);
        free(transport);
        return NULL;
    }
    context->io = io;
    transport->ops = &cbi_ops;
    transport->context = context;
    return transport;
}
