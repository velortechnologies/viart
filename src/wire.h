#ifndef VIART_WIRE_H
#define VIART_WIRE_H
#include "viart.h"

enum { VW_NOP = 0, VW_PUBLISH = 1, VW_SUBSCRIBE = 2, VW_UNSUBSCRIBE = 3,
       VW_EXCLUDE = 4, VW_UNEXCLUDE = 5, VW_PUBLISH_FOR = 6,
       VW_MESSAGE = 0x12, VW_BROADCAST = 0x13, VW_ACK = 0xFE,
       VW_RESPONSE_OK = 1, VW_GREETING = 0xEB };

typedef struct {
    uint8_t kind;
    uint8_t flag;
    uint32_t id_or_len;
    viart_bytes sender, topic, payload;
} viart_wire_in;

uint16_t viart_le16(const uint8_t *p);
uint32_t viart_le32(const uint8_t *p);
void viart_store16(uint8_t *p, uint16_t n);
void viart_store32(uint8_t *p, uint32_t n);
int viart_wire_encode(uint8_t op, uint8_t qos, uint32_t id,
                       const char *target, const void *payload, size_t payload_len,
                       uint8_t **out, size_t *out_len);
/* Decode one complete broker->client frame (6-byte header + optional body). */
int viart_wire_decode(const uint8_t *frame, size_t len, viart_wire_in *out);

#endif
