#ifndef M65_UFI_H
#define M65_UFI_H

#include "m65/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M65_OPCODE_TEST_UNIT_READY 0x00U
#define M65_OPCODE_REQUEST_SENSE 0x03U
#define M65_OPCODE_INQUIRY 0x12U
#define M65_OPCODE_READ_FORMAT_CAPACITIES 0x23U
#define M65_OPCODE_READ_CAPACITY_10 0x25U
#define M65_OPCODE_READ_10 0x28U
#define M65_OPCODE_MODE_SELECT_10 0x55U
#define M65_OPCODE_MODE_SENSE_10 0x5aU
#define M65_FLEXIBLE_DISK_PAGE 0x05U
#define M65_FLEX_PAGE_LENGTH 32U
#define M65_MODE_BUFFER_SIZE 256U
#define M65_MAX_FORMAT_DESCRIPTORS 31U

typedef struct {
    char vendor[9];
    char product[17];
    char firmware[5];
} M65Inquiry;

typedef struct {
    uint32_t blocks;
    uint32_t block_size;
} M65Capacity;

typedef struct {
    uint32_t blocks;
    uint32_t block_size;
    uint8_t descriptor_code;
} M65FormatCapacityDescriptor;

typedef struct {
    M65FormatCapacityDescriptor descriptors[M65_MAX_FORMAT_DESCRIPTORS];
    size_t count;
} M65FormatCapacities;

typedef enum {
    M65_FLEX_TRANSFER_RATE = 0,
    M65_FLEX_HEADS,
    M65_FLEX_SECTORS_PER_TRACK,
    M65_FLEX_BYTES_PER_SECTOR,
    M65_FLEX_CYLINDERS,
    M65_FLEX_WRITE_PRECOMP,
    M65_FLEX_REDUCED_WRITE_CURRENT,
    M65_FLEX_STEP_RATE,
    M65_FLEX_STEP_PULSE_WIDTH,
    M65_FLEX_HEAD_SETTLE_DELAY,
    M65_FLEX_MOTOR_ON_DELAY,
    M65_FLEX_MOTOR_OFF_DELAY,
    M65_FLEX_TRDY_SSN_MO,
    M65_FLEX_SPC,
    M65_FLEX_WRITE_COMPENSATION,
    M65_FLEX_HEAD_LOAD_DELAY,
    M65_FLEX_HEAD_UNLOAD_DELAY,
    M65_FLEX_PIN_34_PIN_2,
    M65_FLEX_PIN_4_PIN_1,
    M65_FLEX_ROTATION_RATE,
    M65_FLEX_FIELD_COUNT
} M65FlexibleField;

typedef struct {
    uint16_t transfer_rate_kbit;
    uint8_t heads;
    uint8_t sectors_per_track;
    uint16_t bytes_per_sector;
    uint16_t cylinders;
    uint16_t medium_rotation_rate_rpm;
    bool field_changeable[M65_FLEX_FIELD_COUNT];
    uint8_t page[M65_FLEX_PAGE_LENGTH];
} M65FlexibleDiskPage;

typedef struct {
    uint8_t raw[M65_MODE_BUFFER_SIZE];
    size_t raw_length;
    size_t page_offset;
    M65FlexibleDiskPage flexible;
} M65ModeParameters;

const char *m65_flexible_field_name(M65FlexibleField field);

size_t m65_cdb_test_unit_ready(uint8_t cdb[16]);
size_t m65_cdb_request_sense(uint8_t cdb[16], uint8_t allocation_length);
size_t m65_cdb_inquiry(uint8_t cdb[16], uint8_t allocation_length);
size_t m65_cdb_read_capacity_10(uint8_t cdb[16]);
size_t m65_cdb_read_format_capacities(uint8_t cdb[16], uint16_t allocation_length);
size_t m65_cdb_mode_sense_10(uint8_t cdb[16], bool changeable, uint16_t allocation_length);
size_t m65_cdb_mode_select_10(uint8_t cdb[16], uint16_t parameter_length);
size_t m65_cdb_read_10(uint8_t cdb[16], uint32_t lba, uint16_t blocks);

bool m65_parse_inquiry(const uint8_t *data, size_t length, M65Inquiry *out,
                       char *detail, size_t detail_size);
bool m65_parse_sense(const uint8_t *data, size_t length, M65Sense *out);
bool m65_parse_read_capacity_10(const uint8_t *data, size_t length, M65Capacity *out,
                                char *detail, size_t detail_size);
bool m65_parse_format_capacities(const uint8_t *data, size_t length,
                                 M65FormatCapacities *out, char *detail, size_t detail_size);
bool m65_parse_mode_parameters(const uint8_t *data, size_t length,
                               M65ModeParameters *out, char *detail, size_t detail_size);
bool m65_apply_changeability(M65FlexibleDiskPage *current,
                             const M65FlexibleDiskPage *mask);
void m65_set_1581_geometry(M65FlexibleDiskPage *page);
bool m65_geometry_is_1581(const M65FlexibleDiskPage *page);
bool m65_geometry_changes_allowed(const M65FlexibleDiskPage *current,
                                  const M65FlexibleDiskPage *desired,
                                  const M65FlexibleDiskPage *mask,
                                  char *detail, size_t detail_size);
bool m65_build_mode_select_payload(const M65FlexibleDiskPage *page,
                                   uint8_t *data, size_t capacity, size_t *length,
                                   char *detail, size_t detail_size);

bool m65_validate_command(const M65Command *command, char *detail, size_t detail_size);

#endif
