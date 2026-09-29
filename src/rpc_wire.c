#include "rpc_wire.h"
#include "wire.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int viart_rpc_parse(const uint8_t *data, size_t len, viart_rpc_frame *out) {
    if (!data || !out || !len) return EINVAL;
    memset(out, 0, sizeof(*out));
    out->kind = data[0];
    if (out->kind == VR_NOTIFICATION) {
        out->payload = data + 1; out->payload_len = len - 1; return 0;
    }
    if (len < 5) return EPROTO;
    out->id = viart_le32(data + 1);
    if (out->kind == VR_REQUEST) {
        const uint8_t *end = memchr(data + 5, 0, len - 5);
        if (!end || end == data + 5) return EPROTO;
        out->method = data + 5; out->method_len = (size_t)(end - out->method);
        out->payload = end + 1; out->payload_len = len - (size_t)(out->payload - data);
        return 0;
    }
    if (out->kind == VR_REPLY) {
        out->payload = data + 5; out->payload_len = len - 5; return 0;
    }
    if (out->kind == VR_ERROR) {
        if (len < 7) return EPROTO;
        out->error_code = (int16_t)viart_le16(data + 5);
        out->payload = data + 7; out->payload_len = len - 7; return 0;
    }
    return EOPNOTSUPP;
}
static int allocate(size_t prefix, const void *data, size_t len,
                    uint8_t **out, size_t *out_len) {
    if (!out || !out_len || (!data && len) || len > SIZE_MAX - prefix) return EINVAL;
    *out = malloc(prefix + len);
    if (!*out) return ENOMEM;
    *out_len = prefix + len;
    return 0;
}
int viart_rpc_encode_request(uint32_t id, const char *method, const void *params,
                             size_t params_len, uint8_t **out, size_t *out_len) {
    if (!method || !*method) return EINVAL;
    size_t m = strlen(method);
    if (m > SIZE_MAX - 6) return EMSGSIZE;
    int err = allocate(6 + m, params, params_len, out, out_len);
    if (err) return err;
    (*out)[0] = VR_REQUEST; viart_store32(*out + 1, id);
    memcpy(*out + 5, method, m); (*out)[5 + m] = 0;
    if (params_len) memcpy(*out + 6 + m, params, params_len);
    return 0;
}
int viart_rpc_encode_notification(const void *data, size_t len,
                                  uint8_t **out, size_t *out_len) {
    int err = allocate(1, data, len, out, out_len);
    if (err) return err;
    (*out)[0] = VR_NOTIFICATION;
    if (len) memcpy(*out + 1, data, len);
    return 0;
}
int viart_rpc_encode_reply(uint8_t kind, uint32_t id, int16_t error_code,
                           const void *data, size_t len, uint8_t **out, size_t *out_len) {
    if (kind != VR_REPLY && kind != VR_ERROR) return EINVAL;
    size_t prefix = kind == VR_REPLY ? 5 : 7;
    int err = allocate(prefix, data, len, out, out_len);
    if (err) return err;
    (*out)[0] = kind; viart_store32(*out + 1, id);
    if (kind == VR_ERROR) viart_store16(*out + 5, (uint16_t)error_code);
    if (len) memcpy(*out + prefix, data, len);
    return 0;
}
