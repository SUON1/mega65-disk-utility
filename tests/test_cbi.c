#include "test.h"

#include "m65/cbi.h"
#include "m65/ufi.h"

#include <stdio.h>
#include <string.h>

enum {
    STEP_OPEN = 1,
    STEP_CLOSE,
    STEP_ADSC,
    STEP_BULK_IN,
    STEP_BULK_OUT,
    STEP_INTERRUPT_IN,
    STEP_CLEAR_IN,
    STEP_CLEAR_OUT,
    STEP_PREPARE_RESET,
    STEP_FINISH_RESET,
    STEP_DESTROY
};

typedef struct {
    M65CbiIo io;
    unsigned int log[64];
    size_t log_count;
    uint8_t cdbs[16][M65_CBI_CDB_LENGTH];
    size_t cdb_count;
    uint8_t current_opcode;
    unsigned int open_count;
    unsigned int close_count;
    bool destroyed;
    M65CbiIoStatus open_status;
    M65CbiIoStatus close_status;
    M65CbiIoStatus ordinary_adsc_status;
    M65CbiIoStatus reset_adsc_status;
    M65CbiIoStatus sense_adsc_status;
    M65CbiIoStatus ordinary_bulk_status;
    M65CbiIoStatus sense_bulk_status;
    M65CbiIoStatus bulk_out_status;
    M65CbiIoStatus ordinary_interrupt_status;
    M65CbiIoStatus reset_interrupt_status;
    M65CbiIoStatus sense_interrupt_status;
    M65CbiIoStatus clear_in_status;
    M65CbiIoStatus clear_out_status;
    M65CbiIoStatus prepare_reset_status;
    M65CbiIoStatus finish_reset_status;
    size_t ordinary_transfer_override;
    size_t sense_transfer_override;
    size_t interrupt_transfer_override;
    uint8_t completion_asc;
    uint8_t completion_ascq;
    uint8_t sense_key;
    uint8_t sense_asc;
    uint8_t sense_ascq;
    bool malformed_sense;
    bool finish_recovered;
    unsigned int bulk_out_count;
    uint8_t bulk_out_opcode;
} FakeCbiIo;

static bool is_reset_cdb(const uint8_t cdb[M65_CBI_CDB_LENGTH])
{
    static const uint8_t expected[M65_CBI_CDB_LENGTH] = {
        0x1dU, 0x04U, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
    };
    return memcmp(cdb, expected, sizeof(expected)) == 0;
}

static void record_step(FakeCbiIo *fake, unsigned int step)
{
    if (fake->log_count < sizeof(fake->log) / sizeof(fake->log[0])) {
        fake->log[fake->log_count] = step;
        ++fake->log_count;
    }
}

static void fake_detail(char *detail, size_t detail_size, const char *text,
                        M65CbiIoStatus status)
{
    if (status != M65_CBI_IO_OK && detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size, "%s", text);
    }
}

static M65CbiIoStatus fake_open(M65CbiIo *io, char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    ++fake->open_count;
    record_step(fake, STEP_OPEN);
    fake_detail(detail, detail_size, "simulated open failure", fake->open_status);
    return fake->open_status;
}

static M65CbiIoStatus fake_close(M65CbiIo *io, char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    ++fake->close_count;
    record_step(fake, STEP_CLOSE);
    fake_detail(detail, detail_size, "simulated close failure", fake->close_status);
    return fake->close_status;
}

static M65CbiIoStatus fake_adsc(M65CbiIo *io,
                                const uint8_t cdb[M65_CBI_CDB_LENGTH],
                                uint32_t timeout_ms,
                                char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    M65CbiIoStatus status;
    (void)timeout_ms;
    fake->current_opcode = cdb[0];
    if (fake->cdb_count < sizeof(fake->cdbs) / sizeof(fake->cdbs[0])) {
        (void)memcpy(fake->cdbs[fake->cdb_count], cdb, M65_CBI_CDB_LENGTH);
        ++fake->cdb_count;
    }
    record_step(fake, STEP_ADSC);
    if (is_reset_cdb(cdb)) {
        status = fake->reset_adsc_status;
    } else {
        status = cdb[0] == M65_OPCODE_REQUEST_SENSE ?
                 fake->sense_adsc_status : fake->ordinary_adsc_status;
    }
    fake_detail(detail, detail_size, "simulated ADSC failure", status);
    return status;
}

static void fill_sense(FakeCbiIo *fake, uint8_t *data, size_t length)
{
    (void)memset(data, 0, length);
    if (length >= M65_UFI_SENSE_LENGTH) {
        data[0] = fake->malformed_sense ? 0x00U : 0x70U;
        data[2] = fake->sense_key;
        data[7] = 10U;
        data[12] = fake->sense_asc;
        data[13] = fake->sense_ascq;
    }
}

static M65CbiIoStatus fake_bulk_in(M65CbiIo *io, void *data, size_t length,
                                   size_t *transferred, uint32_t timeout_ms,
                                   char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    M65CbiIoStatus status;
    size_t amount;
    (void)timeout_ms;
    record_step(fake, STEP_BULK_IN);
    if (fake->current_opcode == M65_OPCODE_REQUEST_SENSE) {
        status = fake->sense_bulk_status;
        amount = fake->sense_transfer_override == SIZE_MAX ?
                 length : fake->sense_transfer_override;
        if (status == M65_CBI_IO_OK) {
            fill_sense(fake, (uint8_t *)data, length);
        }
    } else {
        status = fake->ordinary_bulk_status;
        amount = fake->ordinary_transfer_override == SIZE_MAX ?
                 length : fake->ordinary_transfer_override;
        if (status == M65_CBI_IO_OK) {
            (void)memset(data, 0x5a, length);
        }
    }
    *transferred = amount;
    fake_detail(detail, detail_size, "simulated bulk-in failure", status);
    return status;
}

static M65CbiIoStatus fake_bulk_out(M65CbiIo *io, const void *data,
                                    size_t length, size_t *transferred,
                                    uint32_t timeout_ms,
                                    char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    (void)data;
    (void)timeout_ms;
    record_step(fake, STEP_BULK_OUT);
    ++fake->bulk_out_count;
    fake->bulk_out_opcode = fake->current_opcode;
    *transferred = fake->ordinary_transfer_override == SIZE_MAX ?
                   length : fake->ordinary_transfer_override;
    fake_detail(detail, detail_size, "simulated bulk-out failure",
                fake->bulk_out_status);
    return fake->bulk_out_status;
}

static M65CbiIoStatus fake_interrupt(
    M65CbiIo *io, uint8_t status_bytes[M65_CBI_STATUS_LENGTH],
    size_t *transferred, uint32_t timeout_ms,
    char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    M65CbiIoStatus status;
    (void)timeout_ms;
    record_step(fake, STEP_INTERRUPT_IN);
    if (fake->current_opcode == 0x1dU) {
        status = fake->reset_interrupt_status;
    } else {
        status = fake->current_opcode == M65_OPCODE_REQUEST_SENSE ?
                 fake->sense_interrupt_status : fake->ordinary_interrupt_status;
    }
    status_bytes[0] = fake->current_opcode == M65_OPCODE_REQUEST_SENSE ?
                      0U : fake->completion_asc;
    status_bytes[1] = fake->current_opcode == M65_OPCODE_REQUEST_SENSE ?
                      0U : fake->completion_ascq;
    *transferred = fake->interrupt_transfer_override == SIZE_MAX ?
                   M65_CBI_STATUS_LENGTH : fake->interrupt_transfer_override;
    fake_detail(detail, detail_size, "simulated interrupt failure", status);
    return status;
}

static M65CbiIoStatus fake_clear(M65CbiIo *io,
                                 M65DataDirection bulk_direction,
                                 char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    record_step(fake, bulk_direction == M65_DATA_IN ?
                       STEP_CLEAR_IN : STEP_CLEAR_OUT);
    if (bulk_direction == M65_DATA_IN) {
        fake_detail(detail, detail_size, "simulated clear-IN failure",
                    fake->clear_in_status);
        return fake->clear_in_status;
    }
    fake_detail(detail, detail_size, "simulated clear-OUT failure",
                fake->clear_out_status);
    return fake->clear_out_status;
}

static M65CbiIoStatus fake_prepare_reset(M65CbiIo *io,
                                         char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    record_step(fake, STEP_PREPARE_RESET);
    fake_detail(detail, detail_size, "simulated reset preparation failure",
                fake->prepare_reset_status);
    return fake->prepare_reset_status;
}

static M65CbiIoStatus fake_finish_reset(M65CbiIo *io, bool recovered,
                                        char *detail, size_t detail_size)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    record_step(fake, STEP_FINISH_RESET);
    fake->finish_recovered = recovered;
    fake_detail(detail, detail_size, "simulated reset finish failure",
                fake->finish_reset_status);
    return fake->finish_reset_status;
}

static void fake_destroy(M65CbiIo *io)
{
    FakeCbiIo *fake = (FakeCbiIo *)io->context;
    fake->destroyed = true;
    record_step(fake, STEP_DESTROY);
}

static const M65CbiIoOps fake_ops = {
    fake_open,
    fake_close,
    fake_adsc,
    fake_bulk_in,
    fake_bulk_out,
    fake_interrupt,
    fake_clear,
    fake_prepare_reset,
    fake_finish_reset,
    fake_destroy
};

static void fake_init(FakeCbiIo *fake)
{
    (void)memset(fake, 0, sizeof(*fake));
    fake->io.ops = &fake_ops;
    fake->io.context = fake;
    fake->ordinary_transfer_override = SIZE_MAX;
    fake->sense_transfer_override = SIZE_MAX;
    fake->interrupt_transfer_override = SIZE_MAX;
}

static M65Transport *opened_transport(FakeCbiIo *fake)
{
    M65Transport *transport = m65_cbi_transport_create(&fake->io);
    char detail[M65_MAX_ERROR_TEXT];
    EXPECT_TRUE(transport != NULL);
    if (transport != NULL) {
        EXPECT_EQ_INT(transport->ops->acquire_exclusive(
                          transport, detail, sizeof(detail)),
                      M65_TRANSPORT_OK);
    }
    return transport;
}

static M65Command inquiry_command(uint8_t *data, size_t length)
{
    M65Command command;
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_inquiry(command.cdb, (uint8_t)length);
    command.direction = M65_DATA_IN;
    command.data = data;
    command.data_length = length;
    command.timeout_ms = 1000U;
    return command;
}

static M65Command mode_select_command(uint8_t *data, size_t length)
{
    M65Command command;
    (void)memset(data, 0, length);
    if (length >= 10U) {
        data[8] = M65_FLEXIBLE_DISK_PAGE;
        data[9] = (uint8_t)(length - 10U);
    }
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_mode_select_10(
        command.cdb, (uint16_t)length);
    command.direction = M65_DATA_OUT;
    command.data = data;
    command.data_length = length;
    command.timeout_ms = 1000U;
    return command;
}

static size_t count_step(const FakeCbiIo *fake, unsigned int wanted)
{
    size_t count = 0U;
    size_t index;
    for (index = 0U; index < fake->log_count; ++index) {
        if (fake->log[index] == wanted) {
            ++count;
        }
    }
    return count;
}

static M65Transport *transport_after_interrupt_timeout(FakeCbiIo *fake)
{
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[36];

    fake_init(fake);
    fake->ordinary_interrupt_status = M65_CBI_IO_TIMEOUT;
    transport = opened_transport(fake);
    if (transport == NULL) {
        return NULL;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_TIMEOUT);
    fake->ordinary_interrupt_status = M65_CBI_IO_OK;
    return transport;
}

static void expect_steps(const FakeCbiIo *fake, const unsigned int *steps,
                         size_t count)
{
    EXPECT_EQ_U64(fake->log_count, count);
    if (fake->log_count == count) {
        EXPECT_MEMEQ(fake->log, steps, count * sizeof(steps[0]));
    }
}

static void test_data_in_order_padding_and_sense(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[36];
    static const unsigned int steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN,
        STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };
    static const uint8_t expected_inquiry[M65_CBI_CDB_LENGTH] = {
        0x12U, 0U, 0U, 0U, 36U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    static const uint8_t expected_sense[M65_CBI_CDB_LENGTH] = {
        0x03U, 0U, 0U, 0U, 18U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    fake_init(&fake);
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    (void)memset(&command.cdb[6], 0xa5, 10U);
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 0U);
    EXPECT_EQ_U64(result.transferred, sizeof(data));
    EXPECT_TRUE(result.sense.valid);
    EXPECT_MEMEQ(fake.cdbs[0], expected_inquiry, sizeof(expected_inquiry));
    EXPECT_MEMEQ(fake.cdbs[1], expected_sense, sizeof(expected_sense));
    expect_steps(&fake, steps, sizeof(steps) / sizeof(steps[0]));
    transport->ops->destroy(transport);
    EXPECT_TRUE(fake.destroyed);
    EXPECT_EQ_U64(fake.close_count, 1U);
}

static void test_adsc_setup(void)
{
    const uint8_t cdb[M65_CBI_CDB_LENGTH] = {
        0x28U, 0U, 0U, 0U, 0U, 9U, 0U, 0U, 1U, 0U, 0U, 0U
    };
    M65CbiAdsc adsc;
    EXPECT_TRUE(m65_cbi_build_adsc(7U, cdb, &adsc));
    EXPECT_EQ_U64(adsc.request_type, 0x21U);
    EXPECT_EQ_U64(adsc.request, 0U);
    EXPECT_EQ_U64(adsc.value, 0U);
    EXPECT_EQ_U64(adsc.index, 7U);
    EXPECT_EQ_U64(adsc.length, 12U);
    EXPECT_MEMEQ(adsc.cdb, cdb, sizeof(cdb));
    EXPECT_FALSE(m65_cbi_build_adsc(0U, NULL, &adsc));
    EXPECT_FALSE(m65_cbi_build_adsc(0U, cdb, NULL));
}

static void test_nondata_and_data_out_order(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t payload[40] = {0U};
    static const unsigned int nondata_steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_INTERRUPT_IN,
        STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };
    static const unsigned int out_steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_BULK_OUT, STEP_INTERRUPT_IN,
        STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };

    fake_init(&fake);
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_test_unit_ready(command.cdb);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    expect_steps(&fake, nondata_steps,
                 sizeof(nondata_steps) / sizeof(nondata_steps[0]));
    transport->ops->destroy(transport);

    fake_init(&fake);
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    payload[8] = M65_FLEXIBLE_DISK_PAGE;
    payload[9] = 30U;
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_mode_select_10(command.cdb,
                                                (uint16_t)sizeof(payload));
    command.direction = M65_DATA_OUT;
    command.data = payload;
    command.data_length = sizeof(payload);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 0U);
    expect_steps(&fake, out_steps, sizeof(out_steps) / sizeof(out_steps[0]));
    transport->ops->destroy(transport);
}

static void test_ufi_status_and_recovered_error(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[36];

    fake_init(&fake);
    fake.completion_asc = 0x3aU;
    fake.sense_key = 2U;
    fake.sense_asc = 0x3aU;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 2U);
    EXPECT_EQ_U64(result.sense.key, 2U);
    EXPECT_EQ_U64(result.sense.asc, 0x3aU);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.completion_asc = 0x17U;
    fake.completion_ascq = 1U;
    fake.sense_key = 1U;
    fake.sense_asc = 0x17U;
    fake.sense_ascq = 1U;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 0U);
    EXPECT_EQ_U64(result.sense.key, 1U);
    transport->ops->destroy(transport);
}

static void test_adsc_and_bulk_stalls(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[36];
    uint8_t payload[40] = {0U};
    static const unsigned int adsc_steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };
    static const unsigned int bulk_steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_BULK_IN, STEP_CLEAR_IN, STEP_INTERRUPT_IN,
        STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };
    static const unsigned int bulk_out_steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_BULK_OUT, STEP_CLEAR_OUT, STEP_INTERRUPT_IN,
        STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };

    fake_init(&fake);
    fake.ordinary_adsc_status = M65_CBI_IO_STALL;
    fake.sense_key = 5U;
    fake.sense_asc = 0x24U;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 2U);
    expect_steps(&fake, adsc_steps, sizeof(adsc_steps) / sizeof(adsc_steps[0]));
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.ordinary_bulk_status = M65_CBI_IO_STALL;
    fake.completion_asc = 0x11U;
    fake.sense_key = 3U;
    fake.sense_asc = 0x11U;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 2U);
    expect_steps(&fake, bulk_steps, sizeof(bulk_steps) / sizeof(bulk_steps[0]));
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.bulk_out_status = M65_CBI_IO_STALL;
    fake.completion_asc = 0x26U;
    fake.sense_key = 5U;
    fake.sense_asc = 0x26U;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    payload[8] = M65_FLEXIBLE_DISK_PAGE;
    payload[9] = 30U;
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_mode_select_10(command.cdb,
                                                (uint16_t)sizeof(payload));
    command.direction = M65_DATA_OUT;
    command.data = payload;
    command.data_length = sizeof(payload);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 2U);
    expect_steps(&fake, bulk_out_steps,
                 sizeof(bulk_out_steps) / sizeof(bulk_out_steps[0]));
    transport->ops->destroy(transport);
}

static void test_timeout_and_unplug_propagation(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[36];

    fake_init(&fake);
    fake.ordinary_adsc_status = M65_CBI_IO_TIMEOUT;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_TIMEOUT);
    EXPECT_EQ_U64(fake.cdb_count, 1U);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.ordinary_interrupt_status = M65_CBI_IO_STALL;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    fake.ordinary_interrupt_status = M65_CBI_IO_OK;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(count_step(&fake, STEP_PREPARE_RESET), 1U);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.ordinary_bulk_status = M65_CBI_IO_NO_DEVICE;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_NO_DEVICE);
    EXPECT_EQ_U64(fake.cdb_count, 1U);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.ordinary_interrupt_status = M65_CBI_IO_TIMEOUT;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_TIMEOUT);
    EXPECT_EQ_U64(fake.cdb_count, 1U);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.sense_bulk_status = M65_CBI_IO_TIMEOUT;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_TIMEOUT);
    EXPECT_EQ_U64(fake.cdb_count, 2U);
    transport->ops->destroy(transport);
}

static void test_protocol_errors_and_nonrecursive_sense(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[M65_UFI_SENSE_LENGTH];

    fake_init(&fake);
    fake.interrupt_transfer_override = 1U;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.completion_asc = 0x3aU;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.sense_transfer_override = 14U;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    command = inquiry_command(data, sizeof(data));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    transport->ops->destroy(transport);

    fake_init(&fake);
    fake.sense_key = 2U;
    fake.sense_asc = 0x3aU;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    (void)memset(&command, 0, sizeof(command));
    command.cdb_length = m65_cdb_request_sense(command.cdb,
                                               (uint8_t)sizeof(data));
    command.direction = M65_DATA_IN;
    command.data = data;
    command.data_length = sizeof(data);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 0U);
    EXPECT_EQ_U64(result.sense.key, 2U);
    EXPECT_EQ_U64(fake.cdb_count, 1U);
    transport->ops->destroy(transport);
}

static void test_reset_recovery_exact_sequence(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t payload[40];
    static const uint8_t expected_reset[M65_CBI_CDB_LENGTH] = {
        0x1dU, 0x04U, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
    };
    static const unsigned int steps[] = {
        STEP_OPEN, STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN,
        STEP_PREPARE_RESET, STEP_ADSC, STEP_INTERRUPT_IN,
        STEP_CLEAR_IN, STEP_CLEAR_OUT, STEP_FINISH_RESET,
        STEP_ADSC, STEP_BULK_OUT, STEP_INTERRUPT_IN,
        STEP_ADSC, STEP_BULK_IN, STEP_INTERRUPT_IN
    };

    transport = transport_after_interrupt_timeout(&fake);
    if (transport == NULL) {
        return;
    }
    command = mode_select_command(payload, sizeof(payload));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(result.scsi_status, 0U);
    EXPECT_EQ_U64(fake.cdb_count, 4U);
    EXPECT_MEMEQ(fake.cdbs[1], expected_reset, sizeof(expected_reset));
    EXPECT_EQ_U64(fake.bulk_out_count, 1U);
    EXPECT_EQ_U64(fake.bulk_out_opcode, M65_OPCODE_MODE_SELECT_10);
    EXPECT_TRUE(fake.finish_recovered);
    expect_steps(&fake, steps, sizeof(steps) / sizeof(steps[0]));
    transport->ops->destroy(transport);
}

typedef enum {
    RESET_FAIL_PREPARE = 0,
    RESET_FAIL_ADSC,
    RESET_FAIL_INTERRUPT,
    RESET_FAIL_STATUS,
    RESET_FAIL_CLEAR_IN,
    RESET_FAIL_CLEAR_OUT,
    RESET_FAIL_FINISH
} ResetFailureStage;

static void run_reset_failure_case(ResetFailureStage stage,
                                   M65TransportStatus expected_status)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t payload[40];

    transport = transport_after_interrupt_timeout(&fake);
    if (transport == NULL) {
        return;
    }
    switch (stage) {
    case RESET_FAIL_PREPARE:
        fake.prepare_reset_status = M65_CBI_IO_ERROR;
        break;
    case RESET_FAIL_ADSC:
        fake.reset_adsc_status = M65_CBI_IO_TIMEOUT;
        break;
    case RESET_FAIL_INTERRUPT:
        fake.reset_interrupt_status = M65_CBI_IO_PROTOCOL;
        break;
    case RESET_FAIL_STATUS:
        fake.completion_asc = 0x20U;
        break;
    case RESET_FAIL_CLEAR_IN:
        fake.clear_in_status = M65_CBI_IO_ERROR;
        break;
    case RESET_FAIL_CLEAR_OUT:
        fake.clear_out_status = M65_CBI_IO_NO_DEVICE;
        break;
    case RESET_FAIL_FINISH:
        fake.finish_reset_status = M65_CBI_IO_ERROR;
        break;
    }

    command = mode_select_command(payload, sizeof(payload));
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, expected_status);
    EXPECT_TRUE(strstr(result.detail, "simulated interrupt failure") != NULL);
    EXPECT_TRUE(strstr(result.detail, "reset failed") != NULL);
    EXPECT_EQ_U64(fake.bulk_out_count, 0U);
    if (stage == RESET_FAIL_PREPARE) {
        EXPECT_EQ_U64(fake.cdb_count, 1U);
        EXPECT_EQ_U64(count_step(&fake, STEP_FINISH_RESET), 0U);
    } else {
        EXPECT_EQ_U64(fake.cdb_count, 2U);
        EXPECT_EQ_U64(count_step(&fake, STEP_FINISH_RESET), 1U);
    }
    if (stage == RESET_FAIL_CLEAR_IN || stage == RESET_FAIL_CLEAR_OUT ||
        stage == RESET_FAIL_FINISH) {
        EXPECT_EQ_U64(count_step(&fake, STEP_CLEAR_IN), 1U);
        EXPECT_EQ_U64(count_step(&fake, STEP_CLEAR_OUT), 1U);
    }
    if (stage == RESET_FAIL_FINISH) {
        EXPECT_TRUE(fake.finish_recovered);
    } else if (stage != RESET_FAIL_PREPARE) {
        EXPECT_FALSE(fake.finish_recovered);
    }

    fake.prepare_reset_status = M65_CBI_IO_OK;
    fake.reset_adsc_status = M65_CBI_IO_OK;
    fake.reset_interrupt_status = M65_CBI_IO_OK;
    fake.completion_asc = 0U;
    fake.clear_in_status = M65_CBI_IO_OK;
    fake.clear_out_status = M65_CBI_IO_OK;
    fake.finish_reset_status = M65_CBI_IO_OK;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_OK);
    EXPECT_EQ_U64(fake.bulk_out_count, 1U);
    EXPECT_TRUE(fake.finish_recovered);
    EXPECT_EQ_U64(count_step(&fake, STEP_PREPARE_RESET), 2U);
    transport->ops->destroy(transport);
}

static void test_reset_failure_propagation_and_retry(void)
{
    run_reset_failure_case(RESET_FAIL_PREPARE, M65_TRANSPORT_IO);
    run_reset_failure_case(RESET_FAIL_ADSC, M65_TRANSPORT_TIMEOUT);
    run_reset_failure_case(RESET_FAIL_INTERRUPT, M65_TRANSPORT_PROTOCOL);
    run_reset_failure_case(RESET_FAIL_STATUS, M65_TRANSPORT_PROTOCOL);
    run_reset_failure_case(RESET_FAIL_CLEAR_IN, M65_TRANSPORT_IO);
    run_reset_failure_case(RESET_FAIL_CLEAR_OUT, M65_TRANSPORT_NO_DEVICE);
    run_reset_failure_case(RESET_FAIL_FINISH, M65_TRANSPORT_IO);
}

static void test_pending_recovery_does_not_bypass_allowlist(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[512] = {0U};

    transport = transport_after_interrupt_timeout(&fake);
    if (transport == NULL) {
        return;
    }
    (void)memset(&command, 0, sizeof(command));
    command.cdb[0] = 0x2aU;
    command.cdb_length = 10U;
    command.direction = M65_DATA_OUT;
    command.data = data;
    command.data_length = sizeof(data);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    EXPECT_EQ_U64(fake.cdb_count, 1U);
    EXPECT_EQ_U64(fake.bulk_out_count, 0U);
    EXPECT_EQ_U64(count_step(&fake, STEP_PREPARE_RESET), 0U);
    transport->ops->destroy(transport);
}

static void test_safety_and_cleanup(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    M65Command command;
    M65CommandResult result;
    uint8_t data[512] = {0U};
    char detail[M65_MAX_ERROR_TEXT];

    fake_init(&fake);
    transport = m65_cbi_transport_create(&fake.io);
    EXPECT_TRUE(transport != NULL);
    if (transport == NULL) {
        return;
    }
    (void)memset(&command, 0, sizeof(command));
    command.cdb[0] = 0x2aU;
    command.cdb_length = 10U;
    command.direction = M65_DATA_OUT;
    command.data = data;
    command.data_length = sizeof(data);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    EXPECT_EQ_U64(fake.cdb_count, 0U);
    EXPECT_EQ_INT(transport->ops->acquire_exclusive(
                      transport, detail, sizeof(detail)), M65_TRANSPORT_OK);
    EXPECT_EQ_INT(transport->ops->acquire_exclusive(
                      transport, detail, sizeof(detail)), M65_TRANSPORT_OK);
    EXPECT_EQ_U64(fake.open_count, 1U);

    (void)memset(&command, 0, sizeof(command));
    (void)memcpy(command.cdb,
                 ((const uint8_t[M65_CBI_CDB_LENGTH]){
                     0x1dU, 0x04U, 0xffU, 0xffU, 0xffU, 0xffU,
                     0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
                 }),
                 M65_CBI_CDB_LENGTH);
    command.cdb_length = M65_CBI_CDB_LENGTH;
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    EXPECT_EQ_U64(fake.cdb_count, 0U);

    (void)memset(&command, 0, sizeof(command));
    command.cdb[0] = 0x2aU;
    command.cdb_length = 10U;
    command.direction = M65_DATA_OUT;
    command.data = data;
    command.data_length = sizeof(data);
    command.timeout_ms = 1000U;
    result = transport->ops->execute(transport, &command);
    EXPECT_EQ_INT(result.transport_status, M65_TRANSPORT_PROTOCOL);
    EXPECT_EQ_U64(fake.cdb_count, 0U);
    EXPECT_EQ_INT(transport->ops->release_exclusive(
                      transport, detail, sizeof(detail)), M65_TRANSPORT_OK);
    EXPECT_EQ_INT(transport->ops->release_exclusive(
                      transport, detail, sizeof(detail)), M65_TRANSPORT_OK);
    EXPECT_EQ_U64(fake.close_count, 1U);
    transport->ops->destroy(transport);
    EXPECT_TRUE(fake.destroyed);
    EXPECT_EQ_U64(fake.close_count, 1U);

    fake_init(&fake);
    fake.open_status = M65_CBI_IO_PERMISSION;
    transport = m65_cbi_transport_create(&fake.io);
    EXPECT_TRUE(transport != NULL);
    if (transport != NULL) {
        EXPECT_EQ_INT(transport->ops->acquire_exclusive(
                          transport, detail, sizeof(detail)),
                      M65_TRANSPORT_PERMISSION);
        transport->ops->destroy(transport);
        EXPECT_EQ_U64(fake.close_count, 0U);
        EXPECT_TRUE(fake.destroyed);
    }
}

static void test_close_failure_is_retried_by_destroy(void)
{
    FakeCbiIo fake;
    M65Transport *transport;
    char detail[M65_MAX_ERROR_TEXT];
    fake_init(&fake);
    fake.close_status = M65_CBI_IO_ERROR;
    transport = opened_transport(&fake);
    if (transport == NULL) {
        return;
    }
    EXPECT_EQ_INT(transport->ops->release_exclusive(
                      transport, detail, sizeof(detail)), M65_TRANSPORT_IO);
    EXPECT_EQ_U64(fake.close_count, 1U);
    transport->ops->destroy(transport);
    EXPECT_EQ_U64(fake.close_count, 2U);
    EXPECT_TRUE(fake.destroyed);
}

void test_cbi(void)
{
    test_adsc_setup();
    test_data_in_order_padding_and_sense();
    test_nondata_and_data_out_order();
    test_ufi_status_and_recovered_error();
    test_adsc_and_bulk_stalls();
    test_timeout_and_unplug_propagation();
    test_protocol_errors_and_nonrecursive_sense();
    test_reset_recovery_exact_sequence();
    test_reset_failure_propagation_and_retry();
    test_pending_recovery_does_not_bypass_allowlist();
    test_safety_and_cleanup();
    test_close_failure_is_retried_by_destroy();
}
