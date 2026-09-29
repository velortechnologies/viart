#ifndef VIART_BROKER_PROTOCOL_H
#define VIART_BROKER_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

enum { BP_PUBLISH=1, BP_SUBSCRIBE=2, BP_UNSUBSCRIBE=3,
       BP_EXCLUDE=4, BP_UNEXCLUDE=5, BP_PUBLISH_FOR=6,
       BP_MESSAGE=0x12, BP_BROADCAST=0x13, BP_ACK=0xfe,
       BP_ERR_DATA=0x72, BP_ERR_NOT_SUPPORTED=0x75,
       BP_ERR_BUSY=0x76, BP_ERR_NOT_DELIVERED=0x77,
       BP_ERR_NOT_REGISTERED=0x71, BP_ERR_ACCESS=0x79, BP_OK=1 };

typedef struct {
    uint32_t id;
    uint8_t op, qos;
    const uint8_t *body;
    size_t body_len;
    const uint8_t *target, *payload;
    size_t target_len, payload_len;
} bp_command;

/* Return 0 for a valid complete command, EAGAIN for a partial command. */
int bp_parse(const uint8_t *buf, size_t len, size_t max_body,
             bp_command *out, size_t *used);
uint8_t *bp_ack(uint32_t id, uint8_t result, size_t *len);
uint8_t *bp_delivery(uint8_t kind, bool realtime, const char *sender,
                     const uint8_t *topic, size_t topic_len,
                     const uint8_t *payload, size_t payload_len, size_t *len);
#endif
