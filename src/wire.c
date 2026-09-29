#include "wire.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

uint16_t viart_le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
uint32_t viart_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
void viart_store16(uint8_t *p, uint16_t n) { p[0] = n & 255; p[1] = n >> 8; }
void viart_store32(uint8_t *p, uint32_t n) {
    p[0] = n & 255; p[1] = (n >> 8) & 255; p[2] = (n >> 16) & 255; p[3] = n >> 24;
}

int viart_wire_encode(uint8_t op, uint8_t qos, uint32_t id,
                       const char *target, const void *payload, size_t payload_len,
                       uint8_t **out, size_t *out_len) {
    if (!out || !out_len || !target || qos > 3 || (!payload && payload_len)) return EINVAL;
    if (op != VW_PUBLISH && op != VW_MESSAGE && op != VW_BROADCAST &&
        op != VW_SUBSCRIBE && op != VW_UNSUBSCRIBE &&
        op != VW_EXCLUDE && op != VW_UNEXCLUDE && op != VW_PUBLISH_FOR) return EINVAL;
    size_t target_len = strlen(target);
    if (target_len == 0 || target_len > UINT16_MAX) return EINVAL;
    bool subscription = op == VW_SUBSCRIBE || op == VW_UNSUBSCRIBE ||
                        op == VW_EXCLUDE || op == VW_UNEXCLUDE;
    if (subscription && payload_len) return EINVAL;
    size_t extra = subscription ? 0u : 1u;
    if (payload_len > UINT32_MAX || target_len + extra > UINT32_MAX - payload_len)
        return EMSGSIZE;
    size_t body_len = target_len + payload_len + extra;
    uint8_t *buf = malloc(9 + body_len);
    if (!buf) return ENOMEM;
    viart_store32(buf, id);
    buf[4] = op | (uint8_t)(qos << 6);
    viart_store32(buf + 5, (uint32_t)body_len);
    memcpy(buf + 9, target, target_len);
    if (!subscription) buf[9 + target_len] = 0;
    if (payload_len) memcpy(buf + 9 + target_len + 1, payload, payload_len);
    *out = buf;
    *out_len = 9 + body_len;
    return 0;
}

int viart_wire_decode(const uint8_t *frame, size_t len, viart_wire_in *out) {
    if (!frame || !out || len < 6) return EINVAL;
    memset(out, 0, sizeof(*out));
    out->kind = frame[0];
    out->id_or_len = viart_le32(frame + 1);
    out->flag = frame[5];
    if (out->kind == VW_NOP || out->kind == VW_ACK) return len == 6 ? 0 : EPROTO;
    if (out->kind != VW_PUBLISH && out->kind != VW_MESSAGE && out->kind != VW_BROADCAST)
        return EPROTO;
    if (out->id_or_len != len - 6) return EPROTO;
    const uint8_t *body = frame + 6;
    size_t body_len = len - 6;
    const uint8_t *sep = memchr(body, 0, body_len);
    if (!sep) return EPROTO;
    out->sender = (viart_bytes){body, (size_t)(sep - body)};
    size_t used = out->sender.len + 1;
    if (out->kind == VW_PUBLISH) {
        const uint8_t *topic = body + used;
        const uint8_t *topic_sep = memchr(topic, 0, body_len - used);
        if (!topic_sep) return EPROTO;
        out->topic = (viart_bytes){topic, (size_t)(topic_sep - topic)};
        used += out->topic.len + 1;
    }
    out->payload = (viart_bytes){body + used, body_len - used};
    return 0;
}
