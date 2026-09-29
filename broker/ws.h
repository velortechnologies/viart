#ifndef VIART_BROKER_WS_H
#define VIART_BROKER_WS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
int bp_ws_upgrade(const uint8_t *header,size_t len,uint8_t **reply,size_t *reply_len);
int bp_ws_bearer(const uint8_t *header,size_t len,char **token);
int bp_ws_encode(uint8_t opcode,const uint8_t *payload,size_t len,uint8_t **out,size_t *out_len);
int bp_ws_decode(const uint8_t *data,size_t len,size_t max_payload,size_t *used,
                 uint8_t *opcode,bool *fin,uint8_t **payload,size_t *payload_len);
#endif
