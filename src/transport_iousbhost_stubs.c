#include "m65/transport_iousbhost_bridge.h"
#include <stddef.h>

int m65_iousbhost_open(uint16_t vid, uint16_t pid,
                       m65_iousbhost_ctx_t **ctx_out)
{
    (void)vid; (void)pid;
    if (ctx_out) *ctx_out = NULL;
    return M65_IOUSBHOST_ERR_NOT_FOUND;
}

int m65_iousbhost_send_ufi(m65_iousbhost_ctx_t *ctx,
                            const uint8_t *cdb, uint8_t cdb_len,
                            uint8_t *data, size_t data_len,
                            int direction,
                            uint8_t cbi_status_out[2])
{
    (void)ctx; (void)cdb; (void)cdb_len;
    (void)data; (void)data_len; (void)direction; (void)cbi_status_out;
    return M65_IOUSBHOST_ERR_NOT_FOUND;
}

void m65_iousbhost_print_endpoints(m65_iousbhost_ctx_t *ctx) { (void)ctx; }
void m65_iousbhost_close(m65_iousbhost_ctx_t *ctx)          { (void)ctx; }
