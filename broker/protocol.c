#include "protocol.h"
#include "wire.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int bp_parse(const uint8_t *buf, size_t len, size_t max_body,
             bp_command *out, size_t *used) {
    if (!buf || !out || !used) return EINVAL;
    *used = 0;
    if (len < 9) return EAGAIN;
    uint32_t body_len = viart_le32(buf + 5);
    if (body_len > max_body) return EMSGSIZE;
    if (len < 9u + body_len) return EAGAIN;
    memset(out, 0, sizeof(*out));
    out->id = viart_le32(buf);
    out->op = buf[4] & 0x3f;
    out->qos = buf[4] >> 6;
    out->body = buf + 9;
    out->body_len = body_len;
    *used = 9u + body_len;
    if (buf[4] == 0) return body_len == 0 ? 0 : EPROTO; /* ping */
    if (out->op != BP_PUBLISH && out->op != BP_MESSAGE && out->op != BP_BROADCAST &&
        out->op != BP_SUBSCRIBE && out->op != BP_UNSUBSCRIBE &&
        out->op != BP_EXCLUDE && out->op != BP_UNEXCLUDE && out->op != BP_PUBLISH_FOR) return EOPNOTSUPP;
    if (body_len == 0) return EPROTO;
    if (out->op == BP_SUBSCRIBE || out->op == BP_UNSUBSCRIBE ||
        out->op == BP_EXCLUDE || out->op == BP_UNEXCLUDE) return 0;
    const uint8_t *sep = memchr(out->body, 0, body_len);
    if (!sep || sep == out->body) return EPROTO;
    out->target = out->body;
    out->target_len = (size_t)(sep - out->body);
    out->payload = sep + 1;
    out->payload_len = body_len - out->target_len - 1;
    return 0;
}

uint8_t *bp_ack(uint32_t id, uint8_t result, size_t *len) {
    if (!len) return NULL;
    uint8_t *buf = malloc(6);
    if (!buf) return NULL;
    buf[0] = BP_ACK;
    viart_store32(buf + 1, id);
    buf[5] = result;
    *len = 6;
    return buf;
}

uint8_t *bp_delivery(uint8_t kind, bool realtime, const char *sender,
                     const uint8_t *topic, size_t topic_len,
                     const uint8_t *payload, size_t payload_len, size_t *len) {
    if (!sender || !payload || !len || (kind != BP_PUBLISH && kind != BP_MESSAGE && kind != BP_BROADCAST))
        return NULL;
    size_t sender_len = strlen(sender);
    size_t body_len = sender_len + 1 + (kind == BP_PUBLISH ? topic_len + 1 : 0) + payload_len;
    if (body_len > UINT32_MAX || body_len > SIZE_MAX - 6) return NULL;
    uint8_t *buf = malloc(6 + body_len);
    if (!buf) return NULL;
    buf[0] = kind;
    viart_store32(buf + 1, (uint32_t)body_len);
    buf[5] = realtime ? 1 : 0;
    size_t pos = 6;
    memcpy(buf + pos, sender, sender_len); pos += sender_len; buf[pos++] = 0;
    if (kind == BP_PUBLISH) {
        memcpy(buf + pos, topic, topic_len); pos += topic_len; buf[pos++] = 0;
    }
    memcpy(buf + pos, payload, payload_len);
    *len = 6 + body_len;
    return buf;
}
