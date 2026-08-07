#ifndef M65_TYPES_H
#define M65_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M65_SCHEMA_VERSION 1U
#define M65_BLOCK_SIZE 512U
#define M65_1581_BLOCKS 1600U
#define M65_1581_IMAGE_SIZE ((size_t)M65_BLOCK_SIZE * (size_t)M65_1581_BLOCKS)
#define M65_MAX_SENSE_BYTES 64U
#define M65_MAX_ERROR_TEXT 256U

typedef struct {
    bool valid;
    uint8_t response_code;
    uint8_t key;
    uint8_t asc;
    uint8_t ascq;
} M65Sense;

typedef enum {
    M65_TRANSPORT_OK = 0,
    M65_TRANSPORT_PERMISSION,
    M65_TRANSPORT_NO_DEVICE,
    M65_TRANSPORT_TIMEOUT,
    M65_TRANSPORT_IO,
    M65_TRANSPORT_PROTOCOL
} M65TransportStatus;

typedef enum {
    M65_DATA_NONE = 0,
    M65_DATA_IN,
    M65_DATA_OUT
} M65DataDirection;

typedef struct {
    uint8_t cdb[16];
    size_t cdb_length;
    M65DataDirection direction;
    void *data;
    size_t data_length;
    uint32_t timeout_ms;
} M65Command;

typedef struct {
    M65TransportStatus transport_status;
    uint8_t scsi_status;
    size_t transferred;
    M65Sense sense;
    char detail[M65_MAX_ERROR_TEXT];
} M65CommandResult;

typedef enum {
    M65_PROBE_OK = 0,
    M65_PROBE_NO_DEVICE,
    M65_PROBE_PERMISSION,
    M65_PROBE_TRANSPORT,
    M65_PROBE_UNSUPPORTED,
    M65_PROBE_MISMATCH,
    M65_PROBE_USAGE
} M65ProbeCode;

#endif
