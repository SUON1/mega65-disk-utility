#ifndef M65_TRANSPORT_IOUSBHOST_BRIDGE_H
#define M65_TRANSPORT_IOUSBHOST_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — implementation details hidden in transport_iousbhost.m */
typedef struct m65_iousbhost_ctx m65_iousbhost_ctx_t;

/* Error codes (negative = error, 0 = success) */
#define M65_IOUSBHOST_OK              0
#define M65_IOUSBHOST_ERR_NOT_FOUND  (-1)  /* no matching interface service in IORegistry */
#define M65_IOUSBHOST_ERR_CAPTURE    (-2)  /* DeviceCapture init failed; IOReturn in log */
#define M65_IOUSBHOST_ERR_PIPE       (-3)  /* required endpoint not found in descriptors */
#define M65_IOUSBHOST_ERR_IO         (-4)  /* ADSC or data transfer failed */
#define M65_IOUSBHOST_ERR_STALL      (-5)  /* endpoint STALL (auto-cleared; retry allowed) */
#define M65_IOUSBHOST_ERR_TIMEOUT    (-6)  /* transfer exceeded timeout */

/*
 * Find and capture the TEAC CBI interface.
 * Caller must be root. No entitlement or IOServiceAuthorize() required for root.
 * On success: *ctx_out is set to a newly allocated context.
 * On failure: *ctx_out is NULL; IOReturn code printed to stderr.
 */
int m65_iousbhost_open(uint16_t vid, uint16_t pid,
                       m65_iousbhost_ctx_t **ctx_out);

/*
 * Send one UFI command via CBI (ADSC → data phase → interrupt status).
 * cdb/cdb_len:     UFI command descriptor block (12 bytes max).
 * data/data_len:   transfer buffer; NULL for zero-length (command-only).
 * direction:       0 = host-to-device (data out), 1 = device-to-host (data in).
 * cbi_status_out:  receives the two-byte CBI interrupt status; may be NULL.
 * Returns M65_IOUSBHOST_OK on success, negative error code otherwise.
 */
int m65_iousbhost_send_ufi(m65_iousbhost_ctx_t *ctx,
                            const uint8_t *cdb, uint8_t cdb_len,
                            uint8_t *data, size_t data_len,
                            int direction,
                            uint8_t cbi_status_out[2]);

/*
 * Print captured endpoint addresses to stdout (used by diagnose command).
 */
void m65_iousbhost_print_endpoints(m65_iousbhost_ctx_t *ctx);

/*
 * Release all resources. Calls [iface destroy], which resets the USB device
 * and allows the kernel to re-register the mass-storage driver.
 * ctx must not be used after this call.
 */
void m65_iousbhost_close(m65_iousbhost_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* M65_TRANSPORT_IOUSBHOST_BRIDGE_H */
