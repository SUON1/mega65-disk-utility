#ifndef M65_FAKE_TRANSPORT_H
#define M65_FAKE_TRANSPORT_H

#include "m65/transport.h"
#include "m65/ufi.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    M65Transport transport;
    M65TransportStatus acquire_status;
    M65TransportStatus release_status;
    M65TransportStatus fail_status;
    uint8_t fail_opcode;
    unsigned int fail_occurrence;
    unsigned int opcode_counts[256];
    unsigned int acquire_count;
    unsigned int release_count;
    unsigned int read_count;
    unsigned int mode_select_count;
    bool acquired;
    bool desired_mode_rejected;
    bool accept_wrong_geometry;
    bool mismatch_repeated_read;
    bool interrupt_after_desired;
    bool destroyed;
    uint8_t original_page[M65_FLEX_PAGE_LENGTH];
    uint8_t current_page[M65_FLEX_PAGE_LENGTH];
    uint8_t mask_page[M65_FLEX_PAGE_LENGTH];
} FakeTransport;

void fake_transport_init(FakeTransport *fake);
void fake_transport_set_all_geometry_changeable(FakeTransport *fake, bool changeable);
bool fake_transport_is_restored(const FakeTransport *fake);

#endif
