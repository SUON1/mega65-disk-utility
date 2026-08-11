#ifndef M65_PROBE_H
#define M65_PROBE_H

#include "m65/transport.h"
#include "m65/ufi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    M65ProbeCode code;
    char reason[M65_MAX_ERROR_TEXT];
    M65Sense sense;
    M65Inquiry inquiry;
    bool unit_ready;
    M65Capacity capacity;
    M65FormatCapacities format_capacities;
    M65ModeParameters current_mode;
    M65ModeParameters changeable_mode;
    bool exclusive_acquired;
    bool exclusive_released;
} M65InspectReport;

typedef enum {
    M65_1581_SUPPORTED = 0,
    M65_1581_UNSUPPORTED,
    M65_1581_INCONCLUSIVE
} M651581Status;

typedef struct {
    M65ProbeCode code;
    M651581Status status;
    char reason[M65_MAX_ERROR_TEXT];
    M65Sense sense;
    M65FlexibleDiskPage original;
    M65FlexibleDiskPage changeable;
    M65FlexibleDiskPage requested;
    M65FlexibleDiskPage accepted;
    bool acknowledgement_supplied;
    bool exclusive_acquired;
    bool exclusive_released;
    bool original_saved;
    bool controller_changed;
    bool controller_restored;
    bool boundary_lba_9_read;
    bool boundary_lba_1599_read;
    bool repeated_reads_match;
    uint8_t *image;
    size_t image_length;
} M651581Report;

M65ProbeCode m65_inspect(M65Transport *transport, M65InspectReport *report);
M65ProbeCode m65_test_1581(M65Transport *transport, bool acknowledgement,
                           M651581Report *report);

/*
 * Map the outcome of the diagnose workflow to a process exit code.  Kept in
 * the portable core (linked into m65core) so it is unit-testable without the
 * macOS transport.  transport_created indicates whether the IOUSBHost capture
 * transport was successfully created; create_status carries the transport
 * status when creation failed; inspect_code is the M65ProbeCode returned by
 * m65_inspect when the transport was created.
 */
int m65_diagnose_exit_code(bool transport_created,
                           M65TransportStatus create_status,
                           M65ProbeCode inspect_code);
void m65_1581_report_destroy(M651581Report *report);
void m65_probe_request_interrupt(void);
void m65_probe_clear_interrupt(void);
const char *m65_1581_status_text(M651581Status status);

#endif
